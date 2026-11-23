# 19.5 System error support [syserr]

[Contents](../../README.md) · up: [19 Diagnostics library \[diagnostics\]](diagnostics.md) · prev: [19.4.1 Header `<cerrno>` synopsis \[cerrno.syn\]](cerrno.syn.md) · next: [19.5.1 Header `<system_error>` synopsis \[system.error.syn\]](system.error.syn.md)

**1** This subclause describes components that the standard library and C++ programs may use to report error conditions originating from the operating system or other low-level application program interfaces.

**2** Components described in this subclause shall not change the value of `errno` ([[errno]](errno.md)). Implementations should leave the error states provided by other libraries unchanged.

## Subclauses

- [19.5.1 Header `<system_error>` synopsis \[system.error.syn\]](system.error.syn.md)
- [19.5.2 Class `error_category` \[syserr.errcat\]](syserr.errcat.md)
  - [19.5.2.1 Overview \[syserr.errcat.overview\]](syserr.errcat.overview.md)
  - [19.5.2.2 Virtual members \[syserr.errcat.virtuals\]](syserr.errcat.virtuals.md)
  - [19.5.2.3 Non-virtual members \[syserr.errcat.nonvirtuals\]](syserr.errcat.nonvirtuals.md)
  - [19.5.2.4 Program-defined classes derived from `error_category` \[syserr.errcat.derived\]](syserr.errcat.derived.md)
  - [19.5.2.5 Error category objects \[syserr.errcat.objects\]](syserr.errcat.objects.md)
- [19.5.3 Class `error_code` \[syserr.errcode\]](syserr.errcode.md)
  - [19.5.3.1 Overview \[syserr.errcode.overview\]](syserr.errcode.overview.md)
  - [19.5.3.2 Constructors \[syserr.errcode.constructors\]](syserr.errcode.constructors.md)
  - [19.5.3.3 Modifiers \[syserr.errcode.modifiers\]](syserr.errcode.modifiers.md)
  - [19.5.3.4 Observers \[syserr.errcode.observers\]](syserr.errcode.observers.md)
  - [19.5.3.5 Non-member functions \[syserr.errcode.nonmembers\]](syserr.errcode.nonmembers.md)
- [19.5.4 Class `error_condition` \[syserr.errcondition\]](syserr.errcondition.md)
  - [19.5.4.1 Overview \[syserr.errcondition.overview\]](syserr.errcondition.overview.md)
  - [19.5.4.2 Constructors \[syserr.errcondition.constructors\]](syserr.errcondition.constructors.md)
  - [19.5.4.3 Modifiers \[syserr.errcondition.modifiers\]](syserr.errcondition.modifiers.md)
  - [19.5.4.4 Observers \[syserr.errcondition.observers\]](syserr.errcondition.observers.md)
  - [19.5.4.5 Non-member functions \[syserr.errcondition.nonmembers\]](syserr.errcondition.nonmembers.md)
- [19.5.5 Comparison functions \[syserr.compare\]](syserr.compare.md)
- [19.5.6 System error hash support \[syserr.hash\]](syserr.hash.md)
- [19.5.7 Class `system_error` \[syserr.syserr\]](syserr.syserr.md)
  - [19.5.7.1 Overview \[syserr.syserr.overview\]](syserr.syserr.overview.md)
  - [19.5.7.2 Members \[syserr.syserr.members\]](syserr.syserr.members.md)
