#include "report/converter/analyzer_types.h"

#include <span>
#include <string_view>

#include "report/converter/clang_tidy.h"
#include "report/converter/json_formats.h"
#include "report/converter/line_formats.h"
#include "report/converter/result_file_formats.h"
#include "report/converter/sanitizers.h"
#include "report/converter/xml_formats.h"
#include "report/hash/report_hash.h"

namespace cc::report {
namespace {

// The example texts are those of the Python classes' EXAMPLE_CMD.
constexpr AnalyzerType kAnalyzerTypes[] = {
    {.tool_name = "clang-tidy",
     .name = "Clang Tidy",
     .url = "https://clang.llvm.org/extra/clang-tidy",
     .example_command =
         "# Run clang-tidy and redirect its console output to a file.\n"
         "clang-tidy my_file.cpp > ./clang-tidy_reports.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of clang-tidy.\n"
         "report-converter -t clang-tidy -o ./codechecker_clang_tidy_reports     ./clang-tidy_reports.out\n"
         "\n"
         "# Store the clang-tidy reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_clang_tidy_reports -n clang-tidy",
     .hash_type = HashType::kPathSensitive,
     .get_reports = GetClangTidyReports},
    {.tool_name = "clang-tidy-yaml",
     .name = "Clang Tidy",
     .url = "https://clang.llvm.org/extra/clang-tidy",
     .example_command =
         "# Run clang-tidy and export the fixes/diagnostics to a yaml file.\n"
         "clang-tidy --export-fixes=./clang-tidy_reports.yaml my_file.cpp\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of clang-tidy.\n"
         "report-converter -t clang-tidy-yaml     -o ./codechecker_clang_tidy_reports ./clang-tidy_reports.yaml\n"
         "\n"
         "# Store the clang-tidy reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_clang_tidy_reports -n clang-tidy",
     .hash_type = HashType::kPathSensitive,
     .get_reports = GetClangTidyYamlReports},
    {.tool_name = "coccinelle",
     .name = "Coccinelle",
     .url = "https://github.com/coccinelle/coccinelle",
     .example_command =
         "# Change directory to your project (e.g. the Linux kernel).\n"
         "cd path/to/linux/kernel/repository\n"
         "\n"
         "# Run Coccicheck and redirect the output to a file.\n"
         "make coccicheck MODE=report V=1 > ./coccinelle_reports.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Coccinelle.\n"
         "report-converter -t coccinelle -o ./codechecker_coccinelle_reports     ./coccinelle_reports.out\n"
         "\n"
         "# Store the Coccinelle reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_coccinelle_reports -n coccinelle",
     .hash_type = HashType::kContextFree,
     .get_reports = GetCoccinelleReports},
    {.tool_name = "cppcheck",
     .name = "Cppcheck",
     .url = "http://cppcheck.sourceforge.net",
     .example_command =
         "# Collect the compilation commands with absolute path conversion.\n"
         "CC_LOGGER_ABS_PATH=1 CodeChecker log -o compile_command.json -b \"make\"\n"
         "\n"
         "# Run Cppcheck using the generated compile command database.\n"
         "mkdir cppcheck_reports\n"
         "cppcheck --project=compile_command.json --plist-output=./cppcheck_reports\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Cppcheck.\n"
         "report-converter -t cppcheck -o ./codechecker_cppcheck_reports     ./cppcheck_reports\n"
         "\n"
         "# Store the Cppcheck reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_cppcheck_reports -n cppcheck",
     .hash_type = HashType::kContextFree,
     .get_reports = GetCppcheckReports},
    {.tool_name = "cpplint",
     .name = "cpplint",
     .url = "https://github.com/cpplint/cpplint",
     .example_command =
         "# Run cpplint and redirect the output to a file.\n"
         "cpplint sample.cpp > ./sample.out 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of cpplint.\n"
         "report-converter -t cpplint -o ./codechecker_cpplint_reports ./sample.out\n"
         "\n"
         "# Store the cpplint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_cpplint_reports -n cpplint",
     .hash_type = HashType::kContextFree,
     .get_reports = GetCpplintReports},
    {.tool_name = "eslint",
     .name = "ESLint",
     .url = "https://eslint.org/",
     .example_command =
         "# Run ESLint and generate a json output file.\n"
         "eslint -o ./eslint_reports.json -f json /path/to/my/project\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of ESLint.\n"
         "report-converter -t eslint -o ./codechecker_eslint_reports     ./eslint_reports.json\n"
         "\n"
         "# Store the ESLint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_eslint_reports -n eslint",
     .hash_type = HashType::kContextFree,
     .get_reports = GetEslintReports},
    {.tool_name = "gcc",
     .name = "GNU Compiler Collection Static Analyzer",
     .url = "https://gcc.gnu.org/wiki/StaticAnalyzer",
     .example_command =
         "# Compile and analyze with GCC's static analyzer (GCC 13+), producing a\n"
         "# sarif output file.\n"
         "g++ -fanalyzer -fdiagnostics-format=sarif-file my_file.cpp\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of GCC's static analyzer.\n"
         "report-converter -t gcc -o ./codechecker_gcc_reports my_file.cpp.sarif\n"
         "\n"
         "# Store the gcc reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_gcc_reports -n gcc",
     .hash_type = HashType::kContextFree,
     .get_reports = GetGccReports},
    {.tool_name = "golint",
     .name = "Golint",
     .url = "https://github.com/golang/lint",
     .example_command =
         "# Run Golint and redirect the output to a file.\n"
         "golint /path/to/your/project > ./golint_reports.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Golint.\n"
         "report-converter -t golint -o ./codechecker_golint_reports ./golint_reports.out\n"
         "\n"
         "# Store the Golint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_golint_reports -n golint",
     .hash_type = HashType::kContextFree,
     .get_reports = GetGolintReports},
    {.tool_name = "fbinfer",
     .name = "Facebook Infer",
     .url = "https://fbinfer.com",
     .example_command =
         "# Run Facebook Infer, which produces an 'infer-out' directory containing\n"
         "# a 'report.json' file.\n"
         "infer capture -- clang++ main.cpp\n"
         "infer analyze\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Facebook Infer.\n"
         "report-converter -t fbinfer -o ./codechecker_fbinfer_reports ./infer-out\n"
         "\n"
         "# Store the Infer reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_fbinfer_reports -n fbinfer",
     .hash_type = HashType::kContextFree,
     .get_reports = GetInferReports},
    {.tool_name = "jscpd",
     .name = "JSCPD",
     .url = "https://www.npmjs.com/package/jscpd",
     .example_command =
         "# Run jscpd and generate a json report.\n"
         "jscpd --reporters json -o ./jscpd_reports /path/to/my/project\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of jscpd.\n"
         "report-converter -t jscpd -o ./codechecker_jscpd_reports     ./jscpd_reports/jscpd-report.json\n"
         "\n"
         "# Store the jscpd reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_jscpd_reports -n jscpd",
     .hash_type = HashType::kContextFree,
     .get_reports = GetJscpdReports},
    {.tool_name = "kernel-doc",
     .name = "Kernel-Doc",
     .url = "https://github.com/torvalds/linux/blob/master/scripts/kernel-doc",
     .example_command =
         "# Change directory to your kernel source repository.\n"
         "cd path/to/linux/kernel/repository\n"
         "\n"
         "# Run Kernel-Doc (via the Sphinx docs build) and redirect the output to a\n"
         "# file.\n"
         "make htmldocs 2>&1 | tee kernel-docs.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Kernel-Doc.\n"
         "report-converter -t kernel-doc -o ./codechecker_kernel_doc_reports     ./kernel-docs.out\n"
         "\n"
         "# Store the Kernel-Doc reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_kernel_doc_reports -n kernel-doc",
     .hash_type = HashType::kContextFree,
     .get_reports = GetKernelDocReports},
    {.tool_name = "mdl",
     .name = "Markdownlint",
     .url = "https://github.com/markdownlint/markdownlint",
     .example_command =
         "# Run Markdownlint and redirect the output to a file.\n"
         "mdl /path/to/your/project > ./mdl_reports.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Markdownlint.\n"
         "report-converter -t mdl -o ./codechecker_mdl_reports ./mdl_reports.out\n"
         "\n"
         "# Store the Markdownlint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_mdl_reports -n mdl",
     .hash_type = HashType::kContextFree,
     .get_reports = GetMarkdownlintReports},
    {.tool_name = "pmd",
     .name = "PMD",
     .url = "https://pmd.github.io/",
     .example_command =
         "# Run PMD and generate a json report.\n"
         "pmd check -d /path/to/my/project -R rulesets/java/quickstart.xml \\\n"
         "    -f json -r ./pmd_reports.json\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of PMD.\n"
         "report-converter -t pmd -o ./codechecker_pmd_reports ./pmd_reports.json\n"
         "\n"
         "# Store the PMD reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_pmd_reports -n pmd",
     .hash_type = HashType::kPathSensitive,
     .get_reports = GetPmdReports},
    {.tool_name = "pvs-studio",
     .name = "PVS-Studio",
     .url = "https://pvs-studio.com/en/",
     .example_command =
         "# Run the PVS-Studio analysis (see https://pvs-studio.com/en/docs/ for the\n"
         "# full setup), producing a PVS-Studio.json report.\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# JSON report of PVS-Studio.\n"
         "report-converter -t pvs-studio -o ./codechecker_pvs_studio_reports     ./PVS-Studio.json\n"
         "\n"
         "# Store the PVS-Studio reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_pvs_studio_reports -n pvs_studio",
     .hash_type = HashType::kContextFree,
     .get_reports = GetPvsStudioReports},
    {.tool_name = "pyflakes",
     .name = "Pyflakes",
     .url = "https://github.com/PyCQA/pyflakes",
     .example_command =
         "# Run Pyflakes and redirect the output to a file.\n"
         "pyflakes /path/to/your/project > ./pyflakes_reports.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Pyflakes.\n"
         "report-converter -t pyflakes -o ./codechecker_pyflakes_reports     ./pyflakes_reports.out\n"
         "\n"
         "# Store the Pyflakes reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_pyflakes_reports -n pyflakes",
     .hash_type = HashType::kContextFree,
     .get_reports = GetPyflakesReports},
    {.tool_name = "pylint",
     .name = "Pylint",
     .url = "https://www.pylint.org",
     .example_command =
         "# Run Pylint and generate a json output file.\n"
         "pylint -f json /path/to/my/project > ./pylint_reports.json\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Pylint.\n"
         "report-converter -t pylint -o ./codechecker_pylint_reports     ./pylint_reports.json\n"
         "\n"
         "# Store the Pylint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_pylint_reports -n pylint",
     .hash_type = HashType::kContextFree,
     .get_reports = GetPylintReports},
    {.tool_name = "roslynator",
     .name = "Roslynator.DotNet.Cli",
     .url = "https://github.com/JosefPihrt/Roslynator#roslynator-command-line-tool-",
     .example_command =
         "# Change directory to your project or solution.\n"
         "cd path/to/your/project_or_solution\n"
         "\n"
         "# Run Roslynator (use an .sln file instead of .csproj to analyze a\n"
         "# solution).\n"
         "roslynator analyze sample.csproj --output sample.xml\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Roslynator.\n"
         "report-converter -t roslynator -o ./codechecker_roslynator_reports ./sample.xml\n"
         "\n"
         "# Store the Roslynator reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_roslynator_reports -n roslynator",
     .hash_type = HashType::kContextFree,
     .get_reports = GetRoslynatorReports},
    {.tool_name = "ruff",
     .name = "ruff",
     .url = "https://docs.astral.sh/ruff/",
     .example_command =
         "# Run ruff and generate a json output file.\n"
         "ruff check --output-format json /path/to/my/project > ./ruff_reports.json\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of ruff.\n"
         "report-converter -t ruff -o ./codechecker_ruff_reports ./ruff_reports.json\n"
         "\n"
         "# Store the ruff reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_ruff_reports -n ruff",
     .hash_type = HashType::kContextFree,
     .get_reports = GetRuffReports},
    {.tool_name = "asan",
     .name = "AddressSanitizer",
     .url = "https://clang.llvm.org/docs/AddressSanitizer.html",
     .example_command =
         "# Compile your program with debug info and a frame pointer.\n"
         "clang++ -fsanitize=address -g -fno-omit-frame-pointer asan.cpp\n"
         "\n"
         "# Run your program and redirect the output to a file.\n"
         "ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-6.0/bin/llvm-symbolizer \\\n"
         "./a.out > asan.output 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of AddressSanitizer.\n"
         "report-converter -t asan -o ./asan_results asan.output",
     .hash_type = HashType::kContextFree,
     .get_reports = GetAsanReports},
    {.tool_name = "lsan",
     .name = "LeakSanitizer",
     .url = "https://clang.llvm.org/docs/LeakSanitizer.html",
     .example_command =
         "# Compile your program with debug info and AddressSanitizer.\n"
         "clang -fsanitize=address -g lsan.c\n"
         "\n"
         "# Run your program and redirect the output to a file.\n"
         "ASAN_OPTIONS=detect_leaks=1 ./a.out > lsan.output 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of LeakSanitizer.\n"
         "report-converter -t lsan -o ./lsan_results lsan.output",
     .hash_type = HashType::kContextFree,
     .get_reports = GetLsanReports},
    {.tool_name = "msan",
     .name = "MemorySanitizer",
     .url = "https://clang.llvm.org/docs/MemorySanitizer.html",
     .example_command =
         "# Compile your program with debug info and a frame pointer.\n"
         "clang++ -fsanitize=memory -g -fno-omit-frame-pointer msan.cpp\n"
         "\n"
         "# Run your program and redirect the output to a file.\n"
         "MSAN_SYMBOLIZER_PATH=/usr/lib/llvm-6.0/bin/llvm-symbolizer \\\n"
         "./a.out > msan.output 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of MemorySanitizer.\n"
         "report-converter -t msan -o ./msan_results msan.output",
     .hash_type = HashType::kContextFree,
     .get_reports = GetMsanReports},
    {.tool_name = "tsan",
     .name = "ThreadSanitizer",
     .url = "https://clang.llvm.org/docs/ThreadSanitizer.html",
     .example_command =
         "# Compile your program with debug info.\n"
         "clang++ -fsanitize=thread -g tsan.cpp\n"
         "\n"
         "# Run your program and redirect the output to a file.\n"
         "./a.out > tsan.output 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of ThreadSanitizer.\n"
         "report-converter -t tsan -o ./tsan_results tsan.output",
     .hash_type = HashType::kContextFree,
     .get_reports = GetTsanReports},
    {.tool_name = "ubsan",
     .name = "UndefinedBehaviorSanitizer",
     .url = "https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html",
     .example_command =
         "# Compile your program with debug info and a frame pointer.\n"
         "clang++ -fsanitize=undefined -g -fno-omit-frame-pointer ubsanitizer.cpp\n"
         "\n"
         "# Run your program and redirect the output to a file.\n"
         "UBSAN_OPTIONS=print_stacktrace=1 \\\n"
         "UBSAN_SYMBOLIZER_PATH=/usr/lib/llvm-6.0/bin/llvm-symbolizer \\\n"
         "./a.out > ubsan.output 2>&1\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of UndefinedBehaviorSanitizer.\n"
         "report-converter -t ubsan -o ./ubsan_results ubsan.output",
     .hash_type = HashType::kContextFree,
     .get_reports = GetUbsanReports},
    {.tool_name = "smatch",
     .name = "Smatch",
     .url = "https://repo.or.cz/w/smatch.git",
     .example_command =
         "# Change directory to your kernel source repository.\n"
         "cd path/to/linux/kernel/repository\n"
         "\n"
         "# Run Smatch (warnings are written to smatch_warns.txt by default).\n"
         "path/to/smatch/smatch_scripts/test_kernel.sh\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Smatch.\n"
         "report-converter -t smatch -o ./codechecker_smatch_reports ./smatch_warns.txt\n"
         "\n"
         "# Store the Smatch reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_smatch_reports -n smatch",
     .hash_type = HashType::kContextFree,
     .get_reports = GetSmatchReports},
    {.tool_name = "sparse",
     .name = "Sparse",
     .url = "https://git.kernel.org/pub/scm/devel/sparse/sparse.git",
     .example_command =
         "# Change directory to your kernel source repository.\n"
         "cd path/to/linux/kernel/repository\n"
         "\n"
         "# Run Sparse and redirect the output to a file.\n"
         "make C=1 2>&1 | tee sparse.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Sparse.\n"
         "report-converter -t sparse -o ./codechecker_sparse_reports ./sparse.out\n"
         "\n"
         "# Store the Sparse reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_sparse_reports -n sparse",
     .hash_type = HashType::kContextFree,
     .get_reports = GetSparseReports},
    {.tool_name = "sphinx",
     .name = "Sphinx",
     .url = "https://github.com/sphinx-doc/sphinx",
     .example_command =
         "# Change directory to your kernel source repository.\n"
         "cd path/to/linux/kernel/repository\n"
         "\n"
         "# Run Sphinx and redirect the output to a file.\n"
         "make htmldocs 2>&1 | tee sphinx.out\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of Sphinx.\n"
         "report-converter -t sphinx -o ./codechecker_sphinx_reports ./sphinx.out\n"
         "\n"
         "# Store the Sphinx reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_sphinx_reports -n sphinx",
     .hash_type = HashType::kContextFree,
     .get_reports = GetSphinxReports},
    {.tool_name = "spotbugs",
     .name = "spotbugs",
     .url = "https://spotbugs.github.io",
     .example_command =
         "# Run SpotBugs, generating xml output with messages.\n"
         "spotbugs -xml:withMessages -output ./bugs.xml -textui /path/to/your/project\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of SpotBugs.\n"
         "report-converter -t spotbugs -o ./codechecker_spotbugs_reports ./bugs.xml\n"
         "\n"
         "# Store the SpotBugs reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_spotbugs_reports -n spotbugs",
     .hash_type = HashType::kContextFree,
     .get_reports = GetSpotbugsReports},
    {.tool_name = "tslint",
     .name = "TSLint",
     .url = "https://palantir.github.io/tslint",
     .example_command =
         "# Run TSLint and generate a json output file.\n"
         "tslint --format json /path/to/my/ts/file -o ./tslint_reports.json\n"
         "\n"
         "# Use 'report-converter' to create a CodeChecker report directory from the\n"
         "# analyzer result of TSLint.\n"
         "report-converter -t tslint -o ./codechecker_tslint_reports     ./tslint_reports.json\n"
         "\n"
         "# Store the TSLint reports with CodeChecker.\n"
         "CodeChecker store ./codechecker_tslint_reports -n tslint",
     .hash_type = HashType::kContextFree,
     .get_reports = GetTslintReports},
};

}  // namespace

std::span<const AnalyzerType> AnalyzerTypes() { return kAnalyzerTypes; }

const AnalyzerType* FindAnalyzerType(std::string_view tool_name) {
  for (const AnalyzerType& type : kAnalyzerTypes) {
    if (type.tool_name == tool_name) return &type;
  }
  return nullptr;
}

}  // namespace cc::report
