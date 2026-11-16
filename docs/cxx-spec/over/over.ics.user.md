# 12.4.3.1.2 User-defined conversion sequences [over.ics.user]

[Contents](../README.md) · up: [12.4.3.1 Implicit conversion sequences \[over.best.ics\]](over.best.ics.md) · prev: [12.4.3.1.1 Standard conversion sequences \[over.ics.scs\]](over.ics.scs.md) · next: [12.4.3.1.3 Ellipsis conversion sequences \[over.ics.ellipsis\]](over.ics.ellipsis.md)

**1** A *user-defined conversion sequence* consists of an initial standard conversion sequence followed by a user-defined conversion ([[class.conv]](../class/class.conv.md)) followed by a second standard conversion sequence. If the user-defined conversion is specified by a constructor ([[class.conv.ctor]](../class/class.conv.ctor.md)), the initial standard conversion sequence converts the source type to the type required by the argument of the constructor. If the user-defined conversion is specified by a conversion function ([[class.conv.fct]](../class/class.conv.fct.md)), the initial standard conversion sequence converts the source type to the implicit object parameter of the conversion function.

**2** The second standard conversion sequence converts the result of the user-defined conversion to the target type for the sequence; any reference binding is included in the second standard conversion sequence. Since an implicit conversion sequence is an initialization, the special rules for initialization by user-defined conversion apply when selecting the best user-defined conversion for a user-defined conversion sequence (see [[over.match.best]](over.match.best.md) and [[over.best.ics]](over.best.ics.md)).

**3** If the user-defined conversion is specified by a specialization of a conversion function template, the second standard conversion sequence shall have exact match rank.

**4** A conversion of an expression of class type to the same class type is given Exact Match rank, and a conversion of an expression of class type to a base class of that type is given Conversion rank, in spite of the fact that a constructor (i.e., a user-defined conversion function) is called for those cases.
