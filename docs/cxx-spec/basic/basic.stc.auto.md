# 6.7.5.3 Automatic storage duration [basic.stc.auto]

[Contents](../README.md) · up: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md) · prev: [6.7.5.2 Thread storage duration \[basic.stc.thread\]](basic.stc.thread.md) · next: [6.7.5.4 Dynamic storage duration \[basic.stc.dynamic\]](basic.stc.dynamic.md)

**1** Block-scope variables not explicitly declared `static`, `thread_local`, or `extern` have *automatic storage duration*. The storage for these entities lasts until the block in which they are created exits.

**2** [*Note 1*: These variables are initialized and destroyed as described in [[stmt.dcl]](../stmt/stmt.dcl.md). — *end note*]

**3** If a variable with automatic storage duration has initialization or a destructor with side effects, an implementation shall not destroy it before the end of its block nor eliminate it as an optimization, even if it appears to be unused, except that a class object or its copy/move may be eliminated as specified in [[class.copy.elision]](../class/class.copy.elision.md).
