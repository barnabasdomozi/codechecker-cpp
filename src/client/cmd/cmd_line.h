// Port of codechecker_client/cmd_line.py: the JSON form of the API's data
// in the output of the cmd subcommands (CmdLineOutputEncoder).
//
// A struct becomes an object of its fields, as Python's json module writes
// the __dict__ of a Thrift object: the fields in the order of the IDL file,
// fields that are not set as null, enum values as numbers, maps as objects
// (a number key as its decimal text) and lists as arrays.

#ifndef CC_CLIENT_CMD_CMD_LINE_H_
#define CC_CLIENT_CMD_CMD_LINE_H_

#include <yyjson.h>

#include "api/thrift/products_types.h"
#include "api/thrift/report_server_types.h"

namespace cc::client::cmd {

// These functions return the JSON object of their struct, built in `doc`.
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const RunData& run);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const RunHistoryData& history);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const ReportData& report);
yyjson_mut_val* ToJson(yyjson_mut_doc* doc, const Product& product);

}  // namespace cc::client::cmd

#endif  // CC_CLIENT_CMD_CMD_LINE_H_
