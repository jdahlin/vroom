# 32.2.2 Exceptions [thread.req.exception]

[Contents](../README.md) · up: [32.2 Requirements \[thread.req\]](thread.req.md) · prev: [32.2.1 Template parameter names \[thread.req.paramname\]](thread.req.paramname.md) · next: [32.2.3 Native handles \[thread.req.native\]](thread.req.native.md)

**1** Some functions described in this Clause are specified to throw exceptions of type `system_error` ([[syserr.syserr]](syserr.syserr.md)). Such exceptions are thrown if any of the function’s error conditions is detected or a call to an operating system or other underlying API results in an error that prevents the library function from meeting its specifications. Failure to allocate storage is reported as described in [[res.on.exception.handling]](res.on.exception.handling.md).

[*Example 1*: Consider a function in this clause that is specified to throw exceptions of type `system_error` and specifies error conditions that include `operation_not_permitted` for a thread that does not have the privilege to perform the operation. Assume that, during the execution of this function, an `errno` of `EPERM` is reported by a POSIX API call used by the implementation. Since POSIX specifies an `errno` of `EPERM` when “the caller does not have the privilege to perform the operation”, the implementation maps `EPERM` to an `error_condition` of `operation_not_permitted` ([[syserr]](syserr.md)) and an exception of type `system_error` is thrown. — *end example*]

**2** The `error_code` reported by such an exception’s `code()` member function compares equal to one of the conditions specified in the function’s error condition element.
