# The C++ front end's memory

Peak RSS of `vpp -x c++ -fsyntax-only` against `clang++ -fsyntax-only` on
OpenTTD (the CMake precompiled header as `-include`), 2026-11-28, x86-64
Linux, clang 23.  Times are from one run on a shared machine and vary by
tens of percent; the RSS figures repeat to a few MB.

| file | before | after | clang++ | after / clang |
|---|---|---|---|---|
| network/network_command.cpp | 2119 MB | 648 MB | 963 MB | 0.67 |
| openttd.cpp | 973 MB | 464 MB | 430 MB | 1.08 |
| window.cpp | 504 MB | 289 MB | 281 MB | 1.03 |

The objects `vpp -c` writes for the three are byte for byte the same before
and after.  The target of using significantly less than clang is met for the largest
file, not for the other two.

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

## What is left

openttd.cpp and window.cpp are still a little above clang; they have not been
profiled by themselves.  In network_command.cpp the core record is still
1.4 M x 120 bytes; whether the entity table or something else is now the
largest part has not been measured.  `substitute_specialization` makes its
own binding scope (sharing it gained under 3 MB), and the specializations' own
scopes (`record_instantiation`) are kept per specialization.
