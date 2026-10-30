#include "analyzer/cli/checkers.h"

#include <stddef.h>
#include <yyjson.h>

#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analyzer/context/analyzer_context.h"
#include "analyzer/drivers/analyzer.h"
#include "analyzer/drivers/analyzer_types.h"
#include "common/checker_labels.h"
#include "common/cli/verbose.h"
#include "common/package.h"
#include "platform/subprocess.h"
#include "util/argparse.h"
#include "util/json.h"
#include "util/logging.h"
#include "util/print.h"
#include "util/pypath.h"
#include "util/result.h"
#include "util/strings.h"
#include "util/twodim.h"

namespace cc::analyzer::cli {
namespace {

namespace argparse = util::argparse;

using Rows = std::vector<std::vector<std::string>>;

constexpr util::Logger kLog("system");

constexpr std::string_view kDescription =
    "Get the list of checkers available and their enabled status in the "
    "supported analyzers.";

// The epilog of the help; {} is the folder of the checker label files.
constexpr std::string_view kEpilog = R"(
The list of checkers that are enabled or disabled by default can be edited by
editing "profile:default" labels in the directory '{}'.

Example scenario: List checkers by labels
-----------------------------------------
List checkers in "sensitive" profile:
    CodeChecker checkers --label profile:sensitive
    CodeChecker checkers --profile sensitive

List checkers in "HIGH" severity:
    CodeChecker checkers --label severity:HIGH
    CodeChecker checkers --severity HIGH

List checkers covering str34-c SEI-CERT rule:
    CodeChecker checkers --label sei-cert-c:str34-c
    CodeChecker checkers --guideline sei-cert-c:str34-c

List checkers covering all SEI-CERT C rules:
    CodeChecker checkers --label guideline:sei-cert-c
    CodeChecker checkers --guideline sei-cert-c

List available profiles, guidelines and severities:
    CodeChecker checkers --profile
    CodeChecker checkers --guideline
    CodeChecker checkers --severity

List labels and their available values:
    CodeChecker checkers --label
    CodeChecker checkers --label severity
)";

constexpr std::string_view kNoCheckersFound =
    "No checkers with the given label found.";

// A checker as the listing shows it (Python: the tuples of
// __get_detailed_checker_info()).
struct CheckerRow {
  // Whether the checker is in the default profile. The filters of the
  // command do not change it.
  bool enabled = false;
  std::string name;
  std::string analyzer;
  std::string description;
  std::vector<common::Label> labels;  // Sorted by key, then value.
};

// The checkers of the analyzers that can run, by analyzer name.
using CheckersByAnalyzer = std::map<std::string, std::vector<CheckerRow>>;

// Returns the value of the option `dest`, which has nargs '?': nullopt if
// it was not given, "" if it was given without a value.
std::optional<std::string> OptionalValue(const argparse::Namespace& args,
                                         std::string_view dest) {
  if (!args.Has(dest)) return std::nullopt;
  return args.Get(dest);
}

// Returns whether the option `dest`, which has nargs '?', was given without
// a value or with an empty one; then the command lists what can be given.
bool GivenWithoutValue(const argparse::Namespace& args,
                       std::string_view dest) {
  return OptionalValue(args, dest) == std::optional<std::string>("");
}

// Returns whether descriptions.json describes the value `value` of the
// label `key`.
bool IsDescribed(const common::CheckerLabels& labels, std::string_view key,
                 const std::string& value) {
  const auto& described = labels.Description(key);
  return std::ranges::find(described, value,
                           &std::pair<std::string, std::string>::first) !=
         described.end();
}

// Prints the values of the label `key` that descriptions.json describes,
// under the header `title`, and with `details` their descriptions too
// (Python: __print_profiles() and __print_severities()).
void PrintDescribedValues(const common::CheckerLabels& labels,
                          std::string_view key, const std::string& title,
                          std::string_view format, bool details) {
  std::vector<std::string> header = {title};
  if (details) header.emplace_back("Description");
  Rows rows;
  for (const auto& [value, description] : labels.Description(key)) {
    if (details) {
      rows.push_back({value, description});
    } else {
      rows.push_back({value});
    }
  }
  util::Print(util::twodim::ToString(
      format, util::twodim::KeysForFormat(format, std::move(header)), rows));
}

// Prints every guideline with its rules, the values of the label named
// after the guideline (Python: __print_guidelines()).
void PrintGuidelines(const common::CheckerLabels& labels,
                     std::string_view format) {
  std::vector<std::pair<std::string, std::vector<std::string>>> guidelines;
  for (const auto& [guideline, description] : labels.Description("guideline")) {
    std::vector<std::string> rules = labels.OccurringValues(guideline);
    std::ranges::stable_sort(rules);
    guidelines.emplace_back(guideline, std::move(rules));
  }
  if (format == "rows") {
    for (const auto& [guideline, rules] : guidelines) {
      util::Print("Guideline: " + guideline);
      util::Print("Rules: " + util::Join(rules, ", "));
    }
    return;
  }
  const std::vector<std::string> keys =
      util::twodim::KeysForFormat(format, {"Guideline", "Rules"});
  if (format == "json") {
    // The rules are a list here, so twodim cannot write them.
    const util::MutableJsonDocument document = util::NewJsonDocument();
    yyjson_mut_doc* doc = document.get();
    yyjson_mut_val* list = yyjson_mut_arr(doc);
    for (const auto& [guideline, rules] : guidelines) {
      yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, list);
      yyjson_mut_obj_add(object, util::MakeJsonString(doc, keys[0]),
                         util::MakeJsonString(doc, guideline));
      yyjson_mut_val* rule_list = yyjson_mut_arr(doc);
      for (const std::string& rule : rules) {
        yyjson_mut_arr_append(rule_list, util::MakeJsonString(doc, rule));
      }
      yyjson_mut_obj_add(object, util::MakeJsonString(doc, keys[1]),
                         rule_list);
    }
    util::Print(util::DumpJson(list));
    return;
  }
  Rows rows;
  for (const auto& [guideline, rules] : guidelines) {
    rows.push_back({guideline, util::Join(rules, ", ")});
  }
  util::Print(util::twodim::ToString(format, keys, rows));
}

