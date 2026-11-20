/*
 * sema.h: semantic analysis of a translation unit's syntax tree
 * (doc/PLAN-cxx-sema.md): [lex.phases]/1.7 "syntactically and semantically
 * analyzed".
 *
 * Analysis does not annotate the syntax tree.  It builds the semantic
 * program: the types (one record per distinct type), the entities with
 * their declarations, and from step S1 a typed tree per initializer and
 * function body.  Like the syntax tree these are arrays the translation
 * unit owns, referred to by 32-bit indexes, with nothing global.
 *
 * A construct whose rules are not written yet is not guessed at: its
 * declaration's result is `unsupported`, with the subclause that owns the
 * rules, and the declaration contributes nothing.  The count over a corpus
 * is the work list, and is zero when analysis is complete.
 */
#ifndef VCC_CPLUSPLUS_SEMA_H
#define VCC_CPLUSPLUS_SEMA_H

#include "frontend/cplusplus/parse.h"
#include "frontend/cplusplus/syntax.h"

namespace cplusplus
{

/* ---- types -------------------------------------------------------------- */

/* a type of the translation unit's type table; 0 is no type */
using type_index = uint32_t;

/* [basic.type.qualifier]/1: "a const-qualified version, a
   volatile-qualified version, and a const-volatile-qualified version";
   [ext:gnu_restrict]'s __restrict, on pointers only, a third */
enum cv_qualifier : uint32_t {
    cv_none = 0,
    cv_const = 1,
    cv_volatile = 2,
    cv_restrict = 4,
    /* [dcl.fct]/1: the ref-qualifier of a non-static member function,
       kept with its cv-qualifier-seq (entity::member_cv and the
       declarator's function_cv alone; never a type's qualifiers) */
    cv_ref_lvalue = 8,
    cv_ref_rvalue = 16,
};

/* the cv-qualifiers of a member function's cv-qualifier-seq and
   ref-qualifier: those that qualify `this` */
inline uint32_t object_cv(uint32_t member_cv)
{
    return member_cv & (cv_const | cv_volatile);
}

/*
 * A possibly cv-qualified type: the cv-unqualified type's index and the
 * qualifiers.  [basic.type.qualifier]/1: "The cv-qualified or
 * cv-unqualified versions of a type are distinct types", but they are one
 * record here, so removing or comparing qualifiers reads no table.
 */
struct qualified_type {
    uint32_t bits = 0;
    static qualified_type make(type_index type, uint32_t cv)
    {
        return {type << 3 | cv};
    }
    type_index unqualified() const
    {
        return bits >> 3;
    }
    uint32_t cv() const
    {
        return bits & 7;
    }
    bool operator==(const qualified_type &other) const
    {
        return bits == other.bits;
    }
};

/* [basic.fundamental], and the ways of [basic.compound]/1 to construct a
   compound type */
enum class type_kind : uint8_t {
    none,
    fundamental,
    array,            /* [dcl.array] */
    function,         /* [dcl.fct] */
    pointer,          /* [dcl.ptr] */
    lvalue_reference, /* [dcl.ref] */
    rvalue_reference,
    class_type,        /* [class], unions included; the entity says which */
    enumeration,       /* [dcl.enum] */
    pointer_to_member, /* [dcl.mptr] */
    /* [temp.param]/1: a template-parameter's type (a type parameter);
       made by no analysis yet */
    template_parameter,
    /* [temp.dep.type]/9: a type that depends on template parameters
       without being one */
    dependent,
    /* [dcl.spec.auto]/1: auto or decltype(auto) before deduction */
    placeholder,
    /* [temp.variadic]/5: a pack expansion whose pattern (operand) names a
       template parameter pack, the type of a function parameter pack
       ([dcl.fct]/21) in a template's own declaration */
    pack_expansion,
    /* [ext:clang_atomic] _Atomic(T): operand T */
    atomic,
    /* [ext:gnu_complex] _Complex T: operand T, a floating-point type */
    complex,
    /* [ext:clang_blocks] a block pointer to operand (a function type) */
    block_pointer,
    /* [ext:clang_bit_int] _BitInt(N): bound N, or entity the non-type
       template-parameter N names in a template's own declaration */
    bit_int,
    /* [ext:clang_ext_vector_type] a vector of bound elements of type
       operand, or of the non-type template-parameter entity's value */
    ext_vector,
};

struct type {
    type_kind kind = type_kind::none;
    /* kind fundamental: which */
    fundamental_type fundamental = fundamental_type::none;
    /* function: [dcl.fct]/2 "the parameter-declaration-clause terminates
       with an ellipsis" */
    bool variadic = false;
    /* function: [dcl.fct]/1 "noexcept_opt function of ...": a non-throwing
       exception specification is part of the type since C++17
       ([except.spec]/2, [diff.cpp14.except]/1) */
    bool non_throwing = false;
    /* function: [dcl.fct]/6 the cv-qualifier-seq (cv_const, cv_volatile)
       and the ref-qualifier (0 none, 1 &, 2 &&) of a function type that
       is not a member function declaration's own (whose cv-qualifier-seq
       is the entity's member_cv): the function type a pointer to member
       refers to, a typedef-name's */
    uint8_t function_cv = 0, function_ref = 0;
    /* array: [dcl.array]/1 whether the constant-expression is present (if
       not, an "array of unknown bound of T") */
    bool bound_known = false;
    /* array: the element type; function: the return type; pointer and
       reference: the type pointed or referred to; pointer to member: the
       member's type */
    qualified_type operand;
    /* class, enumeration: the entity; pointer to member: the class's (0
       for a dependent class, whose type is bound) */
    uint32_t entity = 0;
    /* array: the bound N; in a template's own declaration a bound that is
       a non-type template-parameter's name (T[N], [temp.deduct.type]/8's
       T[i]) is that parameter's entity, with bound_known and bound 0;
       pointer to member of a dependent class ([temp.dep.type]/9): the
       class's type */
    uint64_t bound = 0;
    /* function: the parameter types are parameters[first_parameter ..
       first_parameter + parameter_count) */
    uint32_t first_parameter = 0, parameter_count = 0;
};

/*
 * The types of a translation unit.  A type is made once: making it again
 * gives the same index, so two types are the same type exactly when their
 * indexes are equal.
 */
class types
{
  public:
    types();
    types(const types &) = delete;
    types &operator=(const types &) = delete;

    type_index fundamental(fundamental_type which);
    type_index pointer_to(qualified_type pointee);
    type_index lvalue_reference_to(qualified_type referred);
    type_index rvalue_reference_to(qualified_type referred);
    type_index array_of(qualified_type element, uint64_t bound);
    type_index array_of_unknown_bound_of(qualified_type element);
    type_index array_of_parameter_bound(qualified_type element,
                                        uint32_t parameter);
    type_index function(qualified_type result, const qualified_type *parameters,
                        size_t parameter_count, bool variadic,
                        bool non_throwing = false, uint32_t function_cv = 0,
                        uint32_t function_ref = 0);
    /* the function type f with the qualifiers of [dcl.fct]/6 */
    type_index function_qualified(type_index f, uint32_t function_cv,
                                  uint32_t function_ref);
    type_index of_entity(type_kind kind, uint32_t entity);
    type_index pointer_to_member(uint32_t class_entity, qualified_type member);
    type_index dependent_pointer_to_member(type_index class_type,
                                           qualified_type member);
    type_index pack_expansion_of(qualified_type pattern);
    type_index atomic_of(qualified_type value);
    type_index complex_of(qualified_type value);
    type_index block_pointer_to(qualified_type function);
    type_index bit_int(uint64_t width, uint32_t parameter);
    type_index ext_vector_of(qualified_type element, uint64_t count,
                             uint32_t parameter);

    const type &operator[](type_index t) const
    {
        return records[t];
    }
    const qualified_type *parameters(type_index t) const
    {
        return parameter_lists.data() + records[t].first_parameter;
    }
    /* the number of types, the unused type 0 included */
    size_t size() const
    {
        return records.size();
    }

  private:
    /* the type equal to t, or t added */
    type_index intern(const type &t, const qualified_type *parameters);
    uint32_t hash(const type &t, const qualified_type *parameters) const;
    bool same(const type &a, const qualified_type *a_parameters,
              type_index b) const;
    void grow();

    owned_array<type> records;
    owned_array<qualified_type> parameter_lists;
    /* open addressing: a type's index, 0 for an empty slot */
    owned_array<type_index> slots;
};

/* ---- entities ----------------------------------------------------------- */

/* an entity of the translation unit; 0 is no entity */
using entity_index = uint32_t;

/* [basic.pre]/3: "An entity is a value, object, reference, structured
   binding, function, enumerator, type, class member, bit-field, template,
   template specialization, namespace, or pack."  The kinds a declaration
   introduces; a class member or a bit-field is a variable or a function
   whose scope is a class. */
enum class entity_kind : uint8_t {
    none,
    variable, /* an object or a reference: [basic.pre]/6 */
    structured_binding,
    function,
    enumerator,
    class_type,
    enumeration,
    typedef_name, /* [dcl.typedef]: no entity of its own in the standard's
                     list, but a declared name with a type */
    template_entity,
    template_specialization,
    namespace_entity,
    pack,
    /* not an entity: [temp.res]/3's typename-specifier with a dependent
       nested-name-specifier, typename Q::name, a member of a type not
       known until instantiation; its type is a type_kind::dependent of
       it, value the qualifier Q (a type_index), name the identifier */
    dependent_member,
    /* not an entity: [ext:clang_type_traits] a clang-type-transformation
       of a dependent type, __remove_cv(T); its type is a
       type_kind::dependent of it, value the operand (cv << 32 | its
       type_index), name the keyword's identifier */
    type_transformation,
    /* not an entity: [temp.dep.constexpr] a value-dependent template
       argument other than a parameter's name (is_integral<T>::value), in
       a template's own declaration; the template_argument's
       template_entity, its value no parameter's position (UINT32_MAX).
       An instantiation analyses the argument again with its arguments.
       What mangling knows of it: for [!] Q<args>::name written so, type
       the dependent Q<args>, name the member's identifier, value 1 (3 if
       negated), one entity for the same three ([temp.over.link]); else
       value 0, an entity of its own */
    dependent_value,
    /* not an entity: a dependent type in a template's own declaration
       known only by how it is written: [temp.dep.type]/10.3
       decltype(expression) of a type-dependent expression, a builtin
       template's dependent template-id; its type is a type_kind::dependent
       of it, value the syntax node, one for nodes of the same tokens
       ([temp.over.link]/5: equivalent expressions).  An instantiation
       analyses it again */
    dependent_written,
    /* not an entity: a block scope or a function parameter scope
       ([basic.scope.block], [basic.scope.param]), the scope the names
       declared in it are bound in */
    block_scope,
};

/* [basic.link]/2 */
enum class linkage : uint8_t {
    none,
    internal,
    external,
};

/* whether an evaluation is a constant expression ([expr.const]/11) with a
   value analysis knows, is not one, or analysis cannot tell */
enum class constness : uint8_t { constant, not_constant, unknown };

/* [special]: a member function a class's objects are made and ended by */
enum class special_member : uint8_t { none, constructor, destructor };

/* [except.spec]/7: what is known of a default member initializer's
   exceptions */
enum class initializer_exception : uint8_t { unknown, cannot_throw, may_throw };
/* [temp.explicit]/2: "An explicit instantiation definition is an explicit
   instantiation that does not begin with the extern keyword" */
enum class instantiation_kind : uint8_t { none, declaration, definition };
/* [basic.stc.dynamic]/1: operator new, operator new[], operator delete
   and operator delete[] */
enum class allocation_function : uint8_t {
    none,
    new_,
    new_array,
    delete_,
    delete_array
};

/* [ext:gnu_attributes] the GNU attributes analysis has rules for, as a
   function's or variable's flags */
enum gnu_attribute_flag : uint32_t {
    gnu_noinline = 1,
    gnu_always_inline = 2,
    gnu_noreturn = 4,
    gnu_weak = 8,
    gnu_used = 16,
    gnu_section = 32,
    gnu_aligned = 64,
    gnu_packed = 128,
    /* nothrow: no code changes, but clang's function type is noexcept */
    gnu_nothrow = 256,
    /* visibility("default"), type_visibility("default"): what vcc's
       symbols have already; never set in a declaration's flags */
    gnu_default_visibility = 512,
    /* clang's using_if_exists, on a using-declarator */
    gnu_using_if_exists = 1024,
    /* visibility("hidden"): the symbol is hidden (STV_HIDDEN) */
    gnu_hidden_visibility = 2048,
    /* abi_tag("tag"): the tag is part of the mangled name ([abi:mangling]
       <abi-tag>); the string-literal is the entity's abi_tag */
    gnu_abi_tag = 4096,
    /* clang's exclude_from_explicit_instantiation: an explicit
       instantiation of the class leaves the member out */
    gnu_exclude_from_explicit_instantiation = 8192,
    /* [ext:gnu_library_builtins] __builtin_is_constant_evaluated: a
       manifestly constant evaluation's call is true, any other false */
    gnu_is_constant_evaluated = 16384,
    /* [ext:gnu_library_builtins] a builtin of vcc's C front end
       (__builtin_unreachable, __c11_atomic_thread_fence): lowered to its
       builtin node, no function */
    gnu_compiler_builtin = 32768,
    /* clang's enable_if with a constant true condition: a function of its
       own, preferred in overload resolution, mangled with
       Ua9enable_ifILb1EE (gnu_attribute_specifier) */
    gnu_enable_if = 65536,
    /* mode(M): the type of the typedef or variable (gnu_mode_type) */
    gnu_mode = 131072,
    /* [ext:gnu_library_builtins] __builtin_popcountg, __builtin_clzg and
       __builtin_ctzg (with gnu_compiler_builtin): a constant expression
       when its arguments are (bit_count_builtin) */
    gnu_bit_count = 262144,
    /* [ext:gnu_library_builtins] a builtin of vcc's C front end whose
       arguments or result are not those of one function type
       (__atomic_*, __sync_*, __c11_atomic_*, __builtin_va_start): declared
       as a function taking anything, so that lookup finds it; a call is not
       supported yet (call_with) */
    gnu_untyped_builtin = 524288,
    /* [ext:gnu_library_builtins] the C library function a __builtin_NAME
       declares implicitly (library_builtin): a declaration of the library's
       own name, with or without noexcept, is of this function, as clang's
       are */
    gnu_library_function = 1048576,
    /* [ext:gnu_library_builtins] __builtin_constant_p and the lock-free
       queries (with gnu_untyped_builtin): a constant expression when their
       arguments are (query_builtin) */
    gnu_query_builtin = 2097152,
    /* [dcl.attr.nouniqueaddr] [[no_unique_address]] (the standard's, not a
       GCC attribute): a non-static data member that may share its bytes
       with another subobject (entity::no_unique_address) */
    gnu_no_unique_address = 4194304,
    /* "gnu_inline: This attribute should be used with a function that is
       also declared with the inline keyword ... In C++, this attribute does
       not depend on extern in any way, but it still requires the inline
       keyword to enable its special behavior": the definition is used only
       for inlining, never emitted (glibc's __extern_inline) */
    gnu_inline_definition = 8388608,
    /* "constructor (priority)", "destructor (priority)": the function runs
       before main, or after it returns or exit is called, as an entry of
       .init_array or .fini_array (the priority is gnu_priority) */
    gnu_constructor = 16777216,
    gnu_destructor = 33554432,
    /* "cleanup (cleanup_function)": a local variable's function, called
       with its address when it goes out of scope (gnu_cleanup) */
    gnu_cleanup = 67108864,
};
/* the GNU attributes of a function's own symbol that analysis carries to
   member functions and function templates */
const uint32_t gnu_function_symbol_flags =
    gnu_hidden_visibility | gnu_abi_tag |
    gnu_exclude_from_explicit_instantiation;
/* noinline, always_inline and noreturn, which lowering gives a member
   function or function template's specialization as a non-member's */
const uint32_t gnu_inline_flags =
    gnu_noinline | gnu_always_inline | gnu_noreturn;

struct entity {
    entity_kind kind = entity_kind::none;
    linkage linkage_of_name = linkage::none;
    /* [basic.def]/2: a declaration of it was a definition */
    bool defined = false;
    /* an instantiated member function whose body analysis does not support:
       left undefined, and a use of it is refused (body_failures' index
       plus one) */
    uint32_t unsupported_body = 0;
    /* the preprocessor's identifier number, 0 for an unnamed entity */
    uint32_t name = 0;
    /* the entity whose scope the name is declared in (a namespace, a
       class, a function); 0 at global namespace scope */
    entity_index enclosing = 0;
    qualified_type type;
    /* [expr.const]/4: a variable usable in constant expressions: whether
       it is (constant, with its value as expression_meaning keeps one),
       is not, or analysis could not tell (unknown) */
    constness usable = constness::not_constant;
    uint64_t value = 0;
    /* [dcl.constexpr]/1 */
    bool constexpr_ = false;
    /* [dcl.constexpr]/2: "A function or function template declared with
       the consteval specifier is called an immediate function" (also
       constexpr_ and inline_) */
    bool immediate = false;
    /* [dcl.inline]: an inline function or variable; [namespace.def]/7: an
       inline namespace */
    bool inline_ = false;
    /* a scope: a declaration analysis did not follow may have brought
       names into it that are not held one by one (a using-directive whose
       namespace is not known, a using-enum-declaration): a name not found
       in it is not known to be undeclared */
    bool unknown_names = false;
    /* a name hold_names kept with no entity (kind none) because a
       template-declaration or explicit-specialization declared it: what
       it denotes needs [temp]'s rules */
    bool held_by_template = false;
    /* the subclause whose missing rules made the declaration that
       hold_names held this name not analysed (declaration() sets it from
       that declaration's first refusal): a use of the name is refused for
       that reason, not for [temp.names] */
    const char *held_because = nullptr;
    /* a scope (a namespace, 0 the global one, or a block): a
       using-directive appears in it ([namespace.udir]; an unnamed or
       inline namespace's implied one in its enclosing namespace) */
    bool has_using_directive = false;
    /* [basic.scope.hiding]/2: "If a class name or enumeration name and a
       variable, data member, function, or enumerator are declared in the same
       declarative region (in any order) with the same name ..., the class or
       enumeration name is hidden wherever the variable, data member, function,
       or enumerator name is visible.": the class or enumeration this entity's
       name hides in its scope, 0 if none */
    entity_index hidden_type = 0;
    /* a variable: [basic.stc.auto]/1, a local variable not declared static
       (or a parameter) */
    bool automatic = false;
    /* a variable: [ext:clang_c11_thread_local] declared _Thread_local, of
       thread storage duration ([basic.stc.thread]) with C11's rules */
    bool c11_thread_local = false;
    /* a variable: [dcl.constinit]/1 declared constinit (by any of its
       declarations) */
    bool constinit_ = false;
    /* a block_scope: the scope whose names it shall not redeclare (the
       function parameter scope for the outermost block of a function
       definition, the scope of a statement's condition for its
       substatements' outermost blocks), and whether that is the parameter
       scope */
    entity_index conflicts = 0;
    bool parameter_scope = false;
    /* [basic.link]: its declarations, in order, are a chain through
       `declarations`: the first and the last */
    uint32_t first_declaration = 0, last_declaration = 0;
    /* the semantic tree of its definition: a function's
       semantic_kind::function_body, a variable's initializer (0: none) */
    uint32_t definition = 0;
    /* an array of unknown bound whose initializer would give the bound
       ([dcl.array]/7) and was not analysed: its type is not known */
    bool bound_unknown = false;
    /* a member of a class (enclosing is the class): [class.mem]/3 a
       non-static data member or a non-static member function, which has
       a `this` ([class.this]) */
    bool nonstatic_member = false;
    /* [class.access]: a member's access (0 public, 1 protected, 2
       private) */
    uint8_t access = 0;
    /* a non-static member function: [class.mfct.non-static]/5 the
       cv-qualifier-seq of its declarator, which qualifies `this` */
    uint32_t member_cv = 0;
    /* a non-static data member declared mutable ([dcl.stc]/8): the type of
       an access to it takes no const from the object ([expr.ref]/6.2) */
    bool mutable_member = false;
    /* a non-static data member: its number among the class's and, once
       the class is complete, its offset in bytes ([class.mem]/19,
       [abi:class-types]) */
    uint32_t field = 0;
    /* an anonymous union (or clang's anonymous struct): the unnamed
       member object of it in the enclosing class ([class.union.anon]/1),
       through which a member's name in that class designates it */
    entity_index anonymous_object = 0;
    /* a non-static data member: its default member initializer
       ([class.mem]/10: a brace-or-equal-initializer or braced-init-list
       node), 0 if none */
    SyntaxRef default_initializer = 0;
    /* ... and whether it can throw ([except.spec]/7), once a constructor
       using it is defined (semantic_analysis::default_member_initializer) */
    initializer_exception initializer_throws = initializer_exception::unknown;
    /* a class: its default constructor is declared = default
       ([dcl.fct.def.default]), declared as the implicit one is */
    bool defaulted_default_constructor = false;
    /* a class: its implicit copy constructor is deleted, a subobject's
       being deleted or not accessible ([class.copy.ctor]/10.1) */
    bool implicit_copy_deleted = false;
    /* ... and explicit: explicit X() = default ([class.conv.ctor]/2: a
       copy-initialization does not consider it) */
    bool explicit_default_constructor = false;
    /* X& operator=(const X&) = default (defaulted_copy_assignment) */
    bool defaulted_copy_assignment = false;
    /* X(X&&) = default and X& operator=(X&&) = default, trivial
       ([class.copy.ctor]/11, [class.copy.assign]/9): no entity; the move
       constructor copies the bytes (construct) */
    bool defaulted_move_constructor = false;
    /* X(const X&) = default: no entity when trivial */
    bool defaulted_copy_constructor = false;
    bool defaulted_move_assignment = false;
    /* [class.default.ctor]/2: the implicit default constructor is deleted
       (a subobject without a usable default constructor) */
    bool implicit_default_deleted = false;
    /* [expr.prim.lambda.closure]/13: a closure type's copy assignment
       operator is deleted (a lambda-capture) */
    bool closure_assignment_deleted = false;
    /* a function (the first of its overloads) a using-declaration names
       ([namespace.udecl]/11) */
    bool named_by_using = false;
    uint64_t offset = 0;
    /* a class: its non-static data members, in declaration order, are
       semantic_analysis::class_members[first_member .. first_member +
       member_count); its size and alignment once it is complete
       ([class.mem]/6: "A class is considered a completely-defined object
       type ... at the closing } of the class-specifier") */
    uint32_t first_member = 0, member_count = 0;
    uint64_t size = 0;
    /* a class: [abi:general] its data size (dsize), the size without the
       tail padding a derived class may reuse (its size, for a POD for the
       purpose of layout: [abi:pod]); its direct base classes
       ([class.derived]/2), in the order of the base-specifier-list, are
       semantic_analysis::class_bases[first_base .. first_base +
       base_count) */
    uint64_t data_size = 0;
    uint32_t first_base = 0, base_count = 0;
    /* a base class subobject ([intro.object]/2): an unnamed non-static
       member of the derived class (enclosing) whose type is the base
       class, with its offset and access ([class.access.base]/1); one that
       is not empty is also among the class's non-static data members
       (field) */
    bool base_subobject = false;
    /* a base subobject of a class template's specialization whose
       base-specifier depends on a template-parameter: [temp.dep]/3, its
       class's members are not found by unqualified lookup */
    bool dependent_base = false;
    /* a class: its alignment; a non-static data member: the alignment
       its aligned attribute asks for, 0 if none ([ext:gnu_attributes]) */
    uint32_t alignment = 0;
    /* a class: the alignment its alignment-specifiers ask for, 0 if none
       ([dcl.align]) */
    uint32_t requested_alignment = 0;
    /* a non-static data member with the packed attribute: aligned to 1
       before its aligned attribute ([ext:gnu_attributes]) */
    bool packed = false;
    /* an unnamed class: [dcl.typedef]/9 "the first typedef-name declared
       by the declaration to be that class type ... is used to denote the
       class type ... for linkage purposes only" */
    uint32_t linkage_name = 0;
    /* a member function that is a constructor ([class.ctor]) or a
       destructor ([class.dtor]); named by its class's name, not bound in
       the class's scope */
    special_member special = special_member::none;
    /* a constructor: the class's next one, 0 at the last */
    entity_index next_constructor = 0;
    /* a function: [over.pre]/1 "When two or more different declarations
       are specified for a single name in the same scope, that name is said
       to be overloaded": the scope's next function of the name, 0 at the
       last.  The name is bound to the first; the others are reached
       through it (overload_set) */
    entity_index next_overload = 0;
    /* a member function that is the first of its name's chain: the held
       name (held_by_template) of the member function templates of the same
       name in the class, which [temp.mem]/2 overloads with the chain; 0
       if none */
    entity_index member_templates = 0;
    /* a function: [dcl.fct.def.delete]/1 a deleted function */
    bool deleted = false;
    /* a function: [dcl.fct.default]/1 the number of its last parameters
       that have a default argument (default_argument) */
    uint32_t default_arguments = 0;
    /* ... and where its parameters' are in
       semantic_analysis::default_argument_nodes (0: none has one) */
    uint32_t first_default = 0;
    /* ... not known: a declaration's default arguments were not analysed,
       or it is in another scope than the first ([dcl.fct.default]/4: "For
       non-template functions, default arguments can be added in later
       declarations of a function that appear within the same scope.
       Declarations that appear in different scopes have completely distinct
       sets of default arguments.") */
    bool unknown_defaults = false;
    /* a function: [over.oper]/1 an operator function, and the operator
       it implements (its name is operator_name's) */
    bool operator_function = false;
    preprocessing_op_or_punc implemented = {};
    /* ... or one of the allocation and deallocation functions
       ([basic.stc.dynamic]/1) */
    allocation_function allocation = allocation_function::none;
    /* ... or a conversion function ([class.conv.fct]): its name is
       conversion_function_name's, shared by all; explicit_ when declared
       explicit ([class.conv.fct]/2) */
    bool conversion_function = false, explicit_ = false;
    /* [dcl.fct.spec]/2: a constructor template's explicit(condition) whose
       condition is value-dependent: decided per specialization, so unknown
       on the template itself */
    bool explicit_dependent = false;
    /* a class: the copy assignment operators it declares
       ([class.copy.assign]/1) not deleted on their first declaration */
    uint32_t user_copy_assignments = 0;
    /* a class: its user-declared constructors (a chain through
       next_constructor) and destructor, 0 if none */
    entity_index first_constructor = 0, destructor = 0;
    /* a class: [temp.mem]/1 the first constructor template it declares
       without defining it (its template-declaration), 0 if none.  Such a
       class has a user-declared constructor; the template is not
       analysed, and a construction of the class that would consider it is
       not either */
    SyntaxRef constructor_template = 0;
    /* a class: the name (not bound) its constructor templates defined in
       it hold as function_templates (member_function_template), 0 if
       none */
    entity_index constructor_templates = 0;
    /* a non-static data member that is a bit-field ([class.bit]): its
       width and the offset of its first bit in the class (offset is the
       byte that bit is in); an unnamed one (name 0) is in the layout but
       not a member ([class.bit]/2) */
    bool bit_field = false;
    /* a non-static data member declared [[no_unique_address]]
       ([dcl.attr.nouniqueaddr]): potentially-overlapping
       ([intro.object]/7), laid out by [abi:class-types] II as clang does */
    bool no_unique_address = false;
    uint16_t bit_width = 0;
    uint64_t bit_offset = 0;
    /* a class: a union ([class.union]/1: "A union is a class defined
       with the class-key union") */
    bool union_ = false;
    /* a class that is defined: the class-key of its class-specifier */
    token_index class_key = 0;
    /* a function or variable: [ext:gnu_attributes] the GNU attributes
       its declarations gave it (gnu_attribute_flag) and a section
       attribute's string-literal, 0 if none; its aligned attribute's
       alignment is alignment */
    uint32_t gnu_flags = 0;
    token_index section = 0;
    /* ... and an abi_tag attribute's string-literal, 0 if none */
    token_index abi_tag = 0;
    /* ... gnu_constructor's or gnu_destructor's priority (0: the default)
       and gnu_cleanup's function */
    uint32_t gnu_priority = 0;
    entity_index gnu_cleanup = 0;
    /* [ext:gnu_asm_labels] a function's or variable's asm label: the
       string-literal that is its symbol, 0 if none */
    token_index asm_label = 0;
    /* [dcl.link]/1: a function or variable with C language linkage,
       whose name is its symbol */
    bool c_language_linkage = false;
    /* a variable or function internal only because its type is or is
       made of a class or enumeration without external linkage (an unnamed
       class, [basic.link]/4.3; one of an unnamed namespace): [impl.clang]
       its symbol is local, and mangled as an external name's is, without
       L */
    bool unnamed_type_linkage = false;
    /* a special member function declared implicitly ([class.default.ctor]/1,
       [class.copy.ctor]/6, [class.dtor]/4) */
    bool implicit = false;
    /* a constructor of a class that a using-declarator names a base
       class's constructors for ([namespace.udecl]/3): the base's
       constructor it forwards to ([class.inhctor.init]/1); implicit too */
    entity_index inherited_constructor = 0;
    /* a class: a using-declarator names the constructors of one of its
       bases ([namespace.udecl]/3), whose constructors it inherits at its
       closing }; no aggregate ([dcl.init.aggr]/1.1) */
    bool inherits_constructors = false;
    /* an enumeration ([dcl.enum]): its underlying type once it is
       complete (none before), whether it is scoped ([dcl.enum]/2) and
       whether its underlying type is fixed ([dcl.enum]/5) */
    fundamental_type underlying = fundamental_type::none;
    bool scoped = false, fixed_underlying = false;
    /* ... and, its underlying type not fixed, the type it promotes to
       ([conv.prom]/4) */
    fundamental_type promoted = fundamental_type::none;
    /* a function template's specialization ([temp.spec]): its
       instantiation record (semantic_analysis::instantiations) plus one;
       a function template (template_entity) or one of its type
       parameters: its function_template record plus one */
    uint32_t instantiation = 0;
    /* a function template's specialization, a member function of a class
       template's specialization or such a class: the subject of an
       explicit instantiation ([temp.explicit]/2) declaration (extern) or
       definition */
    instantiation_kind explicitly_instantiated = instantiation_kind::none;
    /* a non-static member function: [class.virtual]/2 a virtual function
       (declared virtual, or overriding one), pure ([class.abstract]/2),
       and its slot in the virtual table of its class (the index of its
       function pointer after the offset to top and the RTTI pointer,
       [abi:vtable-components]) */
    bool virtual_ = false, pure = false, final_function = false;
    uint32_t vtable_slot = 0;
    /* a class: [class.virtual]/1 a polymorphic (dynamic, [abi:definitions])
       class: its record in semantic_analysis::dynamic_classes plus one, 0
       if it is none */
    uint32_t dynamic = 0;
    /* a base class subobject that is a virtual base ([class.mi]/4): a
       direct one (its base-specifier has virtual) in class_bases, or one
       of a class's virtual bases, direct or indirect
       (semantic_analysis::class_virtual_bases), whose offset is its
       offset in a complete object of the class; neither is a member */
    bool virtual_base = false;
    /* a class: its virtual bases, direct and indirect, are
       semantic_analysis::class_virtual_bases[first_virtual_base ..
       first_virtual_base + virtual_base_count), in the order of their
       construction ([class.base.init]/13.1: depth-first left-to-right, a
       base's virtual bases before it) */
    uint32_t first_virtual_base = 0, virtual_base_count = 0;
    /* a class: [abi:general] nvsize and nvalign, its size and alignment
       without its virtual bases (a base subobject's); for a class
       without virtual bases nvsize is its data size */
    uint64_t nv_size = 0;
    uint32_t nv_alignment = 0;
    /* a function declared with a return type that has a placeholder
       ([dcl.spec.auto]/3), deduced or not */
    bool placeholder_return = false;
    /* a closure type ([expr.prim.lambda.closure]/1): its lambda-expression
       (0: the class is no closure) and its function call operator */
    SyntaxRef lambda = 0;
    entity_index call_operator = 0;
    /* a closure type of an inline variable's initializer: the variable,
       the closure's context ([abi:closure-types]) */
    entity_index closure_variable = 0;
    /* an init-capture's name ([expr.prim.lambda.capture]/6): the variable
       is the closure's non-static data member this names */
    entity_index capture_member = 0;
    /* a structured binding ([dcl.struct.bind]): the variable e of its
       declaration (0 while its initializer is analysed, /2) and what it
       names: e's member m_i (/5), the variable r_i (/4), or 0 for e's
       element field (/3); its type is the referenced type */
    entity_index binding_object = 0, binding_target = 0;
    /* the variable e: its structured bindings, first_binding ..
       first_binding + binding_count, which name it ([abi:mangle.name]) */
    entity_index first_binding = 0;
    uint32_t binding_count = 0;
};

/* [class.virtual], [abi:vtable-components]: what a dynamic class adds to
   its entity: its primary base (a base class subobject entity sharing the
   virtual table pointer at offset 0, [abi:class-types] I; 0: the class
   allocates the pointer itself), its virtual functions' final overriders
   by slot (semantic_analysis::vtable_slots[first_slot .. first_slot +
   slot_count)), whether it is abstract ([class.abstract]/2) and its key
   function ([abi:vague-vtable]: the first non-pure virtual function not
   inline at the closing }, 0 if none) */
struct dynamic_class {
    entity_index primary_base = 0;
    uint32_t first_slot = 0, slot_count = 0;
    bool abstract = false;
    entity_index key_function = 0;
    /* [abi:vtable-general]: the virtual table group, the primary virtual
       table (semantic_analysis::vtable_groups[first_group], whose slots are
       first_slot .. first_slot + slot_count) and a secondary one per
       dynamic base class subobject that is not a primary base, in the
       order of [abi:vtable-construction] (each non-primary dynamic base's
       group, in declaration order) */
    uint32_t first_group = 0, group_count = 0;
    /* [class.dtor]/12: the deallocation function the deleting destructor
       of a virtual destructor calls, and whether it takes the size */
    entity_index deallocation = 0;
    bool deallocation_sized = false;
    /* [abi:vtable-components]: the virtual bases whose offsets the primary
       virtual table has, nearest the address point first
       (semantic_analysis::vbase_entries[first_vbase_entry ..
       first_vbase_entry + vbase_entry_count), classes): the primary
       base's, then the class's others in inheritance graph order */
    uint32_t first_vbase_entry = 0, vbase_entry_count = 0;
};

/* one virtual table of a group ([abi:vtable-general]): the offset of the
   subobject whose virtual table pointer points into it (its offset to
   top negated), and its function slots, vtable_slots[first_slot ..
   first_slot + slot_count); vtable_adjustments has, per slot, what a
   this-adjusting thunk adds to the subobject's address to call the
   overrider ([abi:thunks]: 0, the function itself) */
struct vtable_group {
    uint64_t offset = 0;
    uint32_t first_slot = 0, slot_count = 0;
    /* the class whose primary virtual table this is (its vbase offsets
       are the table's, [abi:vtable-components]); when the subobject is
       in a virtual base, that base's class and the subobject's offset in
       it (offset is then in the complete object); whether the table is
       the virtual base's own primary one, which has vcall offsets */
    entity_index subobject_class = 0, in_virtual_base = 0;
    uint64_t offset_in_virtual = 0;
    bool virtual_primary = false;
};

/* one declaration of an entity: the syntax node that declares it */
struct entity_declaration {
    SyntaxRef node = 0;
    /* the entity's next declaration, 0 at the last */
    uint32_t next = 0;
};

class entities
{
  public:
    entities();
    entities(const entities &) = delete;
    entities &operator=(const entities &) = delete;

