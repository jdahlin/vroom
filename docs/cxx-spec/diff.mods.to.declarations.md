# C.6.3 Modifications to declarations [diff.mods.to.declarations]

[Contents](README.md) · up: [C.6 C standard library \[diff.library\]](diff.library.md) · prev: [C.6.2.7 Macro `NULL` \[diff.null\]](diff.null.md) · next: [C.6.4 Modifications to behavior \[diff.mods.to.behavior\]](diff.mods.to.behavior.md)

**1** Header `<cstring>` ([[cstring.syn]](lib/cstring.syn.md)): The following functions have different declarations:

- `strchr`
- `strpbrk`
- `strrchr`
- `strstr`
- `memchr`

Subclause [[cstring.syn]](lib/cstring.syn.md) describes the changes.

**2** Header `<cwchar>` ([[cwchar.syn]](lib/cwchar.syn.md)): The following functions have different declarations:

- `wcschr`
- `wcspbrk`
- `wcsrchr`
- `wcsstr`
- `wmemchr`

Subclause [[cwchar.syn]](lib/cwchar.syn.md) describes the changes.

**3** Header `<cstddef>` ([[cstddef.syn]](lib/cstddef.syn.md)) declares the name `nullptr_t` in addition to the names declared in `<stddef.h>` ([[depr.c.headers]](depr.c.headers.md)) in the C standard library.
