# 29.4.2 Overview [iostream.objects.overview]

[Contents](../../README.md) · up: [29.4 Standard iostream objects \[iostream.objects\]](iostream.objects.md) · prev: [29.4.1 Header `<iostream>` synopsis \[iostream.syn\]](iostream.syn.md) · next: [29.4.3 Narrow stream objects \[narrow.stream.objects\]](narrow.stream.objects.md)

**1** In this Clause, the type name `FILE` refers to the type `FILE` declared in `<cstdio>` ([[cstdio.syn]](cstdio.syn.md)).

**2** The header `<iostream>` declares objects that associate objects with the standard C streams provided for by the functions declared in `<cstdio>`, and includes all the headers necessary to use these objects.

**3** The objects are constructed and the associations are established at some time prior to or during the first time an object of class `ios_base::Init` is constructed, and in any case before the body of `main` ([[basic.start.main]](../../basic/basic.start.main.md)) begins execution.[^1] The objects are not destroyed during program execution.[^2] The results of including `<iostream>` in a translation unit shall be as if `<iostream>` defined an instance of `ios_base::Init` with static storage duration.

**4** Mixing operations on corresponding wide- and narrow-character streams follows the same semantics as mixing such operations on `FILE`s, as specified in the C standard library.

**5** Concurrent access to a synchronized ([[ios.members.static]](ios.members.static.md)) standard iostream object’s formatted and unformatted input ([[istream]](istream.md)) and output ([[ostream]](ostream.md)) functions or a standard C stream by multiple threads does not result in a data race ([[intro.multithread]](../../basic/intro.multithread.md)). [*Note 1*: Users must still synchronize concurrent use of these objects and streams by multiple threads if they wish to avoid interleaved characters. — *end note*]

*See also:* ISO C 7.21.2

[^1]: If it is possible for them to do so, implementations should initialize the objects earlier than required.
[^2]: Constructors and destructors for objects with static storage duration can access these objects to read input from `stdin` or write output to `stdout` or `stderr`.
