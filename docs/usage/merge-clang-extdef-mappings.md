# Merge CTU definition maps (`merge-clang-extdef-mappings`)

For CTU analysis, Clang's `clang-extdef-mapping` tool writes, for each source
file, which functions it defines (by USR). `merge-clang-extdef-mappings`
combines the per-file outputs into one `externalDefMap.txt`.
`CodeChecker analyze --ctu` does this itself; the tool is for custom CTU
pipelines.

```
merge-clang-extdef-mappings -i INPUT_FOLDER -o OUTPUT_FILE
```

## Example ✅

```sh
E=/usr/lib/llvm-19/bin/clang-extdef-mapping     # not on PATH on Debian
mkdir -p extdef
$E -p . main.c     > extdef/main.txt
$E -p . lib/util.c > extdef/util.txt
merge-clang-extdef-mappings -i extdef -o externalDefMap.txt
cat externalDefMap.txt
```

```
9:c:@F@main /home/user/demo/main.c
11:c:@F@divide /home/user/demo/main.c
14:c:@F@local_div /home/user/demo/main.c
10:c:@F@deref /home/user/demo/main.c
13:c:@F@get_zero /home/user/demo/lib/util.c
13:c:@F@get_null /home/user/demo/lib/util.c
```

Each line is `<USR length>:<USR> <file that defines it>`.

## Conflicting definitions are dropped ✅

If a USR is defined in more than one file, it is left out of the result,
because it would be ambiguous for CTU. In the test, a copy of `util.txt`
pointing to `lib/util_copy.c` was added to the input folder. The merged file
then had only the four `main.c` lines; `get_zero` and `get_null` were gone.

Exit code `0`.
