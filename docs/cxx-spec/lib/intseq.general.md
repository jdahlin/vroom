# 20.3.1 In general [intseq.general]

[Contents](../README.md) · up: [20.3 Compile-time integer sequences \[intseq\]](intseq.md) · prev: [20.3 Compile-time integer sequences \[intseq\]](intseq.md) · next: [20.3.2 Class template `integer_sequence` \[intseq.intseq\]](intseq.intseq.md)

**1** The library provides a class template that can represent an integer sequence. When used as an argument to a function template the template parameter pack defining the sequence can be deduced and used in a pack expansion. [*Note 1*: The `index_sequence` alias template is provided for the common case of an integer sequence of type `size_t`; see also [[tuple.apply]](tuple.apply.md). — *end note*]