    entity_index make(entity_kind kind, uint32_t name, entity_index enclosing);
    /* the entity a name bound in a scope refers to, 0 if none: what a
       declaration in the scope finds of an earlier one
       ([basic.scope.declarative]/4).  A name is bound where its entity is
       declared (entity::enclosing), and an entity can be bound elsewhere
       too, perhaps under another name: by a using-declaration
       ([namespace.udecl]/1), as a namespace-alias ([namespace.alias]/2),
       by a block-scope declaration of a namespace's function or variable
       ([basic.link]/7), an unscoped enumerator in its enumeration
       ([dcl.enum]/11) */
    entity_index find(entity_index scope, uint32_t name) const;
    void bind(entity_index scope, uint32_t name, entity_index e,
              bool by_using_declaration = false);
    /* whether the name is bound in the scope by a using-declaration
       ([namespace.udecl]) and not declared there */
    bool bound_by_using(entity_index scope, uint32_t name) const;
    /* another declaration of the entity ([basic.link]/1: "A name ... can
       be introduced by declarations in more than one place) */
    void add_declaration(entity_index e, SyntaxRef node);

    entity &operator[](entity_index e)
    {
        return records[e];
    }
    const entity &operator[](entity_index e) const
    {
        return records[e];
    }
    const entity_declaration &declaration(uint32_t d) const
    {
        return declarations[d];
    }
    size_t size() const
    {
        return records.size();
    }

  private:
    owned_array<entity> records;
    owned_array<entity_declaration> declarations;
    /* the names bound in scopes: open addressing over (scope, name); an
       empty slot has entity 0 */
    struct binding {
        entity_index scope = 0;
        uint32_t name = 0;
        entity_index entity = 0;
        bool by_using_declaration = false;
    };
    owned_array<binding> slots;
    size_t used = 0;
    void grow();
};

/* ---- analysis ----------------------------------------------------------- */

/* what analysis concluded about one declaration of the translation unit */
enum class analysis_result : uint8_t {
    /* its rules are written, and it meets them */
    well_formed,
    /* [intro.compliance]/2.2: it "contains a violation of [a] diagnosable
       rule": a diagnostic was issued */
    ill_formed,
    /* rules it needs are not written yet: nothing is concluded */
    unsupported,
    /* the parser's: it has an erroneous node, or one kept as tokens */
    not_parsed,
};
const char *analysis_result_name(analysis_result result);

/*
 * The early return of an analysis: a step that is not well formed is the
 * function's conclusion.
 *
 *     VTRY(r, expression(e, m));       analysis_result r = ...; return r unless well_formed
 *     VTRY_SET(r, operand_value(n, m)); r = ...; the same, for an r already declared
 *
 * Statements, not expressions: they are written on a line of their own, as
 * the two lines of the `if` they stand for.
 */
#define VTRY(name, ...)                                                        \
    analysis_result name = (__VA_ARGS__);                                      \
    if (name != analysis_result::well_formed)                                  \
    return name
#define VTRY_SET(name, ...)                                                    \
    name = (__VA_ARGS__);                                                      \
    if (name != analysis_result::well_formed)                                  \
    return name

/* the conclusion about a construct of two parts: ill-formed if either is,
   else unsupported if either is */
inline analysis_result worse(analysis_result a, analysis_result b)
{
    if (a == analysis_result::ill_formed || b == analysis_result::ill_formed)
        return analysis_result::ill_formed;
    if (a == analysis_result::not_parsed || b == analysis_result::not_parsed)
        return analysis_result::not_parsed;
    if (a == analysis_result::unsupported || b == analysis_result::unsupported)
        return analysis_result::unsupported;
    return analysis_result::well_formed;
}

/* a construct analysis has no rules for yet */
/* where in vcc's source a function was called from: its default argument's
   __builtin_FILE/LINE/FUNCTION are the caller's (as std::source_location) */
struct call_site {
    const char *file;
    unsigned line;
    const char *function;
    explicit call_site(const char *f = __builtin_FILE(),
                       unsigned l = __builtin_LINE(),
                       const char *fn = __builtin_FUNCTION())
        : file(f), line(l), function(fn)
    {
    }
};

struct unsupported_construct {
    SyntaxRef node = 0;
    /* the subclause whose rules are missing, without brackets */
    const char *subclause = "";
    /* the check in vcc that refused it: its source file, line and
       function (not_supported's call site) */
    const char *file = "";
    uint32_t line = 0;
    const char *function = "";
};

struct analysed_declaration {
    SyntaxRef node = 0;
    analysis_result result = analysis_result::unsupported;
    /* result unsupported: the first reason, an index into
       semantic_analysis::unsupported */
    uint32_t first_unsupported = 0;
    /* whether the analysis recorded a reason (first_unsupported is one) */
    bool has_unsupported = false;
};

/* a name a declaration declared, with the type analysis gave it: what
   -fcxx-dump=declaration-claims has clang check */
struct declared_name {
    SyntaxRef declaration = 0;
    /* the declaration of the translation unit it is in
       (analysed_declaration::node), and the entity it declared */
    SyntaxRef outermost = 0;
    entity_index entity = 0;
    token_index id = 0;
    entity_kind kind = entity_kind::none;
    qualified_type type;
    /* whether a claim can be placed just after the declaration: not for a
       declaration in a condition or init-statement */
    bool claimable = true;
};

/* ---- values of constant expressions (constant-expression.cpp) ---- */

/* [expr.const]/11: the value a constant expression's evaluation gives, as
   the evaluator of the semantic tree (expression/constant-evaluation.cpp)
   computes it.  Only integer and double are filled today; the other kinds
   are the shapes [basic.types] objects have, which the evaluator answers
   unsupported ([expr.const]) for until it models them. */
enum class constant_value_kind : uint8_t {
    none,
    /* [basic.fundamental]/11, up to 128 bits */
    integer,
    /* [basic.fundamental]/12: float and double as binary64; long double
       as binary128 (softfloat.h), not filled yet */
    floating,
    long_double,
    /* [basic.compound]/3: a null pointer value, or an object's address
       and an offset in it: designator is the entity, offset bytes */
    pointer,
    /* a glvalue: the object it designates, as pointer */
    lvalue,
    /* [dcl.array]: elements, [class.mem]: members, [class.union]: the
       active member: values in constant_evaluation::elements */
    array,
    class_object,
    union_object,
};

struct constant_value {
    constant_value_kind kind = constant_value_kind::none;
    unsigned __int128 integer = 0;
    double floating = 0;
    /* pointer and lvalue: the entity designated, 0 for a null pointer;
       the byte offset in it */
    entity_index designator = 0;
    uint64_t offset = 0;
    /* array, class_object, union_object: the values of the elements or
       members are elements[first .. first + count); union_object: count 1,
       member the active member */
    uint32_t first = 0, count = 0;
    entity_index member = 0;
    /* pointer to an element of an array ([basic.compound]/3, [expr.add]/4):
       the array's elements are elements[base .. base + extent), offset the
       index of the element pointed to (extent: one past the last), first
       its place; extent 0: a pointer to one object */
    uint32_t base = 0, extent = 0;
};

/* [implimits]/2's quantities for constant evaluation, the bracketed
   numbers: "Recursive constexpr function invocations [512]",
   "Full-expressions evaluated within a core constant expression
   [1048576]" */
constexpr uint32_t constexpr_invocation_limit = 512;
constexpr uint32_t constant_full_expression_limit = 1048576;

/* [basic.fundamental]/2: the integer types; [basic.fundamental]/12: the
   floating-point types (_BitInt and _Float16 are neither here: analysis
   has no rules for them yet) */
bool fundamental_type_is_integral(fundamental_type t);
bool fundamental_type_is_floating(fundamental_type t);
/* [conv.integral]/3: "the unique value of the destination type that is
   congruent to the source integer modulo 2^N": as an expression_meaning
   keeps it, sign-extended for a signed type ([conv.bool] for bool) */
uint64_t integer_wrap(const implementation_defined_behavior &behavior,
                      fundamental_type t, unsigned __int128 v);
/* whether the integer type t can represent v */
bool integer_holds(const implementation_defined_behavior &behavior,
                   fundamental_type t, __int128 v);
/* whether the value v of the integer type t (wrapped to it) is what
   integer_wrap keeps: a 128-bit type's value only when it fits in 64 bits
   (an operation's result otherwise has no value analysis knows) */
bool integer_kept_exactly(const implementation_defined_behavior &behavior,
                          fundamental_type t, unsigned __int128 v);
/* the value an expression_meaning keeps for the integer type t */
__int128 integer_value(const implementation_defined_behavior &behavior,
                       fundamental_type t, uint64_t kept);
/* the constness of an operation on operands of the given constnesses,
   before it is evaluated */
constness combined_constness(constness a, constness b);
/* a floating-point value as an expression_meaning keeps it */
double floating_value(uint64_t kept);
uint64_t floating_kept(double v);
/* the least power of two that is n or more (1 for 0) */
uint64_t power_of_two_at_least(uint64_t n);
/* [bit.cast]/1 of a value as an expression_meaning keeps it, from one
   arithmetic type to another of the same size: false where analysis does
   not know the object representation (long double, 128-bit integers) or
   the result has no value of to (bool) */
bool bit_cast_kept(const implementation_defined_behavior &behavior,
                   fundamental_type from, fundamental_type to, uint64_t kept,
                   uint64_t &result);
/* the operators of the arithmetic types on values as expression_meaning
   keeps them (expression/folding.cpp): constant with the value in out,
   not_constant where [expr.pre]/4 makes the operation undefined, unknown
   for what is not folded (long double, 128 bits, a pragma's rounding).
   fold_binary: operands converted to t, a shift's right operand of type
   right; fold_unary: + - ~ on an operand promoted to t, ! on a bool */
constness fold_binary(const implementation_defined_behavior &b,
                      bool floating_point_pragma, preprocessing_op_or_punc op,
                      fundamental_type t, fundamental_type right, uint64_t x,
                      uint64_t y, uint64_t &out);
constness fold_unary(const implementation_defined_behavior &b,
                     preprocessing_op_or_punc op, fundamental_type t,
                     uint64_t x, uint64_t &out);

/* the cv-qualifiers of a type, an array's those of its elements
   ([basic.type.qualifier]/3) */
uint32_t cv_of(const types &table, qualified_type t);
/* whether the function types a and b have the same parameter-type-list
   ([dcl.fct]/4) */
bool same_parameters(const types &table, type_index a, type_index b);
/* the function whose definition a block-scope entity is declared in, 0
   for an entity not in a function (abi/itanium/mangling.cpp) */
entity_index enclosing_function(const entities &table, entity_index e);
/* the function a member of a local class (or of a class nested in one)
   is in (for a member function of a local class: the next function out),
   0 for any other entity (abi/itanium/mangling.cpp) */
entity_index next_enclosing_function(const entities &table, entity_index f);
/* [impl.clang] (clang's VisibleNoLinkage): whether c, a local class or
   closure type (or a class nested in one), is named in other translation
   units: the outermost function it is in is inline or a template's
   specialization (abi/itanium/mangling.cpp) */
bool visible_local_class(const entities &table, entity_index c);

/* [basic.lval]/1: "Every expression belongs to exactly one of the
   fundamental classifications in this taxonomy: lvalue, xvalue, or
   prvalue." */
enum class value_category : uint8_t { prvalue, lvalue, xvalue };

/* what analysis found an expression to be ([expr.pre]/1: "An expression
   is a sequence of operators and operands that specifies a computation") */
struct expression_meaning {
    /* [expr.type]/1: never a reference type */
    qualified_type type;
    value_category category = value_category::prvalue;
    /* a prvalue: whether it is a constant expression and its value: an
       integral value sign- or zero-extended to 64 bits, or a float or
       double value as binary64 bits.  A glvalue: whether evaluating it
       (not reading the object) is constant */
    constness evaluation = constness::unknown;
    uint64_t value = 0;
    /* [conv.ptr]/1: an integer literal with value zero */
    bool zero_literal = false;
    /* a string-literal, perhaps parenthesized ([dcl.init.string]) */
    token_index string_literal = 0;
    /* an id-expression, perhaps parenthesized: the variable or function */
    entity_index entity = 0;
    /* an id-expression naming a structured binding: the binding (decltype
       of it is the referenced type, [dcl.type.decltype]/1.1) */
    entity_index binding = 0;
    /* a floating-point operand or a comma operator inside: not an integral
       constant expression before C++11 (C++03 [expr.const]/1) */
    bool beyond_integral_constant = false;
    /* a glvalue that designates a bit-field ([class.bit]), or the value
       read from one: its width (0: not a bit-field), for [conv.prom]/5 */
    uint16_t bit_field_width = 0;
    /* the expression in the semantic tree, with the conversions applied to
       it so far: 0 while it has none */
    uint32_t node = 0;
    /* [over.over]/1: the name of an overload set or of function templates
       as an operand, not yet resolved (it names no one function): the
       token of the name; node is 0, and the operand is analysed for the
       type of the parameter it is passed to (call_with) */
    token_index overload_name = 0;
};

/* ---- the semantic tree (sema/semantic-tree.cpp) ---- */

/*
 * The operations the standard describes, typed, with every implicit step
 * explicit (doc/PLAN-cxx-sema.md 3.2): what lowering reads.  An expression
 * node has the type and value category analysis gave the expression; a
 * statement node none.
 */
enum class semantic_kind : uint8_t {
    none,
    /* ---- expressions ---- */
    /* a prvalue whose value analysis knows (a literal, sizeof, a folded
       operation): value, as expression_meaning keeps it */
    constant,
    /* at: the string-literal; an lvalue of array type */
    string_literal,
    /* a: the variable or function named; a reference variable names the
       object or function it refers to */
    entity,
    /* op: a conversion_kind; a: the operand */
    conversion,
    /* op: the operator (+ - ~ !); a */
    unary,
    /* &a, *a */
    address,
    indirection,
    /* op: an increment_kind; a: the lvalue */
    increment,
    /* op: the operator; a, b: the converted operands (pointer arithmetic
       too) */
    binary,
    logical_and,
    logical_or,
    /* op: = or the compound operator; a: the lvalue, b: the converted
       right operand; a compound assignment's computation type is
       computation */
    assignment,
    /* a ? b : c; a glvalue when b and c are */
    conditional,
    /* a, b */
    comma,
    /* a: the function lvalue or pointer prvalue; the arguments, each
       initialized as its parameter, are list[first .. first + count); b:
       for a non-static member function, the object's address, its
       implicit object argument ([over.match.funcs]/4: `this`), else 0;
       c: 1 for a virtual call ([class.virtual]/16: the final overrider in
       the object's dynamic type, through its virtual table) */
    call,
    /* a: the pointer prvalue, b: the integer prvalue (E1[E2] with the
       operands in that order) */
    subscript,
    /* [expr.ref]: a: the object (a glvalue of class type), b: the
       non-static data member; an lvalue if the object is one */
    member,
    /* [expr.ref]/6.3.2: a: the object's address (a prvalue), b: the
       non-static member function; only the postfix-expression of a call */
    bound_member_function,
    /* [expr.unary.op]/3.1: &C::m, a prvalue of type "pointer to member of
       class C": a: the non-static member m (a data member or a member
       function) */
    member_pointer,
    /* [expr.mptr.oper]: E1.*E2 and E1->*E2: a: the address of the object
       (a prvalue of type pointer to the member pointer's class), b: the
       pointer to member (a prvalue).  To a data member, an lvalue (or an
       xvalue) of its type; to a member function, a prvalue of function
       type, only the postfix-expression of a call ([expr.mptr.oper]/6) */
    member_pointer_access,
    /* ---- initializers ---- */
    /* value- or zero-initialization: a scalar's 0, an array's all zero */
    zero,
    /* an array's elements, each initialized, in list[first .. first +
       count); the rest zero */
    initializer_list,
    /* a: the glvalue a reference binds to; a prvalue: a temporary
       materialized from it ([conv.rval]) */
    bind_reference,
    /* [class.ctor]/2: a constructor called: a: the object it initializes
       (a glvalue; 0 for the object the initializer is of, a declared
       variable, or a temporary when the node is used as a prvalue), b: the
       constructor, its arguments (each initialized as its parameter) in
       list[first .. first + count) */
    construct,
    /* [class.dtor]/13: a destructor called: a: the object (a glvalue), b:
       the destructor */
    destroy,
    /* [expr.new]: a: the allocation function's call (a void* prvalue), b:
       the object's initialization (0: none), a construct node whose
       object is the allocated node's, or the value stored in it; c: 1 when
       the allocation function may return a null pointer, which the
       initialization is skipped for ([expr.new]/20); d: for an array of
       a class with a constructor or destructor, the element count (a
       std::size_t prvalue, evaluated once), b then each element's
       initialization (its object *allocated, the element), value the
       size of the array cookie before the elements ([abi:array-cookies]:
       0 for none) */
    new_object,
    /* the pointer (a prvalue of type) the enclosing new_object's
       allocation returned, or the enclosing delete_object's operand (in
       an array's construction or destruction, the element's) */
    allocated,
    /* the element count (a std::size_t prvalue) the enclosing new_object
       or delete_object holds (new_object's d; the array cookie's) */
    array_count,
    /* [expr.new]/9, /15: an array's size in bytes, its count a times the
       element's size value plus d, the array cookie's size; the size no
       allocation can give (all ones) when that does not fit in
       std::size_t */
    array_size,
    /* [expr.delete]: a: the operand (a pointer prvalue), b: the
       destructor's call on *allocated (0: none), c: the deallocation
       function's call; nothing when the operand is a null pointer.
       computation: the type of the allocated node, the operand's without
       the pointee's cv-qualifiers ([expr.delete]/11).  value: 1 when the
       deleting destructor is called through the virtual table instead of
       b and c; d: for an array with a cookie, its size: b is each
       element's destruction (last first), c is given the cookie's address
       as allocated and array_count is the cookie's count */
    delete_object,
    /* [expr.throw]: a: the operand (0: none, a rethrow); not lowered */
    throw_exception,
    /* [expr.typeid]: an lvalue of type const std::type_info; not lowered */
    type_identification,
    /* [ext:gnu_va_arg] __builtin_va_arg (a, T): a: the va_list (an
       lvalue); a prvalue of the node's type */
    va_arg_,
    /* [ext:gnu_asm_statements] one operand of an asm statement (the value
       of its expression is no result): a: the expression (an lvalue for an
       output; an input is a prvalue, or an lvalue of an object the asm may
       take in memory), at: the constraint's string-literal token, b: the
       symbolic name's identifier token (0: none), value: 1 for an output */
    gnu_asm_operand,
    /* [ext:clang_source_location] __builtin_FILE() and the like: op: a
       source_location_builtin; at: where it is written; b: the function it
       is written in (0: none); a prvalue of type unsigned int or const
       char*.  Inside a default_argument it is the call's */
    source_location,
    /* [dcl.fct.default]/5: a default argument whose value depends on the
       call that uses it (source_location nodes inside): a: the argument's
       initialization; at: the call (the constructor of a default member
       initializer), b: the function the call is in (0: none) */
    default_argument,
    /* [class.cdtor]/4, [abi:vtable-ctor]: a: the address of the object
       under construction or destruction (this), b: its class, whose
       virtual table pointer is stored in it */
    vtable_pointer_store,
    /* ---- statements ---- */
    /* the statements in list[first .. first + count) */
    block,
    /* a: the expression */
    expression_statement,
    /* a: the variable declared, b: its initializer (0: none) */
    declaration_statement,
    /* a declaration of several variables: their declaration_statements in
       list[first .. first + count), in the enclosing block's scope */
    declarations,
    /* a: the condition (bool), b: the substatement, c: the else (0: none),
       d: the init-statement (0: none) */
    if_statement,
    /* a: the condition, b: the substatement */
    while_statement,
    /* a: the substatement, b: the condition */
    do_statement,
    /* a: the init-statement (0: none), b: the condition (0: none), c: the
       expression (0: none), d: the substatement */
    for_statement,
    /* a: the condition (promoted), b: the substatement, d: the
       init-statement */
    switch_statement,
    /* value: the case's converted value; a: the statement */
    case_label,
    /* a: the statement */
    default_label,
    /* value: the identifier's number; a: the statement */
    identifier_label,
    /* value: the identifier's number */
    goto_statement,
    break_statement,
    continue_statement,
    /* a: the operand, initialized as the result (0: none) */
    return_statement,
    /* [ext:gnu_asm_statements] asm (...): at: the template's
       string-literal token; list[first .. first + count): the outputs then
       the inputs, gnu_asm_operand nodes; a: how many are outputs; b, c:
       the clobbers' string-literal tokens, list[b .. b + c); d: the labels
       of asm goto, list[d .. d + value >> 8); value & 1: volatile, & 2:
       inline, & 4: goto, & 8: extended (a colon) */
    gnu_asm_statement,
    /* ; */
    null_statement,
    /* a condition that declares: a: the declaration_statement, b: its
       variable's value converted as the condition ([stmt.pre]/6, /7) */
    condition_declaration,
    /* a function definition: a: the outermost block; the parameters'
       entities (0 for an unnamed one) are list[first .. first + count),
       a non-static member function's `this` first (an unnamed variable of
       pointer type, [class.this]/1); b: a constructor's member
       initializations ([class.base.init]), a block of expression
       statements, run before a (0: none); c: a destructor's destruction
       of its members and bases ([class.dtor]/13), a block run after a
       (0: none); evaluation not_constant when the body's analysis was not
       well-formed (a tree in part, which the evaluator does not run) */
    function_body,
};

/* [ext:clang_source_location]: the value a source_location node has */
enum class source_location_builtin : uint8_t {
    file,      /* __builtin_FILE: the presumed file name */
    file_name, /* __builtin_FILE_NAME: its last component */
    function,  /* __builtin_FUNCTION: the function's name, "" outside one */
    line,      /* __builtin_LINE */
    column,    /* __builtin_COLUMN */
};

const char *semantic_kind_name(semantic_kind kind);

/* [conv]: the standard conversions, and the explicit ones of [expr.cast]
   analysis has */
enum class conversion_kind : uint8_t {
    lvalue_to_rvalue,    /* [conv.lval] */
    array_to_pointer,    /* [conv.array] */
    function_to_pointer, /* [conv.func] */
    integral,            /* [conv.integral], [conv.prom] */
    floating,            /* [conv.double], [conv.fpprom] */
    floating_integral,   /* [conv.fpint] */
    boolean,             /* [conv.bool] */
    null_pointer, /* [conv.ptr]/1: a null pointer constant to a pointer */
    pointer,      /* [conv.ptr]/2, [conv.qual]: the same address */
    to_void,      /* [expr.static.cast]/6 */
    /* the explicit conversions of a cast ([expr.cast]/4) that are not
       implicit ones: */
    qualification_cast,  /* [expr.const.cast]/3: the same address */
    pointer_cast,        /* [expr.static.cast]/13, [expr.reinterpret.cast]/6,
                            /7, /8: the same address, another type */
    pointer_to_integral, /* [expr.reinterpret.cast]/4 */
    integral_to_pointer, /* [expr.reinterpret.cast]/5 */
    /* [conv.ptr]/3: a pointer to a derived class to a pointer to its base
       class subobject: b: the base subobject (entity::base_subobject); a
       null pointer stays one */
    derived_to_base,
    /* [expr.static.cast]/11: a pointer to a base class subobject to a
       pointer to the derived class containing it: b: the base subobject;
       a null pointer stays one */
    base_to_derived,
    /* [conv.mem]/1: a null pointer constant to the null member pointer
       value */
    null_member_pointer,
    /* [conv.mem]/2: a pointer to member of a base class to a pointer to
       member of the derived class: value: the base's offset in it, added
       ([abi:data-member-pointers]); a null member pointer stays one */
    member_pointer_to_derived,
    /* [expr.static.cast]/12: the other way: value: the offset subtracted */
    member_pointer_to_base,
    /* [conv.qual]/3 of a pointer to member: the same value */
    member_pointer_qualification,
    /* [ext:clang_bit_cast] [bit.cast]/1: the operand's object
       representation as a value of another type of the same size */
    bit_cast,
    /* [ext:clang_convertvector] each element of a vector converted to
       the element type of a vector type of as many elements */
    vector_convert,
};

/* [expr.cast]/4: the casts that may perform an explicit conversion */
enum cast_kinds : uint8_t {
    cast_const = 1,       /* [expr.const.cast] */
    cast_static = 2,      /* [expr.static.cast] */
    cast_reinterpret = 4, /* [expr.reinterpret.cast] */
    /* the cast notation: each, and static_cast or reinterpret_cast
       followed by a const_cast */
    cast_notation = 7,
};

enum class increment_kind : uint8_t {
    pre_increment,
    pre_decrement,
    post_increment,
    post_decrement,
};

struct semantic_node {
    semantic_kind kind = semantic_kind::none;
    uint8_t op = 0;
    value_category category = value_category::prvalue;
    constness evaluation = constness::unknown;
    qualified_type type;
    /* a compound assignment's computation type */
    qualified_type computation;
    /* the token the node is reported at */
    token_index at = 0;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    /* a list's first index and its count */
    uint32_t first = 0, count = 0;
    uint64_t value = 0;
};

class semantic_tree
{
  public:
    semantic_tree();
    semantic_tree(const semantic_tree &) = delete;
    semantic_tree &operator=(const semantic_tree &) = delete;

