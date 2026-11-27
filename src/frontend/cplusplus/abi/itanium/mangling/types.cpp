/*
 * mangling/types.cpp: [abi:mangling-type] <type> and its alternatives (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *     <prefix> ::= <template-param>
 *     <prefix> ::= <prefix> <unqualified-name>
 *
 * typename Q::name ([temp.res]/3) for a dependent Q: N, the template-param
 * Q is made of and each name after it, E.  [abi:mangling-compression]:
 * each prefix is a candidate, the same one as the type it names (Q::a in
 * Q::a::b, as clang 23); the type as a whole is the one type() adds.
 */
bool itanium_mangler::nested_name_of_dependent_member(entity_index e)
{
    enc.write(terminal::nested_name);
    if (!prefix_of_dependent_member(
            static_cast<type_index>(analysis.entity_table[e].value)))
        return false;
    if (!source_name(analysis.entity_table[e].name))
        return false;
    enc.write(terminal::end);
    return true;
}

bool itanium_mangler::prefix_of_dependent_member(type_index q)
{
    qualified_type t = qualified_type::make(q, cv_none);
    const struct type &r = record(t);
    if (r.kind == type_kind::template_parameter)
        return type(t);
    if (r.kind != type_kind::dependent)
        return false;

    const entity &prefix = analysis.entity_table[r.entity];
    /* typename __remove_cvref(T)::name: the transformation's own <type>,
       as clang writes it */
    if (prefix.kind == entity_kind::type_transformation)
        return type(t);
    /* typename decltype(e)::name: the <decltype>, [abi:mangle.name] */
    if (prefix.kind == entity_kind::dependent_written)
        return type(t);
    if (prefix.kind != entity_kind::template_specialization &&
        prefix.kind != entity_kind::dependent_member)
        return false;
    if (substitution(t))
        return true;

    if (prefix.kind == entity_kind::template_specialization) {
        entity_index c = record(t).entity;
        if (!prefix.enclosing || is_std(prefix.enclosing)) {
            if (!class_enum_type(t))
                return false;
        } else if (analysis.entity_table[prefix.enclosing].kind !=
                       entity_kind::namespace_entity &&
                   analysis.entity_table[prefix.enclosing].kind !=
                       entity_kind::class_type) {
            /* (a member class template of a class: the class's <prefix>
               the same way, typename sp<int>::get<Y>::type) */
            return false;
        } else if (prefix.instantiation &&
                   substitution(analysis.specialized_template(c))) {
            /* ns::Q<args> with ns::Q seen: its <substitution>, then the
               arguments */
            if (!template_args(c))
                return false;
        } else {
            /* ns::Q<args> in the enclosing <nested-name>: the namespace's
               <prefix>, Q (a candidate as the template-prefix), the
               arguments */
            if (!this->prefix(prefix.enclosing) || !unqualified_name(c) ||
                !class_template_args(c))
                return false;
        }
    } else {
        if (!prefix_of_dependent_member(static_cast<type_index>(prefix.value)))
            return false;
        if (!source_name(prefix.name))
            return false;
    }
    add_candidate(t);
    return true;
}

/*
 * [abi:mangling.named]:
 *
 *     <class-enum-type> ::= <name>
 *
 * A class or an enumeration of namespace or class scope: its name, as an
 * entity's.  One of a block scope: its <local-name>'s scope first
 * ([abi:mangling-scope]).
 */
