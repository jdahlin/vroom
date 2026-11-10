# An `#include`d file is not found

The preprocessor searched for the file and did not find it.  For `#include "name"` it looks
in the including file's directory, then in the `-iquote` and `-I` directories, then in the
system directories; for `#include <name>` it starts at the `-I` directories.  The search
stops the compilation, since nothing after it can be checked without the declarations.

Check the spelling and the case (the file system may be case-sensitive), add the
directory with `-I`, or install the package that provides the header.  For a header that
only some systems have, test for it with `__has_include`.

```c wrong
#include "config_local.h"
int x;
```

```c fixed
#if __has_include("config_local.h")
#include "config_local.h"
#endif
int x;
```
