# 12.4 Overload resolution [over.match]

[Contents](README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.3 Declaration matching \[over.dcl\]](over.dcl.md) · next: [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md)

**1** Overload resolution is a mechanism for selecting the best function to call given a list of expressions that are to be the arguments of the call and a set of *candidate functions* that can be called based on the context of the call. The selection criteria for the best function are the number of arguments, how well the arguments match the parameter-type-list of the candidate function, how well (for non-static member functions) the object matches the implicit object parameter, and certain other properties of the candidate function. [*Note 1*: The function selected by overload resolution is not guaranteed to be appropriate for the context. Other restrictions, such as the accessibility of the function, can make its use in the calling context ill-formed. — *end note*]

**2** Overload resolution selects the function to call in seven distinct contexts within the language:

- invocation of a function named in the function call syntax ([[over.call.func]](over.call.func.md));
- invocation of a function call operator, a pointer-to-function conversion function, a reference-to-pointer-to-function conversion function, or a reference-to-function conversion function on a class object named in the function call syntax ([[over.call.object]](over.call.object.md));
- invocation of the operator referenced in an expression ([[over.match.oper]](over.match.oper.md));
- invocation of a constructor for default- or direct-initialization ([[dcl.init]](dcl.init.md)) of a class object ([[over.match.ctor]](over.match.ctor.md));
- invocation of a user-defined conversion for copy-initialization ([[dcl.init]](dcl.init.md)) of a class object ([[over.match.copy]](over.match.copy.md));
- invocation of a conversion function for initialization of an object of a non-class type from an expression of class type ([[over.match.conv]](over.match.conv.md)); and
- invocation of a conversion function for conversion in which a reference ([[dcl.init.ref]](dcl.init.ref.md)) will be directly bound ([[over.match.ref]](over.match.ref.md)).

Each of these contexts defines the set of candidate functions and the list of arguments in its own unique way. But, once the candidate functions and argument lists have been identified, the selection of the best function is the same in all cases:

- First, a subset of the candidate functions (those that have the proper number of arguments and meet certain other conditions) is selected to form a set of viable functions ([[over.match.viable]](over.match.viable.md)).
- Then the best viable function is selected based on the implicit conversion sequences ([[over.best.ics]](over.best.ics.md)) needed to match each argument to the corresponding parameter of each viable function.

**3** If a best viable function exists and is unique, overload resolution succeeds and produces it as the result. Otherwise overload resolution fails and the invocation is ill-formed. When overload resolution succeeds, and the best viable function is not accessible ([[class.access]](class.access.md)) in the context in which it is used, the program is ill-formed.

**4** Overload resolution results in a *usable candidate* if overload resolution succeeds and the selected candidate is either not a function ([[over.built]](over.built.md)), or is a function that is not deleted and is accessible from the context in which overload resolution was performed.

## Subclauses

- [12.4.1 Candidate functions and argument lists \[over.match.funcs\]](over.match.funcs.md)
  - [12.4.1.1 Function call syntax \[over.match.call\]](over.match.call.md)
    - [12.4.1.1.1 Call to named function \[over.call.func\]](over.call.func.md)
    - [12.4.1.1.2 Call to object of class type \[over.call.object\]](over.call.object.md)
  - [12.4.1.2 Operators in expressions \[over.match.oper\]](over.match.oper.md)
  - [12.4.1.3 Initialization by constructor \[over.match.ctor\]](over.match.ctor.md)
  - [12.4.1.4 Copy-initialization of class by user-defined conversion \[over.match.copy\]](over.match.copy.md)
  - [12.4.1.5 Initialization by conversion function \[over.match.conv\]](over.match.conv.md)
  - [12.4.1.6 Initialization by conversion function for direct reference binding \[over.match.ref\]](over.match.ref.md)
  - [12.4.1.7 Initialization by list-initialization \[over.match.list\]](over.match.list.md)
  - [12.4.1.8 Class template argument deduction \[over.match.class.deduct\]](over.match.class.deduct.md)
- [12.4.2 Viable functions \[over.match.viable\]](over.match.viable.md)
- [12.4.3 Best viable function \[over.match.best\]](over.match.best.md)
  - [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md)
    - [12.4.3.1.1 Standard conversion sequences \[over.ics.scs\]](over.ics.scs.md)
    - [12.4.3.1.2 User-defined conversion sequences \[over.ics.user\]](over.ics.user.md)
    - [12.4.3.1.3 Ellipsis conversion sequences \[over.ics.ellipsis\]](over.ics.ellipsis.md)
    - [12.4.3.1.4 Reference binding \[over.ics.ref\]](over.ics.ref.md)
    - [12.4.3.1.5 List-initialization sequence \[over.ics.list\]](over.ics.list.md)
  - [12.4.3.2 Ranking implicit conversion sequences \[over.ics.rank\]](over.ics.rank.md)
