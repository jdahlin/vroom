# 29.2.3 Thread safety [iostreams.threadsafety]

[Contents](../../README.md) · up: [29.2 Iostreams requirements \[iostreams.requirements\]](iostreams.requirements.md) · prev: [29.2.2 Positioning type limitations \[iostreams.limits.pos\]](iostreams.limits.pos.md) · next: [29.3 Forward declarations \[iostream.forward\]](iostream.forward.md)

**1** Concurrent access to a stream object ([[string.streams]](string.streams.md), [[file.streams]](file.streams.md)), stream buffer object ([[stream.buffers]](stream.buffers.md)), or C Library stream ([[c.files]](c.files.md)) by multiple threads may result in a data race ([[intro.multithread]](../../basic/intro.multithread.md)) unless otherwise specified ([[iostream.objects]](iostream.objects.md)). [*Note 1*: Data races result in undefined behavior ([[intro.multithread]](../../basic/intro.multithread.md)). — *end note*]

**2** If one thread makes a library call *a* that writes a value to a stream and, as a result, another thread reads this value from the stream through a library call *b* such that this does not result in a data race, then *a*’s write synchronizes with *b*’s read.