// Prints the label keys (Python: __print_labels()).
void PrintLabelKeys(const common::CheckerLabels& labels,
                    std::string_view format) {
  Rows rows;
  for (const std::string& key : labels.Labels()) rows.push_back({key});
  util::Print(util::twodim::ToString(
      format, util::twodim::KeysForFormat(format, {"Label"}), rows));
}

// Prints the values of the label `key` (Python: __print_label_values()).
// The reference keeps the header "Value" in every format.
void PrintLabelValues(const common::CheckerLabels& labels,
                      const std::string& key, std::string_view format) {
  Rows rows;
  for (const std::string& value : labels.OccurringValues(key)) {
    rows.push_back({value});
  }
  if (rows.empty()) {
    kLog.Info(std::format("Label \"{}\" doesn't exist. Use \"CodeChecker "
                          "checkers --label\" command to list available "
                          "labels.",
                          key));
    return;
  }
  util::Print(util::twodim::ToString(format, {"Value"}, rows));
}

// Prints the --checker-config options of the analyzers `analyzers` that can
// run, with their documentation if `details` (Python:
// __print_checker_config()). Returns the exit code: 1 if an analyzer has
// no checker options.
int PrintCheckerOptions(const std::vector<std::string>& analyzers,
                        std::string_view format, bool details) {
  const AnalyzerAvailability availability =
      CheckSupportedAnalyzers(analyzers);
  // Python: check_available_analyzers(exit_on_error=False) warns here, and
  // the command warns again after the list.
  PrintUnsupportedAnalyzers(availability.failed);
  std::vector<std::string> header = {"Option"};
  if (details) header.emplace_back("Description");
  Rows rows;
  std::vector<std::string> failures;
  for (const std::string& analyzer : availability.enabled) {
    const std::vector<CheckerOption> options =
        FindAnalyzer(analyzer)->CheckerConfigOptions();
    // The reference means to exempt Cppcheck and GCC, but its conditions
    // make every analyzer without options a failure.
    if (options.empty()) {
      failures.push_back(analyzer);
      continue;
    }
    for (const CheckerOption& option : options) {
      std::string name = std::format("{}:{}:{}", analyzer, option.checker,
                                     option.option);
      if (details) {
        rows.push_back({std::move(name), option.documentation});
      } else {
        rows.push_back({std::move(name)});
      }
    }
  }
  if (!rows.empty()) {
    util::Print(util::twodim::ToString(
        format, util::twodim::KeysForFormat(format, std::move(header)),
        rows));
  }
  PrintUnsupportedAnalyzers(availability.failed);
  if (!failures.empty()) {
    kLog.Error(std::format(
        "Failed to get checker configuration options for '{}' analyzer(s)! "
        "Please try to upgrade your analyzer version to use this feature.",
        util::Join(failures, ", ")));
    return 1;
  }
  return 0;
}

