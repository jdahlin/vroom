# The C++ front end's memory

Peak RSS of `vpp -x c++ -fsyntax-only` against `clang++ -fsyntax-only` on
OpenTTD (the CMake precompiled header as `-include`), 2026-11-28, x86-64
Linux, clang 23.  Times are from one run on a shared machine and vary by
tens of percent; the RSS figures repeat to a few MB.

| file | before | after | clang++ | after / clang |
|---|---|---|---|---|
| network/network_command.cpp | 2119 MB | 591 MB | 963 MB | 0.61 |
| openttd.cpp | 973 MB | 425 MB | 430 MB | 0.99 |
| window.cpp | 504 MB | 269 MB | 281 MB | 0.96 |

The objects `vpp -c` writes for the three are byte for byte the same before
and after.  The target of using significantly less than clang is met for the largest
file; the other two are about even with it.

## Where it went (network_command.cpp, before)

3 674 615 entities of 456 bytes are 1.6 GB of the 2.1 GB.  The 377 625
types (40 bytes each) are 15 MB.  By kind: typedef-names 1.52 M, block
scopes 1.04 M, enumerators 0.41 M, packs 0.22 M, variables 0.12 M,
functions 0.09 M.  Most were template-parameter bindings: a scope and an
entity per template-argument, made on every substitution.  The largest
sites were the constraint check (413 k scopes, also for templates with no
constraint), alias template specializations (246 k) and default
template-arguments (125 k).

## What was done

| change | network_command.cpp |
|---|---|
| no scope for an unconstrained template's constraint check | 2120 -> 1483 MB |
| one scope per (parameters, arguments, enclosing): `argument_scope` | 1483 -> 1040 MB |
| the entity's bools as bit-fields (456 -> 384 bytes) | 1040 -> 941 MB |

Afterwards there are about 1.44 M entities.

## Splitting the entity

A dump of every record after the changes above (network_command.cpp):
1 437 333 records, 1 164 092 of them distinct, and 366 621 distinct once the
name, type, value, enclosing scope and declaration chain are left out, so
interning whole records gains little (and records are mutable, their index
their identity).  A record had 16.7 bytes of non-zero members on average of
384; 75 members, 276 bytes, were set in under 2 % of the records.

So `entity` (120 bytes) keeps what many records set, and `entity_details`
(296 bytes) the rest, made on the first write (`e.edit().x`) and read as the
defaults until then (`e.details().x`).  Copies of an entity share the details
until either writes.  network_command.cpp 941 -> 648 MB, openttd.cpp 583 ->
464 MB, window.cpp 345 -> 289 MB.  cxx-fe-bench: +0.28 % instructions against
main for all the changes here.

## Padding and empty chains

- `entity`'s and `entity_details`' members ordered by alignment: 120 -> 104
  and 296 -> 272 bytes.
- `record_chains` keeps its chains' ends in pages made for keys that have
  records.  Five of them are keyed by entity and had 8 bytes (one 4) for
  every entity, about 40 MB in network_command.cpp.  This also took
  cxx-fe-bench from +0.28 % to -6.6 % instructions against main.

## What is left

openttd.cpp at the previous step (463 MB resident): the core entities 82 MB,
the details 55 MB, tokens 51 MB and preprocessing tokens 31 MB (both stay for
the whole unit), the semantic tree 38 MB, the name binding slots 17 MB, the
syntax tree about 23 MB, instantiation records 10 MB.  No single part is
large any more; the candidates are details per kind, 32-bit indexes for the
details pointer, the two token arrays, and `semantic_node` (48 bytes).