    uint32_t add(const semantic_node &n);
    /* a list of node or entity indexes: its first index */
    uint32_t list(const uint32_t *items, size_t count);
    /* node n's list ([first, + count)): items[0 .. count) */
    void set_list(uint32_t n, const uint32_t *items, size_t count)
    {
        if (count)
            nodes[n].first = list(items, count);
        nodes[n].count = static_cast<uint32_t>(count);
    }

    const semantic_node &operator[](uint32_t n) const
    {
        return nodes[n];
    }
    semantic_node &operator[](uint32_t n)
    {
        return nodes[n];
    }
    const uint32_t *items(uint32_t first) const
    {
        return lists.data() + first;
    }
    size_t size() const
    {
        return nodes.size();
    }

  private:
    owned_array<semantic_node> nodes;
    owned_array<uint32_t> lists;
};

/* one expression analysis gave a type: what -fcxx-dump=type-claims has clang
   check */
struct analysed_expression {
    SyntaxElement at;
    SyntaxRef declaration = 0;
    expression_meaning meaning;
    /* where a claim about it can be placed: just after this token (the
       statement of a block it is in), or after the declaration if 0; not
       at all if placeable is false (a name of a condition would be out of
       scope there) */
    token_index place = 0;
    bool placeable = true;
};

/* the traits of one type-id: [meta.unary.cat], [meta.unary.comp],
   [meta.unary.prop]'s cv and sign questions, [meta.unary.prop.query]'s
   rank; clang's spellings */
enum class unary_trait_kind {
#define UNARY_TRAIT(kind, spelling) kind,
#include "../../../frontend/cplusplus/ext/clang/unary-traits.inc"
#undef UNARY_TRAIT
};

/* [class.inhctor.init]/1: the initialization of a constructor inherited
   from class `base`'s constructor `constructor`: the base subobject by it,
   from the parameters `parameters` of the inheriting constructor */
struct inherited_call {
    entity_index base = 0, constructor = 0;
    const owned_array<entity_index> *parameters = nullptr;
};

/* [ext:gnu_asm_statements] the characters of the ordinary string-literal at
   token t, escape sequences decoded, appended to out (ext/gnu/asm-statement.cpp) */
void gnu_asm_string_value(token_stream &tokens, token_index t,
                          owned_buffer &out);

class semantic_analysis
{
  public:
    /* an expression_statement's d: run only by the complete object
       constructor or destructor */
    static constexpr uint32_t complete_object_only = 1;
    semantic_analysis(const syntax_tree &tree, token_stream &tokens,
                      const parser &p, diagnostic_consumer &diagnostics);
    semantic_analysis(const semantic_analysis &) = delete;
    semantic_analysis &operator=(const semantic_analysis &) = delete;

    /* [basic.link]: every declaration of the translation unit, in order */
    void translation_unit();
    /* one declaration of a declaration-seq */
    analysis_result declaration(SyntaxRef n);
    /* for -fcxx-dump=type-claims: the value of each analysed prvalue that
       analysis did not fold and the evaluator of constant expressions can
       (expression/constant-evaluation.cpp), into its analysed_expression, so
       that the claims have clang check the evaluator's values too */
    void evaluate_analysed_expressions();

    types type_table;
    entities entity_table;
    owned_array<analysed_declaration> declarations;
    owned_array<unsupported_construct> unsupported;
    /* the names held (note_held) by the declarations being analysed */
    owned_array<entity_index> held_pending;
    owned_array<unsupported_construct> body_failures;
    owned_array<declared_name> declared;
    owned_array<analysed_expression> expressions;
    /* the semantic tree of every definition analysed */
    semantic_tree semantics;
    /* the entities a declaration of declarations defined with a tree
       (entity::definition): functions, and variables of namespace scope
       with an initializer */
    struct definition_record {
        SyntaxRef declaration = 0;
        entity_index entity = 0;
    };
    owned_array<definition_record> definitions;
    /* the non-static data members of the classes (entity::first_member) */
    owned_array<entity_index> class_members;
    /* the direct base class subobjects of the classes (entity::first_base) */
    owned_array<entity_index> class_bases;
    /* the virtual base subobjects of the classes
       (entity::first_virtual_base) */
    owned_array<entity_index> class_virtual_bases;
    /* the classes of the vbase offsets of the virtual tables
       (dynamic_class::first_vbase_entry) */
    owned_array<entity_index> vbase_entries;
    /* the virtual base subobject of class c whose class is v, 0 if v is
       no virtual base of c */
    entity_index virtual_base_of(entity_index c, entity_index v) const;
    /* the virtual bases of class c in inheritance graph order (preorder,
       [abi:class-types] III), classes */
    void virtual_bases_in_graph_order(entity_index c,
                                      owned_array<entity_index> &out) const;
    /* the base class subobjects of class base in an object of class c:
       through non-virtual bases only, and in all ([class.mi]/4: a virtual
       base is one subobject however many paths lead to it) */
    uint32_t nonvirtual_subobjects(entity_index c, entity_index base) const;
    uint32_t base_subobjects(entity_index c, entity_index base) const;
    /* the direct non-virtual base of c on a non-virtual path to base */
    entity_index nonvirtual_step(entity_index c, entity_index base) const;
    /* the dynamic classes (entity::dynamic) and their virtual tables'
       functions (dynamic_class::first_slot) */
    owned_array<dynamic_class> dynamic_classes;
    owned_array<entity_index> vtable_slots;
    owned_array<int64_t> vtable_adjustments;
    owned_array<vtable_group> vtable_groups;
    /* while a class's group is made: per slot, whether the class itself
       overrides it (its thunk's adjustment is the group's offset, known
       after the layout: dynamic_class_offsets) */
    owned_array<bool> vtable_own;
    struct group_source {
        entity_index base = 0;
        entity_index virtual_class = 0;
        uint64_t offset = 0;
    };
    owned_array<group_source> vtable_group_sources;
    void dynamic_class_offsets(entity_index c);
    const dynamic_class *dynamic_of(entity_index c) const
    {
        if (!entity_table[c].dynamic)
            return nullptr;
        return &dynamic_classes[entity_table[c].dynamic - 1];
    }
    /* t, or the innermost element type of an array t ([dcl.array]) */
    qualified_type element_type(qualified_type t) const
    {
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        return t;
    }
    /* the class of type t or of its innermost array element, 0 for any
       other type */
    entity_index element_class(qualified_type t) const
    {
        const struct type &r = type_table[element_type(t).unqualified()];
        if (r.kind != type_kind::class_type)
            return 0;
        return r.entity;
    }
    /* whether entity k is a class (not a union or an enumeration) */
    /* [namespace.std]: whether scope is ::std, the namespace std of the
       global namespace */
    bool std_namespace(entity_index scope) const;
    /* whether entity e has a name and it is spelled text */
    bool named(entity_index e, const char *text) const;
    bool is_class_entity(entity_index k) const
    {
        return k && entity_table[k].kind == entity_kind::class_type;
    }
    bool abstract_class(qualified_type t) const;
    /* the keyword node n begins with, none when it begins with a node or
       a token that is not a keyword */
    standard_keyword leading_keyword(SyntaxRef n) const
    {
        if (!tree[n].child_count || !tree.children(n)[0].is_token())
            return standard_keyword::none;
        return p.keyword_of(tokens[tree.children(n)[0].index()]);
    }
    /* whether x is the keyword k */
    bool is_keyword(SyntaxElement x, standard_keyword k) const
    {
        return x.is_token() && p.keyword_of(tokens[x.token()]) == k;
    }
    /* whether a class is trivially copyable and has a trivial destructor:
       its objects are copied as their bytes, passed in registers */
    bool trivial_class(entity_index c) const;
    /* [abi:definitions], [class.prop]/5 (std::is_empty): no non-static data
       member but zero-width bit-fields, no virtual function or base, every
       base empty (ext/clang/class-traits.cpp) */
    bool empty_class(entity_index c) const;
    /* the explicit instantiation a function is the subject of, its own or
       its class's ([temp.explicit]/10) */
    instantiation_kind explicitly_instantiated(entity_index f) const;
    /* [basic.types]/10: whether t is a literal type (unknown:
       analysis cannot tell; basic/literal-types.cpp) */
    constness literal_type(qualified_type t) const;
    constness literal_class(entity_index c) const;
    bool trivial_defaulted_default_constructor(entity_index c) const;
    bool copies_bytes(entity_index c) const;
    bool trivial_special_members(entity_index c) const;
    bool is_copy_constructor(entity_index c, entity_index k) const;
    bool declares_move_constructor(entity_index c) const;
    /* a user-declared copy constructor whose parameter is an lvalue reference
       ([class.copy.ctor]/1) */
    bool declares_copy_constructor(entity_index c) const;
    bool copy_assignment_deleted(entity_index c) const;
    bool default_constructor_deleted(entity_index c) const;
    bool default_constructed(entity_index c, entity_index k) const;
    /* whether the destructor of subobject class k is not accessible from
       the destructor or constructor of class c ([class.access.base]/1) */
    bool subobject_destructor_inaccessible(entity_index c,
                                           entity_index k) const;
    entity_index implicitly_movable(SyntaxElement operand);
    /* whether destroying an object of class c runs code */
    bool destroys(entity_index c) const;
    /* the type's size and alignment in bytes ([expr.sizeof],
       [expr.alignof]), 0 if not complete */
    uint64_t size_of_type(qualified_type t) const;
    /* the size (and alignment) of a pointer, which a reference member
       takes in a class ([abi:class-types]) */
    uint64_t pointer_size() const;
    uint64_t alignment_of_type(qualified_type t) const;
    /* [class.virtual]/2: whether member function e has the name,
       parameter-type-list and cv-qualification of virtual function g (a
       destructor: whether both are destructors) */
    bool overrides_function(entity_index e, entity_index g) const;

    /* the uses of names lookup resolved, each with the entity found:
       what -fcxx-dump=lookup-claims writes for clang to
       check */
    struct name_use {
        token_index at = 0;
        entity_index entity = 0;
        SyntaxRef declaration = 0;
    };
    owned_array<name_use> name_uses;

    /* the syntax tree analysed */
    const parser &parsed() const
    {
        return p;
    }
    /* a token's spelling, as written (an entity's abi_tag, its section) */
    size_t token_spelling(token_index i, vbuffer *out) const
    {
        return tokens.spelling(i, out);
    }

    /* the closure numbers of the translation unit [impl.clang] gives a
       lambda whose closure has no mangling number ($_N, in the order the
       symbols of their members are made); numbered only when allowed (by
       lowering) */
    mutable owned_array<entity_index> unnumbered_closures;
    mutable bool number_closures = false;

  private:
    /* the node needs rules that are not written: those of the subclause
       that defines its nonterminal, or of the one named */
    /* the construct at n has no rules yet: recorded with the subclause
       and the call site (the check that refused it, for the diagnostic's
       note) */
    analysis_result not_supported(SyntaxRef n, call_site where = call_site());
    analysis_result not_supported(SyntaxRef n, const char *subclause,
                                  call_site where = call_site());
    /* whether the parser gave up on a part of the declaration */
    bool parsed_completely(SyntaxRef n) const;

    /* the node at, or its first token, violates a rule: the diagnostic,
       naming the subclause */
    analysis_result ill_formed(SyntaxRef at, const char *rule,
                               const char *subclause);
    analysis_result ill_formed_at_token(token_index at, const char *rule,
                                        const char *subclause);

    /* a rule clang enforces only when asked: ill-formed when the
       diagnostic is an error on this command line, else well-formed with
       the warning (or nothing) */
    /* whether token at comes from a system header (# 1 "f" 3, -isystem) */
    bool in_system_header(token_index at) const;
    analysis_result extension_used(diagnostic_id id, token_index at,
                                   const char *argument);

    /* [dcl.pre] */
    analysis_result empty_declaration(EmptyDeclaration n);
    analysis_result static_assert_declaration(StaticAssertDeclaration n);
    /* [dcl.typedef]/2: an alias-declaration's typedef-name */
    analysis_result alias_declaration(AliasDeclaration n);
    /* ... the type its defining-type-id denotes */
    analysis_result alias_type(SyntaxRef n, qualified_type &type);
    /* [temp.pre], [temp.expl.spec], [temp.explicit], [temp.deduct.guide]
       (template/): the declarations of templates and of their
       specializations; no rules yet, each answers its subclause */
    analysis_result template_declaration(TemplateDeclaration t);
    analysis_result template_simple_declaration(TemplateDeclaration t,
                                                SimpleDeclaration s);
    /* [temp.fct]: a function template defined at namespace scope */
    analysis_result function_template_definition(SyntaxRef n,
                                                 SyntaxRef definition);
    analysis_result explicit_specialization(ExplicitSpecialization n);
    analysis_result explicit_instantiation(ExplicitInstantiation n);
    analysis_result class_explicit_instantiation(SyntaxRef n, SyntaxRef id,
                                                 bool extern_);
    analysis_result member_explicit_instantiation(SyntaxRef n,
                                                  SyntaxRef declaration,
                                                  uint32_t lead, bool extern_);
    /* whether the declarator may have a simple-template-id as its
       declarator-id */
    bool declarator_template_id = false;
    analysis_result deduction_guide(DeductionGuide n);
    analysis_result deduction_guide(TemplateDeclaration t, DeductionGuide n);
    analysis_result deduction_guide_candidate(TemplateDeclaration t,
                                              DeductionGuide n);
    /* the function type of a deduction-guide: its parameters returning the
       class template specialization it names (in the scope current) */
    analysis_result deduction_guide_type(SyntaxRef guide, qualified_type &type);
    /* [over.match.class.deduct]/1, [dcl.type.class.deduct]/2: the class
       template specialization the placeholder for a deduced class type
       (the class template whose function_templates' index plus one is
       ct) names when initialized from the arguments of n, a postfix
       expression C(arguments) or C{arguments} */
    analysis_result deduced_class(SyntaxRef n, uint32_t ct, entity_index &c);
    analysis_result simple_declaration(SyntaxRef n);
    analysis_result function_definition(FunctionDefinition definition);
    /* [dcl.link]: extern "C" or extern "C++", and the declarations in it */
    analysis_result linkage_specification(LinkageSpecification n);
    /* a declaration of the kinds analysis has (declaration's switch) */
    analysis_result declaration_by_kind(SyntaxRef n);
    /* [dcl.link]/5: the language linkage of the declarations analysed
       now: "C" (true) or "C++"; and whether the linkage-specification is
       the declaration's own, without braces ([dcl.link]/7) */
    bool c_language = false;
    bool linkage_without_braces = false;
    analysis_result init_declarators(SyntaxRef declaration,
                                     SyntaxRef specifiers, bool definition);

    /* [dcl.spec]: what a decl-specifier-seq says */
    struct specified {
        qualified_type type;
        /* [dcl.type]/3: a defining-type-specifier that is not a
           cv-qualifier was there */
        bool has_type = false;
        bool typedef_ = false, static_ = false, extern_ = false,
             inline_ = false, constexpr_ = false, virtual_ = false;
        /* [dcl.constexpr]/2: consteval (constexpr_ too) */
        bool consteval_ = false;
        /* [class.friend]: the friend specifier (only where friend_allowed) */
        bool friend_ = false;
        /* [dcl.stc]/8: mutable (only where mutable_allowed) */
        bool mutable_ = false;
        /* [ext:clang_c11_thread_local] _Thread_local */
        bool c11_thread_local = false;
        /* [dcl.stc]/1: thread_local (c11_thread_local too) */
        bool cxx_thread_local = false;
        /* [dcl.constinit]/1: constinit */
        bool constinit_ = false;
        /* [class.conv.fct]/2: explicit (only where explicit_allowed) */
        bool explicit_ = false;
        /* explicit ( value-dependent condition ): see entity */
        bool explicit_dependent = false;
        /* a class-specifier or elaborated-type-specifier was there: a
           declaration with no declarator declares the class
           ([dcl.pre]/5) */
        bool declares_class = false;
        /* ... and it was an enum-specifier */
        bool declares_enumeration = false;
        /* [ext:gnu_attributes] the GNU attributes among the
           decl-specifiers and before them */
        uint32_t gnu_flags = 0;
        token_index section = 0, abi_tag = 0;
        uint32_t gnu_priority = 0;
        entity_index gnu_cleanup = 0;
        uint64_t aligned = 0;
        /* [ext:gnu_attributes] the alignment a typedef-name among the
           type-specifiers was declared with (aligned), 0 if none: a
           declared object or member of that type, not a pointer to it,
           is aligned so (the type table has no type for the name) */
        uint32_t typedef_aligned = 0;
    };
    /* the alignment of typedef_aligned that applies to declared, the type
       a declarator made of specified (arrays keep it, a pointer,
       reference or function does not) */
    uint32_t typedef_alignment_of(type_index specified, qualified_type declared,
                                  uint32_t typedef_aligned) const;
    analysis_result decl_specifier_seq(SyntaxRef seq, specified &s,
                                       bool parameter);
    /* [temp.explicit], [temp.expl.spec]: the function template's
       specialization a declaration of one declarator names (its
       instantiation record, which an explicit specialization makes);
       its decl-specifier-seq's meaning in s */
    analysis_result named_specialization(SyntaxRef n, SyntaxRef declaration,
                                         uint32_t lead, bool specialization,
                                         bool extern_, specified &s,
                                         uint32_t &index);
    /* [dcl.type.simple] Table 14: the type of the simple-type-specifier
       keywords of a decl-specifier-seq */
    struct simple_type_keywords {
        unsigned signed_ = 0, unsigned_ = 0, short_ = 0, long_ = 0, char_ = 0,
                 int_ = 0, double_ = 0, alone = 0, int128 = 0, float128 = 0,
                 complex = 0;
        fundamental_type alone_type = fundamental_type::none;
        token_index first = 0;
    };
    analysis_result simple_type(SyntaxRef seq, const simple_type_keywords &k,
                                type_index &type);
    analysis_result complex_type(SyntaxRef seq, const simple_type_keywords &k,
                                 unsigned keywords, type_index &type);

