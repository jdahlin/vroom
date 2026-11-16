# 29.11.2.3 File system race behavior [fs.race.behavior]

[Contents](../README.md) · up: [29.11.2 Conformance \[fs.conformance\]](fs.conformance.md) · prev: [29.11.2.2 Operating system dependent behavior conformance \[fs.conform.os\]](fs.conform.os.md) · next: [29.11.3 Normative references \[fs.norm.ref\]](fs.norm.ref.md)

**1** A *file system race* is the condition that occurs when multiple threads, processes, or computers interleave access and modification of the same object within a file system. Behavior is undefined if calls to functions provided by subclause [[fs.race.behavior]](fs.race.behavior.md) introduce a file system race.

**2** If the possibility of a file system race would make it unreliable for a program to test for a precondition before calling a function described herein, *Preconditions:* is not specified for the function. [*Note 1*: As a design practice, preconditions are not specified when it is unreasonable for a program to detect them prior to calling the function. — *end note*]
