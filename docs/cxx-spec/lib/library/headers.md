# 16.5.1.2 Headers [headers]

[Contents](../../README.md) · up: [16.5.1 Library contents and organization \[organization\]](organization.md) · prev: [16.5.1.1 Library contents \[contents\]](contents.md) · next: [16.5.1.3 Freestanding implementations \[compliance\]](compliance.md)

**1** Each element of the C++ standard library is declared or defined (as appropriate) in a *header*.[^1]

**2** The C++ standard library provides the *C++ library headers*, shown in [Table 21](headers.md#tab:headers.cpp).

<a id="tab:headers.cpp"></a>
**Table 21: C++ library headers** [tab:headers.cpp]

|  |
| --- |
| `<algorithm>` |
| `<any>` |
| `<array>` |
| `<atomic>` |
| `<barrier>` |
| `<bit>` |
| `<bitset>` |
| `<charconv>` |
| `<chrono>` |
| `<codecvt>` |
| `<compare>` |
| `<complex>` |
| `<concepts>` |
| `<condition_variable>` |
| `<coroutine>` |
| `<deque>` |
| `<exception>` |
| `<execution>` |
| `<filesystem>` |
| `<format>` |
| `<forward_list>` |
| `<fstream>` |
| `<functional>` |
| `<future>` |
| `<initializer_list>` |
| `<iomanip>` |
| `<ios>` |
| `<iosfwd>` |
| `<iostream>` |
| `<istream>` |
| `<iterator>` |
| `<latch>` |
| `<limits>` |
| `<list>` |
| `<locale>` |
| `<map>` |
| `<memory>` |
| `<memory_resource>` |
| `<mutex>` |
| `<new>` |
| `<numbers>` |
| `<numeric>` |
| `<optional>` |
| `<ostream>` |
| `<queue>` |
| `<random>` |
| `<ranges>` |
| `<ratio>` |
| `<regex>` |
| `<scoped_allocator>` |
| `<semaphore>` |
| `<set>` |
| `<shared_mutex>` |
| `<source_location>` |
| `<span>` |
| `<sstream>` |
| `<stack>` |
| `<stdexcept>` |
| `<stop_token>` |
| `<streambuf>` |
| `<string>` |
| `<string_view>` |
| `<strstream>` |
| `<syncstream>` |
| `<system_error>` |
| `<thread>` |
| `<tuple>` |
| `<typeindex>` |
| `<typeinfo>` |
| `<type_traits>` |
| `<unordered_map>` |
| `<unordered_set>` |
| `<utility>` |
| `<valarray>` |
| `<variant>` |
| `<vector>` |
| `<version>` |

**3** The facilities of the C standard library are provided in the additional headers shown in [Table 22](headers.md#tab:headers.cpp.c).[^2]

<a id="tab:headers.cpp.c"></a>
**Table 22: C++ headers for C library facilities** [tab:headers.cpp.c]

|  |
| --- |
| `<cassert>` |
| `<cctype>` |
| `<cerrno>` |
| `<cfenv>` |
| `<cfloat>` |
| `<cinttypes>` |
| `<climits>` |
| `<clocale>` |
| `<cmath>` |
| `<csetjmp>` |
| `<csignal>` |
| `<cstdarg>` |
| `<cstddef>` |
| `<cstdint>` |
| `<cstdio>` |
| `<cstdlib>` |
| `<cstring>` |
| `<ctime>` |
| `<cuchar>` |
| `<cwchar>` |
| `<cwctype>` |

**4** The headers listed in [Table 21](headers.md#tab:headers.cpp), or, for a freestanding implementation, the subset of such headers that are provided by the implementation, are collectively known as the *importable C++ library headers*. [*Note 1*: Importable C++ library headers can be imported as module units ([[module.import]](../../module/module.import.md)). — *end note*] [*Example 1*:

```cpp
import <vector>;                // imports the <vector> header unit
std::vector<int> vi;            // OK
```

— *end example*]

**5** Except as noted in [[library]](library.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md), the contents of each header `cname` is the same as that of the corresponding header `name.h` as specified in the C standard library ([[intro.refs]](../../intro/intro.refs.md)). In the C++ standard library, however, the declarations (except for names which are defined as macros in C) are within namespace scope ([[basic.scope.namespace]](../../basic/basic.scope.namespace.md)) of the namespace `std`. It is unspecified whether these names (including any overloads added in [[support]](../support/support.md) through [[thread]](../thread/thread.md) and [[depr]](../../depr/depr.md)) are first declared within the global namespace scope and are then injected into namespace `std` by explicit *using-declaration*s ([[namespace.udecl]](../../dcl/namespace.udecl.md)).

**6** Names which are defined as macros in C shall be defined as macros in the C++ standard library, even if C grants license for implementation as functions. [*Note 2*: The names defined as macros in C include the following: `assert`, `offsetof`, `setjmp`, `va_arg`, `va_end`, and `va_start`. — *end note*]

**7** Names that are defined as functions in C shall be defined as functions in the C++ standard library.[^3]

**8** Identifiers that are keywords or operators in C++ shall not be defined as macros in C++ standard library headers.[^4]

**9** [[depr.c.headers]](../../depr/depr.c.headers.md), C standard library headers, describes the effects of using the `name.h` (C header) form in a C++ program.[^5]

**10** Annex K of the C standard describes a large number of functions, with associated types and macros, which “promote safer, more secure programming” than many of the traditional C library functions. The names of the functions have a suffix of `_s`; most of them provide the same service as the C library function with the unsuffixed name, but generally take an additional argument whose value is the size of the result array. If any C++ header is included, it is implementation-defined whether any of these names is declared in the global namespace. (None of them is declared in namespace `std`.)

**11** [Table 23](headers.md#tab:c.annex.k.names) lists the Annex K names that may be declared in some header. These names are also subject to the restrictions of [[macro.names]](macro.names.md).

<a id="tab:c.annex.k.names"></a>
**Table 23: C standard Annex K names** [tab:c.annex.k.names]

|  |
| --- |
| `abort_handler_s` |
| `asctime_s` |
| `bsearch_s` |
| `constraint_handler_t` |
| `ctime_s` |
| `errno_t` |
| `fopen_s` |
| `fprintf_s` |
| `freopen_s` |
| `fscanf_s` |
| `fwprintf_s` |
| `fwscanf_s` |
| `getenv_s` |
| `gets_s` |
| `gmtime_s` |
| `ignore_handler_s` |
| `localtime_s` |
| `L_tmpnam_s` |
| `mbsrtowcs_s` |
| `mbstowcs_s` |
| `memcpy_s` |
| `memmove_s` |
| `memset_s` |
| `printf_s` |
| `qsort_s` |
| `RSIZE_MAX` |
| `rsize_t` |
| `scanf_s` |
| `set_constraint_handler_s` |
| `snprintf_s` |
| `snwprintf_s` |
| `sprintf_s` |
| `sscanf_s` |
| `strcat_s` |
| `strcpy_s` |
| `strerrorlen_s` |
| `strerror_s` |
| `strlen_s` |
| `strncat_s` |
| `strncpy_s` |
| `strtok_s` |
| `swprintf_s` |
| `swscanf_s` |
| `tmpfile_s` |
| `TMP_MAX_S` |
| `tmpnam_s` |
| `vfprintf_s` |
| `vfscanf_s` |
| `vfwprintf_s` |
| `vfwscanf_s` |
| `vprintf_s` |
| `vscanf_s` |
| `vsnprintf_s` |
| `vsnwprintf_s` |
| `vsprintf_s` |
| `vsscanf_s` |
| `vswprintf_s` |
| `vswscanf_s` |
| `vwprintf_s` |
| `vwscanf_s` |
| `wcrtomb_s` |
| `wcscat_s` |
| `wcscpy_s` |
| `wcsncat_s` |
| `wcsncpy_s` |
| `wcsnlen_s` |
| `wcsrtombs_s` |
| `wcstok_s` |
| `wcstombs_s` |
| `wctomb_s` |
| `wmemcpy_s` |
| `wmemmove_s` |
| `wprintf_s` |
| `wscanf_s` |

[^1]: A header is not necessarily a source file, nor are the sequences delimited by `<` and `>` in header names necessarily valid source file names ([[cpp.include]](../../cpp/cpp.include.md)).
[^2]: It is intentional that there is no C++ header for any of these C headers: `<stdatomic.h>`, `<stdnoreturn.h>`, `<threads.h>`.
[^3]: This disallows the practice, allowed in C, of providing a masking macro in addition to the function prototype. The only way to achieve equivalent inline behavior in C++ is to provide a definition as an extern inline function.
[^4]: In particular, including the standard header `<iso646.h>` has no effect.
[^5]: The `".h"` headers dump all their names into the global namespace, whereas the newer forms keep their names in namespace `std`. Therefore, the newer forms are the preferred forms for all uses except for C++ programs which are intended to be strictly compatible with C.
