# 16.4.2.2.1 General [type.descriptions.general]

[Contents](../README.md) · up: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · prev: [16.4.2.2 Type descriptions \[type.descriptions\]](type.descriptions.md) · next: [16.4.2.2.2 Exposition-only types \[expos.only.types\]](expos.only.types.md)

**1** The Requirements subclauses may describe names that are used to specify constraints on template arguments.[^1] These names are used in library Clauses to describe the types that may be supplied as arguments by a C++ program when instantiating template components from the library.

**2** Certain types defined in [[input.output]](input.output.md) are used to describe implementation-defined types. They are based on other types, but with added constraints.

[^1]: Examples from [[utility.requirements]](utility.requirements.md) include: *Cpp17EqualityComparable*, *Cpp17LessThanComparable*, *Cpp17CopyConstructible*. Examples from [[iterator.requirements]](iterator.requirements.md) include: *Cpp17InputIterator*, *Cpp17ForwardIterator*.
