# 6.7.5.2 Thread storage duration [basic.stc.thread]

[Contents](../README.md) · up: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md) · prev: [6.7.5.1 Static storage duration \[basic.stc.static\]](basic.stc.static.md) · next: [6.7.5.3 Automatic storage duration \[basic.stc.auto\]](basic.stc.auto.md)

**1** All variables declared with the `thread_local` keyword have *thread storage duration*. The storage for these entities lasts for the duration of the thread in which they are created. There is a distinct object or reference per thread, and use of the declared name refers to the entity associated with the current thread.

**2** [*Note 1*: A variable with thread storage duration is initialized as specified in [[basic.start.static]](basic.start.static.md), [[basic.start.dynamic]](basic.start.dynamic.md), and [[stmt.dcl]](../stmt/stmt.dcl.md) and, if constructed, is destroyed on thread exit ([[basic.start.term]](basic.start.term.md)). — *end note*]
