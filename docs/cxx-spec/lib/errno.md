# 19.4 Error numbers [errno]

[Contents](../README.md) · up: [19 Diagnostics library \[diagnostics\]](diagnostics.md) · prev: [19.3.2 The `assert` macro \[assertions.assert\]](assertions.assert.md) · next: [19.4.1 Header `<cerrno>` synopsis \[cerrno.syn\]](cerrno.syn.md)

**1** The contents of the header `<cerrno>` are the same as the POSIX header `<errno.h>`, except that `errno` shall be defined as a macro. [*Note 1*: The intent is to remain in close alignment with the POSIX standard. — *end note*] A separate `errno` value shall be provided for each thread.

## Subclauses

- [19.4.1 Header `<cerrno>` synopsis \[cerrno.syn\]](cerrno.syn.md)