// Returns --guideline `guideline` as a label filter (Python:
// __guideline_to_label()): "guideline:<guideline>" if it is a guideline of
// one of the `analyzers`, else `guideline` itself, which must then be
// "<guideline>:<rule>". The error is logged and has exit code 1.
Result<std::string> GuidelineFilter(const common::CheckerLabels& labels,
                                    const std::string& guideline,
                                    const std::vector<std::string>& analyzers) {
  for (const std::string& analyzer : analyzers) {
    const std::vector<std::string> guidelines =
        labels.OccurringValues("guideline", analyzer);
    if (std::ranges::find(guidelines, guideline) != guidelines.end()) {
      return Result<std::string>("guideline:" + guideline);
    }
  }
  if (guideline.find(':') == std::string::npos) {
    kLog.Error("--guideline parameter is either <guideline> or "
               "<guideline>:<rule>");
    return Result<std::string>(Error{});
  }
  return Result<std::string>(guideline);
}

// Returns the checkers of every analyzer that can run, in the order of
// their checker tables after the label filters `filters` are enabled in
// them (Python: __get_detailed_checker_info()). The order matters: the
// filtered checkers come last. `profile` is the value of --profile, which
// must be a described profile. Errors are logged and have exit code 1.
Result<CheckersByAnalyzer> CollectCheckers(
    const common::CheckerLabels& labels,
    const std::optional<std::string>& profile,
    const std::vector<std::string>& filters) {
  const std::vector<std::string> working =
      CheckSupportedAnalyzers(SupportedAnalyzerNames()).enabled;
  // Python: build_config_handlers() with the arguments of this command,
  // which have no -e/-d items.
  Result<std::map<std::string, AnalyzerConfig>> built =
      BuildConfigs(ConfigArgs(), working);
  if (!built.ok()) return Result<CheckersByAnalyzer>(built.error());
  std::map<std::string, AnalyzerConfig> configs = std::move(built).value();
  if (profile && !working.empty() &&
      !IsDescribed(labels, "profile", *profile)) {
    kLog.Error(std::format("Checker profile '{}' does not exist!", *profile));
    kLog.Error("To list available profiles, use '--profile' without "
               "argument.");
    return Result<CheckersByAnalyzer>(Error{});
  }
  // The filters are enabled like -e items.
  ConfigArgs filter_args;
  for (const std::string& filter : filters) {
    filter_args.ordered_checkers.push_back({.name = filter, .enabled = true});
  }
  CheckersByAnalyzer result;
  for (const std::string& analyzer : working) {
    AnalyzerConfig& config = configs.at(analyzer);
    if (Result<void> initialized = InitializeCheckers(
            FindAnalyzer(analyzer)->Checkers(), filter_args, &config);
        !initialized.ok()) {
      return Result<CheckersByAnalyzer>(initialized.error());
    }
    std::vector<CheckerRow>& rows = result[analyzer];
    for (const CheckerEntry& entry : config.checkers.entries()) {
      std::vector<common::Label> checker_labels =
          labels.AllLabelsOfChecker(entry.name, analyzer);
      std::ranges::stable_sort(checker_labels);
      const bool enabled =
          std::ranges::find(checker_labels,
                            common::Label("profile", "default")) !=
          checker_labels.end();
      rows.push_back({.enabled = enabled,
                      .name = entry.name,
                      .analyzer = analyzer,
                      .description = entry.description,
                      .labels = std::move(checker_labels)});
    }
  }
  return Result<CheckersByAnalyzer>(std::move(result));
}