bool itanium_mangler::class_enum_type(qualified_type t)
{
    entity_index c = record(t).entity;
    const entities &table = analysis.entity_table;
    const entity &x = table[c];
    if (x.kind != entity_kind::class_type &&
        x.kind != entity_kind::enumeration &&
        x.kind != entity_kind::template_specialization)
        return false;
    entity_index local_class = 0;
    entity_index local = local_function(c, &local_class);
    /* [abi:mangling-compression]: a template's name seen before, with its
       prefix, is a <substitution> before the arguments */
    if (x.instantiation && x.enclosing && !is_std(x.enclosing) && !local &&
        !analysis.template_template_parameter(
            analysis.specialized_template(c))) {
        /* [abi:mangle.name]:

               <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E

           with the <template-prefix> a <substitution> */
        itanium_encoder::mark before = enc.position();
        enc.write(terminal::nested_name);
        if (substitution(analysis.specialized_template(c))) {
            if (!template_args(c))
                return false;
            enc.write(terminal::end);
            return true;
        }
        enc.rewind(before);
    } else if (x.instantiation &&
               substitution(analysis.specialized_template(c))) {
        return template_args(c);
    }
    if (x.instantiation && analysis.template_template_parameter(
                               analysis.specialized_template(c))) {
        /* [abi:mangling-type]: a template template-parameter's
           specialization,

               <type> ::= <template-template-param> <template-args>

           [abi:mangle.template-param]:

               <template-template-param> ::= <template-param>

           the parameter a candidate */
        entity_index t2 = analysis.specialized_template(c);
        template_param(table[t2].value);
        candidate y;
        y.scope = t2;
        enc.add_candidate(y);
        return template_args(c);
    }
    if (x.instantiation && is_std(x.enclosing) && !in_inline_namespace(c)) {
        /* [abi:mangling-compression]:
         *
         *     <substitution> ::= Sa
         *     <substitution> ::= Sb
         *
         * ::std::allocator and ::std::basic_string, not candidates
         * themselves; the abbreviations of whole specializations (Ss, Si,
         * So, Sd) are not made, so those are not mangled */
        size_t length = 0;
        const char *identifier = spelling(x.name, &length);
        if (length == 9 && !memcmp(identifier, "allocator", 9)) {
            enc.write(terminal::std_allocator);
            return template_args(c);
        }
        /* <substitution> ::= Ss # std::basic_string<char,
                                    std::char_traits<char>, std::allocator<char> >
                          ::= Si # std::basic_istream<char, std::char_traits<char> >
                          ::= So # std::basic_ostream<char, std::char_traits<char> >
                          ::= Sd # std::basic_iostream<char, std::char_traits<char> >
           the whole specialization (std_substitution); another
           basic_string is Sb and its arguments, another stream its
           name */
        terminal whole = std_substitution(c);
        if (whole != terminal::end) {
            enc.write(whole);
            return true;
        }
        if (length == 12 && !memcmp(identifier, "basic_string", 12)) {
            enc.write(terminal::std_basic_string);
            return template_args(c);
        }
    }
    if (x.enclosing == 0 || is_std(x.enclosing))
        return unscoped_name(c) && class_template_args(c);
    if (local) {
        /* [abi:mangling-scope]: a local class, or one nested in it */
        if (!local_name_scope(local))
            return false;
        if (c == local_class) {
            if (!unqualified_name(c, true))
                return false;
            local_class_discriminator(local_class, local);
            return true;
        }
    } else if (table[x.enclosing].kind != entity_kind::class_type &&
               table[x.enclosing].kind != entity_kind::namespace_entity)
        return false;
    /* <nested-name>: the prefix and the name; the type as a whole is the
       candidate type() adds */
    enc.write(terminal::nested_name);
    if (!prefix(x.enclosing))
        return false;
    if (!unqualified_name(c))
        return false;
    if (!class_template_args(c))
        return false;
    enc.write(terminal::end);
    if (local)
        local_class_discriminator(local_class, local);
    return true;
}

/*
 * [abi:mangle.function-type]:
 *
 *     <bare-function-type> ::= <signature type>+
 *
 * A non-template function's name encodes its parameter types and not its
 * return type; an empty parameter list is the single type v, and an
 * ellipsis is z after the parameters ([abi:mangling-builtin]'s z).  The
 * parameter types are those of the function's type, top-level
 * cv-qualifiers already gone ([dcl.fct]/5).
 */
bool itanium_mangler::bare_function_type(qualified_type t)
{
    struct part_scope {
        const char *&part;
        const char *saved;
        part_scope(const char *&p, const char *now) : part(p), saved(p)
        {
            part = now;
        }
        ~part_scope()
        {
            part = saved;
        }
    } in_types(mangling_part, "the parameter or return types");
    const struct type &f = record(t);
    if (f.kind != type_kind::function)
        return false;
    const qualified_type *parameters =
        analysis.type_table.parameters(t.unqualified());
    for (uint32_t i = 0; i < f.parameter_count; i++)
        if (!type(parameters[i]))
            return false;
    if (f.variadic)
        enc.write(terminal::ellipsis);
    else if (!f.parameter_count)
        enc.builtin_type(fundamental_type::void_);
    return true;
}

