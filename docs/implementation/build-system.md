# Build system

Status: **accepted** (2026-10-25).

This page says how the port is built: where the third-party libraries come
from, how they are compiled, and how the project itself is configured, built,
tested and installed. It answers the questions that
[libraries.md](libraries.md#5-dependencies-by-component) and
[architecture.md](architecture.md) leave to the build system document: how we
get the libraries, how `ldlogger` (32-bit included) is compiled, and how the
web UI is built.

Everything here was checked on the development machine (Debian 13, GCC 14.2,
Clang 19.1, CMake 3.31.6) on 2026-10-25.

**Implemented:** the submodules (§2), the deps build (§3), the imported
targets, options and checks of §4.1–4.2, ldlogger (§4.4) and the presets
(§5). The rest is built together with the parts that need it: Thrift code
generation and the version files (§4.3) with `cc_api`, the web UI (§4.5)
with the server, the `blackbox` target and the license files (§5) with the
first program, and CI (§6) when CI is set up.

## Decisions

1. **CMake** (project-wide decision, see [README](README.md)).
2. **Every third-party library is a git submodule, compiled from source by
   our build.** Each library from [libraries.md](libraries.md) is a submodule
   under `third_party/`, pinned to an upstream release tag. We do not use
   distribution packages, vcpkg, Conan, CMake `FetchContent` or downloaded
   archives, and the build never searches the system for a third-party
   library. The one exception is the npm packages of the web UI and the
   black-box suite (see [Exceptions](#exceptions)).
3. **Third-party libraries are linked statically.** Installed programs need
   only glibc, libstdc++ and libgcc_s at run time.
4. **Two CMake projects.** `third_party/CMakeLists.txt` compiles the
   libraries once into a prefix (the *deps prefix*). The main project uses
   only that prefix.
5. **No network access after checkout**, apart from `npm ci` for the web UI.
6. **No Python at build time.** No library is built with Meson or any other
   tool that needs Python, and no Python bindings are built.

### Why submodules

- **The same versions on every machine.** Matching the Python version
  depends on exact library versions, and distributions differ. For example,
  the reference environment's lxml 6.1.3 bundles libxml2 **2.14.6**, while
  Debian 13's system libxml2 is really **2.9.14** (package version
  `2.12.7+dfsg+really2.9.14`). With a submodule, the port uses 2.14.6
  everywhere.
- **A version change is a commit.** Updating a library changes one gitlink.
  It is reviewed like code, and `git bisect` covers library updates.
- **Offline builds, any distribution.** cpp-httplib, jwt-cpp and yyjson are
  missing or old in many distributions. A checkout builds without
  installing any `-dev` packages.

The costs: a checkout of about 1 GB, a slow first build of the libraries,
and we have to ship security fixes of these libraries ourselves (see
[Updating a library](#7-updating-a-library)).

## 1. Host tools

These programs must be installed on the build machine. They are tools, not
libraries, so they are not submodules.

| Tool | Needed for |
|---|---|
| GCC ≥ 14 or Clang ≥ 19 | C++20 ([cppguide.md](cppguide.md)), C |
| CMake ≥ 3.28 | `JOB_SERVER_AWARE` in the deps build, presets (version 6), `string(JSON)` |
| GNU make | zlib, OpenSSL, SQLite, PostgreSQL and OpenLDAP builds |
| Perl 5 | OpenSSL `Configure`, PostgreSQL build scripts |
| flex, bison | the Thrift compiler; PostgreSQL 17's `configure` also requires them, although libpq does not use them |
| git | submodules, version files |
| gcc-multilib | the 32-bit `ldlogger.so` (optional, see [§4.4](#44-ldlogger)) |
| Node.js, npm | the web UI build and the black-box suite |

Not needed: Python, Meson, `tclsh`, autoconf/automake/libtool, pkg-config.
The `configure` scripts of PostgreSQL and OpenLDAP are committed in their
repositories, and SQLite builds its own bundled Jim Tcl (checked).

On Debian, flex and bison are not installed by default
(`apt install flex bison`). Ninja is optional; the presets work with the
default generator.

## 2. Submodules

### 2.1 Layout

```
codechecker-cpp/
├── .gitmodules
├── CMakeLists.txt               the project: cc_* libraries, programs, tests
├── CMakePresets.json
├── cmake/
│   ├── ThirdParty.cmake         imported targets for the deps prefix (§4.1)
│   ├── DepsSources.cmake        the sources hash both builds compare (§3.1)
│   ├── CheckNeeded.cmake        the run-time dependency check (§4.2)
│   └── ...                      later: Thrift code generation, web UI
├── third_party/
│   ├── CMakeLists.txt           the deps build: one ExternalProject per library
│   ├── checkout.sh              initializes the submodules (§2.3)
│   ├── boost-libraries.txt      the Boost sub-libraries Thrift needs (§3.2)
│   ├── pcre2/                   submodule
│   ├── yyjson/                  submodule
│   └── ...                      one folder per library in §2.2
└── ...                          the rest as in architecture.md §2
```

### 2.2 The libraries

| Library | Path in `third_party/` | Repository | Pinned tag | Why this version | Side |
|---|---|---|---|---|---|
| PCRE2 | `pcre2` | `github.com/PCRE2Project/pcre2` | `pcre2-10.49` | latest release | analyzer |
| yyjson | `yyjson` | `github.com/ibireme/yyjson` | `0.13.0` | the version [libraries.md](libraries.md#json) was checked against | analyzer |
| libyaml | `libyaml` | `github.com/yaml/libyaml` | `0.2.5` | the reference environment's PyYAML 6.0.3 uses libyaml 0.2.5 | analyzer |
| libxml2 | `libxml2` | `gitlab.gnome.org/GNOME/libxml2` | `v2.14.6` | bundled in the reference environment's lxml 6.1.3 (`LIBXML_VERSION`) | analyzer |
| zlib | `zlib` | `github.com/madler/zlib` | `v1.3.1` | the reference environment's version | analyzer |
| libzip | `libzip` | `github.com/nih-at/libzip` | `v1.12.1` | latest release | analyzer |
| OpenSSL | `openssl` | `github.com/openssl/openssl` | `openssl-3.5.9` | the 3.5 LTS branch, which the reference environment also uses (3.5.7); latest patch | analyzer |
| libcurl | `curl` | `github.com/curl/curl` | `curl-8_23_0` | latest release | web |
| cpp-httplib | `cpp-httplib` | `github.com/yhirose/cpp-httplib` | `v0.63.0` | the version [libraries.md](libraries.md#http-server-cpp-httplib) was checked against | web |
| Apache Thrift | `thrift` | `github.com/apache/thrift` | `v0.22.0` | see below | web |
| Boost (headers only) | `boost` | `github.com/boostorg/boost` | `boost-1.92.0` | latest release; only headers are used | web |
| SQLite | `sqlite` | `github.com/sqlite/sqlite` (official mirror) | `version-3.54.1` | see below | web |
| libpq | `postgres` | `github.com/postgres/postgres` (official mirror) | `REL_17_11` | bundled in the reference environment's psycopg2-binary 2.9.13 (`__libpq_version__` 170011) | web |
| jwt-cpp | `jwt-cpp` | `github.com/Thalhammer/jwt-cpp` | `v0.7.2` | latest release | web |
| libldap | `openldap` | `git.openldap.org/openldap/openldap` | `OPENLDAP_REL_ENG_2_6_16` | latest release of the 2.6 (LTS) branch | web |
| GoogleTest | `googletest` | `github.com/google/googletest` | `v1.18.0` | latest release | development |

"Side" says who needs the library: the analyzer side, the web side (only
with `CC_BUILD_WEB=ON`), or only development builds
([libraries.md](libraries.md#5-dependencies-by-component) §5).

There is no PAM library: PAM authentication is not ported
([libraries.md](libraries.md#ldap-and-pam)).

**Thrift 0.22.0.** The Python project's generated stubs were made with
Thrift 0.22.0 (`// Autogenerated by Thrift Compiler (0.22.0)` in
`codechecker_api/js/`, and `codechecker_api/Dockerfile` installs 0.22). The
web UI's lockfile resolves the `thrift` npm runtime to 0.22.0, and
`web/requirements.txt` asks for `thrift~=0.22`. Our compiler generates both
the C++ code and the web UI's JS stubs, so the same version keeps all
generated code equal to the reference's. (The reference environment has the
Python runtime 0.25.0 installed. The wire format is the same.)

**SQLite 3.54.1.** The Python version uses the distribution's libsqlite3
(3.46.1 on Debian 13), so no version is fixed by the reference. Releases
from 3.48.0 on build from the canonical sources without `tclsh`. Settings
that differ between distributions, such as `SQLITE_MAX_VARIABLE_NUMBER`, do
not matter: the server splits long `IN` lists into chunks of 999 itself
(`SQLITE_MAX_VARIABLE_NUMBER = 999` in `report_server.py`).

**Nested submodules.** Three libraries have submodules of their own:

| Library | Nested submodules | We check out |
|---|---|---|
| PCRE2 | `deps/sljit` | `deps/sljit`: the JIT compiler (`pcre2_jit_compile.c` includes `../deps/sljit/sljit_src/sljitLir.c`) |
| Boost | 172 libraries and tools | the 51 libraries in `third_party/boost-libraries.txt` (§3.2) |
| OpenSSL | 11: test suites, fuzz corpora, Python projects (`pyca-cryptography`, `python-ecdsa`, `tlslite-ng`) | none |

### 2.3 Checkout

```sh
git clone https://<host>/codechecker-cpp.git
cd codechecker-cpp
third_party/checkout.sh                 # or: third_party/checkout.sh --analyzer-only
```

`checkout.sh` runs these commands:

```sh
git submodule sync
git submodule update --init --depth 1 --jobs 8 [<analyzer-side paths only>]
git -C third_party/pcre2 submodule update --init --depth 1 deps/sljit
git -C third_party/boost submodule update --init --depth 1 --jobs 8 \
    $(grep -v '^#' third_party/boost-libraries.txt)      # skipped with --analyzer-only
```

**Never use `--recursive` or `git clone --recurse-submodules`.** That would
fetch all 172 Boost submodules (892 MB on their own) and OpenSSL's test
submodules, some of which are Python projects.

`.gitmodules` sets `shallow = true` for every submodule, with the upstream
URLs from §2.2. Shallow fetches of the pinned commits work on all three
hosts (GitHub, gitlab.gnome.org, git.openldap.org; checked).

Size of a full checkout, measured: 988 MB (727 MB of working trees and
261 MB of git objects), about 100 seconds on the development machine. The
largest are PostgreSQL (147 MB), OpenSSL (140 MB), Boost with 51 libraries
(140 MB) and SQLite (113 MB).

### 2.4 Rules

- **Pinned to release tags.** Every gitlink points at the commit of an
  upstream release tag, never at a branch head. CI checks this with
  `git -C third_party/<name> describe --exact-match --tags`.
- **Submodules are never changed.** All builds are out of tree, so
  `git status` stays clean in every submodule. There are no local patches.
  If a library needs one anyway, that is a new decision recorded on this
  page first.
- **One list.** A new submodule is added to the table in §2.2 and to
  [libraries.md](libraries.md), whose rule still applies: adding or
  replacing a library needs a recorded decision.

## 3. Building the libraries (`third_party/`)

### 3.1 How the deps build works

```sh
cmake -S third_party -B build/deps      # -DCC_DEPS_WEB=OFF for the analyzer side only
cmake --build build/deps --parallel
```

- **One `ExternalProject_Add` per library.** `SOURCE_DIR` is the
  submodule, there is no download or update step, and each library builds
  in `build/deps/ep/Build/<name>/`. Everything installs into
  `build/deps/install/`, the deps prefix. Output goes to log files in
  `build/deps/ep/Stamp/<name>/`, and is printed only when a step fails.
  Make-based builds share the parent's job server (`JOB_SERVER_AWARE`). A
  full build takes about 40 seconds on the 12 cores of the development
  machine.
- **Libraries find each other only in the prefix.** Dependencies between
  them are `DEPENDS` edges. Paths are passed explicitly: `OPENSSL_ROOT_DIR`,
  `ZLIB_ROOT`, `CMAKE_PREFIX_PATH` for CMake builds, and `CPPFLAGS`/
  `LDFLAGS` for `configure` builds. Every optional feature that would pull in
  another library is turned off explicitly (§3.2), so a library installed on
  the build machine is never picked up by accident.
- **Static and position-independent.** `BUILD_SHARED_LIBS=OFF` (or
  `--disable-shared`) and `CMAKE_POSITION_INDEPENDENT_CODE=ON`, because the
  programs are linked as PIE.
- **Build type.** `CC_DEPS_BUILD_TYPE`, default `Release`. Use
  `RelWithDebInfo` to step into a library in a debugger. The deps are not
  built with sanitizers. ASan and UBSan builds of the project link them as
  they are.
- **Compiler.** The deps build uses the compilers it is configured with (by
  default `cc`/`c++`). The project can then be built with GCC or Clang
  against the same prefix, because both use libstdc++ on Linux.
- **Sources hash.** `cmake/DepsSources.cmake` hashes the commit of every
  submodule (nested ones included) together with
  `third_party/CMakeLists.txt` and `boost-libraries.txt`. Checking out a
  submodule at another commit makes CMake configure again. When the hash
  differs from the last configure, the deps build deletes its `ep/` and
  `install/` folders and builds every library again. Library updates are
  rare, and a full build is short.
- **Manifest.** The last step writes `build/deps/install/cc-deps-manifest.cmake`
  with the sources hash and the options `CC_DEPS_WEB` and `CC_DEPS_TESTS`.
  The main project reads it at configure time (§4.1).

Dependencies between the libraries:

```
zlib ─────┬──► libzip
          └──► libcurl ◄──┐
OpenSSL ──┬───────────────┘
          ├──► libpq (PostgreSQL)
          └──► libldap (OpenLDAP)
Boost headers ──► Thrift (compiler + libthrift)
PCRE2, yyjson, libyaml, libxml2, SQLite, GoogleTest: no dependencies
cpp-httplib, jwt-cpp: headers only, copied into the prefix
```

### 3.2 Settings per library

**Built with CMake:**

| Library | Settings | Result in the prefix |
|---|---|---|
| PCRE2 | `PCRE2_BUILD_PCRE2_8=ON`, `PCRE2_BUILD_PCRE2_16/32=OFF`, `PCRE2_SUPPORT_JIT=ON` (default is `OFF`), `PCRE2_SUPPORT_UNICODE=ON`, `PCRE2_BUILD_PCRE2GREP=OFF`, `PCRE2_BUILD_TESTS=OFF`, `PCRE2_SUPPORT_LIBZ/LIBBZ2/LIBEDIT/LIBREADLINE=OFF` | `libpcre2-8.a` |
| yyjson | `YYJSON_BUILD_TESTS/FUZZER/MISC/DOC=OFF`; reader options stay on, because `YYJSON_READ_ALLOW_INF_AND_NAN` needs them | `libyyjson.a` |
| libyaml | `BUILD_TESTING=OFF` | `libyaml.a` |
| libxml2 | `LIBXML2_WITH_PYTHON=OFF`, `_ICU=OFF`, `_LZMA=OFF`, `_HTTP=OFF`, `_READLINE=OFF`, `_PROGRAMS=OFF`, `_TESTS=OFF`; `_ICONV=ON` (glibc's iconv) | `libxml2.a` |
| libzip | `ENABLE_BZIP2/LZMA/ZSTD=OFF`; `ENABLE_OPENSSL/GNUTLS/COMMONCRYPTO/WINDOWS_CRYPTO=OFF` (no encrypted archives); `BUILD_TOOLS/REGRESS/OSSFUZZ/EXAMPLES/DOC=OFF` | `libzip.a` |
| libcurl | `BUILD_STATIC_LIBS=ON`, `BUILD_CURL_EXE=OFF`, `BUILD_TESTING/EXAMPLES=OFF`, docs off; `CURL_USE_OPENSSL=ON`; `CURL_USE_LIBPSL=OFF`, `USE_LIBIDN2=OFF`, `USE_NGHTTP2=OFF`, `CURL_USE_LIBSSH2=OFF`, `CURL_USE_GSSAPI=OFF`, brotli and zstd off; `ENABLE_ARES=OFF` (threaded resolver); every protocol except HTTP and HTTPS off (`CURL_DISABLE_FTP`, `_FILE`, `_LDAP`, `_SMTP`, ...); `CURL_CA_BUNDLE=auto`, `CURL_CA_PATH=auto`, `CURL_CA_FALLBACK=ON` | `libcurl.a` |
| Thrift | `BUILD_COMPILER=ON`, `WITH_CPP=ON`; `WITH_C_GLIB/JAVA/JAVASCRIPT/NODEJS/PYTHON=OFF`; `WITH_OPENSSL/ZLIB/LIBEVENT/QT5=OFF` (we use no Thrift transports); `BUILD_TESTING/TUTORIALS=OFF`; `Boost_INCLUDE_DIR=<prefix>/include` | `bin/thrift`, `libthrift.a` |
| GoogleTest | `BUILD_GMOCK=ON`, `INSTALL_GTEST=ON`; built only with `CC_DEPS_TESTS=ON` (default) | `libgtest.a`, `libgtest_main.a`, `libgmock.a` |

libcurl uses only HTTP/1.1, like Python's `http.client`. Its CA bundle and CA
folder are detected when the deps build is configured. The client still
applies `SSL_CERT_FILE`/`SSL_CERT_DIR` with Python's rules
([libraries.md](libraries.md#http-client-libcurl)).

**Built with `configure` and make:**

- **zlib.** `<src>/configure --static --prefix=<prefix>`, then `make` and
  `make install`. Result: `libz.a`. zlib's CMake is not used, because for
  out-of-tree builds it renames `zconf.h` in the source folder
  (`CMakeLists.txt`, line 76), which would change the submodule.
- **OpenSSL.**
  `perl <src>/Configure no-shared no-apps no-docs no-tests
  --prefix=<prefix> --libdir=lib --openssldir=<CC_OPENSSLDIR>`, then
  `make build_libs` and `make install_dev` (libraries and headers only).
  `Configure` detects the platform itself.
  - `--libdir=lib` is needed because the default on x86_64 is `lib64`.
  - `CC_OPENSSLDIR` defaults to the directory `openssl version -d` prints on
    the build machine (`/usr/lib/ssl` on Debian). The default CA file and
    folder and `openssl.cnf` are then the ones Python's `ssl` uses in the
    reference environment (checked: `ssl.get_default_verify_paths()` gives
    `/usr/lib/ssl/cert.pem` and `/usr/lib/ssl/certs`).
  - The legacy provider is not needed. Every name in Python's
    `hashlib.algorithms_guaranteed` is in the default provider.
- **libpq.** PostgreSQL's `configure --prefix=<prefix> --with-ssl=openssl
  --without-icu --without-readline --without-zlib`. Then:
  - `make -C src/backend generated-headers` makes the headers libpq needs,
    such as `utils/errcodes.h`;
  - `make -C src/interfaces/libpq all-static-lib`;
  - `make install` in `src/include`, `src/common` and `src/port`, and
    `make install-lib-static` in `src/interfaces/libpq`;
  - `libpq-fe.h` and `libpq-events.h` are copied into the prefix.

  Only the static library is built. Building the shared `libpq.so` fails a
  check of PostgreSQL's Makefile: no function may call `exit()`, and the
  static OpenSSL in it references `pthread_exit`. libpq's objects are built
  for the shared library, so `libpq.a` is linked with `libpgcommon_shlib.a`
  and `libpgport_shlib.a`, as libpq's Makefile does. Meson is not used,
  because it would add Python to the host tools.
- **libldap.** OpenLDAP's `configure --prefix=<prefix> --sysconfdir=/etc
  --disable-slapd --disable-shared --enable-static --without-cyrus-sasl
  --with-tls=openssl --without-systemd`, then `make depend` and `make` and
  `make install` in `include` and `libraries`. SASL is not needed: the
  server only uses simple binds (`simple_bind_s` in `cc_ldap.py`). Result:
  `libldap.a`, `liblber.a`. `make install` would also write `ldap.conf`
  into the system's `/etc`. The install step therefore sets `sysconfdir`
  to a folder in the build tree. The library still reads
  `/etc/openldap/ldap.conf`.
- **SQLite.** `<src>/configure --disable-shared --disable-tcl`, then
  `make lib`, `make install-lib` and `make install-headers`. SQLite's own
  build makes the amalgamation (checked: `No installed jimsh or tclsh,
  building local bootstrap jimsh0`) and compiles it into `libsqlite3.a`
  with upstream's default compile options. New options are added only with a
  reason recorded here.

`configure` builds with dependencies (libpq, libldap) also get, in `LIBS`,
the system libraries that a static OpenSSL needs (`-pthread -ldl`), so their
link tests pass.

**Not compiled, headers copied into the prefix** (when the deps build is
configured):

- **Boost.** For every line of `boost-libraries.txt`, the deps build copies
  `libs/<library>/include/` into `<prefix>/include/`, which is what Boost's
  `b2 headers` does. That is 51 libraries and 57 MB of headers, and no Boost
  library is compiled. Thrift finds them through CMake's `FindBoost`, since
  Thrift sets `Boost_NO_BOOST_CMAKE` and policy `CMP0167` to `OLD`.
  - The list is the dependency closure that Boost's own CMake computes for
    the headers libthrift includes: `algorithm`, `locale` (only the
    header-only `utf_to_utf` in `TJSONProtocol.cpp`), `numeric_conversion`,
    `smart_ptr`, `tokenizer` and `uuid`.
  - Checked: libthrift 0.22.0 and 0.25.0 both build with only these 51
    libraries.
- **cpp-httplib.** `httplib.h`. `cmake/ThirdParty.cmake` sets
  `CPPHTTPLIB_OPENSSL_SUPPORT` on the imported target, without zlib, brotli
  or zstd support, because the Python server does not compress responses.
- **jwt-cpp.** `include/jwt-cpp/` and the bundled `include/picojson/`
  (`JWT_DISABLE_PICOJSON` stays off; see
  [libraries.md](libraries.md#jwt)).

## 4. The project build

### 4.1 Using the libraries

- **`CC_DEPS_PREFIX`** (default `${CMAKE_SOURCE_DIR}/build/deps/install`)
  says where the deps prefix is. All presets share one deps prefix.
- **Configure-time check.** The main configure step reads the manifest
  (§3.1) and compares it with the checked-out submodule commits and the
  deps build files. If the submodules are missing, the manifest is missing,
  or they do not match, configuring fails with the command to run
  (`third_party/checkout.sh`, or the deps build of §3.1). The project is
  never built against stale libraries.
- **Imported targets, no searching.** `cmake/ThirdParty.cmake` defines one
  imported `INTERFACE` target per library from exact files in the prefix,
  for example `${CC_DEPS_PREFIX}/lib/libpcre2-8.a`. Each target carries
  its own link dependencies, so static link order is handled by CMake. No
  `find_package` or `find_library` is called for a third-party library.
  Include folders are `SYSTEM`, so warnings in third-party headers do not
  fail `-Werror` builds.

| Target | Files | Compile definitions | Links |
|---|---|---|---|
| `third_party::pcre2` | `libpcre2-8.a` | `PCRE2_CODE_UNIT_WIDTH=8`, `PCRE2_STATIC` | |
| `third_party::yyjson` | `libyyjson.a` | | |
| `third_party::yaml` | `libyaml.a` | `YAML_DECLARE_STATIC` | |
| `third_party::xml2` | `libxml2.a` | `LIBXML_STATIC` | `m` |
| `third_party::zlib` | `libz.a` | | |
| `third_party::zip` | `libzip.a` | | zlib |
| `third_party::crypto` | `libcrypto.a` | | `Threads::Threads` |
| `third_party::ssl` | `libssl.a` | | crypto |
| `third_party::curl` | `libcurl.a` | `CURL_STATICLIB` | ssl, zlib |
| `third_party::httplib` | header | `CPPHTTPLIB_OPENSSL_SUPPORT` | ssl |
| `third_party::thrift` | `libthrift.a`, Boost headers | | |
| `third_party::sqlite3` | `libsqlite3.a` | | `Threads::Threads`, `m` |
| `third_party::pq` | `libpq.a`, `libpgcommon_shlib.a`, `libpgport_shlib.a` | | ssl, `m` |
| `third_party::jwt` | headers | | crypto |
| `third_party::ldap` | `libldap.a`, `liblber.a` | | ssl |
| `third_party::gtest` | `libgtest.a`, `libgtest_main.a`, `libgmock.a` | | `Threads::Threads` |

The `cc_*` libraries of [architecture.md](architecture.md#3-libraries-and-their-dependencies)
§3 link these targets as that table lists. `cc_util` links pcre2, yyjson,
yaml, xml2, crypto, zlib and zip. `cc_api` links thrift. `cc_client` links
curl and ssl. `cc_server` links httplib, sqlite3, pq, jwt, ldap, curl and
ssl.

### 4.2 Options and compiler settings

| Option | Default | Meaning |
|---|---|---|
| `CC_BUILD_WEB` | `ON` | build the web side ([architecture.md](architecture.md#3-libraries-and-their-dependencies) §3); needs a deps prefix built with `CC_DEPS_WEB=ON` |
| `CC_BUILD_WEB_UI` | `ON` with `CC_BUILD_WEB` | build `www/` with npm (§4.5) |
| `CC_WEB_UI_DIR` | empty | install this prebuilt `www/` folder instead of building one |
| `CC_BUILD_TESTS` | `ON` | GoogleTest unit tests |
| `CC_LDLOGGER_32BIT` | `ON` | also build the 32-bit `ldlogger.so` (§4.4) |
| `CC_WERROR` | `OFF` (`ON` in CI) | warnings are errors in project code |
| `CC_SANITIZE` | empty | for example `address;undefined` |
| `CC_DEPS_PREFIX` | `build/deps/install` | §4.1 |

- C++: `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`,
  `CMAKE_CXX_EXTENSIONS OFF`, with `-Wall -Wextra -Wpedantic` for project
  code. Coding rules are only in [cppguide.md](cppguide.md); the build just
  turns on the warnings.
- **Exported symbols.** Programs are linked with `-rdynamic`, so stack
  traces can name functions ([libraries.md](libraries.md#errors-and-stack-traces)).
  They are also linked with `-Wl,--exclude-libs,<the third-party archives>`,
  which keeps the statically linked third-party symbols out of the dynamic
  symbol table. Otherwise a system library that glibc loads into the
  program could bind to our OpenSSL copy instead of the system's
  `libcrypto.so.3`. One example is an NSS module (`libnss_ldap`, ...)
  loaded for `getpwuid()`. The `cc_*` symbols stay exported. Both options
  apply to C++ links only, so ldlogger keeps its Makefile's flags.
- **Run-time dependency check.** The `run_time_dependencies` test runs
  `readelf -d` on every program and fails if a `NEEDED` entry is anything
  other than glibc, libstdc++ or libgcc_s. Sanitizer builds may also need
  the compiler's sanitizer runtimes (`libasan`, `libubsan`).
- **Third-party test.** `unit_tests/third_party/` calls every imported
  target once. It checks the settings this page relies on: PCRE2 with JIT
  and Python syntax, yyjson reading `NaN`, libzip's `ZIP_FL_OVERWRITE`,
  curl with only HTTP and HTTPS, and libpq and libldap using OpenSSL.

### 4.3 Generated files

- **Thrift code.** For every `api/*.thrift`, a custom command runs
  `${CC_DEPS_PREFIX}/bin/thrift --gen cpp` into `build/<preset>/gen/api/`.
  The output is compiled into `cc_api` and regenerated when the `.thrift`
  file or the compiler changes. Nothing generated is checked in
  ([libraries.md](libraries.md#rpc-apache-thrift)). For the web UI, the same
  compiler runs with `--gen js:node`, as `codechecker_api/Makefile` does in
  the Python project.
- **API version.** `api/api_version.json` is read with `string(JSON)` at
  configure time and written into a header
  ([architecture.md](architecture.md#9-thrift-api-cc_api) §9).
- **Version files.** `analyzer_version.json` and `web_version.json` are
  written at build time, with the git hash and build date of the port. What
  version number they hold is still open
  ([architecture.md](architecture.md#open-questions), question 1).

### 4.4 ldlogger

The copied C sources in `ldlogger/` are built by CMake with the flags of the
Python project's `analyzer/tools/build-logger/Makefile`, copied as they are:

| Target | Output | Flags |
|---|---|---|
| `ldlogger` | `bin/ldlogger` | `-D_GNU_SOURCE -std=c99 -pedantic -Wall -Wextra -O2 -D__LOGGER_MAIN__` |
| `ldlogger_64` | `ld_logger/lib/64bit/ldlogger.so` | the same plus `-Wno-strict-aliasing -fno-exceptions -fPIC -fomit-frame-pointer -fvisibility=hidden`; linked with `-shared -Wl,--no-as-needed -ldl` |
| `ldlogger_32` | `ld_logger/lib/32bit/ldlogger.so` | as `ldlogger_64`, plus `-m32` when compiling and linking |

- ldlogger is plain C with no third-party libraries. The 32-bit library
  needs only gcc-multilib, not 32-bit copies of any dependency.
- `CC_LDLOGGER_32BIT=OFF` is the same as the Python build's
  `BUILD_LOGGER_64_BIT_ONLY=YES`: there is no `lib/32bit/`.
- The project's C++ settings (`-Werror`, sanitizers) are not applied to
  ldlogger. It is copied code ([README](README.md)), and `ldlogger.so` runs
  inside the user's build processes, where a sanitizer runtime would break
  them.
- One difference from the Python Makefile: its default target builds the
  `ldlogger` program with `-m32`. The port builds the program for the host
  in both modes. Its bitness does not change what it does; only
  `ldlogger.so` has to match the build processes it is loaded into.

### 4.5 Web UI

This answers [architecture.md](architecture.md#open-questions) question 2.
With `CC_BUILD_WEB_UI=ON`, the `web_ui` target:

1. copies `web-ui/` to `build/<preset>/web-ui/web/server/vue-cli/`. `web-ui/`
   is the reference's `web/server/vue-cli/`, copied unchanged together with
   `package.json`, `package-lock.json`, `.npmrc` (`engine-strict = true`,
   with `"node": ">=14.17.0"` in `package.json`) and `.babelrc`. It also
   creates `build/<preset>/web-ui/codechecker_api/` with `api_version.json`
   and the JS stubs generated by our Thrift compiler (§4.3). The copied
   webpack configuration reads them from `../../../codechecker_api`
   (`config/webpack.common.js`), and the copy must not be changed;
2. runs `npm ci` and `npm run build` there. `npm ci` installs exactly the
   versions in the copied lockfile, so the web UI is built from the same
   packages as the reference's;
3. installs `dist/` as `<root>/www/`.

The source tree stays clean, because `node_modules/` and `dist/` are created
in the build folder. With `CC_BUILD_WEB_UI=OFF`, a prebuilt folder can be
given with `CC_WEB_UI_DIR`. Without either one, the server is installed
without `www/`.

## 5. Commands and presets

```sh
third_party/checkout.sh                          # once, and after a submodule update
cmake -S third_party -B build/deps               # the libraries
cmake --build build/deps --parallel
cmake --preset dev                               # the project
cmake --build --preset dev
ctest --preset dev                               # unit tests
cmake --build --preset dev --target blackbox     # black-box suite (architecture.md §13)
```

The deps build is a separate step on purpose. It is slow, it is needed only
when a submodule changes, and every preset below shares it.

`CMakePresets.json` (binary folder `build/<preset>`, all with
`CC_DEPS_PREFIX=${sourceDir}/build/deps/install`):

| Preset | Settings |
|---|---|
| `dev` | `Debug`, unit tests on |
| `dev-clang` | `dev` with Clang |
| `asan` | `dev` with `CC_SANITIZE=address;undefined` |
| `release` | `RelWithDebInfo` |
| `analyzer` | `CC_BUILD_WEB=OFF` |

**Install.** `cmake --install build/release --prefix <root>` creates the
layout of [architecture.md](architecture.md#4-programs-and-install-layout)
§4. The tree can be moved: it has no RPATHs and no third-party shared
libraries, and programs find `<root>` from `/proc/self/exe`. Because
third-party code is linked into the programs, the license file of every
submodule is installed into `<root>/lib/codechecker/licenses/<library>/`.
The BSD, MIT, Apache 2.0, PostgreSQL and OpenLDAP licenses all require the
notice to come with binaries.

**Black-box suite.** The `blackbox` target installs into a temporary
prefix and runs `CODECHECKER_BIN=<prefix>/bin/CodeChecker npm test` in
`blackbox_tests/`.

## 6. CI

| Job | Checkout | Deps build | Project |
|---|---|---|---|
| GCC | full | default | `dev` with `CC_WERROR=ON`; unit tests, black-box suite |
| Clang | full | the GCC job's prefix | `dev-clang` with `CC_WERROR=ON`; unit tests |
| Sanitizers | full | the GCC job's prefix | `asan`; unit tests, black-box suite |
| Analyzer only | `--analyzer-only` | `CC_DEPS_WEB=OFF` | `analyzer` |

- The deps prefix is cached under a key made from the manifest inputs, so CI
  rebuilds the libraries only when a submodule or the deps build changes.
- The analyzer-only job does not even check out the web side's submodules.
  So the analyzer side cannot pick up a web library by accident (rule 5 of
  [libraries.md](libraries.md#how-we-decide)).
- Every job checks that each submodule is at a release tag (§2.4).

## 7. Updating a library

1. Check out the new tag in the submodule
   (`git -C third_party/<name> fetch --depth 1 origin tag <tag>` and
   `git checkout <tag>`).
2. Check the reason in the table of §2.2. libxml2 follows the reference
   environment's lxml, libpq follows psycopg2-binary, and Thrift follows the
   compiler version of the reference stubs. These change only together with
   the reference.
3. For Thrift: check `boost-libraries.txt` again (its includes and Boost's
   dependency closure). For Boost: the same list, at the new Boost version.
4. Build the libraries, then run the unit tests and the black-box suite.
5. Commit the gitlink and the updated row of §2.2 together.

**Security fixes are our job.** Third-party code is linked statically, so a
distribution update does not fix the port. We follow the security
advisories of OpenSSL, libcurl, libxml2, libzip, zlib, SQLite, PostgreSQL
(libpq), OpenLDAP and cpp-httplib, and ship a new build when one affects us.

## Exceptions

1. **npm packages** (decided 2026-10-25). The web UI is copied from the
   reference with its `package.json`, `package-lock.json` and `.npmrc`
   (§4.5), and gets its packages from the npm registry with `npm ci`, as the
   reference does. The lockfile pins the version and integrity hash of each
   of its 1456 packages. The black-box suite (330 packages) works the same
   way with its own lockfile. `npm ci` is the only step of the build that
   needs the network, or a local npm mirror or cache.
2. **The system itself.** glibc, libstdc++, libgcc_s, the kernel and the
   host tools of §1 are not third-party libraries in this sense. The
   analyzers (`clang`, `clang-tidy`, `cppcheck`, ...) and `git` are run, not
   linked ([libraries.md](libraries.md#5-dependencies-by-component) §5).

## Open questions

1. **Building on one distribution and running on another.** OpenSSL's
   `OPENSSLDIR` and libcurl's CA paths are taken from the build machine
   (§3.2). A tree built on Debian and run on Fedora would look for CA files
   in `/usr/lib/ssl`, not in `/etc/pki/tls`. If such builds must work, the
   client has to find the system's CA store at run time.
2. **The LDAP configuration file.** Our libldap reads
   `/etc/openldap/ldap.conf`. Debian's libldap, which python-ldap uses
   there, reads `/etc/ldap/ldap.conf`. This matters only for settings the
   server configuration does not set (for example `TLS_CACERT`). Options:
   choose the path per distribution at build time, or set `LDAPCONF` in the
   server.
3. **Repository hosts.** The submodule URLs point at upstream. Mirrors
   under the project's own organization would keep old pins fetchable if an
   upstream host moves or deletes tags, at the cost of keeping the mirrors
   up to date.
