# 6.7.5.1 Static storage duration [basic.stc.static]

[Contents](README.md) · up: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md) · prev: [6.7.5 Storage duration \[basic.stc\]](basic.stc.md) · next: [6.7.5.2 Thread storage duration \[basic.stc.thread\]](basic.stc.thread.md)

**1** All variables which do not have dynamic storage duration, do not have thread storage duration, and are not local have *static storage duration*. The storage for these entities lasts for the duration of the program ([[basic.start.static]](basic.start.static.md), [[basic.start.term]](basic.start.term.md)).

**2** If a variable with static storage duration has initialization or a destructor with side effects, it shall not be eliminated even if it appears to be unused, except that a class object or its copy/move may be eliminated as specified in [[class.copy.elision]](class.copy.elision.md).

**3** The keyword `static` can be used to declare a local variable with static storage duration. [*Note 1*: [[stmt.dcl]](stmt.dcl.md) describes the initialization of local `static` variables; [[basic.start.term]](basic.start.term.md) describes the destruction of local `static` variables. — *end note*]

**4** The keyword `static` applied to a class data member in a class definition gives the data member static storage duration.