/*
 * [abi:mangling-type]:
 *
 *     <type> ::= <builtin-type>
 *            ::= <qualified-type>
 *            ::= <function-type>
 *            ::= <class-enum-type>
 *            ::= <array-type>
 *            ::= <pointer-to-member-type>
 *            ::= <template-param>
 *            ::= <template-template-param> <template-args>
 *            ::= <decltype>
 *            ::= P <type>
 *            ::= R <type>
 *            ::= O <type>
 *            ::= C <type>
 *            ::= <substitution>
 *
 * P, R and O encode pointers and the two kinds of reference. C encodes
 * a complex pair, and Dp wraps a pack expansion. Substitutable types are
 * entered after their components, in the order they are encoded
 * ([abi:mangling-compression]).  First the forms that are no candidate
 * (a builtin type) or that are their own (the vendor's va_list types),
 * then <substitution>, then the alternatives that are candidates
 * (type_alternative).
 */
bool itanium_mangler::type(qualified_type t, entity_index member_class)
{
    if (analysis.substituted_pack_type && !t.cv() &&
        t.unqualified() == analysis.substituted_pack_type) {
        /* [impl.clang] a substitution candidate itself */
        if (substitution(t, member_class))
            return true;
        enc.write(terminal::substituted_pack);
        add_candidate(t);
        return true;
    }
    const struct type &r = record(t);
    /* [impl.clang] an alias template's specialization kept for its
       dependent arguments ([temp.alias]/4) whose type-id is not dependent
       (void_t<T>): that type.  In a dependent value (And<void_t<T>>::value)
       clang has the value too, which the analysis does not: not mangled
       there */
    if (r.kind == type_kind::dependent &&
        analysis.entity_table[r.entity].alias_expansion.unqualified()) {
        if (dependent_value_depth && !alias_value_trial) {
            alias_value_refused = true;
            return refuse("an alias template's specialization of dependent "
                          "arguments and a non-dependent type in a "
                          "value-dependent template argument (clang "
                          "evaluates it)");
        }
        qualified_type x = analysis.entity_table[r.entity].alias_expansion;
        return type(qualified_type::make(x.unqualified(), x.cv() | t.cv()),
                    member_class);
    }
    if (r.kind == type_kind::fundamental && !t.cv() &&
        (r.fundamental == fundamental_type::builtin_va_list ||
         r.fundamental == fundamental_type::builtin_va_list_tag))
        return va_list_type(t);
    /* [abi:mangling-type] <type> ::= <builtin-type> */
    if (r.kind == type_kind::fundamental && !t.cv())
        return builtin_type(r.fundamental);
    if (r.kind == type_kind::placeholder && !t.cv())
        return placeholder_type(r);
    /* [abi:mangling-type] <type> ::= <substitution> */
    if (substitution(t, member_class))
        return true;
    bool qualified = t.cv() && r.kind != type_kind::array;
    if (!qualified && r.kind == type_kind::dependent &&
        analysis.entity_table[r.entity].kind == entity_kind::dependent_written)
        return type_of_written(t, member_class);
    if (!qualified && r.kind == type_kind::template_parameter &&
        leveled_template) {
        entity_index own = analysis.constraint_own_type_parameter(
            leveled_template, analysis.entity_table[r.entity].value);
        if (!own || t.cv())
            return refuse("a template parameter in a constraint's type");
        return template_param_of_constraint(leveled_template, own);
    }
    if (!type_alternative(t, member_class))
        return refuse(type_kind_description(r.kind));
    add_candidate(t, member_class);
    return true;
}

/* (what kind of type the mangler has no rule for, for refuse) */
const char *itanium_mangler::type_kind_description(type_kind kind)
{
    static const struct {
        type_kind kind;
        const char *text;
    } kinds[] = {
        {type_kind::class_type, "a class type"},
        {type_kind::enumeration, "an enumeration type"},
        {type_kind::dependent, "a dependent type"},
        {type_kind::placeholder, "a placeholder type"},
        {type_kind::function, "a function type"},
        {type_kind::pointer_to_member, "a pointer-to-member type"},
        {type_kind::pack_expansion, "a pack expansion type"},
    };
    const char *what = "a type of a kind it has no rule for";
    for (const auto &k : kinds)
        if (kind == k.kind)
            what = k.text;
    return what;
}

/* the <type> alternatives that are substitution candidates, the
   candidate added by type(); false for a type with no rule */
