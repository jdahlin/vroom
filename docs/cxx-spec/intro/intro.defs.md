# 3 Terms and definitions [intro.defs]

[Contents](../README.md) · prev: [2 Normative references \[intro.refs\]](intro.refs.md) · next: [4 General principles \[intro\]](intro.md)

**1** For the purposes of this document, the terms and definitions given in ISO/IEC 2382-1:1993, the terms, definitions, and symbols given in ISO 80000-2:2009, and the following apply.

**2** ISO and IEC maintain terminological databases for use in standardization at the following addresses:

- ISO Online browsing platform: available at <https://www.iso.org/obp>
- IEC Electropedia: available at <http://www.electropedia.org/>

**3** [[definitions]](../lib/library/definitions.md) defines additional terms that are used only in [[library]](../lib/library/library.md) through [[thread]](../lib/thread/thread.md) and [[depr]](../depr/depr.md).

**4** Terms that are used only in a small portion of this document are defined where they are used and italicized where they are defined.

<a id="defns.access"></a>
### 3.1 access [defns.access]

⟨execution-time action⟩ read ([[conv.lval]](../expr/conv.lval.md)) or modify ([[expr.ass]](../expr/expr.ass.md), [[expr.post.incr]](../expr/expr.post.incr.md), [[expr.pre.incr]](../expr/expr.pre.incr.md)) the value of an object

[*Note 1 to entry*: Only objects of scalar type can be accessed. Attempts to read or modify an object of class type typically invoke a constructor ([[class.ctor]](../class/class.ctor.md)) or assignment operator ([[class.copy.assign]](../class/class.copy.assign.md)); such invocations do not themselves constitute accesses, although they may involve accesses of scalar subobjects. — *end note*]

<a id="defns.argument"></a>
### 3.2 argument [defns.argument]

⟨function call expression⟩ expression in the comma-separated list bounded by the parentheses ([[expr.call]](../expr/expr.call.md))

<a id="defns.argument.macro"></a>
### 3.3 argument [defns.argument.macro]

⟨function-like macro⟩ sequence of preprocessing tokens in the comma-separated list bounded by the parentheses ([[cpp.replace]](../cpp/cpp.replace.md))

<a id="defns.argument.throw"></a>
### 3.4 argument [defns.argument.throw]

⟨throw expression⟩ operand of `throw` ([[expr.throw]](../expr/expr.throw.md))

<a id="defns.argument.templ"></a>
### 3.5 argument [defns.argument.templ]

⟨template instantiation⟩ *constant-expression*, *type-id*, or *id-expression* in the comma-separated list bounded by the angle brackets ([[temp.arg]](../temp/temp.arg.md))

<a id="defns.block"></a>
### 3.6 block [defns.block]

⟨execution⟩ wait for some condition (other than for the implementation to execute the execution steps of the thread of execution) to be satisfied before continuing execution past the blocking operation

<a id="defns.block.stmt"></a>
### 3.7 block [defns.block.stmt]

⟨statement⟩ compound statement ([[stmt.block]](../stmt/stmt.block.md))

<a id="defns.cond.supp"></a>
### 3.8 conditionally-supported [defns.cond.supp]

program construct that an implementation is not required to support

[*Note 1 to entry*: Each implementation documents all conditionally-supported constructs that it does not support. — *end note*]

<a id="defns.diagnostic"></a>
### 3.9 diagnostic message [defns.diagnostic]

message belonging to an implementation-defined subset of the implementation’s output messages

<a id="defns.dynamic.type"></a>
### 3.10 dynamic type [defns.dynamic.type]

⟨glvalue⟩ type of the most derived object ([[intro.object]](../basic/intro.object.md)) to which the glvalue refers

[*Example 1*: If a pointer ([[dcl.ptr]](../dcl/dcl.ptr.md)) `p` whose static type is “pointer to class `B`” is pointing to an object of class `D`, derived from `B` ([[class.derived]](../class/class.derived.md)), the dynamic type of the expression `*p` is “`D`”. References ([[dcl.ref]](../dcl/dcl.ref.md)) are treated similarly. — *end example*]

<a id="defns.dynamic.type.prvalue"></a>
### 3.11 dynamic type [defns.dynamic.type.prvalue]

⟨prvalue⟩ static type of the prvalue expression

<a id="defns.ill.formed"></a>
### 3.12 ill-formed program [defns.ill.formed]

