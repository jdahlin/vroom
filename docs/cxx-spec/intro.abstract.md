# 4.1.1 Abstract machine [intro.abstract]

[Contents](README.md) · up: [4.1 Implementation compliance \[intro.compliance\]](intro.compliance.md) · prev: [4.1 Implementation compliance \[intro.compliance\]](intro.compliance.md) · next: [4.2 Structure of this document \[intro.structure\]](intro.structure.md)

**1** The semantic descriptions in this document define a parameterized nondeterministic abstract machine. This document places no requirement on the structure of conforming implementations. In particular, they need not copy or emulate the structure of the abstract machine. Rather, conforming implementations are required to emulate (only) the observable behavior of the abstract machine as explained below.[^1]

**2** Certain aspects and operations of the abstract machine are described in this document as implementation-defined (for example, `sizeof(int)`). These constitute the parameters of the abstract machine. Each implementation shall include documentation describing its characteristics and behavior in these respects.[^2] Such documentation shall define the instance of the abstract machine that corresponds to that implementation (referred to as the “corresponding instance” below).

**3** Certain other aspects and operations of the abstract machine are described in this document as unspecified (for example, order of evaluation of arguments in a function call ([[expr.call]](expr.call.md))). Where possible, this document defines a set of allowable behaviors. These define the nondeterministic aspects of the abstract machine. An instance of the abstract machine can thus have more than one possible execution for a given program and a given input.

**4** Certain other operations are described in this document as undefined (for example, the effect of attempting to modify a const object). [*Note 1*: This document imposes no requirements on the behavior of programs that contain undefined behavior. — *end note*]

**5** A conforming implementation executing a well-formed program shall produce the same observable behavior as one of the possible executions of the corresponding instance of the abstract machine with the same program and the same input. However, if any such execution contains an undefined operation, this document places no requirement on the implementation executing that program with that input (not even with regard to operations preceding the first undefined operation).

**6** The least requirements on a conforming implementation are:

- Accesses through volatile glvalues are evaluated strictly according to the rules of the abstract machine.
- At program termination, all data written into files shall be identical to one of the possible results that execution of the program according to the abstract semantics would have produced.
- The input and output dynamics of interactive devices shall take place in such a fashion that prompting output is actually delivered before a program waits for input. What constitutes an interactive device is implementation-defined.

These collectively are referred to as the *observable behavior* of the program. [*Note 2*: More stringent correspondences between abstract and actual semantics may be defined by each implementation. — *end note*]

[^1]: This provision is sometimes called the “as-if” rule, because an implementation is free to disregard any requirement of this document as long as the result is *as if* the requirement had been obeyed, as far as can be determined from the observable behavior of the program. For instance, an actual implementation need not evaluate part of an expression if it can deduce that its value is not used and that no side effects affecting the observable behavior of the program are produced.
[^2]: This documentation also includes conditionally-supported constructs and locale-specific behavior. See [[intro.compliance]](intro.compliance.md).
