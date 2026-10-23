# Record build commands (`CodeChecker log`)

The analyzers need to know how each source file is compiled. `CodeChecker log`
runs your build command and records every compiler call into a JSON
compilation database (`compile_commands.json` format). This database is the
input of `CodeChecker analyze`.

On Linux, recording uses `ldlogger`: a shared library loaded with
`LD_PRELOAD` that catches every `exec` of a compiler.

```
CodeChecker log -b COMMAND -o LOGFILE [-k] [-q]
                [--use-absolute-ldpreload-path] [--verbose ...]
```

## Record a build ✅

```sh
cd ~/demo
make clean
CodeChecker log -b "make" -o compile_commands.json
```

```
[INFO] - Starting build...
[INFO] - Using CodeChecker ld-logger.
gcc -c main.c -o main.o
gcc -c lib/util.c -o lib/util.o
gcc -o app main.o lib/util.o
g++ -std=c++17 -c style.cpp -o style.o
[INFO] - Build finished successfully.
```

`compile_commands.json`:

```json
[
	{
		"directory": "/home/user/demo",
		"command": "/usr/bin/gcc -c main.c -o main.o",
		"file": "main.c"
	}
	,
	{
		"directory": "/home/user/demo",
		"command": "/usr/bin/gcc -c lib/util.c -o lib/util.o",
		"file": "lib/util.c"
	}
	,
	{
		"directory": "/home/user/demo",
		"command": "/usr/bin/g++ -std=c++17 -c style.cpp -o style.o",
		"file": "style.cpp"
	}
]
```

Notes:
- An existing output file is overwritten.
- The compiler path is made absolute (`/usr/bin/gcc`).
- The link-only step (`gcc -o app main.o lib/util.o`) is left out by default.
- Only files that were actually compiled are recorded, so run a clean
  build first.
- The build command can be anything: `make`, `ninja`, a direct `g++` call or a
  script, e.g. `-b "cd ~/demo && ./build.sh"`.

## Keep link commands (`-k`) ✅

```sh
CodeChecker log -b "make" -o compile_commands.json -k
```

The file now also has entries for link steps and for object files. With the
demo project it contains 8 entries instead of 3, e.g.:

```
main.o     | /usr/bin/gcc -o app main.o lib/util.o
lib/util.o | /usr/bin/gcc -o app main.o lib/util.o
```

Setting `CC_LOGGER_KEEP_LINK=true` has the same effect ✅.

## Hide the build output (`-q`) ✅

```sh
CodeChecker log -b "make" -o compile_commands.json -q
```

Only CodeChecker's own log lines are printed.

## Load the logger by absolute path ✅

```sh
CodeChecker log -b "make" -o compile_commands.json --use-absolute-ldpreload-path
```

Puts the full path of `ldlogger.so` into `LD_PRELOAD` instead of relying on
`LD_LIBRARY_PATH`. Useful when the build resets `LD_LIBRARY_PATH`.

## Debug the logger ✅

```sh
CodeChecker log -b "make" -o compile_commands.json --verbose debug
```

Writes a `codechecker.logger.debug` file next to the output file. To write it
somewhere else, set `CC_LOGGER_DEBUG_FILE=/path/to/file` ✅.

## Choose which compilers are logged (`CC_LOGGER_GCC_LIKE`) ✅

By default, any executable whose name contains `gcc`, `g++`, `clang`,
`clang++`, `cc` or `c++` is logged
(`CC_LOGGER_GCC_LIKE="gcc:g++:clang:clang++:/cc:c++"`).

```sh
CC_LOGGER_GCC_LIKE="g++" CodeChecker log -b "make" -o compile_commands.json
```

With the demo project only the `style.cpp` entry is recorded.

An entry without a slash matches anywhere in the compiler name. An entry with a
slash must match the end of the path. Start an entry with `/` to match the
binary name exactly.

## Add the compiler's implicit include paths (`CC_LOGGER_DEF_DIRS`) ✅

```sh
CC_LOGGER_DEF_DIRS=1 CodeChecker log -b "make" -o compile_commands.json
```

```
"command": "/usr/bin/gcc -I/usr/include/c++/14 -I/usr/include/x86_64-linux-gnu/c++/14 ... -I/usr/include -c main.c -o main.o"
```

## Make include paths absolute (`CC_LOGGER_ABS_PATH`) ✅

```sh
CC_LOGGER_ABS_PATH=1 CodeChecker log -b "gcc -Ilib -c main.c -o main.o" -o compile_commands.json
```

```
"command": "/usr/bin/gcc -I/home/user/demo/lib -c main.c -o main.o"
```

This applies to the arguments of `-I`, `-idirafter`, `-imultilib`, `-iquote`,
`-isysroot`, `-isystem`, `-iwithprefix`, `-iwithprefixbefore`, `-sysroot` and
`--sysroot`.

## Use `ldlogger` directly, without CodeChecker ✅

The logger works without the `CodeChecker` command. Load it into any build:

```sh
export CC_LOGGER_GCC_LIKE="gcc:g++"
export CC_LOGGER_FILE=$PWD/compile_commands.json
LD_PRELOAD=/path/to/CodeChecker/ld_logger/lib/64bit/ldlogger.so make
```

The result has the same format. 32-bit builds use `ld_logger/lib/32bit/`.
`CC_LOGGER_FILE` sets the output file. The package also has an `ldlogger`
wrapper binary in `bin/`.

## Other ways to get a compilation database

`analyze` accepts any compilation database in the standard format, not only
ones from `CodeChecker log`. Both `"command"` (one string) and `"arguments"`
(list) entries are accepted. Other ways to create one:
CMake's `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, Bear, `intercept-build`, and
[bazel-compile-commands.md](bazel-compile-commands.md).

## Exit code

The exit code of the build command (`0` if the build succeeded).