    /* [dcl.meaning]: the type a declarator gives its declarator-id */
    struct declarator_meaning {
        bool has_id = false;
        token_index id = 0;
        /* a reference the declarator made, rather than a typedef-name's
           ([dcl.ref]/5 and /6) */
        bool reference_made = false;
        /* a qualified-id's class: [dcl.meaning]/1 the declarator declares
           a member of it again (a definition outside the class) */
        entity_index qualifier = 0;
        /* ... or its namespace (0: the global one): a member of it
           declared again outside it ([namespace.memdef]/2) */
        bool qualified_namespace = false;
        entity_index namespace_qualifier = 0;
        /* [ext:gnu_attributes] the alignment an aligned attribute of the
           declarator asks for, 0 if none; the other GNU attributes of the
           declarator (gnu_attribute_flag) and a section attribute's
           string-literal */
        uint64_t aligned = 0;
        uint32_t gnu_flags = 0;
        token_index section = 0, abi_tag = 0;
        uint32_t gnu_priority = 0;
        entity_index gnu_cleanup = 0;
        /* mode (gnu_mode): the mode's identifier */
        token_index mode = 0;
        /* [ext:gnu_asm_labels] the asm label's string-literal, 0 if none */
        token_index asm_label = 0;
        /* the declarator-id is ~ class-name: a destructor ([class.dtor]/1) */
        bool destructor = false;
        /* the declarator-id is an operator-function-id ([over.oper]/1) */
        bool operator_function = false;
        preprocessing_op_or_punc implemented = {};
        allocation_function allocation = allocation_function::none;
        /* the declarator-id is a literal-operator-id ([over.literal]) */
        bool literal_operator = false;
        /* ... or a conversion-function-id ([class.conv.fct]) */
        bool conversion_function = false;
        /* the declarator-id is a simple-template-id (declarator_template_id
           allows it): an explicit specialization's or instantiation's */
        SyntaxRef template_id = 0;
        /* [dcl.fct]/21: the declarator-id (or the abstract declarator)
           has an ellipsis: a function parameter pack (pack_declarator
           allows it) */
        token_index pack = 0;
    };
    /* [dcl.fct]/2: noptr-declarator parameters-and-qualifiers
       trailing-return-type (placeholder-type-specifier.cpp) */
    analysis_result trailing_return(SyntaxRef declaration, SyntaxRef n,
                                    qualified_type &type,
                                    declarator_meaning &m);
    bool pack_declarator = false;
    /* a type-id T ... is a pack expansion's pattern T */
    bool type_id_expansion = false;
    /* [temp.variadic]/5: whether the parameter-declaration d is a pack
       expansion of an instantiation's packs: its ellipsis and the packs'
       length (ellipsis 0: it is not); last: it may be one */
    analysis_result parameter_pack_expansion(SyntaxRef d, bool last,
                                             uint32_t &elements,
                                             token_index &ellipsis);
    bool depends_on_template_parameter(qualified_type t) const;
    /* whether a type is made of a template-parameter type at position from
       or after */
    bool depends_on_parameter_from(qualified_type t, uint64_t from) const;
    /* the ellipsis of the last parameter-declaration's declarator, if it
       declares a function parameter pack, else 0 */
    token_index declared_pack = 0;
    /* [except.spec]/8, /12: a destructor's or deallocation function's type
       non-throwing without a noexcept-specifier */
    void implicitly_non_throwing(const declarator_meaning &m,
                                 qualified_type &type);
    analysis_result declarator(SyntaxRef declaration, SyntaxElement d,
                               qualified_type &type, declarator_meaning &m);
    token_index declarator_id(SyntaxElement d) const;
    /* [dcl.meaning]/1: the nested-name-specifier of a qualified
       declarator-id: the class or namespace whose member is declared */
    analysis_result declarator_qualifier(SyntaxRef nested,
                                         declarator_meaning &m);
    void hold_names(SyntaxRef declaration);
    /* e was just held by a template-declaration (held_by_template): the
       declaration being analysed gives it its reason when it is refused */
    void note_held(entity_index e);
    entity_index make_held_name(uint32_t name, entity_index scope);
    bool declared_by(entity_index e, SyntaxRef node) const;
    analysis_result declare_local(SyntaxRef declaration, token_index id,
                                  entity_kind kind, qualified_type type,
                                  const specified &s,
                                  entity_index &declared_entity);
    /* [basic.lookup.unqual]/1: the entity an unqualified name finds from
       the current scope out, 0 if none or if what it finds is not one
       entity analysis has (lookup_unqualified says which) */
    entity_index lookup(uint32_t name) const;
    /* ---- name lookup ([basic.lookup], basic/name-lookup.cpp) ---- */
    /* which declarations a lookup considers */
    enum class lookup_filter : uint8_t {
        /* all ([basic.lookup]/1) */
        any,
        /* [basic.lookup.elab]/1: "ignoring any non-type names that have
           been declared" */
        types,
        /* [basic.lookup.qual]/1: "lookup of the name preceding that ::
           considers only namespaces, types, and templates whose
           specializations are types" */
        scopes,
        /* [basic.lookup.udir]/1: "only namespace names are considered" */
        namespaces,
        /* [over.match.oper]/3.2: "all member functions are ignored": the
           scopes of classes are not searched */
        non_members,
    };
    /* what a lookup found */
    struct lookup_result {
        /* the entity the name denotes, 0 if there is none or more than
           one */
        entity_index entity = 0;
        /* the scope the name was found bound in: a using-declaration's,
           not the scope of the entity it names */
        entity_index found_in = 0;
        /* a declaration analysis did not follow has the name (hold_names),
           or the declarations found are functions of more than one
           namespace (an overload set): nothing is concluded */
        bool unknown = false;
        /* [namespace.udir]/6: (a note) "If name lookup finds a declaration for
           a name in two different namespaces, and the declarations do not
           declare the same entity and do not declare functions or function
           templates, the use of the name is ill-formed";
           [class.member.lookup]/6 for a class */
        bool ambiguous = false;
        /* unknown: the first name found that analysis kept with no entity
           (kind none) */
        entity_index unknown_entity = 0;
    };
    /* the subclause an unsupported use of the name found as r (not
       known, kind none) waits for: [temp.names] for a template's name,
       else otherwise */
    const char *unknown_name_subclause(entity_index e,
                                       const char *otherwise) const;
    lookup_result lookup_unqualified(uint32_t name, lookup_filter filter) const;
    /* [namespace.qual]/2: the name looked up in the namespace n (0: the
       global namespace) */
    lookup_result lookup_in_namespace(entity_index n, uint32_t name,
                                      lookup_filter filter) const;
    /* [basic.lookup.qual]: the name looked up in the scope a
       nested-name-specifier nominates */
    lookup_result lookup_qualified(entity_index scope, uint32_t name,
                                   lookup_filter filter) const;
    /* the entity name is bound to in scope itself, as the filter sees
       it: 0 if none */
    entity_index declared_in(entity_index scope, uint32_t name,
                             lookup_filter filter) const;
    /* one more declaration found for a name: r's entity if it is the same
       entity or the first, else ambiguous or unknown */
    void found_also(lookup_result &r, entity_index e, entity_index in) const;
    /* [basic.lookup.argdep]: the functions named name in the namespaces
       associated with arguments of these types, added to found (each
       once); false if one of those namespaces has a declaration of the
       name that analysis did not follow.  The function templates found
       (their held names, each once) go to templates; without it a
       function template is one analysis did not follow too */
    bool argument_dependent_lookup(
        uint32_t name, const owned_array<qualified_type> &arguments,
        owned_array<entity_index> &found, entity_index known = 0,
        owned_array<entity_index> *templates = nullptr) const;
    /* [basic.lookup.argdep]/2: the associated classes and namespaces of
       arguments of these types, with the inline namespaces /2 adds; false
       as associated_with */
    bool associated_namespaces(const owned_array<qualified_type> &arguments,
                               owned_array<entity_index> &classes,
                               owned_array<entity_index> &namespaces) const;
    /* [basic.lookup.argdep]/2: the associated entities (classes) and
       namespaces of the type t, added to the sets; false if a class of it
       is one analysis did not follow */
    bool associated_with(qualified_type t, owned_array<entity_index> &classes,
                         owned_array<entity_index> &namespaces) const;
    /* [basic.scope.namespace]: whether s is a namespace (0: the global
       one), and the innermost namespace that encloses the scope s */
    bool is_namespace(entity_index s) const;
    entity_index enclosing_namespace(entity_index s) const;
    /* whether analysis is at block scope (in a function's body or its
       parameters) */
    bool at_block_scope() const
    {
        return !is_namespace(current_scope) &&
               current_scope != template_member_scope;
    }
    /* define_template_member's scope of the template-parameters' names,
       which stands for the class template's namespace */
    entity_index template_member_scope = 0;
    /* whether namespace inner is n or is enclosed by it */
    bool namespace_encloses(entity_index n, entity_index inner) const;
    /* [namespace.udir]/2: "the nearest enclosing namespace which contains
       both the using-directive and the nominated namespace" */
    entity_index common_namespace(entity_index a, entity_index b) const;
    /* record a use of a name lookup resolved (name_uses) */
    void used(token_index at, entity_index e)
    {
        name_use u;
        u.at = at;
        u.entity = e;
        u.declaration = current_declaration;
        name_uses.append(u);
    }
    /* [basic.link]/7: a function declared in a block, or a variable
       declared there with extern: the member of the innermost enclosing
       namespace it declares */
    analysis_result
    declare_in_block_with_linkage(SyntaxRef declaration, token_index id,
                                  entity_kind kind, qualified_type type,
                                  const specified &s, bool initialized,
                                  entity_index &declared_entity);
    /* [basic.link]/7: the functions and variables declared only in
       blocks: members of their namespaces whose names are not bound
       there */
    owned_array<entity_index> block_declared;
    /* [class.friend]/1: a class's friends.  A friend class not declared
       yet ([namespace.memdef]/3: the elaborated-type-specifier declares it
       in the innermost enclosing namespace, where ordinary lookup does
       not find it) is the class of that name and scope declared later
       (befriended 0); a function first declared by a friend declaration
       is in block_declared, unbound in its namespace until declared
       there, and found by argument-dependent lookup through the
       granting class ([basic.lookup.argdep]/4.3) */
    struct friend_record {
        entity_index granting = 0;
        entity_index befriended = 0;
        entity_index scope = 0;
        uint32_t name = 0;
        /* [temp.friend]/1: a function template (its entity) all of whose
           specializations are friends */
        entity_index template_entity = 0;
    };
    owned_array<friend_record> friends;
    /* function_template_definition: the function_templates index plus one
       of the template its last declaration declared or redeclared */
    uint32_t last_function_template = 0;
    analysis_result friend_function_template(entity_index c,
                                             TemplateDeclaration n,
                                             SyntaxRef inner);
    analysis_result friend_member_declaration(entity_index c,
                                              MemberSimpleDeclaration d,
                                              owned_array<SyntaxRef> &bodies);
    analysis_result friend_declaration(entity_index c, SyntaxRef n,
                                       owned_array<SyntaxRef> &bodies);
    analysis_result friend_class(entity_index c, SyntaxRef n,
                                 SyntaxElement type);
    analysis_result qualified_friend_function(entity_index c, SyntaxRef n,
                                              const declarator_meaning &m,
                                              qualified_type type);
    analysis_result friend_function(entity_index c, SyntaxRef n,
                                    owned_array<SyntaxRef> &bodies);
    /* [class.friend]/6: the functions friend declarations define */
    struct friend_definition {
        SyntaxRef definition = 0;
        entity_index function = 0, granting = 0;
    };
    owned_array<friend_definition> friend_definitions;
    entity_index friend_defined_by(SyntaxRef n, entity_index c) const;
    /* the scope unqualified lookup goes to after s: its enclosing one, or
       for a friend function defined in a class, the class
       ([basic.lookup.unqual]/1) */
    entity_index lexical_parent(entity_index s) const;
    /* [class.compare.default]: = default; the class of the defaulted
       comparison function whose body function_body makes */
    bool defaulted_equality_declared(entity_index c, uint32_t name,
                                     qualified_type type);
    entity_index defaulted_comparison = 0;
    analysis_result defaulted_equality(token_index at,
                                       const owned_array<uint32_t> &parameters,
                                       uint32_t &block);
    analysis_result friend_function_body(entity_index c,
                                         FunctionDefinition definition,
                                         entity_index f);
    bool friend_names(const friend_record &f, entity_index e) const;
    bool grants(entity_index k, entity_index e) const;
    bool in_friend_of(entity_index k) const;
    /* a function of block_declared whose name and parameter-type-list
       are these, in namespace scope, 0 if none */
    entity_index declared_unbound(entity_index scope, uint32_t name,
                                  qualified_type type) const;
    /* [basic.scope.hiding]/2: the class or enumeration of the scope that
       the variable or function declare() declares now hides, 0 if none */
    entity_index declaration_hides = 0;
    /* [basic.scope.pdecl]/1: "The point of declaration for a name is
       immediately after its complete declarator": the declarator (an
       array's bound) may have used the name for an entity of an enclosing
       scope, which the name no longer denotes where claims about the
       declaration's expressions are placed: those are not made */
    void no_claims_after_hiding(uint32_t name);
    /* [basic.scope.pdecl]/7.1: the elaborated-type-specifier that is all
       of the declaration analysed now (class-key identifier ;), 0 if
       none */
    SyntaxRef sole_elaborated = 0;
    /* [basic.scope.pdecl]/7: the class an elaborated-type-specifier
       declares in scope, hidden by a non-type of the name if there is
       one */
    entity_index declare_class(entity_index scope, uint32_t name,
                               bool is_union);
    /* [dcl.meaning]/1: the declarator-id analysed now is a qualified-id
       that nominates a namespace: the scope whose member declare()
       declares again */
    bool qualified_declarator = false;
    entity_index qualified_scope = 0;
    /* whether the type is, or is made of, a class or enumeration whose
       name has no external linkage */
    bool type_without_external_linkage(qualified_type t,
                                       bool visible = false) const;
    /* [basic.link]/4.3: whether t is (an array of) an unnamed class
       without a typedef name for linkage purposes */
    bool unnamed_class_object(qualified_type t) const;
    /* [basic.link]/4: whether the namespace n is an unnamed namespace or
       declared directly or indirectly within one */
    bool within_unnamed_namespace(entity_index n) const;
    /* [namespace.udir]: the using-directives, each in the scope (a
       namespace or a block) it appears in; an unnamed namespace's implied
       one ([namespace.unnamed]/1) and an inline namespace's
       ([namespace.def]/7) are among them */
    struct using_directive_record {
        entity_index scope = 0;
        entity_index nominated = 0;
    };
    owned_array<using_directive_record> using_directives;
    /* [basic.scope.namespace]/1, [namespace.def] */
    analysis_result namespace_definition(SyntaxRef n);
    /* [namespace.udir], [namespace.alias], [namespace.udecl] */
    analysis_result using_directive(UsingDirective n);
    analysis_result namespace_alias_definition(NamespaceAliasDefinition n);
    analysis_result using_declaration(UsingDeclaration n);
    analysis_result using_declarator(UsingDeclarator n, bool if_exists = false);
    /* [basic.lookup.udir]: the namespace a namespace-name, after a
       nested-name-specifier (0: none), names */
    analysis_result named_namespace(SyntaxRef at, SyntaxRef qualifier,
                                    token_index name, entity_index &n);
    /* the declarations of a declaration-seq in braces, each analysed as a
       declaration of the translation unit (its own record in
       declarations) */
    void declaration_seq_in(SyntaxRef container);
    /* [basic.lookup.qual]: the scope a nested-name-specifier nominates: a
       namespace (global: the global namespace, entity 0), a class or an
       enumeration */
    struct nominated_scope {
        entity_index entity = 0;
        bool global = false;
        /* [temp.dep.type]/9: the nested-name-specifier names a dependent
           type (only when the caller allows one; entity is then 0) */
        type_index dependent = 0;
    };
    analysis_result nested_name_specifier(NestedNameSpecifier n,
                                          nominated_scope &scope,
                                          bool dependent_allowed = false);
    /* whether class c's scope, for unqualified lookup of name, is to be
       passed over: found only in a dependent base ([temp.dep]/3); unknown
       when a dependent base and another one both may have it */
    /* [temp.dep.type]/9: whether a base-specifier's class-or-decltype in
       class template specialization c's class-specifier names one of the
       template-parameters (bound in c) */
    bool dependent_base_specifier(entity_index c, SyntaxElement name) const;
    uint32_t own_pack_parameter(uint32_t name) const;
    uint32_t explicit_argument_position(uint32_t ft, uint32_t i) const;
    analysis_result base_class_name(SyntaxElement name, SyntaxRef n,
                                    entity_index &base, token_index &id);
    bool in_dependent_base_only(entity_index c, uint32_t name,
                                bool &unknown) const;
    /* [temp.res]/3: typename-specifier, as a type */
    analysis_result typename_type(SyntaxRef n, type_index &type, uint32_t &cv);
    analysis_result missing_member(token_index t, entity_index scope);
    /* a dependent type of kind, one per name and value in list */
    type_index dependent_type(owned_array<entity_index> &list, entity_kind kind,
                              uint32_t name, uint64_t value);
    /* the type typename Q::name for a dependent Q, one per Q and name */
    type_index dependent_member_type(type_index qualifier, uint32_t name);
    type_index dependent_written_type(SyntaxRef n);
    bool names_dependent_entity(token_index first, token_index end) const;
    bool pack_depends(entity_index pack) const;
    owned_array<entity_index> dependent_members;
    owned_array<entity_index> dependent_written_types;
    /* a block scope in the current one, made current */
    entity_index open_scope(entity_index conflicts, bool parameters);
    analysis_result pointer_operator(SyntaxRef op, qualified_type &type,
                                     declarator_meaning &m);
    analysis_result array(SyntaxRef at, SyntaxElement bound,
                          qualified_type &type);
    analysis_result reference(SyntaxRef at, bool rvalue, qualified_type &type,
                              declarator_meaning &m);
    /* [dcl.ref]/6: the type lvalue (rvalue) reference to type, a reference
       type collapsing; type is not cv void */
    qualified_type reference_to(qualified_type type, bool rvalue);
    /* the array type array (a record of type_kind::array) is with its
       element type replaced by element (its bound kept) */
    type_index array_with_element(const struct type &array,
                                  qualified_type element);
    /* [dcl.fct] */
    analysis_result function(SyntaxRef parameters, qualified_type &type);
    analysis_result misplaced_function_qualifiers(SyntaxRef n,
                                                  qualified_type type,
                                                  bool member);
    /* [ext:gnu_attributes] the attributes of a gnu-attribute-specifier
       (or a gnu-attribute-specifier-seq) analysis has rules for */
    struct gnu_attributes {
        uint64_t aligned = 0;
        uint32_t flags = 0;
        token_index section = 0, abi_tag = 0;
        uint32_t priority = 0;
        entity_index cleanup = 0;
        /* mode (gnu_mode): the mode's identifier */
        token_index mode = 0;
        /* [ext:clang_ext_vector_type] taken where allowed (an
           alias-declaration): ext_vector_type(N)'s N, or the entity of the
           non-type template-parameter N names */
        bool ext_vector_allowed = false;
        uint64_t ext_vector = 0;
        entity_index ext_vector_parameter = 0;
    };
    analysis_result gnu_attribute_specifier(SyntaxRef n, gnu_attributes &g);
    analysis_result add_gnu_abi_tag(SyntaxRef at, token_index literal,
                                    gnu_attributes &g);
    analysis_result gnu_integer_argument(SyntaxRef at, SyntaxElement argument,
                                         uint64_t &value);
    analysis_result gnu_cleanup_function(SyntaxRef at, SyntaxElement a,
                                         uint32_t count, gnu_attributes &g);
    bool gnu_cleanup_takes_pointer(entity_index function) const;
    bool code_align_argument(token_index at) const;
    bool fallthrough_statement(SyntaxElement s) const;
    bool case_or_default_label(SyntaxElement s) const;
    analysis_result gnu_attribute_specifier(SyntaxRef n, declarator_meaning &m);
    analysis_result gnu_attribute_specifier(SyntaxRef n, specified &s);
    analysis_result gnu_mode_type(SyntaxRef at, token_index mode,
                                  qualified_type &type);
    analysis_result array_element_alignment(SyntaxRef at,
                                            qualified_type declared,
                                            uint32_t through);
    uint32_t leading_gnu_attributes(SyntaxRef n) const;
    bool same_abi_tag(token_index a, token_index b) const;
    bool class_specifier_before(SyntaxRef seq, uint32_t i) const;
    bool gnu_or_without_effect(SyntaxRef n) const;
    /* [dcl.attr.grammar]: every attribute at n (an attribute-specifier,
       -seq or -list) changes nothing vcc generates */
    bool attributes_without_effect(SyntaxElement n) const;
    /* ... or [[noreturn]], whose GCC flag goes into flags */
    bool attributes_as_gnu(SyntaxElement n, uint32_t &flags) const;
    token_index scoped_gnu_abi_tag(SyntaxElement n) const;
    bool gnu_attributes_without_effect(SyntaxElement n) const;
    bool class_head_attributes_without_effect(SyntaxElement c) const;
    uint32_t attributes_without_effect_before(SyntaxRef declaration) const;
    bool pack_only_expanded(uint32_t name, token_index first,
                            token_index end) const;
    bool declares_unnamed_class(SyntaxRef seq) const;
    /* [ext:gnu_library_builtins] the function __builtin_NAME (an
       identifier) declares at its first use, 0 if it is not one */
    entity_index library_builtin(uint32_t name);
    entity_index allocation_builtin(uint32_t name);
    /* [ext:gnu_library_builtins] whether the name is spelled __builtin_...:
       [lex.name]/3 reserves it to the implementation, so a scope with
       names analysis did not follow does not hide the builtin */
    bool builtin_spelled(uint32_t name) const;
    entity_index compiler_builtin(uint32_t name, const char *spelling,
                                  size_t length);
    entity_index declare_builtin(uint32_t name, uint32_t symbol,
                                 qualified_type result,
                                 const qualified_type *parameters, size_t count,
                                 bool variadic, uint32_t flags,
                                 bool non_throwing = false);
    /* real: the fundamental_type of builtin_type::real */
    qualified_type builtin_parameter_type(uint8_t t, uint8_t real = 0);
    /* the parameters of the function declarator analysed last: a function
       definition's, with the types they have in its body ([dcl.fct]/5's
       adjustment, top-level cv-qualifiers kept) */
    struct parameter_meaning {
        token_index id = 0;
        qualified_type type;
        /* its parameter-declaration, and the initializer-clause of its
           default argument ([dcl.fct.default]/1), none if it has none */
        SyntaxRef declaration = 0;
        SyntaxElement default_argument;
        /* [temp.variadic]/5: an element of the function parameter pack
           (an instantiation's) of this name, id 0 */
        uint32_t pack_name = 0;
    };
    /* [dcl.fct.default]: the default arguments of function_parameters,
       which the declaration n of the function f gives it (specialization:
       f a function template's specialization, its template's) */
    analysis_result default_arguments(SyntaxRef n, entity_index f,
                                      entity_kind kind,
                                      bool specialization = false);
    /* the member functions of the classes being defined whose default
       arguments wait for the class to be complete ([class.mem]/6): their
       parameters are member_default_parameters[first .. first + count) */
    struct pending_default_arguments {
        entity_index function = 0;
        SyntaxRef declaration = 0;
        uint32_t first = 0, count = 0;
    };
    owned_array<pending_default_arguments> pending_defaults;
    owned_array<parameter_meaning> member_default_parameters;
    /* ... analysed now, of the complete class c */
    bool analysing_member_defaults = false;
    analysis_result member_default_arguments(entity_index c);
    /* ... of the member function f declared by declaration: kept for then */
    void defer_default_arguments(entity_index f, SyntaxRef declaration,
                                 const owned_array<parameter_meaning> &list);
    /* the default arguments' initializations: a function's are
       [entity::first_default .. + its parameter count), 0 for a parameter
       without one */
    owned_array<uint32_t> default_argument_nodes;
    owned_array<parameter_meaning> function_parameters;
    /* [temp.variadic]/2: the name of the function parameter pack of no
       parameters function_parameters has, 0 if none */
    uint32_t function_empty_pack = 0;
    /* the cv-qualifier-seq of the function declarator analysed last
       ([dcl.fct]/6: only a member function's) */
    uint32_t function_cv = 0;
    /* ... and whether it had a noexcept-specifier ([except.spec]/2) */
    bool function_exceptions_specified = false;
    /* a declarator analysed now may have a cv-qualifier-seq: a member
       declarator, or one whose declarator-id is a qualified-id */
    bool in_member_declarator = false;
    /* in a member function template's declarator: 1 the template's own
       (member-template.cpp), 2 a specialization's (instantiation.cpp) */
    uint8_t in_member_template_declarator = 0;
    /* enters (which 1 or 2) or leaves (0) a member function template's
       declarator, a member declarator */
    void member_template_declarator(uint8_t which)
    {
        in_member_declarator = which != 0;
        in_member_template_declarator = which;
    }
    /* [dcl.fct.spec]/2: virtual is analysed on a member-declaration's
       decl-specifier-seq only; a member-declarator's virt-specifiers and
       pure-specifier ([class.mem]/1) for the declarator it holds */
    bool virtual_allowed = false;
    /* [dcl.stc]/8: mutable is analysed on a member-declaration's
       decl-specifier-seq only */
    bool mutable_allowed = false;
    /* [class.friend]: friend is analysed on a friend declaration's
       decl-specifier-seq only */
    bool friend_allowed = false;
    /* a friend function's declarator: a qualified declarator-id leaves the
       class being defined ([class.friend]/1, [basic.lookup.qual]/3) */
    bool friend_declarator = false;
    /* [ext:gnu_float128] the expression analysed next may name an object of
       type __float128 (or a copy of one through an identity cast): set by
       the contexts that only pass the value on (a function's argument, a
       cast's operand), cleared by expression(); every other use of a
       binary128 value is not analysed */
    bool binary128_value_passes = false;
    /* [ext:clang_atomic] the expression analysed next is the operand of &,
       sizeof or an assignment's left operand: an atomic object stays one
       (anywhere else it is read, expression) */
    bool atomic_object_operand = false;
    /* [ext:clang_atomic] the value type of an atomic type the operations
       are analysed for (an integer, enumeration or pointer of 1, 2, 4 or 8
       bytes, which the code generator reads and writes with one
       instruction); 0 for any other */
    type_index atomic_value_type(qualified_type t) const;
    analysis_result atomic_value_check(SyntaxRef n, qualified_type value);
    /* whether e (analysed as m, of type __float128) is an object's name or
       an identity cast of one: a value that is only passed on */
    analysis_result passed_expression(SyntaxElement e, expression_meaning &m);
    analysis_result passed_target_expression(SyntaxElement e,
                                             qualified_type target,
                                             expression_meaning &m);
    bool passes_binary128(SyntaxElement e, const expression_meaning &m) const;
    /* whether a conversion of a __float128 or complex value to another
       type, or from one, is the one analysis does not handle yet */
    bool binary128_or_complex(qualified_type t) const;
    bool binary128_conversion(qualified_type from, qualified_type to) const;
    /* ---- placeholder types and decltype (placeholder-type-specifier.cpp,
       decltype-specifier.cpp) ---- */
    /* [dcl.spec.auto]/4: the decl-specifier-seq analysed next may have a
       placeholder-type-specifier (a variable's or function's declaration);
       decl_specifier_seq clears it */
    bool placeholder_allowed = false;
    /* a declarator's declaration whose type has a placeholder (placeholder),
       or a function's: what [dcl.spec.auto] allows of it */
    analysis_result placeholder_declaration(SyntaxRef declaration,
                                            const declarator_meaning &m,
                                            qualified_type type,
                                            entity_kind kind, bool placeholder,
                                            bool initialized);
    /* [over.load]/3: no function of the name has TYPE's parameters */
    bool other_function_of_placeholder(entity_index prior, entity_index scope,
                                       qualified_type type) const;
    /* [dcl.spec.auto]/1: the placeholder type of auto or decltype(auto),
       of type-constraint C auto when named_concept is C's entity */
    qualified_type placeholder_type(bool decltype_auto,
                                    entity_index named_concept = 0);
    /* whether t is or contains (through pointers, references, arrays and
       a function's return type) a placeholder type, and which (concept:
       its type-constraint's concept, 0 for none) */
    bool contains_placeholder(qualified_type t, bool *decltype_auto = nullptr,
                              entity_index *named_concept = nullptr) const;
    /* [dcl.type.decltype]/1: the type decltype ( expression ) denotes */
    analysis_result decltype_type(SyntaxRef n, qualified_type &type);
    analysis_result clang_type_transformation(ClangTypeTransformation n,
                                              qualified_type &type);
    owned_array<entity_index> type_transformations;
    /* the type decltype(E) denotes for E analysed as m, E unparenthesized
       id-expression or class member access when named; false when the
       entity it names is not known */
    bool decltype_of(const expression_meaning &m, bool named,
                     qualified_type &type);
    /* whether operand (an expression's syntax) is an unparenthesized
       id-expression or class member access */
    bool names_entity(SyntaxElement operand) const;
    /* [dcl.type.auto.deduct]: the type replacing declared (which contains a
       placeholder) for the initializer E analysed as e (named:
       names_entity of it), and what replaced the placeholder in it */
    /* [dcl.type.auto.deduct]/6: C<u> satisfied, of a placeholder whose
       type-constraint names named_concept (0: none) */
    analysis_result placeholder_constraint(SyntaxRef at,
                                           entity_index named_concept,
                                           qualified_type u);
    analysis_result deduce_placeholder(SyntaxRef at, qualified_type declared,
                                       const expression_meaning &e, bool named,
                                       qualified_type &deduced,
                                       qualified_type &replacement);
    /* [dcl.type.auto.deduct]/4: the invented type template parameter U
       (its typedef-name entity, made once) */
    entity_index invented_parameter = 0;
    /* t with its placeholder replaced by by */
    qualified_type replace_placeholder(qualified_type t, qualified_type by);
    /* the function type f with its return type replaced by returns */
    qualified_type function_returning(qualified_type f, qualified_type returns);
    /* a deduced type the analysis does not take (array, function, void) */
    analysis_result deduced_type_supported(SyntaxRef at, qualified_type t);
    /* [dcl.spec.auto]/7: the type the init-declarators of the declaration
       analysed now deduced for the placeholder (none before the first);
       init_declarators and condition save and restore it */
    qualified_type declaration_deduced;
    /* [dcl.spec.auto]/7: the declaration analysed now has a placeholder in
       its decl-specifier-seq and more than one init-declarator */
    bool placeholder_group = false;
    /* the variable whose initializer deduces its type now (saved and
       restored around the deduction) */
    entity_index deducing_variable = 0;
    /* [dcl.spec.auto]/11: a use of e, whose type may have an undeduced
       placeholder */
    analysis_result undeduced_use(token_index t, entity_index e);
    /* [dcl.spec.auto]/8-9: a return statement's operand (or none) in a
       function whose declared return type has a placeholder */
    analysis_result deduce_return(SyntaxRef at, const expression_meaning *e,
                                  bool named);
    /* a function body's analysis begins and ends: its declared return
       type's deduction ([dcl.spec.auto]/3, /9), its return type after */
    void begin_return_deduction(qualified_type returns);
    analysis_result finish_return_deduction(SyntaxRef definition,
                                            analysis_result result,
                                            qualified_type &returns);
    /* [dcl.fct.spec]/3: explicit is analysed on a conversion function's
       decl-specifier-seq only */
    bool explicit_allowed = false;
    /* clang's enable_if accepted: in a constructor template's declarator
       (constructor_template_declarator) */
    bool enable_if_allowed = false;
    /* construct() is called for a base class subobject, which may be of an
       abstract class ([class.abstract]/2) */
    bool constructing_base = false;
    /* temporaries being made for a reference binding (bounded) */
    uint32_t converting_binds = 0;
    bool virt_override = false, virt_final = false, virt_pure = false;
    /* the virtual functions declared, with their class, in order: a
       class's are its new slots at its closing } (dynamic_class_record) */
    struct declared_virtual {
        entity_index c = 0, f = 0;
        /* the slot of a base's virtual table it overrides, or ~0u */
        uint32_t overrides = ~0u;
        SyntaxRef declaration = 0;
    };
    owned_array<declared_virtual> declared_virtuals;
    analysis_result dynamic_class_record(entity_index c, SyntaxRef n);
    bool primary_base_choice(entity_index c, entity_index &primary) const;
    entity_index virtual_base_overrider(entity_index c, entity_index v,
                                        const vtable_group &bg, uint32_t k,
                                        bool &ambiguous) const;
    analysis_result virtual_member(entity_index c, entity_index e,
                                   SyntaxRef declaration, token_index id,
                                   bool virtual_specified);
    uint32_t vtable_pointer_store(entity_index c, token_index at);
    /* whether the syntax is a virt-specifier or virt-specifier-seq
       ([class.mem]/1), and which */
    bool virt_specifier(SyntaxElement d, bool &override_, bool &final_) const;
    analysis_result parameter_declaration(ParameterDeclaration n,
                                          qualified_type &type, bool &unnamed,
                                          token_index &id,
                                          SyntaxElement &default_argument);
    /* cv-qualifiers added to a type: to an array's elements
       ([basic.type.qualifier]/3), none to a reference or a function type
       ([dcl.ref]/1, [dcl.fct]/7) */
    qualified_type add_cv(qualified_type t, uint32_t cv);
    analysis_result pointer_qualifier(SyntaxRef op, token_index t,
                                      qualified_type &type);

    analysis_result init_declarator(SyntaxRef declaration, SyntaxElement item,
                                    const specified &s, bool definition);

    /* [basic.link], [basic.scope.declarative]: the name of a declarator
       declared at global namespace scope, or redeclared; the entity */
    analysis_result declare(SyntaxRef declaration, token_index id,
                            entity_kind kind, qualified_type type,
                            const specified &s, bool definition,
                            bool initialized, entity_index &declared_entity);