// Makes the doc_url labels of `rows` that are not URLs absolute paths in
// the www folder of the package (Python: __post_process_result()).
void MakeDocUrlsAbsolute(std::vector<CheckerRow>* rows) {
  const std::string www_dir = util::pypath::Join(
      {platform::GetEnv("CC_DATA_FILES_DIR").value_or(""), "www"});
  for (CheckerRow& row : *rows) {
    for (auto& [key, value] : row.labels) {
      if (key != "doc_url" || value.starts_with("http")) continue;
      // Python: value.strip(os.sep).
      const size_t begin = value.find_first_not_of('/');
      const std::string relative =
          begin == std::string::npos
              ? ""
              : value.substr(begin, value.find_last_not_of('/') - begin + 1);
      value = util::pypath::Normpath(util::pypath::Join({www_dir, relative}));
    }
  }
}

// Prints `rows` in the custom format of the command, one block of lines per
// checker (Python: __print_checkers_custom_format()).
void PrintCustomFormat(const std::vector<CheckerRow>& rows) {
  for (const CheckerRow& row : rows) {
    util::Print(row.name);
    util::Print(std::string("  Status: ") +
                (row.enabled ? "enabled" : "disabled"));
    util::Print("  Analyzer: " + row.analyzer);
    util::Print("  Description: " + row.description);
    util::Print("  Labels:");
    for (const auto& [key, value] : row.labels) {
      util::Print("    " + key + ":" + value);
    }
    util::Print("");
  }
}

// Returns the labels of `row` as "key:value" strings.
std::vector<std::string> LabelStrings(const CheckerRow& row) {
  std::vector<std::string> labels;
  for (const auto& [key, value] : row.labels) {
    labels.push_back(key + ":" + value);
  }
  return labels;
}

// Returns `rows` as JSON: the checker names, or with `details` an object
// per checker (Python: __print_checkers_json_format()).
std::string CheckersJson(const std::vector<CheckerRow>& rows, bool details) {
  const util::MutableJsonDocument document = util::NewJsonDocument();
  yyjson_mut_doc* doc = document.get();
  yyjson_mut_val* list = yyjson_mut_arr(doc);
  for (const CheckerRow& row : rows) {
    if (!details) {
      yyjson_mut_arr_append(list, util::MakeJsonString(doc, row.name));
      continue;
    }
    yyjson_mut_val* object = yyjson_mut_arr_add_obj(doc, list);
    yyjson_mut_obj_add_str(doc, object, "status",
                           row.enabled ? "enabled" : "disabled");
    yyjson_mut_obj_add_val(doc, object, "name",
                           util::MakeJsonString(doc, row.name));
    yyjson_mut_obj_add_val(doc, object, "analyzer",
                           util::MakeJsonString(doc, row.analyzer));
    yyjson_mut_obj_add_val(doc, object, "description",
                           util::MakeJsonString(doc, row.description));
    yyjson_mut_val* labels = yyjson_mut_arr(doc);
    for (const std::string& label : LabelStrings(row)) {
      yyjson_mut_arr_append(labels, util::MakeJsonString(doc, label));
    }
    yyjson_mut_obj_add_val(doc, object, "labels", labels);
  }
  return util::DumpJson(list);
}

