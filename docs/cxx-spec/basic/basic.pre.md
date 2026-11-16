# 6.1 Preamble [basic.pre]

[Contents](../README.md) · up: [6 Basics \[basic\]](basic.md) · prev: [6 Basics \[basic\]](basic.md) · next: [6.2 Declarations and definitions \[basic.def\]](basic.def.md)

**1** [*Note 1*: This Clause presents the basic concepts of the C++ language. It explains the difference between an object and a name and how they relate to the value categories for expressions. It introduces the concepts of a declaration and a definition and presents C++’s notion of type, scope, linkage, and storage duration. The mechanisms for starting and terminating a program are discussed. Finally, this Clause presents the fundamental types of the language and lists the ways of constructing compound types from these. — *end note*]

**2** [*Note 2*: This Clause does not cover concepts that affect only a single part of the language. Such concepts are discussed in the relevant Clauses. — *end note*]

**3** An *entity* is a value, object, reference, structured binding, function, enumerator, type, class member, bit-field, template, template specialization, namespace, or pack.

**4** A *name* is a use of an *identifier* ([[lex.name]](../lex/lex.name.md)), *operator-function-id* ([[over.oper]](../over/over.oper.md)), *literal-operator-id* ([[over.literal]](../over/over.literal.md)), *conversion-function-id* ([[class.conv.fct]](../class/class.conv.fct.md)), or *template-id* ([[temp.names]](../temp/temp.names.md)) that denotes an entity or label ([[stmt.goto]](../stmt/stmt.goto.md), [[stmt.label]](../stmt/stmt.label.md)).

**5** Every name that denotes an entity is introduced by a *declaration*. Every name that denotes a label is introduced either by a `goto` statement ([[stmt.goto]](../stmt/stmt.goto.md)) or a *labeled-statement* ([[stmt.label]](../stmt/stmt.label.md)).

**6** A *variable* is introduced by the declaration of a reference other than a non-static data member or of an object. The variable’s name, if any, denotes the reference or object.

**7** A *local entity* is a variable with automatic storage duration ([[basic.stc.auto]](basic.stc.auto.md)), a structured binding ([[dcl.struct.bind]](../dcl/dcl.struct.bind.md)) whose corresponding variable is such an entity, or the `*this` object ([[expr.prim.this]](../expr/expr.prim.this.md)).

**8** Some names denote types or templates. In general, whenever a name is encountered it is necessary to determine whether that name denotes one of these entities before continuing to parse the program that contains it. The process that determines this is called *name lookup* ([[basic.lookup]](basic.lookup.md)).

**9** Two names are *the same* if

- they are *identifier*s composed of the same character sequence, or
- they are *operator-function-id*s formed with the same operator, or
- they are *conversion-function-id*s formed with the same type, or
- they are *template-id*s that refer to the same class, function, or variable ([[temp.type]](../temp/temp.type.md)), or
- they are *literal-operator-id*s ([[over.literal]](../over/over.literal.md)) formed with the same literal suffix identifier.

**10** A name used in more than one translation unit can potentially refer to the same entity in these translation units depending on the linkage ([[basic.link]](basic.link.md)) of the name specified in each translation unit.
