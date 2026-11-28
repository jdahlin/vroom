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
- **D3b, variables** (done for C): globals, parameters and locals of C
  functions at `-O0` (`codegen/debug-frame.cpp`, `DW_OP_fbreg`);
  `bench/debug-values.py` prints them.  Left: lexical blocks (locals are the
  function's, so a shadowed name is ambiguous); no location for VLAs, arm64
  locals aligned over 16, parameters passed by reference (arm64) or left in
  the caller's frame, and x86-64 frames without a frame pointer; thread-local
  and function-local static variables; C++ (D4).
- **D4a, C++ variables** (done): the C entries for C++ too: parameters,
  `this`, locals and globals at `-O0`; classes with members, base classes
  (`DW_TAG_inheritance`) and references.
- **D4b, C++ names** (done): namespaces as `DW_TAG_namespace` around
  functions, globals and classes; a class's name with its template arguments
  (types, integers and packs of them; other arguments are `?`) and the
  classes around it.
- **D4c, enumerations** (done): a variable, field or result of an
  enumeration type, or a pointer, reference or array of one, is described by a
  `DW_TAG_enumeration_type` built for the debugger (code generation still
  sees the underlying type).  Its enumerators are found by scanning every
  entity, once per enumeration.
- **D4d, class members** (done): member functions and static data members
  defined in the unit are declared in their class (object parameter
  artificial), their definitions refer to the declaration.  Members only
  declared or never used here are not listed.
- **D4e, virtual bases** (done): each direct virtual base is a
  `DW_TAG_inheritance` located through the virtual table.  Constructors and
  destructors are no longer listed in their class (their variants made wrong
  declarations); the virtual table pointer is not marked artificial.
- **D5, optimized code**: location lists at `-O1` and up; until then a
  variable there is optimized out.

## Cost of `-g` (x86-64, `-O0`, compiler instructions)

| input | without | with `-g` | object |
|---|---|---|---|
| SQLite amalgamation | 2.97 G | 3.36 G (+13 %) | 1.58 -> 2.18 MB |
| 3000 C++ classes and enums | 1.76 G | 2.53 G (+44 %; 4.48 G before the lookups were indexed) | 1.60 -> 3.1 MB |

## What is left, in order

1. Function-local static and thread-local variables (no entry).
2. Lexical blocks: locals are the function's, so shadowed names are
   ambiguous.  Needs a range per scope that survives VIR's block order.
3. No location: VLAs, arm64 locals aligned over 16, parameters passed by
   reference or left in the caller's frame, x86-64 frames without a frame
   pointer.
4. `DW_AT_decl_file`/`decl_line` (the `.file` table is written before the
   unit's entries are known).
5. C++: constructors and destructors in their class, the artificial vtable
   pointer, template arguments of functions, other template arguments (`?`).
6. C: typedef names under a pointer or on a result; vectors, `_BitInt`,
   scalable types.
7. Check on Windows (winedbg), C++ on arm64, and COFF weak functions.
8. D5: optimized code (location lists).

## Validation

`llvm-dwarfdump --verify` (`bench/vcc-dwarf.py`), the entries compared with
clang's; from D3 a scripted debugger session printing values.