// Prints `rows` in `format`: custom, json or a twodim format.
void PrintCheckerRows(const std::vector<CheckerRow>& rows,
                      std::string_view format, bool details) {
  if (format == "json") {
    util::Print(CheckersJson(rows, details));
    return;
  }
  if (rows.empty()) {
    kLog.Info(kNoCheckersFound);
    return;
  }
  if (format == "custom") {
    PrintCustomFormat(rows);
    return;
  }
  std::vector<std::string> header = {"Name"};
  Rows table;
  if (details) {
    header = {"Status", "Name", "Analyzer", "Description", "Labels"};
    for (const CheckerRow& row : rows) {
      table.push_back({row.enabled ? "+" : "-", row.name, row.analyzer,
                       row.description, util::Join(LabelStrings(row), ", ")});
    }
  } else {
    for (const CheckerRow& row : rows) table.push_back({row.name});
  }
  util::Print(util::twodim::ToString(
      format, util::twodim::KeysForFormat(format, std::move(header)), table));
}

// Prints the checkers of `args.analyzers`, filtered by --label, --profile,
// --guideline and --severity (Python: __print_checkers()). Errors are
// logged and have exit code 1.
Result<void> PrintCheckers(const argparse::Namespace& args,
                           const common::CheckerLabels& labels,
                           std::string_view format) {
  const std::vector<std::string>& analyzers = args.GetList("analyzers");
  const std::optional<std::string> profile = OptionalValue(args, "profile");
  const std::optional<std::string> label = OptionalValue(args, "label");
  const std::optional<std::string> severity = OptionalValue(args, "severity");
  const std::optional<std::string> guideline = OptionalValue(args, "guideline");
  // In the order the reference enables them, which decides the order of
  // the checkers.
  std::vector<std::string> filters;
  if (profile) filters.push_back("profile:" + *profile);
  if (label) filters.push_back(*label);
  if (severity) filters.push_back("severity:" + *severity);
  if (guideline) {
    Result<std::string> filter = GuidelineFilter(labels, *guideline, analyzers);
    if (!filter.ok()) return Result<void>(filter.error());
    filters.push_back(std::move(filter).value());
  }

  const Result<CheckersByAnalyzer> checkers =
      CollectCheckers(labels, profile, filters);
  if (!checkers.ok()) return Result<void>(checkers.error());
  std::vector<CheckerRow> rows;
  // An analyzer given twice is listed twice, as in the reference.
  for (const std::string& analyzer : analyzers) {
    const auto found = checkers.value().find(analyzer);
    if (found == checkers.value().end()) continue;
    if (filters.empty()) {
      rows.insert(rows.end(), found->second.begin(), found->second.end());
      continue;
    }
    const std::vector<std::string> matching =
        labels.CheckersByLabels(filters, analyzer);
    const std::unordered_set<std::string> names(matching.begin(),
                                                matching.end());
    std::ranges::copy_if(found->second, std::back_inserter(rows),
                         [&names](const CheckerRow& row) {
                           return names.contains(row.name);
                         });
  }
  MakeDocUrlsAbsolute(&rows);
  PrintCheckerRows(rows, format, args.Has("details"));
  return Result<void>();
}

}  // namespace

