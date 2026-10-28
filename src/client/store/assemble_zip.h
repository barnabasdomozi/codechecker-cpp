// Port of assemble_zip() of codechecker_client/cli/store.py: the zip of
// report folders and source files that `store` uploads (store.md §2.2).

#ifndef CC_CLIENT_STORE_ASSEMBLE_ZIP_H_
#define CC_CLIENT_STORE_ASSEMBLE_ZIP_H_

#include <string>
#include <vector>

#include "client/client.h"
#include "common/checker_labels.h"
#include "util/result.h"

namespace cc::client::store {

// Writes the zip to upload into the file `zip_file`, compressed with zlib,
// and prints a summary of it (Python: assemble_zip()). The zip holds the
// reports of the result files in `inputs` without duplicates, the files of
// their report folders, the source files that the server of `results`
// lacks or that have review status comments, and the blame information of
// the source files. `products` is the client of the same product. The
// result files without duplicates are written into a new folder in
// `temp_dir`, which is removed at the end; `jobs` threads read the result
// files.
//
// The error message is empty if the failure is already reported (a failed
// API call, changed source files, too many reports); otherwise it says what
// failed.
Result<void> AssembleZip(const std::vector<std::string>& inputs,
                         const std::string& zip_file,
                         const common::CheckerLabels& labels,
                         const std::string& temp_dir, int jobs,
                         ResultsClient& results, ProductClient& products);

}  // namespace cc::client::store

#endif  // CC_CLIENT_STORE_ASSEMBLE_ZIP_H_