    /* ---- the semantic tree (sema/semantic-tree.cpp) ---- */
    /* a node for the expression m is now */
    uint32_t expression_node(semantic_kind kind, const expression_meaning &m,
                             token_index at, uint32_t a = 0, uint32_t b = 0,
                             uint8_t op = 0);
    /* m's node, converted to what m now is */
    void converted(expression_meaning &m, conversion_kind kind);
    uint32_t statement_node(semantic_kind kind, token_index at, uint32_t a = 0,
                            uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
    token_index first_token(SyntaxElement c) const;

    /* ---- expressions ([expr]) ---- */
    /* [expr.pre]: an expression's type, value category and value; the
       expression is recorded in expressions */
    analysis_result expression(SyntaxElement e, expression_meaning &m);
    analysis_result expression_kind(SyntaxElement e, expression_meaning &m);
    /* [expr.prim.literal], [lex.literal] */
    analysis_result literal(token_index t, expression_meaning &m);
    /* [lex.string]: the string literal token t (its suffix, if any, not
       counted), false if its element type is none analysis has */
    bool string_literal_meaning(token_index t, expression_meaning &m);
    /* a constant prvalue of the fundamental type with the value, at the
       token at, with its node */
    expression_meaning integer_constant(fundamental_type type, uint64_t value,
                                        token_index at);
    /* [ext.gnu] an imaginary constant: a numeric literal suffixed i or j */
    bool imaginary_literal(token_index t) const;
    /* [expr.prim.id.unqual] */
    analysis_result id_expression(token_index t, expression_meaning &m);
    /* [expr.prim.id.qual] */
    analysis_result qualified_id(QualifiedId n, expression_meaning &m);
    analysis_result qualified_template_id(SyntaxRef n, expression_meaning &m);
    analysis_result class_member_value(SyntaxRef n, token_index t,
                                       entity_index e, expression_meaning &m);
    /* [expr.prim.id]: the expression that names the entity e, found by
       lookup of the identifier t */
    analysis_result named_entity(token_index t, entity_index e,
                                 expression_meaning &m, bool record_use = true);
    /* [expr.prim.this] */
    analysis_result function_this(token_index t, expression_meaning &m);
    analysis_result this_pointer(token_index t, expression_meaning &m);
    /* *this, an lvalue ([expr.unary.op]/1); own: the function's own this
       (function_this), not a lambda's captured one */
    analysis_result this_object(token_index t, expression_meaning &m,
                                bool own = false);

    /* ---- lambda expressions ([expr.prim.lambda], lambda-*.cpp,
       closure-type.cpp) */
    enum class capture_default : uint8_t { none, copy, reference };
    /* an entity a lambda captures ([expr.prim.lambda.capture]): a
       variable (0: this), by reference or not (*this: star_this), the
       closure's member for it, and an init-capture's initialization (its
       variable then) */
    struct capture_record {
        entity_index variable = 0, member = 0;
        bool by_reference = false, star_this = false, init_capture = false;
        token_index at = 0;
        uint32_t initialization = 0;
    };
    /* a lambda-expression whose compound-statement is analysed: its closure
       type, function call operator, the function of the context it is in,
       its capture-default, whether it is mutable, the type of this in that
       context (none if it has no this) and its captures,
       lambda_captures[first_capture ..] */
    struct lambda_context {
        entity_index closure = 0, call = 0, enclosing_function = 0;
        capture_default default_capture = capture_default::none;
        bool mutable_ = false, constexpr_ = false;
        qualified_type enclosing_this;
        SyntaxRef at = 0;
        uint32_t first_capture = 0;
    };
    owned_array<lambda_context> lambdas;
    owned_array<capture_record> lambda_captures;
    /* the lambda whose compound-statement is analysed now (its body, not a
       local class's member function in it), nullptr if none */
    lambda_context *current_lambda();
    analysis_result lambda_expression(LambdaExpression n,
                                      expression_meaning &m);
    analysis_result
    lambda_call_type(SyntaxRef n, SyntaxRef d, lambda_context &l,
                     qualified_type &type,
                     owned_array<parameter_meaning> &parameters);
    analysis_result lambda_body(SyntaxRef n, SyntaxElement body,
                                lambda_context &l,
                                owned_array<parameter_meaning> &parameters,
                                owned_array<capture_record> &captures);
    analysis_result generic_lambda(SyntaxRef n, SyntaxRef list,
                                   SyntaxRef declarator, lambda_context &l,
                                   bool has_capture, expression_meaning &m);
    analysis_result lambda_specialization(SyntaxRef n, bool body,
                                          qualified_type &type,
                                          entity_index specialization);
    analysis_result lambda_introducer(LambdaIntroducer n, lambda_context &l);
    analysis_result lambda_captures_of(SyntaxRef n,
                                       owned_array<SyntaxElement> &out);
    analysis_result init_capture(InitCapture n, lambda_context &l,
                                 capture_record &c);
    /* the current lambda's init-captures' variables, bound in its
       function parameter scope */
    analysis_result bind_init_captures(entity_index scope);
    /* [expr.prim.lambda.capture]/10, /12: the type of the member that
       captures an entity of type t */
    analysis_result capture_type(SyntaxRef at, qualified_type t,
                                 bool by_reference, qualified_type &type);
    /* the type of local entity e where a lambda-expression is: an
       enclosing lambda's member when that lambda captures it by copy */
    qualified_type enclosing_capture_type(entity_index e, size_t count,
                                          size_t end) const;
    /* class c's members one range of class_members again
       (class-specifier.cpp) */
    void make_members_contiguous(entity_index c);
    /* the closure's non-static data member of type type for a capture */
    entity_index capture_member_of(entity_index closure, qualified_type type);
    /* the entity e of an enclosing function named at t in the body: its
       capture's member ([expr.prim.lambda.capture]/11) */
    analysis_result captured_entity(token_index t, entity_index e,
                                    expression_meaning &m);
    /* this in the body: the captured this ([expr.prim.lambda.capture]/12) */
    analysis_result captured_this(token_index t, expression_meaning &m);
    /* member of the closure object, through the call operator's this */
    analysis_result closure_member(token_index t, entity_index member,
                                   expression_meaning &m);
    /* the closure object: each capture's member initialized in the
       lambda-expression's context ([expr.prim.lambda.capture]/15) */
    analysis_result closure_object(lambda_context &l,
                                   const owned_array<capture_record> &captures,
                                   expression_meaning &m);
    /* the closure type c complete after the body: its layout, special
       members and, for a lambda without a lambda-capture, the conversion
       function */
    analysis_result closure_class(entity_index c, SyntaxRef n,
                                  bool has_capture);
    /* [expr.prim.lambda.closure]/7: the conversion function to pointer to
       function of a lambda without captures, and the static member
       function it returns */
    /* [expr.call]/14: a call's type and category from its result type */
    void call_result(qualified_type result, expression_meaning &m) const;
    analysis_result immediate_invocation(SyntaxRef n, expression_meaning &m);
    analysis_result closure_conversion(entity_index c, entity_index call,
                                       SyntaxRef n);
    /* [expr.prim.paren] */
    analysis_result parenthesized(SyntaxRef n, expression_meaning &m);
    /* [expr.unary] */
    analysis_result unary(SyntaxRef n, expression_meaning &m);
    analysis_result size_of(SyntaxRef n, bool alignment, expression_meaning &m);
    analysis_result increment(SyntaxRef n, SyntaxElement operand,
                              bool decrement, bool postfix,
                              expression_meaning &m);
    /* ... of the operand analysed as m (syntax operand, if it has one),
       the operator at the token at */
    analysis_result increment_of(SyntaxRef n, token_index at,
                                 SyntaxElement operand, bool decrement,
                                 bool postfix, expression_meaning &m);
    /* [expr.unary.op]/1, [over.unary]: *m, as increment_of */
    analysis_result indirection_of(SyntaxRef n, token_index at,
                                   SyntaxElement operand,
                                   expression_meaning &m);
    /* [expr.post] */
    analysis_result postfix(SyntaxRef n, expression_meaning &m);
    /* [expr.ref]: E1.E2 and E1->E2 */
    analysis_result member_reference(SyntaxRef n, expression_meaning &m);
    /* [expr.cast], [expr.type.conv] */
    analysis_result cast(SyntaxRef n, expression_meaning &m);
    /* the conversion of m to to by the casts in kinds (cast_kinds: the
       cast notation's, [expr.cast]/4, or one named cast's) */
    analysis_result explicit_conversion(SyntaxRef at, qualified_type to,
                                        expression_meaning &m,
                                        uint8_t kinds = cast_notation);
    /* ill-formed: no cast in kinds performs the conversion */
    analysis_result no_cast(SyntaxRef at, uint8_t kinds);
    /* explicit_conversion's pointer to pointer conversions */
    analysis_result pointer_conversion(SyntaxRef at, qualified_type to,
                                       expression_meaning &m, uint8_t kinds);
    /* ... to a reference: the glvalue m through a pointer to it */
    analysis_result reference_cast(SyntaxRef at, qualified_type to,
                                   expression_meaning &m, uint8_t kinds);
    /* ... to a pointer to member ([expr.static.cast]/12) */
    analysis_result member_pointer_cast(SyntaxRef at, qualified_type to,
                                        expression_meaning &m, uint8_t kinds);
    /* ... between pointers to classes related by derivation */
    analysis_result class_pointer_cast(SyntaxRef at, qualified_type to,
                                       expression_meaning &m, uint8_t kinds,
                                       bool &handled);
    /* [expr.mul] to [expr.log.or] */
    analysis_result binary(SyntaxRef n, expression_meaning &m);
    /* E1 op E2 of the operands analysed (their syntax, none for operands
       [stmt.ranged]/1 makes): an operator function ([over.binary]) or the
       built-in operator */
    analysis_result binary_of(SyntaxRef n, token_index at,
                              preprocessing_op_or_punc op,
                              SyntaxElement left_syntax,
                              expression_meaning &left,
                              SyntaxElement right_syntax,
                              expression_meaning &right, expression_meaning &m);
    analysis_result builtin_binary(SyntaxRef n, token_index at,
                                   preprocessing_op_or_punc op,
                                   expression_meaning &left,
                                   expression_meaning &right,
                                   expression_meaning &m);
    analysis_result binary_built(analysis_result r, token_index at,
                                 preprocessing_op_or_punc op,
                                 const expression_meaning &left,
                                 const expression_meaning &right,
                                 expression_meaning &m);
    analysis_result arithmetic(SyntaxRef n, preprocessing_op_or_punc op,
                               expression_meaning &left,
                               expression_meaning &right,
                               expression_meaning &m);
    bool class_operand(const expression_meaning &m) const;
    analysis_result comparison(SyntaxRef n, preprocessing_op_or_punc op,
                               expression_meaning &left,
                               expression_meaning &right,
                               expression_meaning &m);
    /* [expr.spaceship]: the built-in three-way comparison of operands
       that are prvalues */
    analysis_result three_way_comparison(SyntaxRef n, expression_meaning &left,
                                         expression_meaning &right,
                                         expression_meaning &m);
    /* [expr.type]/3: the composite pointer type of two operands, one a
       pointer and the other a pointer or a null pointer constant, both
       converted to it; unsupported where analysis has no rule */
    analysis_result composite_pointer_type(SyntaxRef at, expression_meaning &a,
                                           expression_meaning &b);
    /* [expr.cond] */
    analysis_result conditional(SyntaxRef n, expression_meaning &m);
    analysis_result noexcept_operator(SyntaxRef n, expression_meaning &m);
    analysis_result clang_type_trait(SyntaxRef n, expression_meaning &m);
    /* [meta.rel] is_convertible; known false when analysis cannot tell */
    bool copy_constructible_from(entity_index c, value_category category,
                                 bool &known);
    bool convertible_types(qualified_type from, qualified_type to, bool &known);
    /* [meta.unary.prop] is_constructible and is_nothrow_constructible
       (ext/clang/constructible-traits.cpp): the analysis of T t(declval<
       Args>()...); */
    analysis_result clang_constructible_trait(SyntaxRef n, bool nothrow,
                                              expression_meaning &m);
    analysis_result constructible_types(SyntaxRef n, qualified_type t,
                                        const qualified_type *types,
                                        uint32_t count, bool nothrow,
                                        bool &value);
    bool class_destructible(entity_index c, bool &throws);
    bool default_initializer_pending(entity_index c) const;
    bool same_or_derived_class(const expression_meaning &a, entity_index c);
    analysis_result
    aggregate_paren_initialization(SyntaxRef n, entity_index c,
                                   owned_array<expression_meaning> &arguments,
                                   bool nothrow, bool &value);
    analysis_result aggregate_value_initialization(SyntaxRef n, entity_index c,
                                                   bool nothrow, bool &value);
    /* [except.spec]/8-9 of the implicit constructor f; known is false
       when it cannot be told */
    bool implicit_constructor_throws(entity_index f, bool &known);
    bool default_initializer_throws(entity_index e, bool &throws);
    bool subobject_constructor_throws(entity_index k, bool copy, bool &known);
    /* declval<T>() as an expression */
    expression_meaning declval_meaning(qualified_type t);
    /* [ext:clang_type_traits] the answer of the trait of one type-id
       numbered which (ext/clang/unary-traits.inc) for the non-dependent
       t; false if the analysis has none */
    bool unary_trait(int which, qualified_type t, int64_t &value);
    /* ext/clang/class-traits.cpp: the traits that ask about a class */
    bool class_property_trait(int which, qualified_type t, bool &answer);
    bool derives_from(entity_index d, entity_index b) const;
    analysis_result clang_base_of_trait(SyntaxRef n, expression_meaning &m);
    bool polymorphic_class(entity_index c) const;
    bool virtual_destructor(entity_index c) const;
    bool triviality_unknown(entity_index c) const;
    bool class_trivially_copyable(entity_index c, bool &known);
    analysis_result gnu_offsetof(SyntaxRef n, expression_meaning &m);
    analysis_result clang_bit_cast(SyntaxRef n, expression_meaning &m);
    analysis_result gnu_va_arg(SyntaxRef n, expression_meaning &m);
    /* ---- ext/gnu/asm-statement.cpp ---- */
    analysis_result gnu_asm_statement(GnuAsmStatement n, uint32_t &built);
    analysis_result gnu_asm_operand(GnuAsmOperand n, bool output,
                                    uint32_t &built);
    analysis_result clang_convertvector(SyntaxRef n, expression_meaning &m);
    bool builtin_addressof_call(SyntaxRef n);
    /* clang's type-generic bit counts: which one the function e is (1
       popcount, 2 clz, 3 ctz; 0 none), the check of a call's arguments,
       and its value in a constant evaluation */
    int bit_count_builtin(entity_index e) const;
    int query_builtin(entity_index e) const;
    analysis_result query_builtin_result(SyntaxRef n, int which,
                                         const owned_array<uint32_t> &nodes,
                                         qualified_type &result);
    analysis_result bit_count_arguments(SyntaxRef n, entity_index called,
                                        const owned_array<uint32_t> &nodes);
    analysis_result va_list_builtin_result(SyntaxRef n, int which,
                                           const owned_array<uint32_t> &nodes);
    analysis_result overflow_builtin_result(SyntaxRef n, const char *spelling,
                                            size_t length,
                                            const owned_array<uint32_t> &nodes,
                                            qualified_type &result);
    analysis_result atomic_builtin_result(SyntaxRef n, entity_index called,
                                          const owned_array<uint32_t> &nodes,
                                          qualified_type &result);
    analysis_result builtin_addressof(SyntaxRef n, expression_meaning &m);
    analysis_result clang_source_location(SyntaxRef n, expression_meaning &m);
    analysis_result clang_source_location_object(SyntaxRef n,
                                                 expression_meaning &m);
    /* a default argument's value is the call's: its source_location nodes
       are made while call_site_depth is up, and counted; a default
       argument that made some is wrapped (bind_call_site makes each call's
       copy).  source_location_at: the place they are bound to at once (a
       default member initializer: the constructor) */
    unsigned call_site_depth = 0;
    uint32_t call_site_nodes = 0;
    token_index source_location_at = 0;
    uint32_t wrap_call_site(uint32_t node, token_index at,
                            uint32_t nodes_before);
    uint32_t bind_call_site(uint32_t node, token_index call);
    analysis_result clang_atomic_type(SyntaxRef n, qualified_type &type);
    uint64_t atomic_layout(qualified_type value, bool alignment) const;
    analysis_result clang_bit_int_type(SyntaxRef n, qualified_type &type);
    /* [ext:gnu_typeof] the type typeof ( expression or type-id ) denotes */
    analysis_result gnu_typeof_type(SyntaxRef n, qualified_type &type);
    analysis_result clang_ext_vector_count(SyntaxRef at, SyntaxElement count,
                                           gnu_attributes &g);
    analysis_result clang_ext_vector_type(SyntaxRef at, const gnu_attributes &g,
                                          qualified_type &type);
    uint64_t ext_vector_layout(const type &record, bool alignment) const;
    uint64_t bit_int_layout(uint64_t width, bool alignment) const;
    entity_index value_parameter_named(SyntaxElement e);
    analysis_result potentially_throwing(SyntaxRef n, uint32_t x,
                                         bool &throwing);
    analysis_result conditional_operands(SyntaxRef n,
                                         const expression_meaning &c,
                                         expression_meaning e2,
                                         expression_meaning e3,
                                         expression_meaning &m);
    /* [expr.ass] */
    analysis_result assignment(SyntaxRef n, expression_meaning &m);
    analysis_result
    assignment_of(SyntaxRef n, token_index at, preprocessing_op_or_punc op,
                  SyntaxElement left_syntax, SyntaxElement right_syntax,
                  expression_meaning &left, expression_meaning &right,
                  expression_meaning &m);
    /* [meta.unary.prop] is_assignable, is_nothrow_assignable,
       is_trivially_assignable (ext/clang/assignable-traits.cpp) */
    analysis_result clang_assignable_trait(SyntaxRef n, int which,
                                           expression_meaning &m);
    /* [expr.ass]/4: = of a class */
    analysis_result class_assignment(SyntaxRef n, preprocessing_op_or_punc op,
                                     expression_meaning &left,
                                     expression_meaning &right,
                                     expression_meaning &m);
    /* [expr.static.cast], [expr.const.cast], [expr.reinterpret.cast] */
    /* [expr.typeid] */
    analysis_result type_identification(SyntaxRef n, expression_meaning &m);
    analysis_result named_cast(SyntaxRef n, standard_keyword keyword,
                               expression_meaning &m);
    /* [expr.comma] */
    analysis_result comma(SyntaxRef n, expression_meaning &m);
    /* a type-id ([dcl.name]) */
    analysis_result type_id(TypeId n, qualified_type &type,
                            uint32_t *typedef_alignment = nullptr);
    analysis_result alignment_specifier(AlignmentSpecifier n,
                                        uint64_t &alignment);
    SyntaxElement class_head_name(SyntaxRef head) const;

    /* ---- standard conversions ([conv]) ---- */
    /* the fundamental type of t, none if t is no fundamental type */
    fundamental_type fundamental_of(qualified_type t) const;
    bool is_integral(qualified_type t) const;
    bool is_floating(qualified_type t) const;
    bool is_arithmetic(qualified_type t) const;
    bool is_pointer(qualified_type t) const;
    bool is_object_pointer(qualified_type t) const;
    /* [conv.ptr]/1, [conv.mem]/1: the null pointer constant m to the null
       (member) pointer value of type to, by conversion kind */
    void null_pointer_constant_to(expression_meaning &m, qualified_type to,
                                  conversion_kind kind);
    /* [dcl.mptr]: a pointer to member; to a member function */
    bool is_member_pointer(qualified_type t) const;
    bool is_member_function_pointer(qualified_type t) const;
    /* [conv.mem]: m, a null pointer constant or a pointer to member, to
       the pointer-to-member type to (handled: to is one) */
    analysis_result member_pointer_conversion(SyntaxRef at,
                                              expression_meaning &m,
                                              qualified_type to, bool &handled);
    /* [conv.mem]/2 (to_base false), [expr.static.cast]/12 (to_base): the
       pointer to member m between class derived and its base on their
       non-virtual path, to type type; access checked if access */
    analysis_result
    member_pointer_along_path(token_index at, entity_index derived,
                              entity_index base, qualified_type type,
                              bool to_base, bool access, expression_meaning &m);
    /* [expr.mptr.oper]: E1.*E2 (arrow false) and E1->*E2, the operands
       analysed */
    analysis_result member_pointer_operator(SyntaxRef n, token_index at,
                                            bool arrow,
                                            expression_meaning &left,
                                            expression_meaning &right,
                                            expression_meaning &m);
    /* [basic.lval]/6: the lvalue-to-rvalue, array-to-pointer and
       function-to-pointer conversions of an operand that is to be a
       prvalue */
    analysis_result operand_value(SyntaxRef at, expression_meaning &m);
    /* [conv.prom] */
    void promote(expression_meaning &m);
    /* the type the integral prvalue m promotes to (its own type when no
       integral promotion applies, [conv.prom]/5 for a bit-field), and the
       type an integral type promotes to */
    fundamental_type integral_promotion(const expression_meaning &m) const;
    fundamental_type promotion_of(fundamental_type t) const;
    /* the enumeration t, 0 if t is no complete enumeration */
    entity_index enumeration_of(qualified_type t) const;
    /* a prvalue of enumeration type as its underlying type
       ([dcl.enum]/8: "the values of the enumeration are the values of the
       underlying type"): false if it has no enumeration type */
    bool enumeration_as_underlying(expression_meaning &m);
    /* [conv.prom]/4-5: a prvalue of unscoped enumeration type promoted
       to an integer type; false if it has no unscoped enumeration type */
    bool promote_enumeration(expression_meaning &m);
    /* the operands of a binary operator that takes arithmetic or
       unscoped enumeration operands: both promoted, or, both of one
       scoped enumeration type and comparison, both as its underlying
       type ([expr.arith.conv]/1.1) */
    void enumeration_operands(expression_meaning &a, expression_meaning &b,
                              bool comparison);
    /* a prvalue converted to the arithmetic type to, its value with it */
    void convert_arithmetic(expression_meaning &m, fundamental_type to);
    /* ... its type and value, without the node */
    void arithmetic_value(expression_meaning &m, fundamental_type to);
    /* [expr.arith.conv]: the operands' common type, both converted */
    fundamental_type usual_arithmetic_conversions(expression_meaning &a,
                                                  expression_meaning &b);
    /* [conv.qual]/2 */
    bool similar(qualified_type a, qualified_type b) const;
    /* [conv.qual]/3: whether a prvalue of type from converts to type to by
       a qualification conversion */
    void member_pointer_qualification(expression_meaning &m, qualified_type to);
    bool same_pointer_level(const struct type &x, const struct type &y) const;
    bool qualification_convertible(qualified_type from,
                                   qualified_type to) const;
    /* [conv.fctptr]/1: whether a prvalue of type from (pointer to noexcept
       function) converts to type to (pointer to function) */
    bool function_pointer_convertible(qualified_type from,
                                      qualified_type to) const;
    /* whether the function types a and b differ in their exception
       specification only */
    bool differ_in_exceptions(qualified_type a, qualified_type b) const;
    /* [class.temporary]/4: whether m is a prvalue of a class with a
       destructor, whose temporary a discarded-value expression would have
       to destroy (not analysed) */
    bool destroyed_temporary(const expression_meaning &m) const;
    /* [dcl.init]/17.9: the standard conversion sequence of an implicit
       conversion of the prvalue m to to (cv-unqualified); ill-formed
       without one */
    analysis_result implicit_conversion(SyntaxRef at, expression_meaning &m,
                                        qualified_type to, bool direct);
    /* [conv.bool], [expr.context]/1: contextually converted to bool */
    analysis_result contextual_bool(SyntaxRef at, expression_meaning &m);

    /* ---- constant expressions ([expr.const], constant-expression.cpp) ----
       What every construct that requires a constant expression asks,
       after operand_value: whether m is one (constant), with its value in
       m as an expression_meaning keeps it.  Unsupported when analysis
       cannot tell. */
    /* [expr.const]/11 */
    analysis_result constant_expression(SyntaxRef at, expression_meaning &m,
                                        bool &constant);
    /* [expr.const]/8 */
    analysis_result integral_constant_expression(SyntaxRef at,
                                                 expression_meaning &m,
                                                 bool &constant);
    /* [expr.const]/10, of the integer type to: m converted to it */
    analysis_result converted_constant_expression(SyntaxRef at,
                                                  expression_meaning &m,
                                                  fundamental_type to,
                                                  bool &constant);
    /* [expr.const]/10: a contextually converted constant expression of
       type bool, from the expression e */
    analysis_result bool_constant_expression(SyntaxRef at, SyntaxElement e,
                                             bool &constant, bool &value);
    /* [except.spec]/2: whether the noexcept-specifier n makes the exception
       specification non-throwing */
    analysis_result noexcept_specifier(NoexceptSpecifier n, bool member,
                                       bool &non_throwing);
    /* ... with the function_parameters in scope ([basic.scope.param]) */
    void open_parameter_scope();
    analysis_result parameter_scope_noexcept(SyntaxRef n, bool member,
                                             bool &non_throwing);
    /* the noexcept-specifiers left value-dependent in templates' own
       declarations */
    owned_array<SyntaxRef> dependent_exception_specifications;
    bool dependent_exception_specification(entity_index t) const;
    /* [expr.const]/5: the evaluation of the semantic tree node as a core
       constant expression (expression/constant-evaluation.cpp): well_formed
       with its value in v and constant true; constant false if it is not one;
       unsupported where the evaluator has no rules yet */
    analysis_result evaluate_constant(SyntaxRef at, uint32_t node,
                                      constant_value &v, bool &constant);
    /* one evaluation's state: the objects of the evaluation (locals and
       parameters of the constexpr functions it calls, [expr.const]/5.8:
       their lifetime began within it), the depth of invocations and the
       full-expressions evaluated, against [implimits] */
    struct constant_evaluation {
        /* the elements of the arrays in objects (an element's value may be
           an array whose elements are further on), and whether each may
           not be modified: an element of a variable whose lifetime did not
           begin within the evaluation ([expr.const]/5.16) */
        owned_array<constant_value> elements;
        owned_array<uint8_t> element_read_only;
        struct object {
            entity_index entity = 0;
            /* frame 0: a variable of static storage duration read */
            uint32_t frame = 0;
            bool read_only = false;
            /* a parameter whose value the check of a body for a potential
               constant expression does not know: a read of it is unknown */
            bool opaque = false;
            constant_value value;
        };
        owned_array<object> objects;
        uint32_t depth = 0;
        uint32_t full_expressions = 0;
        /* the frame of the invocation being evaluated (0: none) and the
           frames numbered so far */
        uint32_t frame = 0, frames = 0;
        /* a break or continue statement being executed ([stmt.jump]) */
        uint8_t jump = 0;
        /* a switch statement's jump to this case or default label
           ([stmt.switch]/5): the statements before it are passed over */
        uint32_t seek = 0;
        /* potential_constant's evaluation: an unknown operand does not stop
           the evaluation of the operands beside it, nor an unknown
           condition that of both arms of a conditional */
        bool potential = false;
        /* evaluate_analysed_expressions' evaluation of any expression,
           which need not be manifestly constant-evaluated
           ([expr.const]/14): __builtin_is_constant_evaluated is unknown */
        bool any_expression = false;
        /* for the claims (evaluate_analysed_expressions): a function whose
           body begins after this token is not defined yet where the claim
           is placed ([expr.const]/5.3); 0: every body counts */
        token_index defined_before = 0;
    };
    /* The evaluator's parts (expression/constant-evaluation.cpp).  Each answers
       constant (with the value), not_constant (not a core constant
       expression), or unknown where the evaluator has no rules (analysis's
       unsupported [expr.const]).  evaluate_object: the object a glvalue
       designates among state.objects; evaluate_read: the value a glvalue's
       lvalue-to-rvalue conversion reads; evaluate_statement: returned is set
       by a return statement, with its value in result */
    constness evaluate_node(constant_evaluation &state, uint32_t node,
                            constant_value &v);
    constness evaluate_statement(constant_evaluation &state, uint32_t node,
                                 bool &returned, constant_value &result);
    constness evaluate_query_builtin(constant_evaluation &state, int which,
                                     const semantic_node &call,
                                     constant_value &v);
    constness evaluate_bit_count(constant_evaluation &state, int which,
                                 const semantic_node &call, constant_value &v);
    constness evaluate_call(constant_evaluation &state, uint32_t node,
                            constant_value &v);
    constness evaluate_object(constant_evaluation &state, uint32_t node,
                              uint32_t &object);
    constness evaluate_read(constant_evaluation &state, uint32_t node,
                            constant_value &v);
    /* [dcl.init.ref]: a reference bound to the glvalue node: the pointer
       to its object */
    constness evaluate_binding(constant_evaluation &state, uint32_t node,
                               constant_value &v);
    /* [conv.array]: the pointer to the first element of the array glvalue */
    constness evaluate_array_pointer(constant_evaluation &state, uint32_t node,
                                     constant_value &v);
    /* [expr.add]/4: p moved by n elements */
    constness offset_pointer(const constant_value &p, __int128 n,
                             constant_value &moved) const;
    /* [expr.unary.op]/1: the place of the object p points to */
    constness pointed_place(const constant_evaluation &state,
                            const constant_value &p, uint32_t &place) const;
    /* [expr.add], [expr.rel], [expr.eq]: a binary operator with a pointer
       operand */
    constness evaluate_pointer_binary(constant_evaluation &state, uint32_t node,
                                      constant_value &v);
    constness evaluate_condition(constant_evaluation &state, uint32_t node,
                                 constant_value &v);
    /* the object of type t at place initialized by init (0: none, its
       value indeterminate), arrays and class objects with their elements
       in state.elements.  The evaluator's own values: a pointer is
       kind pointer with the place it points to in first (count 0: a null
       pointer); a class object is kind class_object, designator its
       class, its non-static data members' values in elements[first ..
       first + count) */
    constness evaluate_initialization(constant_evaluation &state,
                                      qualified_type t, uint32_t init,
                                      bool read_only, uint32_t place);
    /* the class of t when the evaluator models its objects: complete, no
       base classes, not a union (0: not) */
    entity_index evaluated_class(qualified_type t) const;
    /* a copy of v whose elements (an array's, a class object's) are new */
    constant_value copy_value(constant_evaluation &state,
                              const constant_value &v, bool read_only);
    /* [class.ctor]: the constructor call node initializes the object at
       place */
    constness evaluate_construct(constant_evaluation &state, uint32_t node,
                                 uint32_t place);
    /* the invocation of function f (a pointer to the object for a
       non-static member function's this, or kind none), the arguments
       list[first .. first + count) */
    void analyse_postponed_body(entity_index f);
    constness evaluate_invocation(constant_evaluation &state, entity_index f,
                                  const constant_value &self, uint32_t first,
                                  uint32_t count, bool &returned,
                                  constant_value &result);
    /* [dcl.constexpr]/10 for a constexpr variable of class type: whether
       its initialization init is a constant expression */
    constness evaluate_variable(qualified_type t, uint32_t init, token_index at,
                                uint32_t &value);
    /* the tree of constant and initializer_list nodes for an evaluated
       object of type t, which lowering emits as data (0: a value it cannot
       be, an indeterminate member, a pointer) */
    uint32_t value_tree(const constant_evaluation &state, qualified_type t,
                        const constant_value &v, token_index at);
    /* the case or default label of switch body a jump with the kept value
       goes to, or 0 ([stmt.switch]/5) */
    uint32_t switch_target(uint32_t body, uint64_t value, bool &found) const;
    /* [dcl.constexpr]/6: not_constant when no invocation of the
       constexpr function whose body is body can be a constant expression,
       whatever its arguments: the body evaluated with the parameters'
       values unknown fails before it depends on them */
    constness potential_constant(uint32_t body);
    /* an object's place: objects[place], or elements[place & ~element_place]
       when element_place is set */
    static constexpr uint32_t element_place = 0x80000000u;
    constant_value &place_value(constant_evaluation &state, uint32_t place);
    bool place_read_only(const constant_evaluation &state,
                         uint32_t place) const;
    /* the index in state.objects of the entity's object in the current
       frame, or no_frame_object */
    static constexpr uint32_t no_frame_object = UINT32_MAX;
    uint32_t frame_object(const constant_evaluation &state,
                          entity_index e) const;
    /* the arithmetic type the evaluator computes in for t (none: one it has
       no rules for); a conversion and a binary operator on values */
    fundamental_type evaluated_type(qualified_type t) const;
    constness evaluate_conversion(constant_value &v, fundamental_type from,
                                  fundamental_type to);
    constness evaluate_binary(uint8_t op, fundamental_type t,
                              fundamental_type right, fundamental_type result,
                              const constant_value &x, const constant_value &y,
                              constant_value &out);
    constness
    evaluate_wide_binary(uint8_t op, fundamental_type t, fundamental_type right,
                         fundamental_type result, const constant_value &x,
                         const constant_value &y, constant_value &out);
    /* a value of the arithmetic type t as expression_meaning keeps it, and
       back */
    uint64_t kept_of(fundamental_type t, const constant_value &v) const;
    constant_value value_of(fundamental_type t, uint64_t kept) const;
    /* m's value as expression_meaning keeps it, from the evaluator's */
    void evaluated_meaning(expression_meaning &m, const constant_value &v);
    /* whether m's 64 bits hold v of type t (a 128-bit integer may not) */
    bool value_kept_exactly(qualified_type t, const constant_value &v) const;

    /* ---- initialization ([dcl.init]) ---- */
    struct initialized_value {
        constness evaluation = constness::unknown;
        uint64_t value = 0;
        /* the initialization in the semantic tree */
        uint32_t node = 0;
    };
    /* a variable declared with a placeholder type: its initializer
       analysed, the type deduced, the variable's type made it */
    /* the assignment-expression of an initializer "= E", "( E )" or
       "{ E }" (direct: not the first; list: the last); false for another
       form */
    bool initializer_form(SyntaxElement init, SyntaxElement &clause,
                          bool &direct, bool &list) const;
    analysis_result deduced_initializer(SyntaxElement init, entity_index e,
                                        qualified_type &type,
                                        initialized_value &value);
    analysis_result deduced_initialization(SyntaxRef n, expression_meaning &m,
                                           bool named, bool direct, bool list,
                                           entity_index e, qualified_type &type,
                                           initialized_value &value);
    /* the object or reference of type type (an array of unknown bound gets
       its bound) initialized by the initializer node (a
       brace-or-equal-initializer, initializer or braced-init-list) */
    analysis_result initializer(SyntaxElement init, qualified_type &type,
                                initialized_value &result);
    analysis_result initialize_from(SyntaxRef at, SyntaxElement clause,
                                    qualified_type &type, bool direct,
                                    bool list, initialized_value &result);
    analysis_result list_initialization(SyntaxRef list, qualified_type &type,
                                        bool direct, initialized_value &result);
    /* the initializer-clauses of the braced-init-list list; with packs, a
       pack expansion is its pattern once per element, packs[i] the element
       of clauses[i] (no_expansion for another clause), else it is not
       supported */
    analysis_result list_clauses(SyntaxRef list,
                                 owned_array<SyntaxElement> &clauses,
                                 owned_array<uint32_t> *packs = nullptr);
    bool initializer_names_nothing(SyntaxRef init) const;
    analysis_result gather_clauses(SyntaxRef list, SyntaxRef node,
                                   owned_array<SyntaxElement> &clauses,
                                   owned_array<uint32_t> *packs);
    analysis_result list_initialization_of(
        SyntaxRef list, const owned_array<SyntaxElement> &clauses,
        qualified_type &type, bool direct, initialized_value &result);
    /* [dcl.init.list]/3 for a class: an aggregate's elements, each from
       its clause or from an empty list */
    analysis_result class_list_initialization(
        SyntaxRef list, const owned_array<SyntaxElement> &clauses,
        qualified_type type, bool direct, initialized_value &result);
    analysis_result constructor_list_initialization(
        SyntaxRef list, const owned_array<SyntaxElement> &clauses,
        qualified_type type, bool direct, initialized_value &result);
    analysis_result too_many_clauses(SyntaxRef list,
                                     const owned_array<SyntaxElement> &clauses,
                                     size_t next);
    analysis_result
    aggregate_elements(SyntaxRef list,
                       const owned_array<SyntaxElement> &clauses, size_t &next,
                       qualified_type type, initialized_value &result);
    analysis_result aggregate_element(SyntaxRef list,
                                      const owned_array<SyntaxElement> &clauses,
                                      size_t &next, qualified_type element,
                                      initialized_value &one);
    analysis_result aggregate_element_clause(
        SyntaxRef list, const owned_array<SyntaxElement> &clauses, size_t &next,
        qualified_type element, initialized_value &one, SyntaxElement clause,
        SyntaxRef at);
    analysis_result string_initialization(SyntaxRef at,
                                          const expression_meaning &string,
                                          qualified_type &type);
    analysis_result converted_temporary_binding(SyntaxRef at,
                                                expression_meaning &m,
                                                qualified_type reference);
    analysis_result class_reference_binding(SyntaxRef at, expression_meaning &m,
                                            qualified_type reference);
    analysis_result reference_binding(SyntaxRef at, expression_meaning &m,
                                      qualified_type reference);
    /* [dcl.init.list]/7: whether converting m to to narrows */
    analysis_result narrowing(SyntaxRef at, const expression_meaning &m,
                              qualified_type to, bool &narrows);

    /* ---- classes ([class]) ---- */
    /* [class.pre]: a class-specifier, the class's type */
    analysis_result class_specifier(ClassSpecifier n, type_index &type);
    analysis_result class_definition(SyntaxRef n, type_index &type);
    /* "class.nest" or "class.local" for a class declared in scope */
    const char *inner_class_rule(entity_index scope) const;
    const char *local_class_rule(entity_index c) const;
    /* [class.static.data]: the definition of a static data member */
    analysis_result
    static_member_definition(SyntaxRef declaration, token_index id,
                             entity_index qualifier, qualified_type type,
                             const specified &s, bool definition,
                             entity_index &declared_entity);
    /* a type-name, or nested-name-specifier type-name (the node of a
       class-or-decltype, class-head-name ...), that shall name a class:
       the class, a typedef-name's included; the token of its name */
    analysis_result named_class(SyntaxElement name, SyntaxRef at,
                                entity_index &c, token_index &id);
    /* [dcl.type.simple]: nested-name-specifier type-name: the type and,
       for a typedef-name, its cv-qualifiers */
    analysis_result named_member_type(SyntaxRef n, token_index t,
                                      entity_index e, const char *citation,
                                      type_index &type, uint32_t &cv);
    analysis_result qualified_type_name(SyntaxRef n, type_index &type,
                                        uint32_t &cv);
    /* [dcl.enum]: an enum-specifier, the enumeration's type */
    analysis_result enum_specifier(SyntaxRef n, type_index &type);
    analysis_result enum_specifier_rules(SyntaxRef n, type_index &type);
    /* [dcl.type.elab]: enum identifier, the enumeration's type */
    analysis_result elaborated_enum_specifier(ElaboratedEnumSpecifier n,
                                              type_index &type);
    /* [dcl.enum]: an enumerator named by an id-expression */
    analysis_result enumerator_value(token_index t, entity_index e,
                                     expression_meaning &m);
    /* [dcl.type.elab]: class-key identifier, the class's type */
    analysis_result elaborated_type_specifier(ElaboratedTypeSpecifier n,
                                              type_index &type);
    /* [class.mem]: one member-declaration of the class being defined */
    /* [temp.mem]/1: the member template n (a template-declaration) of
       class c */
    analysis_result member_template_declaration(entity_index c,
                                                TemplateDeclaration n,
                                                uint8_t access);
    analysis_result member_function_template(entity_index c, SyntaxRef n,
                                             SyntaxRef definition,
                                             uint8_t access);
    /* a call x.f(args), x->f<T>(args) or x.template f<T>(args) of a
       member function template ([temp.mem]/1): member_reference's object
       and the held name it found, with the template arguments as written,
       for member_template_call to deduce from the arguments */
    entity_index member_template_name = 0;
    expression_meaning member_template_object;
    owned_array<SyntaxElement> member_template_written;
    SyntaxRef member_template_written_at = 0;
    /* the call of the functions and function templates held names
       ([over.call.func]/2, /3): object is the implied object argument
       (none: a static member's context), written the explicit template
       arguments of f<T>(...) */
    analysis_result
    member_template_call(SyntaxRef n, entity_index held,
                         const expression_meaning *object,
                         const owned_array<SyntaxElement> &arguments,
                         owned_array<expression_meaning> &early,
                         const owned_array<SyntaxElement> *written,
                         SyntaxRef written_at, expression_meaning &m);
    analysis_result bound_member_call(
        SyntaxRef n, entity_index f, const expression_meaning &object,
        const owned_array<SyntaxElement> &arguments,
        owned_array<expression_meaning> &early, expression_meaning &m);
    /* [over.match.funcs]/4: a static member function f called, the object
       expression (if any) evaluated and discarded */
    analysis_result static_member_call(
        SyntaxRef n, entity_index f, const expression_meaning *object,
        const owned_array<SyntaxElement> &arguments,
        owned_array<expression_meaning> &early, expression_meaning &m);
    /* [temp.arg.explicit]: the template-arguments of a simple-template-id
       (pack expansions marked in expansion) */
    SyntaxElement member_template_id(SyntaxRef n) const;
    analysis_result member_callee(SyntaxElement callee, entity_index &held,
                                  entity_index &named_in,
                                  owned_array<SyntaxElement> &written,
                                  SyntaxRef &written_at);
    analysis_result written_arguments_of(SyntaxElement id,
                                         owned_array<SyntaxElement> &written,
                                         owned_array<bool> &expansion);
    /* whether this names an object here: the function's own, or the
       enclosing function's of a lambda */
    bool has_this_object();
    uint32_t member_function_template_of(entity_index held) const
    {
        return function_template_of(held, true);
    }
    /* the held name of the member function templates that the class
       member e found by name stands for: e itself when it is that name,
       the templates overloaded with the functions of e's chain
       (entity::member_templates), 0 if none */
    entity_index member_templates_of(entity_index e) const;
    /* [namespace.udecl]/15: the member function templates of the held
       name `from` (a base class's), which a using-declaration brought into
       the class c, join the templates of the name of the class's own
       function head (its held name is made if it has none): copies of
       their records */
    void introduce_member_templates(entity_index c, entity_index head,
                                    entity_index from);
    analysis_result
    function_template_declarator(SyntaxRef definition, uint32_t lead,
                                 entity_index member_of, entity_index scope,
                                 specified &s, declarator_meaning &m,
                                 qualified_type &type);
    analysis_result
    function_template_attributes(SyntaxRef n, const specified &s,
                                 const declarator_meaning &m, uint32_t allowed,
                                 const char *subclause, uint32_t &gnu_flags,
                                 token_index &abi_tag);
    analysis_result member_declaration(entity_index c, MemberDeclaration m,
                                       uint8_t access,
                                       owned_array<SyntaxRef> &bodies);
    analysis_result
    member_declaration_with_specifiers(entity_index c, SyntaxRef node,
                                       uint8_t access,
                                       owned_array<SyntaxRef> &bodies);
    analysis_result member_declarators(entity_index c, SyntaxRef n,
                                       const specified &s, uint8_t access,
                                       uint32_t first,
                                       owned_array<SyntaxRef> &bodies);
    analysis_result member_declarator(entity_index c, SyntaxRef declaration,
                                      SyntaxElement d, const specified &s,
                                      uint8_t access, bool definition);
    /* the body of a member function defined in its class, analysed at the
       end of the class ([class.mem]/6: a complete-class context) */
    analysis_result member_function_body(entity_index c, SyntaxRef definition);
    void instantiated_member_body(entity_index c, SyntaxRef definition);
    analysis_result member_function_definition(entity_index c,
                                               FunctionDefinition definition);
    /* [expr.ref]: the member e of the object m (a glvalue of class type) */
    analysis_result member_access(token_index at, entity_index e,
                                  expression_meaning &m);
    /* [class.mfct.non-static]/2: a name of a non-static member used in a
       member function: (*this).name */
    analysis_result implicit_member_access(token_index at, entity_index e,
                                           expression_meaning &m);
    /* [class.access]: whether a member with this access can be named
       here */
    bool accessible(entity_index member, entity_index object_class = 0) const;
    bool copy_argument_candidate(
        entity_index c, const owned_array<expression_meaning> &arguments) const;
    /* [class.derived]: class c's base class subobject of class base,
       found through c's bases (0 if base is no base of c); ambiguous
       when more than one path leads to it ([class.member.lookup]) */
    entity_index base_step(entity_index c, entity_index base,
                           bool &ambiguous) const;
    entity_index virtual_route(entity_index c, entity_index base) const;
    bool virtual_base_accessible(entity_index c, entity_index v) const;
    bool public_path_to(entity_index c, entity_index v) const;
    analysis_result to_virtual_base(token_index at, entity_index c,
                                    entity_index v, expression_meaning &m);
    /* [class.member.lookup]: name looked up in class c and its bases;
       ambiguous when found in more than one base */
    entity_index class_member_lookup(entity_index c, uint32_t name,
                                     bool &ambiguous) const;
    /* the glvalue m of a class derived from base converted to its base
       class subobject ([expr.ref]/6.2 through [class.derived]/1) */
    analysis_result to_base(token_index at, entity_index base,
                            expression_meaning &m);
    /* the base-clause of class c ([class.derived]) */
    analysis_result base_clause(entity_index c, BaseClause n,
                                standard_keyword key);
    /* [conv.ptr]/3: the pointer m to class from as a pointer to its base
       to, of type type */
    analysis_result pointer_to_base(token_index at, entity_index from,
                                    entity_index to, qualified_type type,
                                    expression_meaning &m);
    /* [conv.ptr]/3: whether the base step of class c is accessible */
    analysis_result base_pointer_access(token_index at, entity_index c,
                                        entity_index step);
    /* [expr.static.cast]/11: the pointer m to class from as a pointer to
       its derived class to, of type type */
    analysis_result pointer_to_derived(token_index at, entity_index from,
                                       entity_index to, qualified_type type,
                                       expression_meaning &m);
    /* the non-virtual bases from class c to its base, outermost first */
    bool nonvirtual_path(entity_index c, entity_index base,
                         owned_array<entity_index> &steps) const;
    analysis_result accessible_path(token_index at, entity_index derived,
                                    entity_index base, bool access,
                                    const char *label,
                                    owned_array<entity_index> &steps);
    /* [expr.cast]/4: whether class c has one base subobject base, on a
       non-virtual path */
    bool single_nonvirtual_path(entity_index c, entity_index base) const;
    /* the pointer m between class derived and its base on a non-virtual
       path, to type, to the base or to the derived class */
    analysis_result pointer_along_path(token_index at, entity_index derived,
                                       entity_index base, qualified_type type,
                                       bool to_derived, bool access,
                                       expression_meaning &m);
    analysis_result defaulted_or_deleted_special_member(
        entity_index c, SyntaxRef n, SyntaxElement d, SyntaxElement body,
        uint8_t access, bool constexpr_specified, bool virtual_specified,
        bool explicit_specified);
    analysis_result defaulted_copy_assignment(entity_index c, SyntaxRef n,
                                              SyntaxElement d,
                                              const specified &s,
                                              uint8_t access);
    analysis_result static_member_initializer(SyntaxRef declarator,
                                              entity_index e,
                                              SyntaxElement init,
                                              const specified &s);
    bool aggregate_value_stored(entity_index c, uint32_t value) const;
    analysis_result default_member_initializer(token_index at, entity_index e,
                                               uint32_t &statement);
    /* the subobject e (a base or a non-static data member) of *this */
    analysis_result subobject(token_index at, entity_index e,
                              expression_meaning &m);
    /* [class.base.init]/13: c's direct bases, then its non-static data
       members, in the order they are constructed */
    void construction_order(entity_index c,
                            owned_array<entity_index> &order) const;
    /* the subobject object's initialization from value: its bytes, stored
       as by an assignment, a statement */
    uint32_t subobject_store(const expression_meaning &object,
                             qualified_type type, token_index at,
                             uint32_t value);
    /* a block of the statements */
    uint32_t complete_object(uint32_t statement);
    uint32_t block_statement(token_index at,
                             const owned_array<uint32_t> &statements);
    /* the classes of the bases and members (arrays' elements) of c */
    void subobject_classes(entity_index c,
                           owned_array<entity_index> &out) const;
    /* [class.default.ctor]/4, [class.dtor]/11: what an implicitly
       defined constructor or destructor of c would do that analysis does
       not; checked when c is complete */
    analysis_result implicit_special_members(entity_index c, SyntaxRef n);
    /* copy: X(const X&); move (with copy): X(X&&) */
    void declare_implicit(entity_index c, special_member kind, bool copy,
                          bool move = false);
    analysis_result implicit_definitions(entity_index c, SyntaxRef n);
    /* [namespace.udecl]/3: the using-declarations of constructors of class
       c's direct bases, kept until its closing } (inherit_constructors) */
    struct inheriting_declaration {
        entity_index c = 0, base = 0;
        token_index at = 0;
    };
    owned_array<inheriting_declaration> inheriting;
    analysis_result inheriting_using(UsingDeclarator n, token_index at,
                                     entity_index base);
    analysis_result inherit_constructors(entity_index c, SyntaxRef n);
    analysis_result inherited_base_construct(token_index at,
                                             const struct inherited_call &call,
                                             entity_index base,
                                             uint32_t &statement);
    /* a function whose definition analysis makes itself (an implicit
       special member, a closure type's __invoke and conversion function):
       its parameter scope, the scope it is in, its parameters (with a
       this_type, the this parameter first) */
    struct synthesized_function {
        entity_index function = 0, scope = 0, outer_scope = 0;
        owned_array<uint32_t> parameters;
    };
    void begin_synthesized(synthesized_function &s, entity_index f,
                           qualified_type returns, qualified_type this_type);
    /* a variable of the parameter scope; a parameter is appended */
    entity_index synthesized_variable(synthesized_function &s, qualified_type t,
                                      bool parameter);
    /* the function-body of block (with a constructor's member
       initializations and a destructor's destructions), the definition
       recorded, the function's state left */
    void finish_synthesized(synthesized_function &s, SyntaxRef declaration,
                            token_index at, uint32_t block,
                            uint32_t members = 0, uint32_t destructions = 0);
    analysis_result implicit_copy(entity_index c, SyntaxRef n,
                                  entity_index source, uint32_t &built,
                                  bool move = false);
    /* [class.dtor]/13: after the body of the destructor of class c, the
       destructors of its members and bases, in the reverse order of
       their construction */
    uint32_t subobject_destructions(entity_index c, token_index at);
    /* [dcl.init]/7: whether the class is const-default-constructible */
    bool const_default_constructible(entity_index c) const;
    /* a class prvalue that is an aggregate's initializer list (T{...})
       or its value-initialization: as an operand that needs an object (a
       member access, a reference binding, a conversion to a base) it is a
       temporary lowering has no rule for yet ([class.temporary]) */
    bool aggregate_prvalue(const expression_meaning &m) const
    {
        return m.category == value_category::prvalue && m.node &&
               (semantics[m.node].kind == semantic_kind::initializer_list ||
                semantics[m.node].kind == semantic_kind::zero);
    }
    /* a lambda's closure prvalue that a reference binds to (initializer.cpp) */
    bool closure_prvalue_binding(const expression_meaning &m) const;
    /* [dcl.init.aggr]/1: whether the class is an aggregate */
    bool is_aggregate(entity_index c) const;
    /* the class whose member function is analysed now, 0 if none */
    entity_index current_class() const;
    /* [dcl.meaning]/1: a member function of qualifier defined outside its
       class */
    analysis_result member_redeclaration(SyntaxRef declaration, token_index id,
                                         entity_index qualifier,
                                         qualified_type type,
                                         const specified &s, bool definition);
    /* [class.ctor], [class.dtor]: a member-declaration without a
       decl-specifier-seq: a constructor or destructor of c */
    analysis_result special_member_declarator(
        entity_index c, SyntaxRef declaration, SyntaxElement d, uint8_t access,
        bool definition, bool constexpr_specified = false,
        bool virtual_specified = false, bool explicit_specified = false,
        bool inline_specified = false);
    /* virtual_specified: in a class, where virtual is one of them */
    bool function_specifiers_only(SyntaxRef seq, bool &constexpr_specified,
                                  bool *virtual_specified = nullptr,
                                  bool *explicit_specified = nullptr,
                                  bool *inline_specified = nullptr) const;
    /* the function-body of a constructor or destructor definition
       (last_declared): ctor-initializer_opt compound-statement */
    analysis_result special_member_body(FunctionDefinition definition,
                                        FunctionBody body);
    /* [dcl.meaning]/1: Class::Class or Class::~Class defined outside the
       class: the function-definition n without a decl-specifier-seq */
    analysis_result special_member_definition(SyntaxRef n, uint32_t at);
    analysis_result anonymous_union_member(entity_index c, SyntaxRef n,
                                           const specified &s, uint8_t access);
    analysis_result bind_anonymous_members(entity_index c, SyntaxRef n,
                                           entity_index u);
    /* whether the declarator d with no decl-specifier-seq declares a
       constructor or destructor of c */
    bool declares_special_member(entity_index c, SyntaxElement d) const;
    /* [over.match.ctor]: the constructor of class c that initializes an
       object from the analysed arguments, the construct node made (its
       object a) */
    analysis_result construct(SyntaxRef at, qualified_type type,
                              owned_array<expression_meaning> &arguments,
                              uint32_t object, initialized_value &result,
                              bool copy = false);
    /* [expr.type.conv]: C(expression-list) or C{} of the class c */
    analysis_result class_conversion(SyntaxRef n, entity_index c,
                                     expression_meaning &m);
    /* ... the prvalue of class c direct-initialized by the arguments */
    analysis_result
    class_direct_initialization(SyntaxRef n, entity_index c,
                                owned_array<expression_meaning> &arguments,
                                expression_meaning &m);
    /* the expressions of an expression-list, analysed */
    analysis_result
    expression_list_meanings(SyntaxElement list,
                             owned_array<expression_meaning> &out);
    /* the class whose member-specification is analysed, 0 if none */
    entity_index defining_class = 0;
    /* [class.access]/6: the class a member's qualified declarator-id
       nominates while its declaration is analysed, 0 otherwise (declarator_access_class) */
    entity_index access_class = 0;
    entity_index declarator_access_class(SyntaxRef declaration,
                                         SyntaxRef specifiers) const;
    /* the tokens of the member-specification of the class being defined
       (class_specifier), 0 outside one */
    token_index defining_members_first = 0, defining_members_end = 0;
    /* [class.mem]/6: whether a name the noexcept-specifier n of a member
       function uses may be declared later in the class, where a complete-
       class context would find it */
    bool noexcept_names_later_member(SyntaxRef n);
    /* [ext:gnu_attributes] the alignment aligned attributes after a
       class-specifier's closing brace ask of the class the
       decl-specifier-seq defines (GCC: they apply to the type), 0 if none;
       class_specifier takes it before the class is laid out */
    uint32_t trailing_class_alignment = 0;
    /* ... and its other attributes (gnu_packed) */
    uint32_t trailing_class_flags = 0;
    /* the access of the member-declaration analysed (an enumeration's and
       its enumerators' in a class) */
    uint8_t declaration_access = 0;
    /* [dcl.typedef] in a class: a member typedef-name */
    analysis_result member_typedef(entity_index c, SyntaxRef declaration,
                                   token_index id, qualified_type type,
                                   const specified &s,
                                   const declarator_meaning &m, uint8_t access);
    analysis_result member_bit_field(entity_index c, SyntaxRef declaration,
                                     SyntaxElement d, const specified &s,
                                     uint8_t access);

    /* the declaration analysed now, for the expressions' records */
    SyntaxRef current_declaration = 0;
    /* the scope analysis is in: 0 the global namespace, else a
       block_scope entity */
    entity_index current_scope = 0;
    /* the entity the last declarator declared */
    entity_index last_declared = 0;
    /* where claims about the expressions analysed now go
       (analysed_expression::place), the scope there, and whether
       declarations analysed now can have claims after them */
    token_index claim_place = 0;
    entity_index claim_scope = 0;
    bool declarations_claimable = true;

    /* ---- statements ([stmt]) ---- */
    /* a point of a function body: the innermost scope there, and a token
       at it (later tokens are later points) */
    struct jump_point {
        entity_index scope = 0;
        token_index at = 0;
    };
    struct label_record {
        uint32_t name = 0;
        jump_point point;
        bool analysed = false;
    };
    struct goto_record {
        uint32_t name = 0;
        jump_point point;
        SyntaxRef node = 0;
    };
    struct switch_record {
        /* the adjusted type of the condition, none if unknown */
        fundamental_type type = fundamental_type::none;
        jump_point point;
        size_t first_value = 0;
        bool has_default = false;
    };
    /* the function definition whose body is analysed */
    struct function_context {
        bool active = false;
        qualified_type returns;
        /* [dcl.spec.auto]/3: the declared return type has a placeholder
           (declared_returns), and the type deduced from the first return
           statement replaced it in returns (deduced) */
        qualified_type declared_returns;
        bool deducing = false, deduced = false;
        unsigned loops = 0, breakable = 0;
        owned_array<label_record> labels;
        owned_array<goto_record> gotos;
        /* [stmt.dcl]/3: the variables whose initialization a jump shall
           not bypass, where each is declared */
        owned_array<jump_point> initialized;
        owned_array<switch_record> switches;
        owned_array<uint64_t> case_values;
        /* a non-static member function's `this` ([class.this]): the
           variable lowering makes its first parameter */
        entity_index this_entity = 0;
        /* the function whose body it is */
        entity_index function = 0;
    } function_state;
    /* the state of the enclosing function's analysis that a lambda's body
       changes, kept aside meanwhile (as class_definition does for a local
       class's member functions) */
    struct lambda_enclosing_state {
        function_context function;
        owned_array<entity_index> block_variables;
        owned_array<parameter_meaning> parameters;
        entity_index scope = 0, last_declared = 0, deducing_variable = 0;
        SyntaxRef declaration = 0;
        token_index claim_place = 0;
        entity_index claim_scope = 0;
        qualified_type declaration_deduced;
        bool placeholder_group = false, in_condition = false;
        uint32_t function_cv = 0, function_empty_pack = 0;
    };
    /* the invocations of constexpr functions analysed so far: an
       initializer that has one is the evaluator's to decide
       ([basic.start.static]/2) */
    uint32_t constexpr_calls = 0;

    /* ---- instantiation (sema/instantiation.cpp, doc/PLAN-cxx-sema.md
       3.5) ----
       An instantiation's result with its key and everything it read from
       outside itself, which is what a cache would check before reusing
       it.  Nothing is instantiated yet. */
    struct template_argument {
        /* [temp.arg]/1: a type, a value (an integral one, of type type,
           sign- or zero-extended as expression_meaning keeps it; or in a
           template's own declaration a non-type template-parameter,
           template_entity, whose position value is) or a template */
        enum class kind_t : uint8_t {
            type,
            value,
            /* [temp.arg.template]/1: a class template, its entity in
               value; in a template's own declaration a template
               template-parameter, template_entity (its position value) */
            template_name,
            /* [temp.variadic]/1's template parameter pack's argument: its
               count elements pack_arguments[value .. + count), the same
               elements always at the same place (make_pack) */
            pack
        } kind = kind_t::type;
        qualified_type type;
        uint64_t value = 0;
        entity_index template_entity = 0;
        uint32_t count = 0;
        bool operator==(const template_argument &o) const
        {
            return kind == o.kind && type == o.type && value == o.value &&
                   template_entity == o.template_entity && count == o.count;
        }
    };
    struct instantiation_input {
        /* what the instantiation looked up outside itself: a name in a
           scope and what it found, an overload set at a call, the
           specializations of a template visible */
        enum class kind_t : uint8_t {
            lookup,
            overload_set,
            specializations
        } kind = kind_t::lookup;
        entity_index scope = 0;
        uint32_t name = 0;
        entity_index found = 0;
    };
    struct instantiation_record {
        /* the key: the template and its arguments
           (template_arguments[first_argument .. + argument_count)) */
        entity_index template_entity = 0;
        uint32_t first_argument = 0, argument_count = 0;
        /* the specialization made ([temp.inst]) */
        entity_index specialization = 0;
        /* inputs[first_input .. + input_count) */
        uint32_t first_input = 0, input_count = 0;
        /* a function template's: the scope its template-parameters'
           names name the arguments in, whether its definition is
           instantiated, waits (pending_definitions) or is being
           analysed */
        entity_index scope = 0;
        bool defined = false, pending = false, defining = false;
        /* a class's: the partial or explicit specialization whose
           class-specifier defines it (function_templates' index plus one;
           0 the primary template's) and its parameters' arguments
           (template_arguments[first_deduced ..]) */
        uint32_t pattern = 0, first_deduced = 0;
        /* a function template's specialization declared by an explicit
           specialization ([temp.expl.spec]/1): never instantiated */
        bool explicit_specialization = false;
        /* a function template's declaration whose substitution failed
           while a candidate was considered ([temp.deduct]/8) */
        bool substitution_failed = false;
        /* a class's definition that failed ([temp.inst]/1): the first
           construct it could not analyse, for the uses that follow */
        bool has_failure = false;
        unsupported_construct failure;
    };
    /* [temp.fct]/1: a function template: its entity (template_entity,
       whose type is the function type with the type parameters' types,
       type_kind::template_parameter), the name hold_names bound for it,
       its template-declaration and function-definition, and its type
       parameters (typedef-names of those types, whose value is their
       position) template_parameter_entities[first_parameter ..
       + parameter_count); unusable when its name declares more than this
       template */
    struct function_template {
        entity_index entity = 0, name = 0;
        SyntaxRef declaration = 0, definition = 0;
        /* a function template's function-definition or (declared, not
           yet defined: definition 0) simple-declaration, whose first two
           children are its decl-specifier-seq and declarator */
        SyntaxRef declarator = 0;
        uint32_t first_parameter = 0, parameter_count = 0;
        bool unusable = false;
        /* a function template: the GNU attributes of its specializations'
           symbols (gnu_function_symbol_flags) and abi_tag */
        uint32_t gnu_flags = 0;
        token_index abi_tag = 0;
        /* a function template: its trailing parameters with a default
           argument ([dcl.fct.default]/1), its specializations' */
        uint32_t default_arguments = 0;
        /* a class template ([temp.class]): definition is its
           class-specifier */
        bool is_class = false;
        /* an alias template ([temp.alias]): definition is its
           alias-declaration */
        bool is_alias = false;
        /* a variable template ([temp.pre]/1): declarator is its
           simple-declaration */
        bool is_variable = false;
        /* a concept ([temp.concept]): definition is its
           constraint-expression */
        bool is_concept = false;
        /* a template template-parameter ([temp.param]/1): the class
           template its name stands for in the template's own
           declaration, never instantiated (a specialization of it is a
           dependent type, [temp.dep.type]/9); its entity's value is its
           position */
        bool is_parameter = false;
        /* a class template's partial ([temp.class.spec]) or explicit
           ([temp.expl.spec]) specialization: its primary template
           (function_templates' index plus one; no name of its own), the
           arguments its template-id gives
           (template_arguments[first_pattern .. + pattern_count), in terms
           of its own parameters) */
        uint32_t primary = 0, first_pattern = 0, pattern_count = 0;
        /* a deduction guide ([temp.deduct.guide]/1) of a template-head: the
           class template it names (function_templates' index plus one);
           its declarator is the deduction-guide, its function type the
           guide's parameters returning the class template's
           specialization it names */
        uint32_t guide_of = 0;
    };
    owned_array<function_template> function_templates;
    function_template new_template(uint32_t name, entity_index outer,
                                   SyntaxRef n, uint32_t index, uint32_t first,
                                   uint32_t count);
    void hold_template_name(function_template &record, uint32_t name,
                            entity_index outer);
    /* [temp.over]/1: "The complete set of candidate functions includes all
       the synthesized declarations and all of the non-template overloaded
       functions of the same name": the functions of a scope a function
       template's name, held, was bound to before the template */
    struct template_name_function {
        entity_index name = 0, function = 0;
    };
    owned_array<template_name_function> template_name_functions;
    /* per template-parameter: a type parameter's typedef-name of its
       template_parameter type, or a non-type parameter's entity (kind
       none, so that the template's own declarator cannot use its value;
       its type the parameter's); and its type-parameter or
       parameter-declaration, for its default template-argument */
    owned_array<entity_index> template_parameter_entities;
    owned_array<SyntaxRef> template_parameter_nodes;
    /* per template-parameter: the first_parameter of the declaration whose
       parameter names its default template-argument uses (its own
       template's, or an earlier declaration's after a merge) */
    owned_array<uint32_t> template_parameter_default_names;
    /* the instantiation whose specialization the declaration analysed
       now declares (its record's index plus one), and how deep
       instantiations nest ([temp.inst]/16) */
    uint32_t instantiating = 0;
    unsigned instantiation_depth = 0;
    /* the state enter_instantiation saves and leave_instantiation puts
       back */
    struct instantiation_context {
        entity_index scope = 0, defining_class = 0, last_declared = 0,
                     member_scope = 0;
        SyntaxRef declaration = 0;
        token_index horizon = 0, callee = 0;
        uint32_t function_cv = 0, instantiating = 0, class_instantiating = 0,
                 variable_instantiating = 0;
        unsigned unevaluated = 0;
        uint8_t access = 0;
        bool in_condition = false, claimable = false, expanding = false;
        function_context function;
        owned_array<parameter_meaning> parameters;
        size_t expressions_before = 0, uses_before = 0;
    };
    void enter_instantiation(instantiation_context &c, entity_index scope,
                             SyntaxRef declaration);
    void leave_instantiation(instantiation_context &c);
    /* an unevaluated operand ([expr.prop]/1) or a default argument is
       analysed: a specialization named there needs no definition
       ([temp.inst]/5) */
    unsigned unevaluated = 0;
    /* a candidate's declaration is instantiated ([temp.over]/1): an
       invalid type or expression in it is a deduction failure
       ([temp.deduct]/8), not an error (its diagnostics are taken back);
       definitions instantiated meanwhile are outside the immediate
       context, their failures counted */
    unsigned substituting = 0;
    uint32_t definition_failures = 0;
    /* the specializations whose definitions wait for the end of the
       translation unit ([temp.point]/7) */
    owned_array<uint32_t> pending_definitions;
    /* the definition of instantiations[index]'s specialization */
    analysis_result instantiate_definition(SyntaxRef at, uint32_t index);
    /* the definitions pending_definitions waits for */
    void instantiate_pending();
    /* in an instantiation, the template's first token: a namespace's
       name declared after it is not the template's to find
       (lookup_unqualified) */
    token_index lookup_horizon = 0;
    /* the function template (member: member function template) whose name
       the held entity is, plus one */
    uint32_t function_template_of(entity_index held, bool member = false) const;
    /* the class template the entity names (its held name, or one of its
       specializations as its injected-class-name), plus one */
    uint32_t class_template_of(entity_index e) const;
    /* the alias template the held name e is, plus one */
    uint32_t alias_template_of(entity_index e) const;
    uint32_t template_of_form(entity_index e,
                              bool function_template::*form) const;
    bool template_id_name(SyntaxRef id, lookup_result &x,
                          entity_index &named) const;
    /* [temp.alias]/1: an alias template defined at namespace scope */
    analysis_result alias_template_definition(SyntaxRef n, SyntaxRef alias);
    /* [temp.alias]/2: the type a simple-template-id naming the alias
       template at denotes */
    /* [ext:clang_builtin_templates] __builtin_common_type<Template,
       TypeIdentity, Empty, T...> (type-traits.cpp) */
    bool is_builtin_common_type(uint32_t name) const;
    analysis_result builtin_common_type(SyntaxRef id, type_index &type);
    /* __type_pack_element<N, T...> */
    bool is_builtin_type_pack_element(uint32_t name) const;
    analysis_result builtin_type_pack_element(SyntaxRef id, type_index &type);
    /* __make_integer_seq<B, T, N> */
    bool is_builtin_make_integer_seq(uint32_t name) const;
    analysis_result builtin_make_integer_seq(SyntaxRef id, type_index &type);
    analysis_result
    builtin_template_arguments(SyntaxRef id,
                               owned_array<SyntaxElement> &written,
                               owned_array<bool> &expansion);
    analysis_result
    builtin_template_types(SyntaxRef id,
                           const owned_array<SyntaxElement> &written,
                           const owned_array<bool> &expansion, size_t from,
                           owned_array<qualified_type> &types, bool &dependent);
    enum class common_found : uint8_t { none, found };
    analysis_result common_type_of(SyntaxRef id, entity_index base,
                                   const qualified_type *types, uint32_t count,
                                   common_found &found, qualified_type &out);
    analysis_result common_type_pair(SyntaxRef id, entity_index base,
                                     qualified_type a, qualified_type b,
                                     common_found &found, qualified_type &out);
    analysis_result common_conditional(SyntaxRef id, qualified_type a,
                                       qualified_type b, common_found &found,
                                       qualified_type &out);
    analysis_result template_with_types(SyntaxRef id, entity_index named,
                                        const qualified_type *types,
                                        uint32_t count, type_index &type);
    qualified_type decayed(qualified_type t);
    analysis_result changed_signedness(SyntaxRef n, qualified_type t,
                                       bool to_signed, qualified_type &type);
    type_index
    dependent_specialization(entity_index t,
                             const owned_array<template_argument> &arguments);
    analysis_result alias_template_specialization(
        SyntaxRef id, uint32_t at,
        const owned_array<template_argument> &arguments, type_index &type,
        uint32_t *cv = nullptr);
    analysis_result alias_template_type(SyntaxRef id, uint32_t at,
                                        type_index &type,
                                        uint32_t *cv = nullptr);
    SyntaxRef variable_specialization_id(SyntaxRef d) const;
    SyntaxRef qualified_variable_specialization_id(SyntaxRef d,
                                                   entity_index &qualifier);
    /* a partial specialization equivalent to another: the template not
       used */
    void equivalent_partial_specializations(uint32_t added);
    analysis_result variable_partial_specialization(SyntaxRef n,
                                                    SyntaxRef declaration,
                                                    SyntaxRef id,
                                                    entity_index qualifier);
    analysis_result variable_specialization(SyntaxRef n, SyntaxRef inner,
                                            SyntaxRef id,
                                            entity_index qualifier);
    /* [temp.concept]: a concept-definition; the concept the held name e
       is, plus one; [temp.names]/8: a concept-id's value */
    analysis_result concept_definition(SyntaxRef n,
                                       ConceptDefinition definition);
    uint32_t concept_of(entity_index e) const;
    analysis_result requires_expression(RequiresExpression n,
                                        expression_meaning &m);
    analysis_result requirement_satisfied(SyntaxElement r, bool &satisfied);
    analysis_result type_constraint_satisfied(SyntaxElement q, qualified_type u,
                                              bool &satisfied);
    analysis_result concept_id(SyntaxRef n, uint32_t ct, expression_meaning &m);
    /* [temp.constr.constr]: whether constraint-expression e is satisfied,
       the template parameters bound in scope */
    analysis_result constraint_satisfied(SyntaxElement e, entity_index scope,
                                         token_index horizon, bool &satisfied);
    /* the variable template the held name e is, plus one */
    uint32_t variable_template_of(entity_index e) const;
    /* [temp.pre]/1: a variable template defined at namespace scope */
    analysis_result variable_template_definition(SyntaxRef n,
                                                 SyntaxRef declaration);
    /* [temp.inst]/7: a simple-template-id naming a variable template's
       specialization, as an id-expression */
    analysis_result variable_template_id(SyntaxRef n, expression_meaning &m);
    analysis_result function_template_id_value(SyntaxRef n, uint32_t ft,
                                               expression_meaning &m);
    /* ... the specialization, instantiated (or found) */
    analysis_result instantiate_variable(SyntaxRef at, uint32_t vt,
                                         const template_argument *arguments,
                                         uint32_t count, entity_index &e);
    /* the instantiation whose variable the declaration analysed now
       declares (its record's index plus one) */
    uint32_t variable_instantiating = 0;
    /* declare()'s part for it */
    analysis_result
    declare_variable_specialization(SyntaxRef declaration, qualified_type type,
                                    const specified &s,
                                    entity_index &declared_entity);
    /* [temp.pre]/1: a template-head's parameters */
    analysis_result template_head_parameters(SyntaxRef n,
                                             owned_array<SyntaxRef> &parameters,
                                             bool packs = false,
                                             bool constrained = false,
                                             bool pack_anywhere = false,
                                             bool type_constraints = false);
    SyntaxElement head_constraint(SyntaxRef n) const;
    SyntaxElement trailing_constraint(SyntaxRef n) const;
    /* [dcl.fct]/22: the decl-specifier-seqs of the parameters of the
       function that declaration (a template-declaration's or a
       function-definition) declares with a generic parameter type
       placeholder, each an invented type template-parameter, appended to
       parameters (a parameter pack's is a pack: invented_pack) */
    bool generic_placeholder(SyntaxRef seq) const;
    /* whether the invented template-parameter of seq is a pack */
    bool invented_pack(SyntaxRef seq) const;
    mutable owned_array<uint32_t> invented_packs;
    bool abbreviated_parameters(SyntaxRef declaration,
                                owned_array<SyntaxRef> &parameters) const;
    /* whether a function-definition declares an abbreviated function
       template ([dcl.fct]/18) */
    bool abbreviated_function(SyntaxRef definition) const
    {
        owned_array<SyntaxRef> parameters;
        return !abbreviated_parameters(definition, parameters) ||
               !parameters.empty();
    }
    /* the name of the invented template-parameter of seq, auto:N, which no
       identifier is spelled as */
    uint32_t invented_parameter_name(SyntaxRef seq) const;
    /* the type a parameter's placeholder denotes: its invented
       template-parameter's, or the argument of one bound in scope */
    bool invented_parameter_type(SyntaxRef seq, qualified_type &type) const;
    /* [temp.constr.atomic]/3: the satisfaction of a concept-id, once per
       concept and arguments (a later different result would be ill-formed,
       no diagnostic required; clang keeps the first, as here).  Its
       arguments are satisfaction_arguments[first .. + count) */
    struct satisfaction {
        uint32_t concept_template = 0, first = 0, count = 0;
        bool satisfied = false;
    };
    owned_array<satisfaction> satisfactions;
    owned_array<template_argument> satisfaction_arguments;
    uint32_t template_record(entity_index t) const;
    analysis_result
    concept_satisfied(SyntaxRef at, uint32_t ct,
                      const owned_array<template_argument> &arguments,
                      bool &satisfied);
    bool same_constraints(SyntaxRef a, SyntaxRef b);
    bool template_head_constrained(SyntaxRef n);
    bool primary_arguments(SyntaxRef n, SyntaxRef primary,
                           const owned_array<template_argument> &arguments,
                           uint32_t first, size_t count);
    bool dependent_arguments(const owned_array<template_argument> &a) const;
    bool same_token_spelling(token_index a, token_index b);
    bool same_tokens(SyntaxRef a, SyntaxRef b);
    /* [temp.deduct]/5: whether the function template's constraints are
       satisfied by the arguments deduced */
    analysis_result
    template_constraints_satisfied(uint32_t function_template,
                                   const owned_array<template_argument> &values,
                                   bool &satisfied);
    /* a template-parameter's identifier (after the ellipsis of a pack's) */
    token_index template_parameter_id(SyntaxRef parameter) const;
    /* [temp.param]/1's template-parameter, as written: a type-parameter
       (a template template-parameter's with its template-head) or a
       parameter-declaration, its ellipsis, identifier and default
       template-argument; false for a form with no rules here (a
       type-constraint) */
    struct template_parameter_parts {
        bool type = false, template_template = false, pack = false;
        bool named = false, has_default = false, has_declarator = false;
        token_index id = 0;
        /* a type-parameter's type-constraint ([temp.param]/1), or 0 */
        SyntaxRef constraint = 0;
        SyntaxRef head = 0;
        SyntaxElement default_argument, declarator;
    };
    bool template_parameter_form(SyntaxRef parameter,
                                 template_parameter_parts &out) const;
    bool same_dependent_parameter_type(SyntaxRef a, SyntaxRef b);
    bool template_parameter_has_default(SyntaxRef parameter) const
    {
        template_parameter_parts x;
        return template_parameter_form(parameter, x) && x.has_default;
    }
    /* the type-parameters and parameter-declarations of a template-head */
    bool template_head_list(SyntaxRef head,
                            owned_array<SyntaxRef> &parameters) const;
    /* [temp.param]: the parameters' entities in scope, for the template
       function_templates[index] will be; reserved is a name they shall not
       have ([temp.local]/6) */
    analysis_result declare_template_parameters(
        SyntaxRef n, const owned_array<SyntaxRef> &parameters,
        entity_index scope, uint32_t &index, uint32_t reserved);
    analysis_result template_parameter_scope(
        SyntaxRef n, const owned_array<SyntaxRef> &parameters,
        entity_index outer, uint32_t reserved, uint32_t &index,
        entity_index &scope, uint32_t &first);
    SyntaxRef sole_decl_specifier(SyntaxRef declaration) const;
    bool expansion_list(SyntaxRef list, owned_array<SyntaxElement> &written,
                        owned_array<bool> &expansion) const;
    bool
    default_missing_after_one(const owned_array<SyntaxRef> &parameters) const;
    analysis_result
    template_id_arguments_in(SyntaxRef id, uint32_t ct, entity_index scope,
                             owned_array<template_argument> &arguments);
    /* [temp.arg]: the written argument for the template's parameter at
       position, given the arguments before it */
    analysis_result template_argument_for(SyntaxRef at, uint32_t template_index,
                                          uint32_t position,
                                          SyntaxElement written,
                                          const template_argument *before,
                                          template_argument &out);
    /* [temp.param]/4: a non-type parameter's dependent type
       (typename enable_if<B, int>::type) with the arguments before it */
    entity_index dependent_value_entity(SyntaxElement written);
    entity_index dependent_variable_specialization(SyntaxRef id,
                                                   entity_index e);
    owned_array<entity_index> dependent_values;
    analysis_result substituted_parameter_type(uint32_t template_index,
                                               uint32_t position,
                                               const template_argument *before,
                                               qualified_type &type);
    /* [temp.param]/6: a non-type template-parameter's type with rules
       here; one whose arguments are addresses */
    bool structural_parameter_type(qualified_type t) const;
    /* [temp.param]/4: the position of the earlier type parameter of the
       same template-parameter-list that is t, UINT32_MAX if none */
    uint32_t own_type_parameter_position(qualified_type t, entity_index scope,
                                         uint32_t first) const;
    bool address_parameter_type(qualified_type t) const;
    /* [temp.arg.nontype]/3: an argument for one of those */
    analysis_result address_argument(SyntaxRef at, qualified_type to,
                                     SyntaxElement written,
                                     template_argument &out);
    /* [temp.arg.template]: the argument for template template-parameter
       parameter */
    analysis_result template_template_argument(SyntaxRef at,
                                               entity_index parameter,
                                               SyntaxElement written,
                                               template_argument &out);
    /* [temp.arg]/4, [temp.param]/12: the arguments not given (set[i]
       false) from the parameters' default template-arguments */
    analysis_result
    default_template_arguments(SyntaxRef at, uint32_t template_index,
                               owned_array<template_argument> &arguments,
                               const owned_array<bool> &set);
    /* a template-parameter's name in scope, as the argument: a
       typedef-name of the type, or a constant of the value */
    void bind_template_argument(entity_index scope, entity_index parameter,
                                const template_argument &a);
    /* [temp.class.spec], [temp.expl.spec]: a class template's partial
       (parameters) or explicit specialization's definition */
    analysis_result
    class_specialization_definition(SyntaxRef n, SyntaxRef specifier,
                                    const owned_array<SyntaxRef> &parameters);
    /* [temp.class.spec.match]/3: whether the deduced arguments substituted
       into the partial specialization's give the use's */
    analysis_result substitute_specialization(
        uint32_t specialization, const owned_array<template_argument> &deduced,
        const template_argument *arguments, uint32_t count, bool &substituted);
    /* analysing a partial specialization's template arguments, which
       matching substitutes again (substitute_specialization) */
    uint32_t partial_arguments = 0;
    /* analysing a specialization's head (with partial_arguments, or an
       explicit specialization's template-id; not in a
       nested-name-specifier, whose class is looked into): a class
       template-id there is declared, not defined ([temp.inst]/2: no completely-defined type
       is needed; instantiate_class) */
    uint32_t declaring_specializations = 0;
    /* deducing a partial specialization's arguments from a use's
       (matching_specialization): a decltype is a non-deduced context
       whose substitution substitute_specialization checks */
    bool matching_partial = false;
    /* the specialization of function_templates[class_template - 1] whose
       pattern the arguments match (0: the primary), with its parameters'
       arguments */
    analysis_result
    matching_specialization(SyntaxRef at, uint32_t class_template,
                            const template_argument *arguments, uint32_t count,
                            uint32_t &pattern,
                            owned_array<template_argument> &deduced);
    /* [temp.class]: a class template defined at namespace scope */
    analysis_result class_template_definition(SyntaxRef n, SyntaxRef specifier);
    /* [temp.names]/1: a simple-template-id's arguments for the class
       template function_templates[ct - 1], defaults included */
    analysis_result
    template_id_arguments(SyntaxRef id, uint32_t ct,
                          owned_array<template_argument> &arguments);
    /* [temp.class]: a class template declared without a definition
       (template<class T> struct S;) */
    analysis_result class_template_declaration(SyntaxRef n,
                                               SyntaxRef elaborated);
    /* [temp.arg.template]/3: whether an argument template's parameters
       match a template template-parameter's */
    bool template_parameters_match(const function_template &wanted,
                                   const function_template &given);
    /* whether the template's parameters (template_parameter_entities from
       first) are another's (from other_first) of the same kinds and
       types, count of each */
    bool same_template_parameters(uint32_t first, uint32_t other_first,
                                  uint32_t count);
    bool equivalent_template(const function_template &f, SyntaxRef n,
                             uint32_t first, uint32_t count,
                             qualified_type type);
    /* [temp.mem.func]/1: a member function of a class template (or of a
       partial specialization, [temp.class.spec.mfunc]/1) defined outside
       it: the definition, analysed per specialization of the class
       (define_template_member) */
    struct template_member_definition {
        SyntaxRef declaration = 0, definition = 0;
        /* a member template's ([temp.mem]/1) own template-declaration,
           which the definition is the declaration of */
        SyntaxRef inner = 0;
        /* the class template, and the partial specialization whose member
           it is (function_templates' index plus one; 0 the primary) */
        entity_index template_entity = 0;
        uint32_t pattern = 0;
        /* the definition's own template-parameters (template_parameter_
           entities' index) */
        uint32_t first_parameter = 0;
    };
    owned_array<template_member_definition> template_member_definitions;
    /* the simple-template-id before the declarator-id's :: of a
       function-definition (box<T>::f), or 0; and the member's name (for an
       operator function, its operator keyword's token) */
    SyntaxRef out_of_class_member(SyntaxRef definition,
                                  token_index *name = nullptr) const;
    SyntaxRef out_of_class_variable_member(SyntaxRef declaration,
                                           token_index *name = nullptr) const;
    SyntaxRef out_of_class_nested(SyntaxRef definition,
                                  token_index *name = nullptr) const;
    entity_index plain_class_nominated(SyntaxRef nested);
    analysis_result define_plain_member_template(SyntaxRef n,
                                                 SyntaxRef definition,
                                                 entity_index c);
    analysis_result define_member_template_of(entity_index c,
                                              SyntaxRef declaration,
                                              SyntaxRef inner,
                                              SyntaxRef definition,
                                              bool recorded);
    analysis_result member_definition_of_template(SyntaxRef n,
                                                  SyntaxRef definition,
                                                  SyntaxRef inner = 0);
    analysis_result define_member_template(uint32_t member, uint32_t index,
                                           bool recorded);
    entity_index defined_special_member(entity_index specialization,
                                        const template_member_definition &o,
                                        token_index name);
    analysis_result member_definition_unsupported(SyntaxRef declaration,
                                                  bool recorded);
    /* template_member_definitions[member] for the class of
       instantiations[index]; after the template-declaration was analysed
       (declarations has it), a failure makes that unsupported */
    analysis_result define_template_member(uint32_t member, uint32_t index,
                                           bool recorded);
    /* instantiate_class's definition of instantiations[index]'s class */
    /* define_class_instantiation outside a substitution's immediate
       context (substituting) */
    analysis_result define_class_outside(SyntaxRef at, uint32_t index,
                                         entity_index &c);
    analysis_result define_class_instantiation(SyntaxRef at, uint32_t index,
                                               entity_index &c);
    /* [temp.names]/1: a simple-template-id that names a class template's
       specialization, as a type; in the scope given (0: unqualified).  An
       alias template's may be cv-qualified: its cv in *cv, refused when
       cv is null */
    analysis_result template_type(SyntaxRef id, entity_index scope,
                                  bool qualified, type_index &type,
                                  uint32_t *cv = nullptr);
    /* [temp.inst]/2: the class template specialization, instantiated */
    analysis_result instantiate_class(SyntaxRef at, uint32_t class_template,
                                      const template_argument *arguments,
                                      uint32_t count, entity_index &c);
    /* the instantiation whose class the class-specifier analysed now
       defines (its record's index plus one) */
    uint32_t class_instantiating = 0;
    /* a call whose name is a function template's (postfix-expression.cpp) */
    analysis_result template_call(SyntaxRef n, token_index name,
                                  uint32_t function_template,
                                  owned_array<template_argument> &specified,
                                  bool qualified,
                                  const owned_array<SyntaxElement> &arguments,
                                  owned_array<expression_meaning> &early,
                                  expression_meaning &f);
    /* a callee that names a function template: f, f<A...>, N::f or
       N::f<A...>; its name, the template (plus one, 0 if the callee is no
       template's name) and the template-arguments given */
    analysis_result template_callee(SyntaxElement callee, token_index &name,
                                    uint32_t &function_template,
                                    owned_array<template_argument> &specified,
                                    bool &qualified);
    analysis_result template_callee_parts(
        SyntaxElement callee, token_index &name, uint32_t &function_template,
        owned_array<template_argument> &specified, bool &qualified);
    /* declare()'s part for a specialization an instantiation makes */
    analysis_result declare_specialization(SyntaxRef declaration,
                                           token_index id, qualified_type type,
                                           const specified &s,
                                           entity_index &declared_entity);
    /* [temp.deduct.call]: P against A, the deduced arguments by position
       (set: deduced or given) */
    /* [temp.deduct.call]/2-3: P and A of a parameter and its argument
       (of value category c) adjusted for deduce_type */
    void adjust_call_deduction(qualified_type &p, qualified_type &a,
                               value_category c);
    /* [temp.deduct.call]/4.3: P a simple-template-id, from a base class
       of the class A; 1 deduced, 0 failed, -1 not known */
    int deduce_from_base(qualified_type p, qualified_type a,
                         owned_array<template_argument> &deduced,
                         owned_array<bool> &set);
    bool deduce_bound(entity_index parameter, uint64_t bound,
                      owned_array<template_argument> &deduced,
                      owned_array<bool> &set);
    bool deduce_type(qualified_type p, qualified_type a,
                     owned_array<template_argument> &deduced,
                     owned_array<bool> &set, bool exact = false);
    /* each time deduction met a form it does not analyse (a pack not at
       the end, a template template-parameter's pack) and answered false
       without knowing: a caller that compares the count refuses */
    uint32_t deduction_unknowns = 0;
    /* t without the cv-qualifiers cv, an array's taken from its elements
       ([basic.type.qualifier]/3) */
    qualified_type remove_cv(qualified_type t, uint32_t cv);
    /* [temp.over]/1: the usable function templates (indices plus one) of
       a held name, in declaration order */
    void function_template_overloads(entity_index held,
                                     owned_array<uint32_t> &out) const;
    /* [temp.func.order] in a call of the given number of arguments
       ([temp.deduct.partial]/3.1): 1 when f is more specialized than g,
       -1 when g is more than f, 0 when neither; unknown set when analysis
       cannot tell */
    int function_template_order(uint32_t f, uint32_t g, size_t arguments,
                                bool &unknown);
    bool at_least_as_specialized(uint32_t f, uint32_t g, size_t arguments,
                                 bool &unknown);
    /* [temp.class.order]: whether partial specialization s (index plus
       one) is at least as specialized as t */
    bool class_at_least_as_specialized(uint32_t s, uint32_t t, bool &unknown);
    owned_array<template_argument> template_arguments;
    /* [temp.variadic]: the elements of the packs among template arguments
       (template_argument::kind_t::pack) */
    owned_array<template_argument> pack_arguments;
    template_argument make_pack(const template_argument *elements,
                                uint32_t count);
    bool deduce_argument(const template_argument &x, const template_argument &y,
                         owned_array<template_argument> &d,
                         owned_array<bool> &set);
    bool deduce_pack_argument(const template_argument &x,
                              const template_argument &y,
                              owned_array<template_argument> &d,
                              owned_array<bool> &set);
    /* a template parameter pack: template_parameter_entities[i]'s */
    bool template_parameter_is_pack(uint32_t i) const;
    bool names_template_parameter_pack(qualified_type t) const;
    /* [temp.variadic]/1: an instantiation's packs (entity_kind::pack, the
       elements pack_elements[value .. + member_count)), and the element
       their names denote while a pack expansion's pattern is analysed
       for it (expanding) */
    owned_array<entity_index> pack_elements;
    bool expanding = false;
    uint32_t expansion_element = 0;
    /* [dcl.init.aggr]/4.2: for the clauses of the list being aggregate
       initialized, the element of the pack each one expands (a clause that
       is no pack expansion's pattern: no_expansion), or null */
    static constexpr uint32_t no_expansion = 0xffffffffu;
    const owned_array<uint32_t> *clause_packs = nullptr;
    bool bound_pack_is_dependent(entity_index e) const;
    entity_index make_bound_pack(entity_index scope, uint32_t name,
                                 const entity_index *elements, uint32_t count);
    /* the pack an identifier names from the current scope, or 0 */
    entity_index find_pack(uint32_t name) const;
    /* [temp.variadic]/5: the number of elements of the packs the pattern
       [first, end) names (false: none, packs of different lengths, or a
       pack expansion within it); own set when it names none but a pack of
       the template's own declaration (own_pack_parameter) */
    bool expansion_count(token_index first, token_index end, uint32_t &count,
                         bool *own = nullptr) const;
    /* expression(pattern) for the element i of the packs it names */
    analysis_result expansion_element_expression(SyntaxElement pattern,
                                                 uint32_t i,
                                                 expression_meaning &m);
    /* [expr.prim.fold] */
    analysis_result fold(SyntaxRef n, expression_meaning &m);
    /* an initializer-list with a pack expansion (an ellipsis child) */
    bool has_pack_expansion(SyntaxElement list) const;
    owned_array<constant_value> instantiation_values;
    owned_array<instantiation_input> instantiation_inputs;
    owned_array<instantiation_record> instantiations;
    /* the instantiation record of the template's specialization for the
       arguments, plus one; 0 if there is none yet */
    uint32_t find_instantiation(entity_index t, const template_argument *a,
                                uint32_t count) const;
    /* a new instantiation record of function template ft (its entity the
       template's) for the arguments, with a block scope in the template's
       enclosing scope binding the template-parameters' names to them
       (bind_template_argument); its index */
    uint32_t record_instantiation(const function_template &ft,
                                  const template_argument *arguments,
                                  uint32_t count);
    /* [temp.inst]/1: the specialization of the template for the
       arguments, instantiated (or found, if it already was) */
    analysis_result instantiate(SyntaxRef at, entity_index template_entity,
                                const template_argument *arguments,
                                uint32_t count, entity_index &specialization);
    /* [temp.deduct]/1: "When a function template specialization is
       referenced, all of the template arguments shall have values"; the
       arguments deduced from a call's ([temp.deduct.call]); on entry
       those the call specifies ([temp.arg.explicit]) */
    analysis_result deduce_from_call(SyntaxRef at, uint32_t function_template,
                                     const expression_meaning *arguments,
                                     uint32_t count,
                                     owned_array<template_argument> &deduced,
                                     bool *failed = nullptr);
    /* [class.mem]/6: member function bodies of nested classes, analysed
       at the closing } of the outermost class being defined */
    struct deferred_body {
        entity_index c;
        SyntaxRef body;
    };
    owned_array<deferred_body> nested_bodies;
    /* [temp.inst]/4: the member function bodies of a class template's
       specialization instantiated while another class is being defined,
       analysed by instantiate_pending in the instantiation's scope */
    struct postponed_body {
        entity_index c;
        SyntaxRef body;
        entity_index scope;
        SyntaxRef declaration;
    };
    owned_array<postponed_body> postponed_bodies;
    bool postpone_member_bodies = false;
    analysis_result analyse_function_body(SyntaxRef definition,
                                          SyntaxElement compound,
                                          SyntaxElement ctor_initializer = {});
    /* [class.base.init]: a constructor's member initializations, the
       statements of a block (function_body's b) */
    analysis_result
    ctor_initializer(SyntaxElement list, uint32_t &built,
                     const struct inherited_call *inherited = nullptr);
    void collect_labels(SyntaxElement s);
    /* the statement functions give the statement's node in built (when
       well-formed) */
    analysis_result statement(SyntaxElement s, uint32_t &built);
    analysis_result attributed_statement(Statement n, uint32_t &built);
    /* a substatement ([stmt.pre]/2): a block scope of its own whose
       outermost names shall not redeclare conflicts' */
    analysis_result substatement(SyntaxElement s, entity_index conflicts,
                                 uint32_t &built);
    analysis_result compound_statement(CompoundStatement n,
                                       entity_index conflicts, uint32_t &built);
    analysis_result block_items(SyntaxRef n, uint32_t &built);
    analysis_result expression_statement(ExpressionStatement n,
                                         uint32_t &built);
    analysis_result selection_statement(SelectionStatement n, uint32_t &built);
    analysis_result iteration_statement(IterationStatement n, uint32_t &built);
    /* [stmt.ranged]: the range-based for statement n, whose
       for-range-declaration is its child declaration */
    analysis_result range_based_for(SyntaxRef n, uint32_t declaration,
                                    uint32_t &built);
    analysis_result exposition_variable(SyntaxRef n, token_index at,
                                        expression_meaning &m,
                                        qualified_type type, bool named,
                                        uint32_t &declaration,
                                        expression_meaning &variable);
    analysis_result range_call(SyntaxRef n, token_index at,
                               const expression_meaning &range, uint32_t name,
                               bool member, expression_meaning &m);
    analysis_result for_range_declaration(SyntaxElement d,
                                          const expression_meaning &element,
                                          uint32_t &built);
    /* ---- [dcl.struct.bind] structured bindings (structured-binding.cpp) */
    /* the structured binding declaration whose ref-qualifier_opt [
       identifier-list ] starts at child first of declaration, its
       decl-specifier-seq's meaning s; supplied: its initializer, analysed
       ("= E", [stmt.ranged]/1), else its last children's.  Its variables
       go to block_variables */
    analysis_result
    structured_binding_declaration(SyntaxRef declaration, uint32_t first,
                                   const specified &s,
                                   const expression_meaning *supplied);
    /* the expression the structured binding b names, at t */
    analysis_result structured_binding_use(token_index t, entity_index b,
                                           expression_meaning &m);
    /* an unnamed variable of the declaration (e, r_i, the array e's
       elements are copied from) of type type, initialized by m */
    analysis_result binding_variable(SyntaxRef n, expression_meaning &m,
                                     qualified_type type, bool named,
                                     bool direct, bool list, entity_index &v);
    analysis_result array_binding_object(SyntaxRef n, token_index at,
                                         expression_meaning &m,
                                         const specified &s, bool direct,
                                         entity_index &e);
    analysis_result tuple_trait_member(SyntaxRef n, const char *trait,
                                       bool indexed, uint32_t i,
                                       qualified_type type, const char *member,
                                       entity_index &found);
    analysis_result array_element(SyntaxRef n, token_index at,
                                  expression_meaning &object, uint64_t i);
    analysis_result tuple_size_of(SyntaxRef n, qualified_type type, bool &found,
                                  uint64_t &value);
    analysis_result tuple_element_of(SyntaxRef n, uint32_t i,
                                     qualified_type type,
                                     qualified_type &element);
    analysis_result binding_get_call(SyntaxRef n, token_index at, uint32_t i,
                                     entity_index e, qualified_type type,
                                     expression_meaning &m);
    analysis_result index_argument(SyntaxRef n, uint32_t template_index,
                                   uint32_t i, template_argument &out);
    analysis_result binding_members_class(SyntaxRef n, entity_index c,
                                          entity_index &holder);
    uint32_t binding_member_count(entity_index c) const;
    /* the class template std::NAME, plus one (0: none declared) */
    analysis_result std_class_template(SyntaxRef n, const char *name,
                                       uint32_t &template_index);
    /* the variables binding_variable makes: linkage::none automatic,
       else of static storage duration with this linkage (at namespace
       scope) */
    linkage binding_storage = linkage::none;
    analysis_result jump_statement(JumpStatement n, uint32_t &built);
    analysis_result labeled_statement(LabeledStatement n, uint32_t &built);
    analysis_result case_label(SyntaxRef n, SyntaxElement value,
                               size_t innermost);
    /* operand: the operand's node, initialized as the result (0: none) */
    analysis_result return_statement(SyntaxRef n, uint32_t &operand);
    /* the variables declared at block scope, in order, until the
       declaration statement that declares them takes them */
    owned_array<entity_index> block_variables;
    /* the declaration statement of block_variables[first ..] */
    uint32_t declaration_statements(size_t first, token_index at);
    SyntaxRef node_of(SyntaxElement c) const;
    /* a condition's declarator is analysed ([stmt.pre]/4) */
    bool in_condition = false;
    /* [stmt.pre]: a condition, its value converted to bool (or, for a
       switch, the condition's value promoted: m) */
    analysis_result condition(SyntaxElement c, bool for_switch,
                              expression_meaning &m);
    analysis_result switch_condition(SyntaxRef at, expression_meaning &m);
    /* [stmt.dcl]/3: a jump from from to to bypasses no initialization */
    bool jump_bypasses(jump_point from, jump_point to) const;
    bool in_scope(jump_point variable, jump_point at) const;
    /* [expr.call] */
    analysis_result call(SyntaxRef n, expression_meaning &m);

    /* ---- overloading ([over]) ---- */
  public:
    /* a function template's specialization (entity::instantiation): its
       template and its arguments, for its name ([abi:mangle.name]) */
    entity_index specialized_template(entity_index e) const
    {
        return instantiations[entity_table[e].instantiation - 1]
            .template_entity;
    }
    /* [abi:mangle.requires-clause]: the requires-clause of the function
       template t's template-head (none: no bits), its template parameter
       and the concept a token of it names (0: neither) */
    SyntaxElement template_trailing_constraint(entity_index t) const;
    SyntaxElement template_constraint(entity_index t) const;
    /* [expr.prim.lambda]/1: a generic lambda's requires-clause's
       constraint-expression (none: no bits); trailing: whether it follows
       the lambda-declarator's parameters */
    SyntaxElement lambda_constraint(SyntaxRef n,
                                    bool *trailing = nullptr) const;
    /* the number of template-parameters of template entity t */
    uint32_t template_parameter_count(entity_index t) const
    {
        return function_templates[entity_table[t].instantiation - 1]
            .parameter_count;
    }
    entity_index constraint_parameter(entity_index t, token_index i) const;
    /* the concept token i names, looked up from scope outward (0: none) */
    entity_index constraint_concept(entity_index scope, token_index i) const;
    /* [abi:mangle.function-param]: the position plus one of function
       template t's parameter named by token i (0: none) */
    uint32_t function_parameter_position(entity_index t, token_index i) const;
    uint32_t token_identifier(token_index i) const
    {
        return tokens.identifier(i);
    }
    /* whether specialization e has internal linkage only from local
       classes or closure types other translation units name too: its
       symbol has its template's linkage (basic/translation-unit.cpp) */
    bool specialization_visible(entity_index e) const;
    uint32_t specialization_argument_count(entity_index e) const
    {
        return instantiations[entity_table[e].instantiation - 1].argument_count;
    }
    qualified_type specialization_argument(entity_index e, uint32_t i,
                                           uint32_t element = 0) const
    {
        return specialization_template_argument(e, i, element).type;
    }
    /* whether argument i is a pack's ([temp.variadic]/1): its number of
       elements plus one, 0 if not; element k + 1 of the functions here
       is its k-th */
    uint32_t specialization_pack(entity_index e, uint32_t i) const
    {
        const template_argument &a = specialization_template_argument(e, i);
        if (a.kind != template_argument::kind_t::pack)
            return 0;
        return a.count + 1;
    }
    /* whether function f is defined by a friend declaration in a class
       ([class.friend]/6) */
    bool defined_by_friend(entity_index f) const;
    /* whether element of argument i is a template's own pack expanded,
       not bound to elements (res<A...> in its own declaration): the
       pack parameter's entity (a type parameter's typedef-name), 0 if
       not */
    entity_index specialization_own_pack(entity_index e, uint32_t i,
                                         uint32_t element) const
    {
        const template_argument &a =
            specialization_template_argument(e, i, element);
        if (a.kind != template_argument::kind_t::pack || a.count != UINT32_MAX)
            return 0;
        return a.template_entity;
    }
    /* whether the argument is a value (non-type, integral): its
       magnitude, and whether it is negative; or, in a template's own
       declaration, the non-type template-parameter it names (its position
       plus one in parameter) */
    bool specialization_value(entity_index e, uint32_t i, uint64_t &magnitude,
                              bool &negative, uint32_t &parameter,
                              uint32_t element = 0) const;
    /* whether the argument is a value-dependent expression
       (entity_kind::dependent_value), which is not mangled yet */
    bool specialization_dependent_value(entity_index e, uint32_t i,
                                        uint32_t element = 0) const;
    /* that argument's entity (an entity_kind::dependent_value) */
    entity_index specialization_entity(entity_index e, uint32_t i,
                                       uint32_t element = 0) const
    {
        return specialization_template_argument(e, i, element).template_entity;
    }
    /* whether the function template t's template-parameter at i is a
       non-type one of a dependent type (template <class T, T v>): that
       type, which [abi:mangle.template-arg] mangles after Tn before the
       argument */
    bool dependent_parameter_type(entity_index t, uint32_t i,
                                  qualified_type &type) const;
    /* the concept-name of the function template t's template-parameter
       at i, a type-parameter with a type-constraint ([temp.param]/1),
       or 0 */
    token_index parameter_type_constraint(entity_index t, uint32_t i) const;
    /* whether the argument is a template ([temp.arg.template]/1): the
       class template, or in a template's own declaration the template
       template-parameter it names (also its position plus one in
       parameter) */
    /* whether the argument is an object's or function's address (a
       pointer parameter's) or the object or function (a reference
       parameter's) */
    bool specialization_address(entity_index e, uint32_t i,
                                entity_index &object, bool &reference,
                                uint32_t element = 0) const;
    /* whether entity e is a template template-parameter */
    bool template_template_parameter(entity_index e) const;
    bool specialization_template_name(entity_index e, uint32_t i,
                                      entity_index &named, uint32_t &parameter,
                                      uint32_t element = 0) const;

  private:
    const template_argument &
    specialization_template_argument(entity_index e, uint32_t i,
                                     uint32_t element = 0) const
    {
        const instantiation_record &x =
            instantiations[entity_table[e].instantiation - 1];
        const template_argument &a = template_arguments[x.first_argument + i];
        if (!element)
            return a;
        return pack_arguments[a.value + element - 1];
    }

  public:
    /* [over.best.ics]/1: "An implicit conversion sequence is a sequence of
       conversions used to convert an argument in a function call to the
       type of the corresponding parameter of the function being called":
       what [over.ics.rank] compares of one */
    struct conversion_sequence {
        enum class form : uint8_t {
            /* no implicit conversion sequence: the candidate is not
               viable ([over.match.viable]/3) */
            bad,
            standard,
            user_defined,
            ellipsis,
            /* one analysis cannot tell (a class it did not follow) */
            unknown,
        };
        form kind = form::bad;
        /* [over.ics.scs]/3: the rank of the standard conversion sequence
           (a user-defined one's second): 0 Exact Match, 1 Promotion, 2
           Conversion */
        uint8_t rank = 0;
        /* the identity conversion: none but lvalue transformations
           ([over.ics.rank]/3.2.1) */
        bool identity = true;
        /* [conv.bool] from a pointer ([over.ics.rank]/4.1) */
        bool pointer_to_bool = false;
        /* to cv void* ([over.ics.rank]/4.4.1) */
        bool to_void_pointer = false;
        /* [conv.prom]/4: an enumeration with a fixed underlying type to it
           ([over.ics.rank]/4.2) */
        bool to_underlying = false;
        /* a reference binding ([over.ics.ref]): to an rvalue reference, to
           an rvalue (a temporary too), to a function lvalue; of the
           implicit object parameter ([over.match.funcs]/4) */
        bool reference = false, rvalue_reference = false, binds_rvalue = false,
             function_lvalue = false, implicit_object = false;
        /* the implicit object parameter of a static member function, which
           "is considered to match any object" ([over.match.funcs]/4) */
        bool any_object = false;
        /* an unknown sequence that is, if the candidate is viable, a
           user-defined conversion sequence (never a standard one): the
           class converted from is unrelated to the parameter's and has
           conversion functions analysis does not follow */
        bool user_defined_if_viable = false;
        /* [over.best.ics]/10: an ambiguous conversion sequence */
        bool ambiguous = false;
        /* the type converted to: the parameter's, or the type a
           reference refers to */
        qualified_type target;
        /* a derived-to-base Conversion ([over.best.ics]/6, [conv.ptr]/3):
           its classes */
        entity_index base_from = 0, base_to = 0;
        /* a user-defined conversion sequence: the converting constructor
           ([over.ics.user]/1) */
        entity_index function = 0;
    };
    /* [over.match.funcs]: a candidate function, or (function 0) the
       implicitly-declared copy constructor of the class constructed,
       whose type is type */
    struct overload_candidate {
        entity_index function = 0;
        qualified_type type;
        /* [over.match.oper]/3: a member candidate among non-member ones:
           the first argument is its implied object argument, the rest
           its parameters' */
        bool member = false;
        /* [over.match.oper]/3.4: a rewritten candidate (an operator== for
           x != y or y == x), and one synthesized with the order of its two
           parameters reversed */
        bool rewritten = false, reversed = false;
        /* a function template specialization: its template
           (function_templates' index plus one), for [over.match.best]/2.4
           and 2.5 */
        uint32_t template_index = 0;
    };

  private:
    /* [temp.deduct]/8: "Invalid types and expressions can result in a
       deduction failure only in the immediate context": what a
       substitution issued and made, taken back when it failed there */
    struct substitution_mark {
        size_t messages = 0, errors = 0, warnings = 0, fatals = 0;
        size_t expressions = 0, uses = 0;
        uint32_t failures = 0;
    };
    substitution_mark mark_substitution() const;
    /* false (nothing undone) when the failure is not the substitution's */
    bool undo_substitution(const substitution_mark &m);
    /* [temp.over]/1: the specializations of the templates in group
       whose deduction from the call's arguments succeeds, appended to
       candidates; with first_arguments, where each one's deduced
       arguments start in all */
    /* specified: template arguments already analysed, put before what
       is deduced (as written's are) */
    /* [temp.over]/1: the member function templates named held, as member
       candidates (member-template.cpp) */
    /* conditions: evaluate an explicit-specifier's condition (in the scope
       of the template-parameters), else skip it */
    analysis_result constructor_specifiers(SyntaxRef seq, specified &s,
                                           bool conditions = true);
    analysis_result constructor_template_declarator(SyntaxRef definition,
                                                    SyntaxElement d,
                                                    qualified_type &type,
                                                    declarator_meaning &m);
    analysis_result constructor_template_candidates(
        SyntaxRef n, entity_index c, const expression_meaning *arguments,
        uint32_t count, bool copy, owned_array<overload_candidate> &candidates,
        owned_array<uint32_t> *first_arguments = nullptr,
        owned_array<template_argument> *all = nullptr);
    void
    function_candidates(const owned_array<entity_index> &functions, bool member,
                        owned_array<overload_candidate> &candidates,
                        owned_array<uint32_t> *first_arguments = nullptr) const;
    analysis_result member_template_candidates(
        SyntaxRef n, entity_index held, bool with_object,
        const expression_meaning *arguments, uint32_t count,
        owned_array<overload_candidate> &candidates,
        owned_array<uint32_t> *first_arguments = nullptr,
        owned_array<template_argument> *all = nullptr,
        const owned_array<SyntaxElement> *written = nullptr,
        SyntaxRef written_at = 0);
    analysis_result template_candidates(
        SyntaxRef n, const owned_array<uint32_t> &group,
        const expression_meaning *meanings, uint32_t count,
        owned_array<overload_candidate> &candidates,
        owned_array<uint32_t> *first_arguments = nullptr,
        owned_array<template_argument> *all = nullptr,
        const owned_array<SyntaxElement> *written = nullptr,
        SyntaxRef written_at = 0,
        const owned_array<template_argument> *specified = nullptr);
    /* a call's template-id naming several function templates: its
       template-arguments as written, checked against each template by
       template_candidates ([temp.over]/1), from template_callee_parts to
       template_call */
    owned_array<SyntaxElement> overloaded_written;
    SyntaxRef overloaded_written_at = 0;
    /* [over.best.ics]: the argument a converted to the type of a parameter;
       user-defined conversions considered unless [over.best.ics]/4 says
       not */
    conversion_sequence
    implicit_conversion_sequence(const expression_meaning &a,
                                 qualified_type parameter, bool user_defined);
    conversion_sequence
    standard_conversion_sequence(const expression_meaning &a,
                                 qualified_type to);
    conversion_sequence reference_sequence(const expression_meaning &a,
                                           qualified_type reference,
                                           bool user_defined);
    conversion_sequence class_sequence(const expression_meaning &a,
                                       qualified_type to, bool user_defined);
    /* [over.match.funcs]/4-5: the object expression to the implicit object
       parameter of the member function f */
    conversion_sequence object_sequence(const expression_meaning &object,
                                        entity_index f) const;
    /* whether class c or a base class of it declares a conversion
       function ([class.conv.fct]/4: "Conversion functions are
       inherited") */
    bool has_conversion_functions(entity_index c) const;
    /* whether the class s has a non-explicit conversion function that may
       yield the class t or one derived from it ([over.match.copy]/1.2) */
    bool has_implicit_conversion_functions(entity_index c) const;
    bool conversion_functions_yield_class(entity_index s, entity_index t,
                                          bool explicit_counts = false) const;
    /* ... a base class of c does */
    bool base_has_conversion_functions(entity_index c) const;
    bool converts_to_non_class(entity_index c, bool explicit_counts) const;
    /* whether class c may bind to a non-const lvalue reference to a non-class
       type: a conversion function returning an lvalue reference
       ([over.match.ref]) */
    bool converts_to_lvalue_reference(entity_index c) const;
    /* the first conversion function class c itself declares (the others
       follow by next_overload), 0 if none */
    entity_index own_conversion_functions(entity_index c) const;
    /* [class.conv.fct]/4, [over.match.conv]/1: the conversion functions of
       class c and of its bases that no conversion function to the same type
       in a nearer class hides, appended to out; false when one is a
       template, which analysis does not follow */
    bool visible_conversion_functions(entity_index c,
                                      owned_array<entity_index> &out) const;
    /* the value a call of the conversion function f gives: its type and
       value category ([expr.call]/14) */
    expression_meaning conversion_function_result(entity_index f) const;
    /* the conversion-function-id that is the declarator-id of the
       member-declaration or function-definition n, 0 if none */
    SyntaxRef conversion_function_id_of(SyntaxRef n) const;
    /* the conversion function of class c the definition declared, 0 if
       none */
    entity_index conversion_function_declared_by(entity_index c,
                                                 SyntaxRef definition);
    /* [over.match.conv]: the conversion function that converts the object
       m (of class type) to the type to, a type that is no class; 0 when
       none does, with ambiguous or unknown (analysis cannot tell) set */
    entity_index conversion_function_for(const expression_meaning &m,
                                         qualified_type to, bool direct,
                                         bool &ambiguous, bool &unknown);
    /* [over.ics.rank]: less than 0 when a is the better conversion
       sequence, more than 0 when b is, 0 when neither is */
    int compare_conversions(const conversion_sequence &a,
                            const conversion_sequence &b) const;
    int compare_standard_conversions(const conversion_sequence &a,
                                     const conversion_sequence &b) const;
    /* [over.match.viable], [over.match.best]: the best viable function of
       the candidates for the arguments (and the object expression of a
       member function call, or none): its index; ill-formed when there is
       no unique one */
    analysis_result
    best_viable_function(SyntaxRef at,
                         const owned_array<overload_candidate> &candidates,
                         const expression_meaning *object,
                         const owned_array<expression_meaning> &arguments,
                         size_t &chosen, bool *none_viable = nullptr);
    /* the functions of the overload set whose first is e
       (entity::next_overload), each once, added to out */
    void overload_set(entity_index e, owned_array<entity_index> &out) const;
    /* [over.call.func]: the token of the name a call's postfix-expression
       is, and the call's callee f made the best viable function of the
       set (its implied object argument's address object, 0 if none) */
    token_index callee_name(SyntaxElement c) const;
    /* [dcl.fct.def.delete]: whether a function-body is = delete ;, the
       function f so defined by the definition n, and (ill-formed) a
       reference to f if it is deleted */
    bool explicit_body(SyntaxElement body, standard_keyword k) const;
    bool deleted_body(SyntaxElement body) const;
    analysis_result deleted_definition(SyntaxRef n, entity_index f);
    analysis_result deleted_use(token_index at, entity_index f);
    analysis_result resolve_call(SyntaxRef n,
                                 const owned_array<SyntaxElement> &arguments,
                                 owned_array<expression_meaning> &early,
                                 const owned_array<entity_index> &set,
                                 uint32_t object, expression_meaning &f);
    /* the token of the name of a function being called whose overload set
       overload resolution chooses from ([over.call.func]); 0 elsewhere,
       where a name of an overload set is [over.over]'s */
    token_index overloaded_callee = 0;
    /* [expr.unary.op]/4: the qualified-id that is the operand of an
       explicit & (not in parentheses), whose non-static member is a
       pointer to member; 0 elsewhere */
    SyntaxRef member_pointer_operand = 0;
    /* [expr.unary.op]/3.2: the glvalue m as the prvalue of its address */
    void address_of_glvalue(token_index at, expression_meaning &m);
    /* [expr.unary.op]/3.1: &C::m, of the non-static member e named at t */
    analysis_result address_of_bit_field(token_index t);
    analysis_result member_pointer_constant(token_index t, entity_index e,
                                            expression_meaning &m);
    /* [expr.call]: the call of the function f (whose type is function_type;
       a member function's object's address object, member the function)
       with the arguments, those analysed so far in early: m */
    analysis_result call_with(SyntaxRef n, expression_meaning &f,
                              qualified_type function_type, uint32_t object,
                              entity_index member,
                              const owned_array<SyntaxElement> &arguments,
                              const owned_array<expression_meaning> &early,
                              expression_meaning &m);
    /* [expr.call]: the call of the function f, named at the token at
       (but not by its name: a literal operator, an allocation function),
       with the analysed arguments, each initialized as its parameter: m */
    analysis_result
    call_function(SyntaxRef n, token_index at, entity_index f,
                  const owned_array<expression_meaning> &arguments,
                  expression_meaning &m);
    /* [over.over]: the expression e analysed for the target type target:
       a name of an overload set (perhaps in parentheses, after &) as the
       function whose type the target's function type is; others as
       expression() does */
    analysis_result target_expression(SyntaxElement e, qualified_type target,
                                      expression_meaning &m);
    /* the first function of the overload set e names that way (its name's
       token at), 0 if e is no such name */
    entity_index named_overload_set(SyntaxElement e, token_index &at) const;
    /* the functions and function templates (function_templates' index plus
       one) a name denotes without arguments ([over.over]/1) */
    struct function_set {
        token_index at = 0;
        owned_array<entity_index> functions;
        owned_array<uint32_t> templates;
    };
    bool function_set_at(token_index at, function_set &out) const;
    bool named_function_set(SyntaxElement e, function_set &out) const;
    /* an operand that is such a name, kept as the overload set it is
       (m.overload_name); false if it is none */
    bool overload_set_operand(SyntaxElement e, expression_meaning &m) const;
    /* [over.over]/1-5: the function of the set whose type is ft (the
       function type of the target): none, one (a function, or a function
       template and the arguments deduced, [temp.deduct.funcaddr]) or
       unknown (the reason, a subclause label, in why) */
    enum class set_selection : uint8_t { none, one, unknown };
    analysis_result select_for_target(SyntaxRef at, const function_set &set,
                                      qualified_type ft, set_selection &how,
                                      entity_index &function,
                                      uint32_t &template_index,
                                      owned_array<template_argument> &deduced,
                                      const char *&why);
    /* the function type FT of the target of a function pointer, function
       reference or function type */
    bool function_target(qualified_type target, qualified_type &ft) const;
    /* [over.ics.scs]: the overload set a converted to the parameter */
    conversion_sequence overload_set_sequence(const expression_meaning &a,
                                              qualified_type parameter);
    /* [temp.deduct.funcaddr]/1: the arguments of a function template
       deduced from the type of the function whose address is taken */
    analysis_result
    deduce_from_overload_set(SyntaxRef at, qualified_type p,
                             const expression_meaning &argument,
                             owned_array<template_argument> &values,
                             owned_array<bool> &set, bool *failed);
    analysis_result deduce_from_function_type(
        SyntaxRef at, uint32_t function_template, qualified_type target,
        owned_array<template_argument> &deduced, bool &failed);
    /* ... the token of that name and the function chosen, which
       named_entity takes it for (0 elsewhere) */
    token_index overload_target_token = 0;
    entity_index overload_target_choice = 0;
    /* ---- [over.oper] operator functions (operator-function.cpp) ---- */
    /* the name of the operator function of op, as an identifier number
       (operator+ for both forms of +, [over.unary]/2) */
    uint32_t operator_name(preprocessing_op_or_punc op);
    /* operator followed by the spelling rest, as an identifier number (no
       identifier is spelt so) */
    uint32_t operator_identifier(const char *rest, size_t length) const;
    /* [class.conv.fct]: the name every conversion function has here (its
       type tells them apart), and the conversion-type-id's type */
    uint32_t conversion_function_name() const;
    analysis_result conversion_type(SyntaxRef id, qualified_type &type);
    /* [class.conv.fct]: a member-declaration whose declarator-id is a
       conversion-function-id, in class c */
    analysis_result conversion_function_member(entity_index c, SyntaxRef n,
                                               SyntaxRef id, uint8_t access,
                                               owned_array<SyntaxRef> &bodies);
    /* [over.match.conv]: the conversion function of m's class that converts
       it to to (0 when none does); the call in m */
    analysis_result conversion_function_call(SyntaxRef at,
                                             expression_meaning &m,
                                             qualified_type to, bool direct,
                                             bool &converted);
    /* the operator-function-id node id as a declarator-id */
    analysis_result operator_function_id(OperatorFunctionId id,
                                         declarator_meaning &m);
    /* [over.oper]/7-10: the rules of the declaration n of operator
       function f */
    analysis_result operator_function_declaration(SyntaxRef n, entity_index f);
    /* [basic.stc.dynamic.allocation]/1, [basic.stc.dynamic.deallocation]/1-3,
       [class.free]/1: the rules of the declaration n of the allocation or
       deallocation function f */
    analysis_result allocation_function_declaration(SyntaxRef n,
                                                    entity_index f);
    /* the name of an allocation or deallocation function */
    uint32_t allocation_function_name(allocation_function kind);
    /* [basic.stc.dynamic]/2: the global allocation and deallocation
       functions implicitly declared, declared once */
    void implicit_allocation_functions();
    bool allocation_functions_declared = false;
    void declare_implicit_allocation_function(allocation_function kind,
                                              qualified_type result,
                                              const qualified_type *parameters,
                                              uint32_t count);
    void implicit_aligned_allocation_functions();
    bool aligned_allocation_declared = false;
    /* the declarations of allocation and deallocation functions analysed;
       when the translation unit spells operator new or operator delete
       more often, a function lookup misses may be the one a call would
       choose (allocation_names_spelled) */
    uint32_t allocation_declarations_analysed = 0;
    uint32_t allocation_names_spelled();
    /* [expr.new]/11, [expr.delete]/9: the functions named kind found in
       class c (0: none; in_class) or else in the global scope;
       class_complete: at c's closing }, whose members (and its bases') are
       all analysed */
    analysis_result allocation_functions(SyntaxRef n, allocation_function kind,
                                         entity_index c,
                                         owned_array<entity_index> &set,
                                         bool &in_class,
                                         bool class_complete = false);

    /* [expr.new] and [expr.delete]: the expressions */
    analysis_result new_expression(NewExpression n, expression_meaning &m);
    analysis_result delete_expression(DeleteExpression n,
                                      expression_meaning &m);
    analysis_result throw_expression(ThrowExpression n, expression_meaning &m);
    analysis_result usual_deallocation(SyntaxRef n, entity_index c, bool global,
                                       bool array, entity_index &function,
                                       bool &sized, bool class_complete = false,
                                       bool cookie = false);
    /* [abi:array-cookies]: the size of the cookie before an array of
       element (of class c, 0 if none) that a new[] or delete[] (::delete[]
       when global) gives, 0 if none */
    analysis_result array_cookie(SyntaxRef n, entity_index c,
                                 qualified_type element, bool global,
                                 uint64_t &cookie);
    /* the type of the new-type-id n ([expr.new]/1), the expression of the
       first bound of its noptr-new-declarator if any (bound) */
    analysis_result new_type_id(NewTypeId n, qualified_type &type,
                                SyntaxElement &bound);
    /* a declaration analysis did not follow may declare an operator
       function (hold_names): what an operator means is not known */
    bool operators_held = false;
    /* [class.copy.assign]/1: f is a copy assignment operator of class c */
    bool copy_assignment_operator(entity_index f, entity_index c) const;
    /* [class.copy.assign]/12: the implicit copy assignment of class c
       assigns its members as the built-in operator does (no member's
       assignment is a user-provided operator=) */
    bool assigned_bitwise(entity_index c);
    /* ---- [over.literal] literal operators (literal-operator.cpp) ---- */
    /* the name of the literal operator of the literal suffix identifier
       suffix, as an identifier number */
    uint32_t literal_operator_name(const char *suffix, size_t length);
    /* the literal-operator-id node id as a declarator-id */
    analysis_result literal_operator_id(LiteralOperatorId id,
                                        declarator_meaning &m);
    /* [over.literal]/2-6: the rules of the declaration n of literal
       operator f */
    analysis_result literal_operator_declaration(SyntaxRef n, entity_index f);
    /* [lex.ext]: the user-defined-literal t as its literal operator's call */
    analysis_result user_defined_literal(token_index t, expression_meaning &m);
    /* [over.match.oper]: the operator op applied to the analysed operands
       (their syntax, for the call), at the token at: handled when an
       operator function is chosen (m its call), not when the operator is
       the built-in one.  postfix: a++ or a--, whose operator function has
       a second parameter of type int */
    analysis_result
    overloaded_operator(SyntaxRef n, token_index at,
                        preprocessing_op_or_punc op, bool postfix,
                        const owned_array<SyntaxElement> &syntax,
                        owned_array<expression_meaning> &operands,
                        bool &handled, expression_meaning &m);
    /* ... of one operand (its syntax s): the operand becomes the operator
       function's call when one is chosen (handled) */
    analysis_result unary_operator_function(SyntaxRef n, token_index at,
                                            preprocessing_op_or_punc op,
                                            bool postfix, SyntaxElement s,
                                            expression_meaning &operand,
                                            bool &handled);
    /* ... of two: m the call when one is chosen (handled) */
    analysis_result binary_operator_function(
        SyntaxRef n, token_index at, preprocessing_op_or_punc op,
        SyntaxElement left_syntax, const expression_meaning &left,
        SyntaxElement right_syntax, const expression_meaning &right,
        bool &handled, expression_meaning &m);
    /* the initialization of type from the analysed expression m */
    analysis_result initialize_with(SyntaxRef at, expression_meaning &m,
                                    qualified_type type, bool direct, bool list,
                                    initialized_value &result);
    /* the same where failure is no error: ok false, what it issued taken
       back ([temp.deduct]/8) */
    analysis_result try_initialization(SyntaxRef n, expression_meaning &a,
                                       qualified_type type, bool direct,
                                       initialized_value &made, bool &ok);
    /* a declarator and its initializer (none if !initialized), as in an
       init-declarator or a condition; supplied, if not null, is an
       initializer analysed already ("= E", [stmt.ranged]/1; init none) */
    analysis_result
    declare_with_initializer(SyntaxRef declaration, SyntaxElement d,
                             SyntaxElement init, bool initialized,
                             const specified &s, bool definition,
                             const expression_meaning *supplied = nullptr);

    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
    diagnostic_consumer &diagnostics;
    const SyntaxContext syntax_cx;
    SyntaxScope syntax_scope{syntax_cx};
};

/* ---- the Itanium C++ ABI (abi/itanium/) ---- */

/* [abi:class-types]: the layout of class c at its closing }, from its
   bases (class_bases) and non-static data members (class_members) in
   declaration order: each one's entity::offset (a bit-field's bit_offset
   too), and the class's size, data_size and alignment.  nullptr, or the
   subclause whose rules a part of the layout needs that are not written
   yet: the class then has no layout, and stays incomplete */
const char *itanium_class_layout(semantic_analysis &analysis, entity_index c);

/* [abi:mangling]: the symbol of entity e, a function or a variable,
   appended to out: its mangled name, or its identifier where the ABI does
   not mangle it; false if the name needs rules not written yet (out may
   have a part of it).  A constructor's or destructor's variant
   ([abi:mangling-special-ctor-dtor]): 1 the complete object's, 2 the base
   object's */
bool itanium_symbol_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index e, vbuffer *out,
                         int variant = 2);
/* [abi:mangling-special-vtables]: the symbol of class c's virtual table
   (_ZTV <type>) */
bool itanium_vtable_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index c, vbuffer *out);
/* its VTT's (_ZTT <type>, [abi:vtable-ctor-vtt]) and the construction
   virtual table of its base at offset (_ZTC <type> <offset> _ <base>) */
bool itanium_vtt_name(const semantic_analysis &analysis, const preprocessor &pp,
                      entity_index c, vbuffer *out);
bool itanium_construction_vtable_name(const semantic_analysis &analysis,
                                      const preprocessor &pp, entity_index c,
                                      uint64_t offset, entity_index base,
                                      vbuffer *out);

} // namespace cplusplus

#endif