bool itanium_mangler::type_alternative(qualified_type t,
                                       entity_index member_class)
{
    const struct type &r = record(t);
    /* [abi:mangling-type] <type> ::= <qualified-type> */
    if (t.cv() && r.kind != type_kind::array)
        return cv_qualified_type(t);
    switch (r.kind) {
    /* [abi:mangling-type] <type> ::= P <type> */
    case type_kind::pointer:
        enc.write(terminal::pointer);
        return type(r.operand);
    /* [abi:mangling-type] <type> ::= R <type> */
    case type_kind::lvalue_reference:
        enc.write(terminal::lvalue_reference);
        return type(r.operand);
    /* [abi:mangling-type] <type> ::= O <type> */
    case type_kind::rvalue_reference:
        enc.write(terminal::rvalue_reference);
        return type(r.operand);
    /* [abi:mangling-type] <type> ::= C <type>  # complex pair (C99) */
    case type_kind::complex:
        enc.write(terminal::complex_pair);
        return type(r.operand);
    case type_kind::pointer_to_member:
        return pointer_to_member_type(t);
    case type_kind::block_pointer:
        /* [abi:mangling-type]'s vendor extended type qualifier, as clang
           mangles a block pointer ([ext:clang_blocks]) */
        enc.write(terminal::vendor_qualifier);
        enc.source_name("block_pointer");
        return type(r.operand);
    case type_kind::atomic:
        /* [abi:mangling-type]'s vendor extended type qualifier, as clang
           mangles _Atomic(T) ([ext:clang_atomic]) */
        enc.write(terminal::vendor_qualifier);
        enc.source_name("_Atomic");
        return type(r.operand);
    case type_kind::bit_int:
        return bit_int_type(r);
    case type_kind::pack_expansion:
        /* [abi:mangle.function-type]: a pack expansion,

               <type> ::= Dp <type>
        */
        enc.write(terminal::pack_expansion);
        return type(r.operand);
    case type_kind::function:
        /* [abi:mangling-type] <type> ::= <function-type> */
        return function_type(t);
    case type_kind::array:
        /* [abi:mangling-type] <type> ::= <array-type> */
        return array_type(t);
    case type_kind::class_type:
    case type_kind::enumeration:
        /* [abi:mangling-type] <type> ::= <class-enum-type> */
        return class_enum_type(t);
    case type_kind::dependent:
        if (analysis.entity_table[r.entity].kind ==
            entity_kind::dependent_member)
            return nested_name_of_dependent_member(r.entity);
        if (analysis.entity_table[r.entity].kind ==
            entity_kind::type_transformation)
            return type_transformation(r);
        return class_enum_type(t);
    case type_kind::template_parameter:
        /* [abi:mangling-type] <type> ::= <template-param> */
        template_param(analysis.entity_table[r.entity].value);
        return true;
    default:
        if (is_vector_kind(r.kind))
            return vector_type(r);
        return false;
    }
}

/* [impl.clang] the vendor's va_list types, each a <substitution>
   candidate as a <class-enum-type>: AAPCS64's std::__va_list (C++ ABI for
   the Arm 64-bit Architecture, 4.2.2), and x86-64 System V's struct
   __va_list_tag, a <source-name> (a va_list parameter is
   P13__va_list_tag) */
bool itanium_mangler::va_list_type(qualified_type t)
{
    if (substitution(t))
        return true;
    if (record(t).fundamental == fundamental_type::builtin_va_list) {
        enc.write(terminal::std_prefix);
        enc.source_name("__va_list");
    } else
        enc.source_name("__va_list_tag");
    add_candidate(t);
    return true;
}

/* [abi:mangling-builtin]: a template's declared return type auto or
   decltype(auto), a builtin type:

       <builtin-type> ::= Da
                      ::= Dc

   Constrained by a concept C ([dcl.spec.auto]/1), Dk (DK of
   decltype(auto)) then C's <name>, as clang (the ABI's <type-constraint>,
   later than the version fetched) */
bool itanium_mangler::placeholder_type(const struct type &r)
{
    static const terminal codes[2][2] = {
        {terminal::auto_type, terminal::decltype_auto_type},
        {terminal::constrained_auto_type,
         terminal::constrained_decltype_auto_type}};
    entity_index constraint = r.entity >> 2;
    enc.write(codes[constraint != 0][(r.entity & 3) == 2]);
    return !constraint || name(constraint);
}

