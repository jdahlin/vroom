# Debug information: functions, types, variables

`-g` writes line tables (`codegen/debug-rows.cpp`, the assembler) and one
compile unit (`codegen/debug-unit.cpp`).  This plan adds what a debugger needs
to name functions and print variables, one pull request per step.  ELF and
COFF; Mach-O has no `-g` yet.

## Decision (2026-11-28)

`-O0` keeps every local and parameter in memory, as clang does, with or without
`-g` (so `-g` still does not change the code).  Today `-O0` keeps some in
registers; D3 changes that and measures run time and compile time.

## Steps

- **D1, functions** (done): a `DW_TAG_subprogram` per function emitted, with
  name, linkage name (C++), address range and external.  C++ units are
  `DW_LANG_C_plus_plus_14`.  Left: `DW_AT_decl_file`/`decl_line`,
  `DW_AT_frame_base` (D3), scopes for C++ names (D4); COFF weak functions
  (C++ inline ones too) have no entry.
- **D2, C types** (done): a C function's result, parameters and the types
  under them: base, `_Complex`, pointer, qualified, atomic, typedef, array,
  struct, union, enum, function (`src/test/debug-info`, `bench/debug-info.py`).
  Left: a typedef name is kept only where a parameter or member is declared
  with it exactly (not a result's, not under a pointer, array or qualifier:
  the AST's types are canonical); vectors, `_BitInt` and scalable types are
  `DW_TAG_unspecified_type`.  C++ functions have no types yet (D4).
- **D3a, locals in memory at -O0** (done): VIR pins the slot of a named local
  or parameter.  x86-64, SQLite amalgamation, `-O0`, against D2: compiler
  instructions 3.85 G -> 2.96 G, text 1457 kB -> 1187 kB; speedtest1
  (`--size 20 :memory:`) instructions executed +2.5 % (task clock too noisy
  to say).
- **D3b, variables**: globals, formal parameters and locals in lexical blocks
  (`DW_OP_fbreg`, `DW_AT_frame_base`), with a debugger test that prints
  values.
- **D4, C++ types**: classes, members, bases, references, namespaces,
  templates.
- **D5, optimized code**: location lists at `-O1` and up; until then a
  variable there is optimized out.

## Validation

`llvm-dwarfdump --verify` (`bench/vcc-dwarf.py`), the entries compared with
clang's; from D3 a scripted debugger session printing values.
