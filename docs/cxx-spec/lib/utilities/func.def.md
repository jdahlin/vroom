# 20.14.2 Definitions [func.def]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14.1 Header `<functional>` synopsis \[functional.syn\]](functional.syn.md) · next: [20.14.3 Requirements \[func.require\]](func.require.md)

**1** The following definitions apply to this Clause:

**2** A *call signature* is the name of a return type followed by a parenthesized comma-separated list of zero or more argument types.

**3** A *callable type* is a function object type ([[function.objects]](function.objects.md)) or a pointer to member.

**4** A *callable object* is an object of a callable type.

**5** A *call wrapper type* is a type that holds a callable object and supports a call operation that forwards to that object.

**6** A *call wrapper* is an object of a call wrapper type.

**7** A *target object* is the callable object held by a call wrapper.

**8** A call wrapper type may additionally hold a sequence of objects and references that may be passed as arguments to the target object. These entities are collectively referred to as *bound argument entities*.

**9** The target object and bound argument entities of the call wrapper are collectively referred to as *state entities*.