/* [abi:mangle.pointer-to-member-type]: "Pointer-to-member types encode the
   class and member types":

       <pointer-to-member-type> ::= M <class type> <member type>

   a member function type is a candidate of its class (member_class) */
bool itanium_mangler::pointer_to_member_type(qualified_type t)
{
    const struct type &r = record(t);
    enc.write(terminal::pointer_to_member);
    type_index cls = analysis.entity_table[r.entity].type.unqualified();
    bool class_ok = false;
    if (cls) {
        class_ok = type(qualified_type::make(cls, cv_none));
    } else if (encoding_template) {
        /* (a class that is a template parameter: its candidate, or its
           <template-param>) */
        bool subst = false;
        uint64_t param_pos = analysis.entity_table[r.entity].value;
        for (size_t i = 0; i < enc.candidate_count(); i++) {
            const candidate &k = enc.candidate_at(i);
            if (k.type.unqualified() && !k.expansion && !k.type.cv() &&
                record(k.type).kind == type_kind::template_parameter &&
                analysis.entity_table[record(k.type).entity].value ==
                    param_pos) {
                type(k.type);
                subst = true;
                break;
            }
        }
        if (!subst)
            template_param(param_pos);
        class_ok = true;
    }
    entity_index owner = 0;
    if (record(r.operand).kind == type_kind::function)
        owner = r.entity;
    return class_ok && type(r.operand, owner);
}

/* [abi:mangling-builtin]:

       <builtin-type> ::= DB <number> _
                      ::= DB <instantiation-dependent expression> _

   A dependent width here is a non-type template parameter.  Clang
   substitutes repeated _BitInt types, so they stay in the substitution
   table for interoperation. */
bool itanium_mangler::bit_int_type(const struct type &r)
{
    if (r.fundamental == fundamental_type::unsigned_int)
        enc.write(terminal::unsigned_bit_int);
    else
        enc.write(terminal::bit_int);
    if (r.entity)
        template_param(analysis.entity_table[r.entity].value);
    else
        enc.number(r.bound);
    enc.write(terminal::underscore);
    return true;
}

/* an ext_vector_type ([ext:clang_ext_vector_type]) as clang mangles it:
   Dv, the element count (a dependent one: its template parameter), _ and
   the element type */
bool itanium_mangler::vector_type(const struct type &r)
{
    enc.write(terminal::vector);
    if (r.entity)
        template_param(analysis.entity_table[r.entity].value);
    else
        enc.number(r.bound);
    enc.write(terminal::underscore);
    return type(r.operand);
}

/* [abi:mangling-builtin]'s vendor extended type, as clang mangles a type
   transformation of a dependent operand:

       <builtin-type> ::= u <source-name> [<template-args>]

   the keyword's name, its operand the one template-arg */
bool itanium_mangler::type_transformation(const struct type &r)
{
    uint64_t operand = analysis.entity_table[r.entity].value;
    enc.write(terminal::vendor_type);
    bool done = source_name(analysis.entity_table[r.entity].name);
    if (done) {
        enc.write(terminal::template_args);
        done = type(
            qualified_type::make(static_cast<type_index>(operand & 0xffffffffu),
                                 static_cast<uint32_t>(operand >> 32)));
        enc.write(terminal::end);
    }
    return done;
}

/*
 * A dependent type known by how it is written (entity_kind::
 * dependent_written): typename Q::template X<args>, __make_integer_seq's
 * specialization, or a decltype, [abi:mangle.decltype]:
 *
 *     <decltype> ::= Dt <expression> E
 *                ::= DT <expression> E
 *
 * Dt selects the declared type of an unparenthesized identifier or member
 * access. DT selects the expression form, including a parenthesized
 * identifier or member access.  The candidate its own.
 */
