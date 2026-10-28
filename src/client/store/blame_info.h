// Port of codechecker_client/blame_info.py: the Git blame information of
// the source files that `store` uploads (store.md §2.3). Like GitPython, it
// runs the `git` program.

#ifndef CC_CLIENT_STORE_BLAME_INFO_H_
#define CC_CLIENT_STORE_BLAME_INFO_H_

#include <string>
#include <vector>

#include "util/result.h"
#include "util/zip.h"

namespace cc::client::store {

// Adds the blame information of each of `files` that is committed in a Git
// repository to `zip`, as "blame/<path without its leading '/'>", and
// returns how many were added (Python: assemble_blame_info()). The files
// are blamed on several threads. `git` must be installed. Fails if the zip
// cannot be written.
Result<int> AssembleBlameInfo(const std::vector<std::string>& files,
                              util::ZipArchive& zip);

}  // namespace cc::client::store

#endif  // CC_CLIENT_STORE_BLAME_INFO_H_