void AddCheckersArguments(argparse::Parser& parser) {
  const std::vector<std::string> analyzer_names = SupportedAnalyzerNames();
  parser.set_help_format(argparse::HelpFormat::kCodeChecker);
  parser.set_allow_abbrev(false);
  parser.set_description(std::string(kDescription));
  parser.set_epilog(std::format(
      kEpilog,
      util::pypath::Join({common::DataFilesDir(), "config", "labels"})));
  parser.AddArgument(
      {.flags = {"--analyzers"},
       .dest = "analyzers",
       .nargs = argparse::Nargs::kOneOrMore,
       .metavar = "ANALYZER",
       .choices = analyzer_names,
       .default_list = analyzer_names,
       .help = "Show checkers only from the analyzers specified."});
  parser.AddArgument({.flags = {"-w", "--warnings"},
                      .dest = "show_warnings",
                      .action = argparse::ArgumentAction::kStoreTrue,
                      .help = "DEPRECATED. Show available warning flags."});
  parser.AddArgument(
      {.flags = {"--details"},
       .dest = "details",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Show details about the checker, such as status, checker "
               "name, analyzer name, severity, guidelines and description. "
               "Status shows if the checker is enabled besides the given "
               "labels. If the labels don't trigger a checker then the "
               "status is determined by the analyzer tool."});
  parser.AddArgument(
      {.flags = {"--label"},
       .dest = "label",
       .nargs = argparse::Nargs::kZeroOrOne,
       .help = "Filter checkers that are attached the given label. The "
               "format of a label is <label>:<value>. If no argument is "
               "given then available labels are listed. If only <label> is "
               "given then available values are listed."});
  parser.AddArgument(
      {.flags = {"--profile"},
       .dest = "profile",
       .nargs = argparse::Nargs::kZeroOrOne,
       .help = "List checkers enabled by the selected profile. If no "
               "argument is given then available profiles are listed."});
  parser.AddArgument(
      {.flags = {"--guideline"},
       .dest = "guideline",
       .nargs = argparse::Nargs::kZeroOrOne,
       .help = "List checkers that report on a specific guideline. Without "
               "additional parameter, the available guidelines and their "
               "corresponding rules will be listed."});
  parser.AddArgument(
      {.flags = {"--severity"},
       .dest = "severity",
       .nargs = argparse::Nargs::kZeroOrOne,
       .help = "List checkers with the given severity. Make sure to indicate "
               "severity in capitals (e.g. HIGH, MEDIUM, etc.) If no "
               "argument is given then available severities are listed."});
  parser.AddArgument(
      {.flags = {"--checker-config"},
       .dest = "checker_config",
       .action = argparse::ArgumentAction::kStoreTrue,
       .help = "Show checker configuration options for all existing "
               "checkers supported by the analyzer. These can be given to "
               "'CodeChecker analyze --checker-config'."});
  parser.AddArgument(
      {.flags = {"-o", "--output"},
       .dest = "output_format",
       .choices = {"rows", "table", "csv", "json", "custom"},
       .default_value = "custom",
       .help = "The format to list the applicable checkers as."});
  common::cli::AddVerboseArgument(parser);
}

int RunCheckers(const argparse::Namespace& args) {
  const std::string& format = args.Get("output_format");
  // Only the table format logs to stdout, so the others can be parsed.
  util::SetupLogger(common::cli::Verbose(args), format == "table"
                                                    ? util::Stream::kStdout
                                                    : util::Stream::kStderr);
  const Result<AnalyzerContext*> context = GetContext();
  if (!context.ok()) return context.error().exit_code;
  const common::CheckerLabels& labels = context.value()->checker_labels();
  // The listings other than the checker list have no custom format.
  const std::string_view list_format =
      format == "custom" ? std::string_view("rows") : std::string_view(format);
  const bool details = args.Has("details");
  const std::optional<std::string> label = OptionalValue(args, "label");

  if (GivenWithoutValue(args, "profile")) {
    PrintDescribedValues(labels, "profile", "Profile name", list_format,
                         details);
  } else if (GivenWithoutValue(args, "severity")) {
    PrintDescribedValues(labels, "severity", "Severity", list_format, details);
  } else if (GivenWithoutValue(args, "guideline")) {
    PrintGuidelines(labels, list_format);
  } else if (GivenWithoutValue(args, "label")) {
    PrintLabelKeys(labels, list_format);
  } else if (label && label->find(':') == std::string::npos) {
    PrintLabelValues(labels, *label, list_format);
  } else if (args.Has("checker_config")) {
    return PrintCheckerOptions(args.GetList("analyzers"), list_format,
                               details);
  } else if (const Result<void> printed = PrintCheckers(args, labels, format);
             !printed.ok()) {
    return printed.error().exit_code;
  }
  return 0;
}

}  // namespace cc::analyzer::cli