bool itanium_mangler::type_of_written(qualified_type t,
                                      entity_index member_class)
{
    entity_index w = record(t).entity;
    SyntaxRef n = SyntaxRef(
        node_index(static_cast<uint32_t>(analysis.entity_table[w].value)));
    if (n.is(syntax_kind::typename_specifier))
        return typename_of_written(t, n);
    if (n.is(syntax_kind::simple_template_id))
        return make_integer_seq_of_written(t);
    if (n.size() != 4 || !encoding_template)
        return false;
    SyntaxElement operand = n[2];
    bool member_access =
        operand.is(syntax_kind::postfix_expression) && operand.size() == 3 &&
        (punctuator(operand[1], preprocessing_op_or_punc::DOT) ||
         punctuator(operand[1], preprocessing_op_or_punc::ARROW));
    if (operand.is_token() || member_access)
        enc.write(terminal::decltype_entity);
    else
        enc.write(terminal::decltype_expression);
    uint32_t saved_first = written_first, saved_count = written_count;
    written_first = analysis.entity_table[w].first_member;
    written_count = analysis.entity_table[w].member_count;
    bool done = expression_of_written(operand);
    pack_element = UINT32_MAX;
    written_first = saved_first;
    written_count = saved_count;
    enc.write(terminal::end);
    if (!done)
        return refuse(type_kind_description(type_kind::dependent));
    add_candidate(t, member_class);
    return true;
}

/* typename Q::template X<args>: N <Q's prefix> X I <args> E E */
bool itanium_mangler::typename_of_written(qualified_type t, SyntaxRef n)
{
    entity_index w = record(t).entity;
    uint32_t wf = analysis.entity_table[w].first_member;
    uint32_t wc = analysis.entity_table[w].member_count;
    uint32_t first = wc;
    for (uint32_t i = 0; i < wc; i++)
        if (analysis.written_bindings[wf + i].cast == n.index().index()) {
            first = i;
            break;
        }
    SyntaxElement id = n[n.size() - 1];
    if (first == wc || !id[0].is_identifier())
        return false;
    const auto &q = analysis.written_bindings[wf + first];
    enc.write(terminal::nested_name);
    if (!prefix_of_dependent_member(q.type.unqualified()) ||
        !source_name(analysis.token_identifier(id[0].token())))
        return false;
    enc.write(terminal::template_args);
    for (uint32_t i = first + 1; i < wc; i++) {
        const auto &a = analysis.written_bindings[wf + i];
        if (a.cast != n.index().index())
            break;
        if (!type(a.type))
            return false;
    }
    enc.write(terminal::end);
    enc.write(terminal::end);
    add_candidate(t);
    return true;
}

/* [abi:mangling-type]: a template-id's <class-enum-type> <template-args>;
   [impl.clang] __make_integer_seq<B, T, N> of a dependent length keeps the
   builtin template's name (builtin_make_integer_seq's bindings) */
bool itanium_mangler::make_integer_seq_of_written(qualified_type t)
{
    const entity &w = analysis.entity_table[record(t).entity];
    const semantic_analysis::written_binding *parts[3] = {};
    for (uint32_t i = 0; i < w.member_count; i++) {
        const auto &b = analysis.written_bindings[w.first_member + i];
        if (b.cast == w.value && !b.identifier && b.element < 3)
            parts[b.element] = &b;
    }
    if (!parts[0] || !parts[1] || !parts[2] || !parts[0]->specialization)
        return false;
    /* (the builtin template's name a candidate as a template prefix is; no
       entity: a scope of its own) */
    if (!substitution(UINT32_MAX)) {
        enc.source_name("__make_integer_seq");
        candidate builtin;
        builtin.scope = UINT32_MAX;
        enc.add_candidate(builtin);
    }
    enc.write(terminal::template_args);
    if (!template_template_arg(parts[0]->specialization, 0) ||
        !type(parts[1]->type))
        return false;
    enc.write(terminal::template_arg_expression);
    template_param(parts[2]->pack_position - 1);
    enc.write(terminal::end);
    enc.write(terminal::end);
    add_candidate(t);
    return true;
}

/*
 * [abi:mangling-builtin]:
 *
 *     <builtin-type> ::= v
 *                    ::= w
 *                    ::= b
 *                    ::= c
 *                    ::= a
 *                    ::= h
 *                    ::= s
 *                    ::= t
 *                    ::= i
 *                    ::= j
 *                    ::= l
 *                    ::= m
 *                    ::= x
 *                    ::= y
 *                    ::= f
 *                    ::= d
 *                    ::= e
 *                    ::= Di
 *                    ::= Ds
 *                    ::= Du
 *                    ::= Dn
 *                    ::= n
 *                    ::= o
 *
 * In the order of the section's table: void, wchar_t, bool, char, signed
 * char, unsigned char, short, unsigned short, int, unsigned int, long,
 * unsigned long, long long, unsigned long long, float, double, long double,
 * char32_t, char16_t, char8_t, std::nullptr_t, __int128, unsigned
 * __int128.  _Float16 is DF16_ (the
 * DF <number> _ form below); _BitInt(N) is type_kind::bit_int's (type()).
 *
 *     <builtin-type> ::= DF <number> _
 */