program that is not well-formed ([[defns.well.formed]](intro.defs.md#defns.well.formed))

<a id="defns.impl.defined"></a>
### 3.13 implementation-defined behavior [defns.impl.defined]

behavior, for a well-formed program construct and correct data, that depends on the implementation and that each implementation documents

<a id="defns.impl.limits"></a>
### 3.14 implementation limits [defns.impl.limits]

restrictions imposed upon programs by the implementation

<a id="defns.locale.specific"></a>
### 3.15 locale-specific behavior [defns.locale.specific]

behavior that depends on local conventions of nationality, culture, and language that each implementation documents

<a id="defns.multibyte"></a>
### 3.16 multibyte character [defns.multibyte]

sequence of one or more bytes representing a member of the extended character set of either the source or the execution environment

[*Note 1 to entry*: The extended character set is a superset of the basic character set ([[lex.charset]](../lex/lex.charset.md)). — *end note*]

<a id="defns.parameter"></a>
### 3.17 parameter [defns.parameter]

⟨function or catch clause⟩ object or reference declared as part of a function declaration or definition or in the catch clause of an exception handler that acquires a value on entry to the function or handler

<a id="defns.parameter.macro"></a>
### 3.18 parameter [defns.parameter.macro]

⟨function-like macro⟩ identifier from the comma-separated list bounded by the parentheses immediately following the macro name

<a id="defns.parameter.templ"></a>
### 3.19 parameter [defns.parameter.templ]

⟨template⟩ member of a *template-parameter-list*

<a id="defns.signature"></a>
### 3.20 signature [defns.signature]

⟨function⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), and enclosing namespace (if any)

[*Note 1 to entry*: Signatures are used as a basis for name mangling and linking. — *end note*]

<a id="defns.signature.friend"></a>
### 3.21 signature [defns.signature.friend]

⟨non-template friend function with trailing *requires-clause*⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), enclosing class, and trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md))

<a id="defns.signature.templ"></a>
### 3.22 signature [defns.signature.templ]

⟨function template⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), enclosing namespace (if any), return type, *template-head*, and trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md)) (if any)

<a id="defns.signature.templ.friend"></a>
### 3.23 signature [defns.signature.templ.friend]

⟨friend function template with constraint involving enclosing template parameters⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), return type, enclosing class, *template-head*, and trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md)) (if any)

<a id="defns.signature.spec"></a>
### 3.24 signature [defns.signature.spec]

⟨function template specialization⟩ signature of the template of which it is a specialization and its template arguments (whether explicitly specified or deduced)

<a id="defns.signature.member"></a>
### 3.25 signature [defns.signature.member]

⟨class member function⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), class of which the function is a member, *cv*-qualifiers (if any), *ref-qualifier* (if any), and trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md)) (if any)

<a id="defns.signature.member.templ"></a>
### 3.26 signature [defns.signature.member.templ]

⟨class member function template⟩ name, parameter-type-list ([[dcl.fct]](../dcl/dcl.fct.md)), class of which the function is a member, *cv*-qualifiers (if any), *ref-qualifier* (if any), return type (if any), *template-head*, and trailing *requires-clause* ([[dcl.decl]](../dcl/dcl.decl.md)) (if any)

<a id="defns.signature.member.spec"></a>
### 3.27 signature [defns.signature.member.spec]

⟨class member function template specialization⟩ signature of the member function template of which it is a specialization and its template arguments (whether explicitly specified or deduced)

<a id="defns.static.type"></a>
### 3.28 static type [defns.static.type]

type of an expression ([[basic.types]](../basic/basic.types.md)) resulting from analysis of the program without considering execution semantics

[*Note 1 to entry*: The static type of an expression depends only on the form of the program in which the expression appears, and does not change while the program is executing. — *end note*]

<a id="defns.unblock"></a>
### 3.29 unblock [defns.unblock]

satisfy a condition that one or more blocked threads of execution are waiting for

<a id="defns.undefined"></a>
### 3.30 undefined behavior [defns.undefined]

behavior for which this document imposes no requirements

[*Note 1 to entry*: Undefined behavior may be expected when this document omits any explicit definition of behavior or when a program uses an erroneous construct or erroneous data. Permissible undefined behavior ranges from ignoring the situation completely with unpredictable results, to behaving during translation or program execution in a documented manner characteristic of the environment (with or without the issuance of a diagnostic message), to terminating a translation or execution (with the issuance of a diagnostic message). Many erroneous program constructs do not engender undefined behavior; they are required to be diagnosed. Evaluation of a constant expression never exhibits behavior explicitly specified as undefined in [[intro]](intro.md) through [[cpp]](../cpp/cpp.md) of this document ([[expr.const]](../expr/expr.const.md)). — *end note*]

<a id="defns.unspecified"></a>
### 3.31 unspecified behavior [defns.unspecified]

behavior, for a well-formed program construct and correct data, that depends on the implementation

[*Note 1 to entry*: The implementation is not required to document which behavior occurs. The range of possible behaviors is usually delineated by this document. — *end note*]

<a id="defns.well.formed"></a>
### 3.32 well-formed program [defns.well.formed]

C++ program constructed according to the syntax rules, diagnosable semantic rules, and the one-definition rule ([[basic.def.odr]](../basic/basic.def.odr.md))
