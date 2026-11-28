# The C++ front end's memory

Peak RSS of `vpp -x c++ -fsyntax-only` against `clang++ -fsyntax-only` on
OpenTTD (the CMake precompiled header as `-include`), 2026-11-28, x86-64
Linux, clang 23.  Times are from one run on a shared machine and vary by
tens of percent; the RSS figures repeat to a few MB.

| file | before | after | clang++ | after / clang |
|---|---|---|---|---|
| network/network_command.cpp | 2119 MB, 8.3 s | 941 MB, 6.2 s | 963 MB, 9.9 s | 0.98 |
| openttd.cpp | 973 MB, 2.8 s | 583 MB, 2.9 s | 430 MB, 4.7 s | 1.36 |
| window.cpp | 504 MB, 1.6 s | 345 MB, 1.5 s | 281 MB, 2.5 s | 1.23 |

The objects `vpp -c` writes for the three are byte for byte the same before
and after.  The target of using significantly less than clang is not met.

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

## What is left

At the point of the last measurement the entity table was still most of the
memory (690 MB of about 900 mapped and resident before the bit-fields;
about 550 MB at 384 bytes).  A dump of every record (before the rebase):

- 1 437 333 records, 1 164 092 of them distinct; 366 621 distinct once the
  name, type, value, enclosing scope and declaration chain are left out.
  Interning whole records therefore gains little, and records are mutable
  with their index as identity.
- A record has 16.7 bytes of non-zero members on average, of 384.
- Typedef-names (34 %) use at most 25 bytes, block scopes (22 %) at most 9.
- 75 members, 276 bytes, are set in fewer than 2 % of the records.

So the next step is not dedup but splitting `entity`: a core of what every
kind uses (about 40-48 bytes) and records per kind (class, function, data
member) made when first written.  Estimated, not measured: the entity table
falls from about 550 MB to under 150 MB, and network_command.cpp to about
550 MB.  It changes every access to a moved member.

Smaller, not done: `substitute_specialization` makes its own binding scope
(sharing it gained under 3 MB), and the specializations' own scopes
(`record_instantiation`) are kept per specialization.
