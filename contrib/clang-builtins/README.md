# Clang builtin TableGen inputs

`REVISION` records the LLVM commit. The files under `clang/` are
unmodified upstream files, including Clang's license. The import covers
the eight entry points in `CLANG_TABLES` in `bench/vcc-builtins.py` and
their transitive TableGen includes. The upstream paths are preserved so
the same include directives work in an LLVM checkout and in this import.

Build the offline reader with a C++20 host compiler and its standard
library. The check needs Python 3 and verifies each table against
`spellings.sha256`. The inventory command additionally runs live Clang
and vpp feature probes.

```sh
make -C src -j 6 clang-builtin-reader-check
bench/vcc-builtins.py --clang-vendored --all-targets \
    --language c --language c++ --no-calls --report-only --json
```

The reader executable defaults to `build/linux/src/clang-builtin-reader`
on Linux or `build/src/clang-builtin-reader` on macOS. Use
`--clang-reader` to select a different build. The reader itself accepts
a Clang include root and a TableGen entry file and prints sorted unique
spellings, one per line. Compiler builds and runtime do not load these files.
Add `--metadata` before the include root to emit JSON Lines containing
the reference records and type variants. Use `make -C src builtin-registrations`
to regenerate vroom's compiled registrations and C stub table.

For an upstream update, copy the entry points, their transitive includes,
and the license from the new revision, then update `REVISION`. Recheck the
reader against the new upstream spelling sets before updating the reference
manifest. Each row records an entry filename, a spelling count, and the
SHA-256 digest of the sorted, deduplicated spellings joined by newlines,
including a final newline. The initial sets were captured from LLVM's
`clang-tblgen` enumerators using these actions.

| Entry files | Reference action |
| --- | --- |
| `Builtins.td`, `BuiltinsX86.td`, `BuiltinsX86_64.td`, `BuiltinsAArch64.td` | `-gen-clang-builtins` |
| `arm_neon.td`, `arm_fp16.td` | `-gen-arm-neon-sema` |
| `arm_sve.td` | `-gen-arm-sve-builtins` |
| `arm_sme.td` | `-gen-arm-sme-builtins` |