bool itanium_mangler::builtin_type(fundamental_type t)
{
    /* [impl.clang] __bf16 on arm64: AAPCS64's vendor type, u <source-name>
       ([abi:mangling-builtin]'s vendor extended type) */
    if (t == fundamental_type::bfloat16 && !analysis.x86_64_target()) {
        enc.write(terminal::vendor_type);
        enc.source_name("__bf16");
        return true;
    }
    return enc.builtin_type(t);
}

/*
 * [abi:mangle.qualified-type]:
 *
 *     <qualified-type> ::= <qualifiers> <type>
 *     <qualifiers> ::= <extended-qualifier>* <CV-qualifiers>
 *     <CV-qualifiers> ::= [r] [V] [K]
 *
 * With no extended qualifier (no vendor's).  The section orders the
 * qualifiers so K is closest to the type, and makes both the qualified
 * type and the type without its qualifiers substitution candidates, not a
 * type with only some of them.
 */
bool itanium_mangler::cv_qualified_type(qualified_type t)
{
    cv_qualifiers(t.cv());
    return type(qualified_type::make(t.unqualified(), cv_none));
}

/* [abi:mangle.qualified-type]:

       <CV-qualifiers> ::= [r] [V] [K]

   restrict, volatile, const, in that order */
void itanium_mangler::cv_qualifiers(unsigned cv)
{
    if (cv & cv_restrict)
        enc.write(terminal::restrict_qualifier);
    if (cv & cv_volatile)
        enc.write(terminal::volatile_qualifier);
    if (cv & cv_const)
        enc.write(terminal::const_qualifier);
}

/* [abi:mangle.qualified-type]:

       <ref-qualifier> ::= R  # &
                       ::= O  # &&

   ref 1 &, 2 &&, 0 none */
void itanium_mangler::ref_qualifier(int ref)
{
    if (ref == 1)
        enc.write(terminal::lvalue_ref_qualifier);
    else if (ref == 2)
        enc.write(terminal::rvalue_ref_qualifier);
}

/*
 * [abi:mangle.function-type]:
 *
 *     <function-type> ::= [<CV-qualifiers>] [<exception-spec>] [Dx] F [Y] <bare-function-type> [<ref-qualifier>] E
 *
 * A function type starts the signature with its return type. Its CV and
 * reference qualifiers belong to the same substitution candidate.
 * The supported exception specification is Do (non-throwing).
 */
bool itanium_mangler::function_type(qualified_type t)
{
    const struct type &function = record(t);
    cv_qualifiers(function.function_cv & (cv_volatile | cv_const));
    /* <exception-spec> ::= Do  # non-throwing */
    if (function.non_throwing)
        enc.write(terminal::non_throwing);

    enc.write(terminal::function_type);
    if (!type(function.operand))
        return false;
    if (!bare_function_type(t))
        return false;
    ref_qualifier(function.function_ref);
    enc.write(terminal::end);
    return true;
}

/*
 * [abi:mangle.array-type]:
 *
 *     <array-type> ::= A [<array bound number>] _ <element type>
 *                  ::= A <instantiation-dependent array bound expression> _ <element type>
 *
 * Omit an unknown bound. A dependent bound here is a template parameter.
 * Array CV-qualifiers go on the element type ([basic.type.qualifier]/3).
 */
bool itanium_mangler::array_type(qualified_type t)
{
    const struct type &array = record(t);
    enc.write(terminal::array_type);
    /* [abi:mangle.array-type]: an instantiation-dependent bound,

           <array-type> ::= A <instantiation-dependent array bound expression> _ <element type>

       a non-type template parameter's name, or an expression of one (its
       structure) */
    if (array.entity && array.bound) {
        if (!expression_of_structure(static_cast<uint32_t>(array.bound)))
            return false;
    } else if (array.entity)
        template_param(analysis.entity_table[array.entity].value);
    else if (array.bound_known)
        enc.number(array.bound);
    enc.write(terminal::underscore);

    qualified_type element = array.operand;
    return type(
        qualified_type::make(element.unqualified(), element.cv() | t.cv()));
}

} // namespace cplusplus
