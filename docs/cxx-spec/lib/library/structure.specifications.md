# 16.4.1.4 Detailed specifications [structure.specifications]

[Contents](../../README.md) · up: [16.4.1 Structure of each clause \[structure\]](structure.md) · prev: [16.4.1.3 Requirements \[structure.requirements\]](structure.requirements.md) · next: [16.4.1.5 C library \[structure.see.also\]](structure.see.also.md)

**1** The detailed specifications each contain the following elements:

- name and brief description
- synopsis (class definition or function declaration, as appropriate)
- restrictions on template arguments, if any
- description of class invariants
- description of function semantics

**2** Descriptions of class member functions follow the order (as appropriate):[^1]

- constructor(s) and destructor
- copying, moving & assignment functions
- comparison functions
- modifier functions
- observer functions
- operators and other non-member functions

**3** Descriptions of function semantics contain the following elements (as appropriate):[^2]

- *Constraints:* the conditions for the function’s participation in overload resolution ([[over.match]](../../over/over.match.md)). [*Note 1*: Failure to meet such a condition results in the function’s silent non-viability. — *end note*] [*Example 1*: An implementation might express such a condition via a *constraint-expression* ([[temp.constr.decl]](../../temp/temp.constr.decl.md)). — *end example*]
- *Mandates:* the conditions that, if not met, render the program ill-formed. [*Example 2*: An implementation might express such a condition via the *constant-expression* in a *static\_assert-declaration* ([[dcl.pre]](../../dcl/dcl.pre.md)). If the diagnostic is to be emitted only after the function has been selected by overload resolution, an implementation might express such a condition via a *constraint-expression* ([[temp.constr.decl]](../../temp/temp.constr.decl.md)) and also define the function as deleted. — *end example*]
- *Preconditions:* the conditions that the function assumes to hold whenever it is called.
- *Effects:* the actions performed by the function.
- *Synchronization:* the synchronization operations ([[intro.multithread]](../../basic/intro.multithread.md)) applicable to the function.
- *Postconditions:* the conditions (sometimes termed observable results) established by the function.
- *Returns:* a description of the value(s) returned by the function.
- *Throws:* any exceptions thrown by the function, and the conditions that would cause the exception.
- *Complexity:* the time and/or space complexity of the function.
- *Remarks:* additional semantic constraints on the function.
- *Error conditions:* the error conditions for error codes reported by the function.

**4** Whenever the *Effects* element specifies that the semantics of some function `F` are *Equivalent to* some code sequence, then the various elements are interpreted as follows. If `F`’s semantics specifies any *Constraints* or *Mandates* elements, then those requirements are logically imposed prior to the *equivalent-to* semantics. Next, the semantics of the code sequence are determined by the *Constraints*, *Mandates*, *Preconditions*, *Effects*, *Synchronization*, *Postconditions*, *Returns*, *Throws*, *Complexity*, *Remarks*, and *Error conditions* specified for the function invocations contained in the code sequence. The value returned from `F` is specified by `F`’s *Returns* element, or if `F` has no *Returns* element, a non-`void` return from `F` is specified by the `return` statements ([[stmt.return]](../../stmt/stmt.return.md)) in the code sequence. If `F`’s semantics contains a *Throws*, *Postconditions*, or *Complexity* element, then that supersedes any occurrences of that element in the code sequence.

**5** For non-reserved replacement and handler functions, [[support]](../support/support.md) specifies two behaviors for the functions in question: their required and default behavior. The *default behavior* describes a function definition provided by the implementation. The *required behavior* describes the semantics of a function definition provided by either the implementation or a C++ program. Where no distinction is explicitly made in the description, the behavior described is the required behavior.

**6** If the formulation of a complexity requirement calls for a negative number of operations, the actual requirement is zero operations.[^3]

**7** Complexity requirements specified in the library clauses are upper bounds, and implementations that provide better complexity guarantees meet the requirements.

**8** Error conditions specify conditions where a function may fail. The conditions are listed, together with a suitable explanation, as the `enum class errc` constants ([[syserr]](../diagnostics/syserr.md)).

[^1]: To save space, items that do not apply to a class are omitted. For example, if a class does not specify any comparison functions, there will be no “Comparison functions” subclause.
[^2]: To save space, elements that do not apply to a function are omitted. For example, if a function specifies no preconditions, there will be no *Preconditions:* element.
[^3]: This simplifies the presentation of complexity requirements in some cases.
