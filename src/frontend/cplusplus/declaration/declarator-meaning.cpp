/*
 * declarator-meaning.cpp: [dcl.meaning] Meaning of declarators, with the
 * subclauses too small for a file of their own: [dcl.ptr], [dcl.ref],
 * [dcl.array] ([dcl.fct] is in parameter-declaration-clause.cpp).  The grammar
 * is in declarator.cpp.
 *
 * [dcl.meaning]/3: "Thus, a declaration of a particular identifier has the
 * form T D where T is of the form attribute-specifier-seq_opt
 * decl-specifier-seq and D is a declarator."  Each form of D is a
 * subclause's "In a declaration T D where D has the form ...": the type D
 * gives its identifier is the one D1 gives it when T has that form's type
 * constructor applied.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [basic.type.qualifier]/3: "Cv-qualifiers applied to an array type attach
 * to the underlying element type"; [dcl.ref]/1: "Cv-qualified references
 * are ill-formed except when the cv-qualifiers are introduced through the
 * use of a typedef-name or decltype-specifier, in which case the
 * cv-qualifiers are ignored"; [dcl.fct]/7: on top of a function type "the
 * cv-qualifiers are ignored".
 */
qualified_type semantic_analysis::add_cv(qualified_type t, uint32_t cv)
{
    if (!cv)
        return t;
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::function:
        return t;
    case type_kind::array:
        return qualified_type::make(
            array_with_element(record, add_cv(record.operand, cv)), cv_none);
    default:
        return qualified_type::make(t.unqualified(), t.cv() | cv);
    }
}

/*
 * A qualifier after a ptr-operator's * ([dcl.ptr]/1: "the cv-qualifiers
 * apply to the pointer"): const, volatile, [ext:gnu_restrict]'s __restrict
 * (C's restrict, [C11 6.7.3]/2: on a pointer type only) and
 * [ext:clang_nullability]'s keywords, which do not change the type.
 */
analysis_result semantic_analysis::pointer_qualifier(SyntaxRef op,
                                                     token_index t,
                                                     qualified_type &type)
{
    switch (p.extension_keyword_of(tokens[t])) {
    case extension_keyword::nullability:
        return analysis_result::well_formed;
    case extension_keyword::restrict:
        if (type_table[type.unqualified()].kind != type_kind::pointer)
            return not_supported(op, "ext.gnu");
        type = add_cv(type, cv_restrict);
        return analysis_result::well_formed;
    case extension_keyword::atomic: {
        /* [ext:clang_atomic] T *_Atomic: the pointer made atomic, as
           _Atomic(T *) (C23 6.7.4.1/3); other cv-qualifiers stay on the
           atomic type.  A repeated qualifier is C's 6.7.3/5 (no effect). */
        type_kind k = type_table[type.unqualified()].kind;
        if (k == type_kind::atomic)
            return analysis_result::well_formed;
        if (k != type_kind::pointer)
            return not_supported(op, "ext.clang");
        qualified_type value =
            qualified_type::make(type.unqualified(), cv_none);
        VTRY(r, atomic_value_check(op, value));
        type = qualified_type::make(type_table.atomic_of(value), type.cv());
        return analysis_result::well_formed;
    }
    default:
        break;
    }
    standard_keyword kw = p.keyword_of(tokens[t]);
    if (kw == standard_keyword::const_)
        type = add_cv(type, cv_const);
    else if (kw == standard_keyword::volatile_)
        type = add_cv(type, cv_volatile);
    else
        return not_supported(op, "ext");
    return analysis_result::well_formed;
}

type_index semantic_analysis::array_with_element(const struct type &array,
                                                 qualified_type element)
{
    if (array.bound_known)
        return type_table.array_of(element, array.bound);
    return type_table.array_of_unknown_bound_of(element);
}

/* [dcl.ref]/6: "If a typedef-name ... denotes a type TR that is a
   reference to a type T, an attempt to create the type lvalue reference to
   cv TR creates the type lvalue reference to T, while an attempt to create
   the type rvalue reference to cv TR creates the type TR." */
qualified_type semantic_analysis::reference_to(qualified_type type, bool rvalue)
{
    const struct type referred = type_table[type.unqualified()];
    if (referred.kind == type_kind::lvalue_reference ||
        referred.kind == type_kind::rvalue_reference) {
        if (!rvalue)
            return qualified_type::make(
                type_table.lvalue_reference_to(referred.operand), cv_none);
        return type;
    }
    if (rvalue)
        return qualified_type::make(type_table.rvalue_reference_to(type),
                                    cv_none);
    return qualified_type::make(type_table.lvalue_reference_to(type), cv_none);
}

/*
 * [dcl.ref]/1: "In a declaration T D where D has either of the forms
 *     & attribute-specifier-seq_opt D1
 *     && attribute-specifier-seq_opt D1
 * ... the type of the identifier of D is derived-declarator-type-list
 * reference to T."
 */
analysis_result semantic_analysis::reference(SyntaxRef at, bool rvalue,
                                             qualified_type &type,
                                             declarator_meaning &m)
{
    const struct type referred = type_table[type.unqualified()];
    if (referred.kind == type_kind::lvalue_reference ||
        referred.kind == type_kind::rvalue_reference) {
        /* [dcl.ref]/5: "There shall be no references to references" */
        if (m.reference_made)
            return ill_formed(at, "there shall be no references to references",
                              "dcl.ref");
    }
    /* [dcl.ref]/1: "A declarator that specifies the type "reference to cv
       void" is ill-formed." (in a substitution, a deduction failure:
       [temp.deduct]/11.4) */
    if (referred.kind == type_kind::fundamental &&
        referred.fundamental == fundamental_type::void_)
        return ill_formed(at,
                          "a declarator that specifies the type \"reference "
                          "to cv void\" is ill-formed",
                          "dcl.ref");
    type = reference_to(type, rvalue);
    m.reference_made = true;
    return analysis_result::well_formed;
}

/*
 *     ptr-operator:
 *         * attribute-specifier-seq_opt cv-qualifier-seq_opt
 *         & attribute-specifier-seq_opt
 *         && attribute-specifier-seq_opt
 *         nested-name-specifier * attribute-specifier-seq_opt cv-qualifier-seq_opt
 *
 * [dcl.ptr]/1: "In a declaration T D where D has the form
 *     * attribute-specifier-seq_opt cv-qualifier-seq_opt D1
 * ... the type of the identifier of D is derived-declarator-type-list
 * cv-qualifier-seq pointer to T."
 */
analysis_result semantic_analysis::pointer_operator(SyntaxRef op,
                                                    qualified_type &type,
                                                    declarator_meaning &m)
{
    using punct = preprocessing_op_or_punc;
    entity_index member_class = 0;
    type_index member_dependent = 0;
    for (auto c : op.elements()) {
        if (c.gnu_attribute_specifier()) {
            /* [ext:gnu_attributes] void *__attribute__((malloc)) f(): the
               attributes after the * appertain to the declaration, as GCC
               takes them (<mm_malloc.h>) */
            VTRY(r, gnu_attribute_specifier(c, m));
            continue;
        }
        if (c.is_attribute())
            return not_supported(op, "dcl.attr");
        if (c.nested_name_specifier()) {
            /* [dcl.mptr]/1: "In a declaration T D where D has the form
                   nested-name-specifier * attribute-specifier-seq_opt
                   cv-qualifier-seq_opt D1
               and the nested-name-specifier denotes a class, ... the type
               of the identifier of D is "derived-declarator-type-list
               cv-qualifier-seq pointer to member of class
               nested-name-specifier of type T"." */
            nominated_scope scope;
            VTRY(r, nested_name_specifier(c, scope, true));
            if (!scope.global && !scope.dependent &&
                (is_namespace(scope.entity) ||
                 entity_table[scope.entity].kind == entity_kind::enumeration))
                return ill_formed(op,
                                  "the nested-name-specifier of a pointer to "
                                  "member declarator shall denote a class",
                                  "dcl.mptr");
            if (scope.dependent && !scope.global) {
                /* [temp.dep.type]/9: a class that is a dependent type,
                   known when the declaration is instantiated */
                member_dependent = scope.dependent;
                continue;
            }
            if (scope.global ||
                entity_table[scope.entity].kind != entity_kind::class_type)
                return not_supported(op, "dcl.mptr");
            member_class = scope.entity;
            continue;
        }
        if (c.is(punct::STAR)) {
            /* [dcl.ref]/5: "There shall be ... no pointers to references." */
            type_kind k = type_table[type.unqualified()].kind;
            if (k == type_kind::lvalue_reference ||
                k == type_kind::rvalue_reference) {
                if (member_class || member_dependent)
                    /* [dcl.mptr]/3: "A pointer to member shall not point
                       to ... a member with reference type" */
                    return ill_formed(op,
                                      "a pointer to member shall not point "
                                      "to a member with reference type",
                                      "dcl.mptr");
                return ill_formed(
                    op, "there shall be no pointers to references", "dcl.ref");
            }
            if (member_class || member_dependent) {
                /* [dcl.mptr]/3: "or "cv void"" */
                if (fundamental_of(type) == fundamental_type::void_)
                    return ill_formed(op,
                                      "a pointer to member shall not point "
                                      "to cv void",
                                      "dcl.mptr");
                if (member_dependent)
                    type = qualified_type::make(
                        type_table.dependent_pointer_to_member(member_dependent,
                                                               type),
                        cv_none);
                else
                    type = qualified_type::make(
                        type_table.pointer_to_member(member_class, type),
                        cv_none);
                continue;
            }
            VTRY(r, misplaced_function_qualifiers(op, type, false));
            type = qualified_type::make(type_table.pointer_to(type), cv_none);
        } else if (c.is(punct::CARET)) {
            /* [ext:clang_blocks] "a block pointer to function type"; clang
               rejects one to any other type */
            if (type_table[type.unqualified()].kind != type_kind::function)
                return ill_formed(op,
                                  "a block pointer shall point to a function "
                                  "type",
                                  "ext.clang");
            type = qualified_type::make(type_table.block_pointer_to(type),
                                        cv_none);
        } else if (c.is(punct::AMPERSAND)) {
            VTRY(r, misplaced_function_qualifiers(op, type, false));
            VTRY_SET(r, reference(op, false, type, m));
        } else if (c.is(punct::AMPERSAND_AMPERSAND)) {
            if (!tokens.files.behavior.has(language_feature::rvalue_references))
                /* [ext.clang] an rvalue reference before C++11, an
                   extension (an error with -pedantic-errors) */
                return not_supported(op, "ext.clang");
            VTRY(r, misplaced_function_qualifiers(op, type, false));
            VTRY_SET(r, reference(op, true, type, m));
        } else if (c.cv_qualifier_seq()) {
            /* [dcl.ptr]/1: the cv-qualifiers apply to the pointer */
            for (auto q : c.elements()) {
                if (!q.is_token())
                    return not_supported(op, "dcl.ptr");
                VTRY(r, pointer_qualifier(op, q.token(), type));
            }
        } else if (c.is_token()) {
            VTRY(r, pointer_qualifier(op, c.token(), type));
        } else {
            return not_supported(op, "dcl.ptr");
        }
    }
    return analysis_result::well_formed;
}

/* [temp.dep.constexpr]/2: in a template's own declaration, the non-type
   template-parameter e names (0 if e is no such name) */
entity_index semantic_analysis::value_parameter_named(SyntaxElement e)
{
    if (!e.is_token() || tokens[e.token()].kind != token_kind::identifier)
        return 0;
    lookup_result x =
        lookup_unqualified(tokens.identifier(e.token()), lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    if (named && entity_table[named].kind == entity_kind::none &&
        entity_table[named].instantiation &&
        !entity_table[named].held_by_template)
        return named;
    return 0;
}

/*
 * [dcl.array]/1: "In a declaration T D where D has the form
 *     D1 [ constant-expression_opt ] attribute-specifier-seq_opt
 * ... the type of the declarator-id in D is derived-declarator-type-list
 * array of N T."  /2: without the constant-expression, "array of unknown
 * bound of T".
 */
analysis_result semantic_analysis::array(SyntaxRef at, SyntaxElement bound,
                                         qualified_type &type)
{
    /* [dcl.array]/4: "U is called the array element type; this type shall
       not be a placeholder type, a reference type, a function type, an
       array of unknown bound, or cv void." */
    const struct type element = type_table[type.unqualified()];
    const char *rule = "the array element type shall not be a reference "
                       "type, a function type, an array of unknown bound, or "
                       "cv void";
    if (element.kind == type_kind::lvalue_reference ||
        element.kind == type_kind::rvalue_reference ||
        element.kind == type_kind::function ||
        (element.kind == type_kind::array && !element.bound_known) ||
        (element.kind == type_kind::fundamental &&
         element.fundamental == fundamental_type::void_))
        return ill_formed(at, rule, "dcl.array");
    /* an array of an abstract class: ill-formed before C++20 (C++17's
       [dcl.array]/1: "T ... shall not be ... an abstract class type"),
       where clang still reports it */
    if (abstract_class(type))
        return not_supported(at, "class.abstract");
    /* [ext:clang_vla] an array of variable length arrays: not analysed */
    if (has_variable_bound(type))
        return not_supported(at, "dcl.array");
    if (!bound) {
        type = qualified_type::make(type_table.array_of_unknown_bound_of(type),
                                    cv_none);
        return analysis_result::well_formed;
    }
    /* "The constant-expression shall be a converted constant expression of
       type std::size_t" ([expr.const]/10: "A converted constant expression
       of type T is an expression, implicitly converted to type T, where the
       converted expression is a constant expression and the implicit
       conversion sequence contains only ... integral conversions other than
       narrowing conversions") */
    /* [temp.dep.constexpr]/2: in a template's own declaration, a bound
       that is a non-type template-parameter's name (T (&)[N]) is
       value-dependent: the type records the parameter, which deduction
       ([temp.deduct.type]/8's T[i]) gives a value; each instantiation
       analyses the declarator again with the parameter bound to it */
    if (entity_index named = value_parameter_named(bound)) {
        if (!is_integral(entity_table[named].type))
            return not_supported(at, "temp.dep.constexpr");
        type = qualified_type::make(
            type_table.array_of_parameter_bound(type, named), cv_none);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    /* [ext:clang_vla] whether the bound may be a run-time value; the
       bound's own declarators (a lambda's, a cast's) may not */
    bool run_time_allowed = variable_length_declarator;
    scoped_value<bool> inner(variable_length_declarator, false);
    VTRY(r, expression(bound, m));
    VTRY_SET(r, operand_value(at, m));
    /* [expr.const]/10: an integral promotion of an unscoped
       enumeration */
    promote_enumeration(m);
    const struct type of_bound = type_table[m.type.unqualified()];
    if (of_bound.kind == type_kind::enumeration ||
        of_bound.kind == type_kind::class_type ||
        of_bound.kind == type_kind::none)
        return not_supported(at, "expr.const");
    if (!is_integral(m.type))
        return ill_formed(at,
                          "the constant-expression shall be a converted "
                          "constant expression of type std::size_t",
                          "dcl.array");
    /* a bound that is not a constant expression: a variable length array
       (C99's, clang's extension), or one clang folds */
    bool constant = false;
    VTRY_SET(r, integral_constant_expression(at, m, constant));
    if (!constant) {
        /* [ext:clang_vla] clang's and GCC's variable length array in C++:
           "variable length arrays in C++ are a Clang extension"
           (-Wvla-cxx-extension, off unless asked): a block-scope
           variable's array, of one dimension with run-time bounds (the
           element type has none), whose bound is evaluated where it is
           declared and converted to std::size_t */
        if (!run_time_allowed || !m.node || !depends_on_run_time_state(m.node))
            return not_supported(at, "expr.const");
        if (has_variable_bound(type))
            return not_supported(at, "dcl.array");
        VTRY_SET(r, extension_used(diagnostic_id::sema_variable_length_array,
                                   at.first_token(), nullptr));
        type = qualified_type::make(
            type_table.array_of_variable_bound(type, m.node), cv_none);
        return analysis_result::well_formed;
    }
    const implementation_defined_behavior &b = tokens.files.behavior;
    fundamental_type f = fundamental_of(m.type);
    __int128 value = integer_value(b, f, m.value);
    /* a negative value narrows: clang's "array size is negative" */
    if (value < 0)
        return ill_formed(at,
                          "the constant-expression shall be a converted "
                          "constant expression of type std::size_t, whose "
                          "conversion is not narrowing",
                          "dcl.array");
    /* an array too large for the address space: clang's error, by size */
    if (value > (UINT64_C(1) << 40))
        return not_supported(at, "dcl.array");
    /* "N shall be greater than zero": zero is clang's extension */
    if (!value) {
        VTRY_SET(r, extension_used(diagnostic_id::sema_zero_length_array,
                                   at.first_token(), nullptr));
    }
    type = qualified_type::make(type_table.array_of(type, uint64_t(value)),
                                cv_none);
    return analysis_result::well_formed;
}

/*
 * [ext:clang_vla] whether the value of the expression node n can differ
 * from one execution to the next: it reads an object, calls a function or
 * modifies something.  One that does not and is no constant expression is
 * one clang folds as an extension ((unsigned long)(int *)0 + 1), not a
 * variable length array's bound.
 */
bool semantic_analysis::depends_on_run_time_state(uint32_t n) const
{
    const semantic_node &x = semantics[n];
    switch (x.kind) {
    case semantic_kind::constant:
    case semantic_kind::entity:
        return false;
    case semantic_kind::conversion:
        if (x.op == static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue))
            return true;
        return depends_on_run_time_state(x.a);
    case semantic_kind::unary:
    case semantic_kind::address:
    case semantic_kind::member:
        return depends_on_run_time_state(x.a);
    case semantic_kind::binary:
    case semantic_kind::logical_and:
    case semantic_kind::logical_or:
    case semantic_kind::comma:
    case semantic_kind::subscript:
        return depends_on_run_time_state(x.a) || depends_on_run_time_state(x.b);
    case semantic_kind::conditional:
        return depends_on_run_time_state(x.a) ||
               depends_on_run_time_state(x.b) || depends_on_run_time_state(x.c);
    default:
        return true;
    }
}

/*
 * [ext:clang_vla] a declaration whose type has a variable length array in
 * it: clang accepts one as an automatic variable, of one dimension, not
 * initialized ("variable-sized object may not be initialized", also for
 * = {} in C++), whose elements need no construction or destruction here.
 * permitted: the declaration is a block-scope variable's (the declarator
 * was analysed with run-time bounds allowed).
 */
analysis_result
semantic_analysis::variable_length_declaration(SyntaxRef declaration,
                                               qualified_type type,
                                               bool permitted, bool initialized)
{
    if (!permitted || !variable_length_array(type))
        return not_supported(declaration, "dcl.array");
    if (initialized)
        return ill_formed(declaration,
                          "a variable length array shall not be initialized",
                          "ext.clang");
    qualified_type element = type_table[type.unqualified()].operand;
    if (!size_of_type(element) || has_variable_bound(element))
        return not_supported(declaration, "dcl.array");
    entity_index record = element_class(element);
    if (record) {
        /* the elements would be constructed and destroyed in a loop at
           run time */
        if (!is_class_entity(record) || destroys(record) ||
            (entity_table[record].first_constructor &&
             !trivial_defaulted_default_constructor(record)))
            return not_supported(declaration, "dcl.init");
    }
    /* [dcl.constexpr]: not a literal type's variable */
    if (function_state.function &&
        entity_table[function_state.function].constexpr_)
        return not_supported(declaration, "dcl.constexpr");
    return analysis_result::well_formed;
}

/*
 * [dcl.meaning]/1: "A declarator contains exactly one declarator-id; it
 * names the entity that is declared."  The declarator-id of d when it is an
 * identifier, without analysing the rest of d; 0 for an operator name, a
 * qualified-id or none.  The first identifier outside the
 * parameters-and-qualifiers, ptr-operators, attributes and initializer is
 * the declarator-id: a noptr-declarator's head comes before its suffixes,
 * so an identifier in an array bound comes after it.
 */
token_index semantic_analysis::declarator_id(SyntaxElement d) const
{
    if (d.is_identifier())
        return d.token();
    if (d.is_token())
        return 0;
    if (d.is(syntax_kind::simple_template_id)) {
        if (d.size() && d[0].is_identifier())
            return d[0].token();
        return 0;
    }
    if (d.is(syntax_kind::operator_function_id,
             syntax_kind::literal_operator_id,
             syntax_kind::conversion_function_id))
        return d.first_token();
    if (!d.is(syntax_kind::init_declarator, syntax_kind::declarator,
              syntax_kind::ptr_declarator, syntax_kind::noptr_declarator))
        return 0;
    for (auto c : d.elements()) {
        if (c.is_token()) {
            if (c.is_identifier())
                return c.token();
            continue;
        }
        token_index id = declarator_id(c);
        if (id || c.qualified_id() || c.destructor_type_id())
            return id;
    }
    return 0;
}

/* whether a template-declaration's innermost declaration is a
   function-definition or a simple-declaration with declarators (a list,
   or the one declarator of a function template's declaration) */
static bool declares_by_declarator(SyntaxRef d)
{
    while ((d.template_declaration() || d.explicit_specialization()) &&
           d.back().is_node())
        d = d.back();
    if (d.function_definition() || d.is(syntax_kind::deduction_guide))
        return true;
    /* (a friend function template's: a member-declaration, [temp.friend]) */
    if (!d.simple_declaration() && !d.is(syntax_kind::member_declaration))
        return false;
    for (auto c : d.elements())
        if (c.is(syntax_kind::init_declarator_list,
                 syntax_kind::init_declarator, syntax_kind::declarator,
                 syntax_kind::noptr_declarator, syntax_kind::ptr_declarator))
            return true;
    return false;
}

/* the alias-declaration a declaration is, or a template-declaration's (an
   alias template), else the declaration itself */
static SyntaxRef alias_declaration_of(SyntaxRef d)
{
    while (d.template_declaration() && d.size() && d.back().is_node())
        d = d.back();
    return d;
}

/*
 * A declaration not analysed (unsupported or not parsed) still declares
 * its names, and a later declaration of one of them is a redeclaration whose
 * meaning depends on this one ([basic.link]/11, [dcl.stc]/6 ...): the names
 * found are bound to an entity of no kind, which makes such a declaration
 * unsupported too.
 */
void semantic_analysis::hold_names(SyntaxRef declaration)
{
    owned_array<token_index> ids;
    /* [over.oper]/1: an operator function, or a conversion function, it
       may declare: the meaning of operators on classes and enumerations is
       not known from now on */
    for (token_index t = declaration.first_token(); t < declaration.end_token();
         t++)
        if (tokens[t].kind == token_kind::keyword &&
            p.keyword_of(tokens[t]) == standard_keyword::operator_)
            operators_held = true;
    if (declaration.is(syntax_kind::linkage_specification)) {
        /* extern "C" { ... }: the declarations in it are the scope's */
        for (auto c : declaration.elements()) {
            if (c.is_token())
                continue;
            if (c.declarations()) {
                for (auto d : c.elements())
                    if (d.is_node())
                        hold_names(d);
                continue;
            }
            hold_names(c);
        }
        return;
    }
    /* the declarator-ids of a simple-declaration or function-definition */
    auto declarator_ids = [&](SyntaxRef d) {
        for (auto c : d.elements()) {
            if (c.init_declarators()) {
                for (auto i : c.elements())
                    ids.append(declarator_id(i));
            } else if (c.identifier_list()) {
                /* [dcl.struct.bind]/1: a structured binding declaration's
                   identifiers */
                for (auto i : c.elements())
                    if (i.is_identifier())
                        ids.append(i.token());
            } else {
                ids.append(declarator_id(c));
            }
        }
    };
    bool by_declarator = parsed_completely(declaration) &&
                         (declaration.is(syntax_kind::simple_declaration,
                                         syntax_kind::function_definition,
                                         syntax_kind::condition));
    /* a template of a function or of declarators: what its declarator-ids
       name (below), not the types its declaration names (an explicit
       specialization's name is a template-id: all its names held) */
    if (!by_declarator && declaration.is(syntax_kind::template_declaration) &&
        parsed_completely(declaration))
        by_declarator = declares_by_declarator(declaration);
    /* A parsed class template specialization declares its class-head name;
       its template arguments and base clause name existing entities. */
    bool class_template = false;
    SyntaxRef inner = declaration;
    while ((inner.template_declaration() || inner.explicit_specialization()) &&
           inner.size() && inner.back().is_node())
        inner = inner.back();
    if (inner != declaration && parsed_completely(inner)) {
        for (SyntaxElement part : inner.elements()) {
            if (!part.is(syntax_kind::decl_specifier_seq))
                continue;
            for (SyntaxElement spec : part.elements()) {
                if (spec.is(syntax_kind::elaborated_type_specifier) &&
                    spec.size()) {
                    SyntaxElement name = spec[spec.size() - 1];
                    if (name.is_identifier()) {
                        ids.append(name.token());
                        class_template = true;
                    } else if (name.is(syntax_kind::simple_template_id) &&
                               name.size() && name[0].is_identifier()) {
                        ids.append(name[0].token());
                        class_template = true;
                    }
                    continue;
                }
                if (!spec.is(syntax_kind::class_specifier) || !spec.size())
                    continue;
                SyntaxElement name = class_head_name(spec[0]);
                if (name.is_identifier()) {
                    ids.append(name.token());
                    class_template = true;
                } else if (name.is(syntax_kind::simple_template_id) &&
                           name.size() && name[0].is_identifier()) {
                    ids.append(name[0].token());
                    class_template = true;
                }
            }
        }
    }
    if (declaration.is(syntax_kind::using_directive)) {
        /* [namespace.udir]: what it makes visible is not known */
        entity_table[current_scope].unknown_names = true;
        return;
    }
    if (declaration.is(syntax_kind::using_declaration,
                       syntax_kind::namespace_alias_definition)) {
        /* the names they bring into the scope: each using-declarator's
           last identifier, the alias; not the namespaces and classes
           named on the way */
        owned_array<SyntaxRef> work;
        work.append(declaration);
        while (!work.empty()) {
            SyntaxRef x = work.back();
            work.resize(work.size() - 1);
            if (x.is(syntax_kind::nested_name_specifier,
                     syntax_kind::qualified_namespace_specifier))
                continue;
            for (uint32_t i = 0; i < x.size(); i++) {
                SyntaxElement c = x[i];
                if (!c.is_token())
                    work.append(c);
                else if (c.is_identifier() &&
                         (x.is(syntax_kind::using_declarator) ||
                          (x == declaration && i == 1)))
                    ids.append(c.token());
            }
        }
    } else if (alias_declaration_of(declaration)
                   .is(syntax_kind::alias_declaration)) {
        /* [dcl.typedef]/2: an alias-declaration declares the identifier
           after using; the names in its type-id are used, not declared
           (holding them would hide a using-declaration's name from every
           later lookup) */
        SyntaxRef alias = alias_declaration_of(declaration);
        if (alias.size() > 1 && alias[1].is_identifier())
            ids.append(alias[1].token());
    } else if (!by_declarator && !class_template) {
        /* the declarators are not known (a declaration not parsed, or of
           a kind analysis has no rules for: a template, a
           using-declaration, ...): every identifier outside parentheses,
           brackets and braces, which holds more names than the declaration
           declares and so only makes more declarations unsupported */
        int depth = 0;
        /* [dcl.init]/1: an initializer declares no name; the
           names after = (up to the next declarator's comma, not one of a
           template-argument-list) are used */
        bool initializer = false;
        int initializer_angles = 0;
        auto is_punctuator_at = [&](token_index at,
                                    preprocessing_op_or_punc op) {
            const token &x = tokens[at];
            return x.kind == token_kind::operator_or_punctuator &&
                   preprocessing_op_or_punc_primary(x.op_or_punc) == op;
        };
        auto is_scope_operator = [&](token_index at) {
            const token &x = tokens[at];
            return x.kind == token_kind::operator_or_punctuator &&
                   preprocessing_op_or_punc_primary(x.op_or_punc) ==
                       preprocessing_op_or_punc::COLON_COLON;
        };
        /* not a template-head's: its names are the template's parameters
           and what their types and defaults name ([temp.param]) */
        token_index start = declaration.first_token();
        for (SyntaxRef x = declaration;
             x.is(syntax_kind::template_declaration) && x.size() >= 2 &&
             x[0].is(syntax_kind::template_head) && !x[x.size() - 1].is_token();
             x = x[x.size() - 1].index())
            start = x[0].end_token();
        for (token_index t = start; t < declaration.end_token(); t++) {
            const token &tok = tokens[t];
            /* a name before or after :: is a qualifier or a member of
               another scope declared again ([dcl.meaning]/1), not a name
               this declaration brings into the scope */
            bool qualified =
                (t + 1 < declaration.end_token() && is_scope_operator(t + 1)) ||
                (t > declaration.first_token() && is_scope_operator(t - 1));
            /* a typename-specifier's simple-template-id before ::
               (typename only_int<T>::type) names a type, never what the
               declaration declares; its template-arguments neither */
            if (tok.kind == token_kind::identifier && !depth && !qualified &&
                t > declaration.first_token() &&
                p.keyword_of(tokens[t - 1]) == standard_keyword::typename_ &&
                t + 1 < declaration.end_token() &&
                is_punctuator_at(t + 1, preprocessing_op_or_punc::LESS)) {
                token_index close = 0;
                int angles = 0, nested = 0;
                for (token_index k = t + 1;
                     k < declaration.end_token() && !close; k++) {
                    if (is_punctuator_at(
                            k, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                        is_punctuator_at(
                            k, preprocessing_op_or_punc::LEFT_BRACKET) ||
                        is_punctuator_at(k,
                                         preprocessing_op_or_punc::LEFT_BRACE))
                        nested++;
                    else if (is_punctuator_at(
                                 k,
                                 preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                             is_punctuator_at(
                                 k, preprocessing_op_or_punc::RIGHT_BRACKET) ||
                             is_punctuator_at(
                                 k, preprocessing_op_or_punc::RIGHT_BRACE))
                        nested--;
                    else if (!nested && is_punctuator_at(
                                            k, preprocessing_op_or_punc::LESS))
                        angles++;
                    else if (!nested &&
                             is_punctuator_at(
                                 k, preprocessing_op_or_punc::GREATER) &&
                             --angles == 0)
                        close = k;
                    if (nested < 0)
                        break;
                }
                if (close && close + 1 < declaration.end_token() &&
                    is_scope_operator(close + 1)) {
                    t = close;
                    continue;
                }
            }
            if (tok.kind == token_kind::identifier && !depth && !qualified &&
                !initializer)
                ids.append(t);
            if (tok.kind != token_kind::operator_or_punctuator)
                continue;
            switch (preprocessing_op_or_punc_primary(tok.op_or_punc)) {
            case preprocessing_op_or_punc::EQUAL:
                if (!depth)
                    initializer = true;
                break;
            case preprocessing_op_or_punc::LESS:
                if (initializer && !depth &&
                    tokens[t - 1].kind == token_kind::identifier)
                    initializer_angles++;
                break;
            case preprocessing_op_or_punc::GREATER:
                if (initializer && !depth && initializer_angles)
                    initializer_angles--;
                break;
            case preprocessing_op_or_punc::GREATER_GREATER:
                if (initializer && !depth) {
                    if (initializer_angles > 2)
                        initializer_angles -= 2;
                    else
                        initializer_angles = 0;
                }
                break;
            case preprocessing_op_or_punc::COMMA:
                if (!depth && !initializer_angles)
                    initializer = false;
                break;
            case preprocessing_op_or_punc::LEFT_BRACE:
            case preprocessing_op_or_punc::LEFT_BRACKET:
            case preprocessing_op_or_punc::LEFT_PARENTHESIS:
                depth++;
                break;
            case preprocessing_op_or_punc::RIGHT_BRACE:
            case preprocessing_op_or_punc::RIGHT_BRACKET:
            case preprocessing_op_or_punc::RIGHT_PARENTHESIS:
                depth--;
                break;
            default:
                break;
            }
        }
    } else if (declaration.is(syntax_kind::condition)) {
        if (declaration.size() > 1)
            ids.append(declarator_id(declaration[1]));
    } else if (!declaration.is(syntax_kind::template_declaration)) {
        /* (a template's: its declaration's, below) */
        declarator_ids(declaration);
    }
    /* a template's declaration: its declarator-ids too, which may be in
       parentheses (template<class T> void (f)(T);) */
    bool by_template = declaration.is(syntax_kind::template_declaration,
                                      syntax_kind::explicit_specialization);
    for (SyntaxRef inner = declaration;
         inner.is(syntax_kind::template_declaration,
                  syntax_kind::explicit_specialization);) {
        auto t = inner;
        if (!t.size() || inner[t.size() - 1].is_token())
            break;
        inner = inner[t.size() - 1].index();
        if ((inner.is(syntax_kind::simple_declaration,
                      syntax_kind::member_declaration,
                      syntax_kind::function_definition)) &&
            parsed_completely(inner))
            declarator_ids(inner);
    }
    for (token_index id : ids) {
        if (!id)
            continue;
        uint32_t name = tokens.identifier(id);
        entity_index found = entity_table.find(current_scope, name);
        bool defines_namespace =
            declaration.is(syntax_kind::named_namespace_definition,
                           syntax_kind::unnamed_namespace_definition,
                           syntax_kind::nested_namespace_definition);
        if (found && defines_namespace &&
            entity_table[found].kind == entity_kind::namespace_entity &&
            entity_table[found].name == name &&
            entity_table[found].enclosing == current_scope) {
            /* a namespace-definition that was not analysed: the
               namespace's members are not all known; the namespace is
               still the namespace */
            entity_table[found].unknown_names = true;
            continue;
        }
        if (found &&
            (entity_table.bound_by_using(current_scope, name) ||
             entity_table[found].kind == entity_kind::namespace_entity)) {
            /* the name of another scope's entity, brought here by a
               using-declaration: here it is not known from now on */
            entity_index e =
                entity_table.make(entity_kind::none, name, current_scope);
            entity_table.bind(current_scope, name, e);
            continue;
        }
        if (found &&
            (entity_table[found].kind == entity_kind::class_type ||
             entity_table[found].kind == entity_kind::enumeration) &&
            declared_by(found, declaration)) {
            /* a declarator-id never names the class or enumeration its
               declaration's specifier defines (typedef struct S {...} S
               __attribute__((aligned(8)))): the name is the declarator's
               entity's, not known; the class stays the one an
               elaborated-type-specifier would find, behind it
               ([basic.scope.hiding]/2) */
            entity_index e =
                entity_table.make(entity_kind::none, name, current_scope);
            entity_table[e].held_by_template = by_template;
            if (by_template)
                note_held(e);
            entity_table[e].hidden_type = found;
            entity_table.bind(current_scope, name, e);
            continue;
        }
        if (found) {
            /* a declaration of the name that analysis could not follow
               (an overload, a redeclaration with default arguments ...):
               what the name denotes is not known from here on */
            if (!declared_by(found, declaration)) {
                entity_table[found].kind = entity_kind::none;
                if (by_template) {
                    entity_table[found].held_by_template = true;
                    note_held(found);
                }
                /* a function template's name declared again (an explicit
                   specialization, an overload): its calls need more than
                   the template */
                for (function_template &t : function_templates)
                    if (t.name == found)
                        t.unusable = true;
            }
            continue;
        }
        entity_index e =
            entity_table.make(entity_kind::none, name, current_scope);
        entity_table[e].held_by_template = by_template;
        if (by_template)
            note_held(e);
        entity_table.bind(current_scope, name, e);
    }
}

/* whether the node is one of the entity's declarations */
bool semantic_analysis::declared_by(entity_index e, SyntaxRef node) const
{
    for (uint32_t d = entity_table[e].first_declaration; d;
         d = entity_table.declaration(d).next)
        if (entity_table.declaration(d).node == node)
            return true;
    return false;
}

/*
 * [dcl.meaning]/1: "When the declarator-id is qualified, the declaration shall
 * refer to a previously declared member of the class or namespace to which the
 * qualifier refers (or, in the case of a namespace, of an element of the inline
 * namespace set of that namespace) or to a specialization thereof; the member
 * shall not merely have been introduced by a using-declaration in the scope of
 * the class or namespace nominated by the nested-name-specifier of the
 * declarator-id."  Such a declaration is one of namespace scope
 * ([class.mfct]/2, [namespace.memdef]/2).
 */
analysis_result semantic_analysis::declarator_qualifier(SyntaxRef nested,
                                                        declarator_meaning &m)
{
    nominated_scope scope;
    VTRY(r, nested_name_specifier(nested, scope));
    if (at_block_scope() || function_state.active ||
        (defining_class && !friend_declarator))
        return not_supported(nested, "dcl.meaning");
    if (friend_declarator && !is_namespace(scope.entity))
        return not_supported(nested, "class.friend");
    /* [basic.lookup.qual]/3: what follows the qualified-id is looked up in
       its namespace, not in the class a friend declaration is in */
    defining_class = 0;
    if (is_namespace(scope.entity)) {
        m.qualified_namespace = true;
        m.namespace_qualifier = scope.entity;
        return analysis_result::well_formed;
    }
    const entity &c = entity_table[scope.entity];
    if (c.kind != entity_kind::class_type || !c.size)
        return not_supported(nested, "basic.lookup.qual");
    m.qualifier = scope.entity;
    return analysis_result::well_formed;
}

/*
 * The type the declarator d gives its declarator-id, from the type T of
 * the decl-specifier-seq (type on entry).
 *
 * In noptr-declarator's left-recursive productions the last suffix is the
 * outermost D, so the suffixes apply from the last to the first: in
 * int a[2][3], D is D1[3] with D1 = a[2], and a is "array of 2 array of 3
 * int".
 */
analysis_result semantic_analysis::declarator(SyntaxRef declaration,
                                              SyntaxElement d,
                                              qualified_type &type,
                                              declarator_meaning &m)
{
    using punct = preprocessing_op_or_punc;
    if (d.is_token()) {
        if (d.is(punct::ELLIPSIS)) {
            /* [dcl.decl]/1: noptr-abstract-pack-declarator is "...";
               [dcl.fct]/21: only in a parameter-declaration, which then
               declares a pack ([temp.param]/17 for a non-type template
               parameter) */
            if (!pack_declarator || m.pack)
                return not_supported(declaration, "temp.variadic");
            m.pack = d.token();
            return analysis_result::well_formed;
        }
        if (!d.is_identifier())
            return not_supported(declaration, "dcl.decl");
        m.has_id = true;
        m.id = d.token();
        return analysis_result::well_formed;
    }
    SyntaxRef n = d;
    switch (d.kind()) {
    case syntax_kind::ptr_declarator:
    case syntax_kind::ptr_abstract_declarator: {
        /* ptr-operator ptr-declarator */
        if (!d[0].ptr_operator())
            return not_supported(n, "dcl.decl");
        analysis_result r = pointer_operator(d[0], type, m);
        if (r != analysis_result::well_formed || d.size() == 1)
            return r;
        if (d.size() != 2)
            return not_supported(n, "dcl.decl");
        return declarator(declaration, d[1], type, m);
    }
    case syntax_kind::parameters_and_qualifiers:
        /* an abstract declarator that is only its parameters */
        return function(n, type);
    case syntax_kind::noptr_declarator:
    case syntax_kind::noptr_abstract_declarator:
        break;
    case syntax_kind::qualified_id: {
        /* [dcl.meaning]/1: a member of a class declared again, the
           declarator-id alone (a static data member's definition,
           [class.static.data]): class-name :: identifier */
        auto q = d.qualified_id();
        if (declarator_template_id && q.size() == 2 &&
            q.nested_name_specifier() &&
            q[1].is(syntax_kind::simple_template_id) &&
            q[1][0].is_identifier()) {
            /* [temp.expl.spec]/1: a variable template's specialization
               named by a qualified template-id */
            VTRY(r, declarator_qualifier(q.nested_name_specifier(), m));
            if (!m.qualified_namespace)
                return not_supported(n, "dcl.meaning");
            declarator_template_id = false;
            m.template_id = q[1];
            m.has_id = true;
            m.id = q[1][0].token();
            return analysis_result::well_formed;
        }
        if (q.size() != 2 || !q.nested_name_specifier() || !q.identifier())
            return not_supported(n, "dcl.meaning");
        VTRY(r, declarator_qualifier(q.nested_name_specifier(), m));
        m.has_id = true;
        m.id = q.identifier().token();
        return analysis_result::well_formed;
    }
    case syntax_kind::declarator:
        return trailing_return(declaration, n, type, m);
    case syntax_kind::gnu_declarator: {
        /* [ext:gnu_asm_labels] declarator gnu-asm-label_opt
           gnu-attribute-specifier-seq_opt: the label is the symbol, one
           ordinary string-literal (concatenated or not) without escape
           sequences here */
        for (auto c : d.elements(1)) {
            if (c.gnu_attribute_specifier()) {
                VTRY(r, gnu_attribute_specifier(c, m));
                continue;
            }
            auto literal = c.gnu_asm_label().string_literal();
            if (c.size() != 4 || !literal)
                return not_supported(n, "ext.gnu");
            owned_buffer written;
            tokens.spelling(literal.token(), written.get());
            if (written.size() < 2 ||
                memchr(written.data(), '\\', written.size()))
                return not_supported(n, "ext.gnu");
            m.asm_label = literal.token();
        }
        return declarator(declaration, d[0], type, m);
    }
    case syntax_kind::operator_function_id:
        return operator_function_id(n, m);
    case syntax_kind::literal_operator_id:
        return literal_operator_id(n, m);
    case syntax_kind::conversion_function_id:
        /* [class.conv.fct]/1: the type is the declaration's
           (conversion_function_member) */
        m.has_id = true;
        m.id = SyntaxElement(d[0]).token();
        m.conversion_function = true;
        tokens.name_token(m.id, conversion_function_name());
        return analysis_result::well_formed;
    case syntax_kind::simple_template_id:
        /* [temp.expl.spec]/1: a specialization's template-id, the whole
           declarator (a variable template's) */
        if (!declarator_template_id || !d[0].is_identifier())
            return not_supported(n);
        declarator_template_id = false;
        m.template_id = n;
        m.has_id = true;
        m.id = d[0].token();
        return analysis_result::well_formed;
    default:
        return not_supported(n);
    }
    /* the head: ( ptr-declarator ) or the declarator-id, or none in an
       abstract declarator; then the suffixes */
    entity_index scope_before = current_scope;
    uint32_t i = 0;
    bool has_head = false;
    SyntaxElement head = {};
    auto parts = NoptrDeclaratorParts(n);
    auto id = parts.declarator_id();
    if (d[0].is(punct::LEFT_PARENTHESIS)) {
        auto inner = parts.parenthesized();
        if (!inner)
            return not_supported(n, "dcl.decl");
        has_head = true;
        head = inner;
        i = 3;
        /* the suffixes are of the type the parenthesized declarator
           derives from (a pointer's or a pointer to member's function
           type, [dcl.mptr]/1), whose cv-qualifier-seq is not a member
           function's ([dcl.fct]/6) */
        in_member_declarator = false;
    } else if (parts.ellipsis()) {
        /* [dcl.fct]/21: "A declarator-id or abstract-declarator
           containing an ellipsis shall only be used in a
           parameter-declaration.  When it is part of a
           parameter-declaration-clause, the parameter-declaration declares
           a function parameter pack" */
        if (!pack_declarator || m.pack)
            return not_supported(n, "temp.variadic");
        m.pack = parts.ellipsis().token();
        i = 1;
        if (id.is_identifier()) {
            has_head = true;
            head = id;
            i = 2;
        }
    } else if (id.is_identifier()) {
        has_head = true;
        head = id;
        i = 1;
    } else if (declarator_template_id && id.simple_template_id() &&
               id.size() >= 3 && id[0].is_identifier()) {
        /* [temp.expl.spec]/1, [temp.explicit]/3: a specialization's
           template-id (its template-name the declarator-id's name) */
        m.template_id = id;
        declarator_template_id = false;
        has_head = true;
        head = id[0];
        i = 1;
    } else if (id.operator_function_id() || id.literal_operator_id() ||
               id.conversion_function_id()) {
        /* [over.oper]/1, [over.literal]/1: an operator function's name */
        has_head = true;
        head = id;
        i = 1;
    } else if (auto q = id.qualified_id()) {
        /* [dcl.meaning]/1: "When the declarator-id is qualified, the
           declaration shall refer to a previously declared member of the
           class or namespace to which the qualifier refers": a
           nested-name-specifier that is one class-name of the global
           namespace */
        SyntaxElement name;
        if (q.size() == 2)
            name = q[1];
        if (auto dtor = name.destructor_type_id();
            q.size() == 2 && dtor.size() == 2 && dtor.identifier()) {
            /* [class.dtor]/1: ~ class-name */
            m.destructor = true;
            name = dtor.identifier();
        }
        /* [over.oper]/1: an operator function's name, X::operator@ */
        bool operator_name = !m.destructor && name.operator_function_id();
        if (q.size() != 2 || !q.nested_name_specifier() ||
            (!operator_name && !name.is_identifier()))
            return not_supported(q, "dcl.meaning");
        VTRY(r, declarator_qualifier(q.nested_name_specifier(), m));
        if (m.destructor && !m.qualifier)
            return not_supported(q, "class.dtor");
        has_head = true;
        head = name;
        i = 1;
        /* the suffixes are the member's: a cv-qualifier-seq may follow one
           of a class */
        in_member_declarator = m.qualifier != 0;
        /* [basic.lookup.qual]/3: "In a declaration in which the declarator-id
           is a qualified-id, names used before the qualified-id being declared
           are looked up in the defining namespace scope; names following the
           qualified-id are looked up in the scope of the member's class or
           namespace.": the parameters are analysed in that scope (of a
           member template defined outside its class, [temp.mem]/1: the
           member's template-parameters' scope, whose enclosing scope is
           the class's, [temp.local]) */
        if (m.qualifier && entity_table[scope_before].enclosing == m.qualifier)
            current_scope = scope_before;
        else if (m.qualifier)
            current_scope = m.qualifier;
        else
            current_scope = m.namespace_qualifier;
    } else if (auto u = id.destructor_type_id()) {
        /* [class.dtor]/1: ~ class-name, in its class */
        if (!in_member_declarator || u.size() != 2 || !u.identifier())
            return not_supported(id, "dcl.meaning");
        m.destructor = true;
        has_head = true;
        head = u.identifier();
        i = 1;
    }
    struct suffix {
        bool function;
        uint32_t child;
        bool has_bound;
    };
    /* the scope a qualified declarator-id changed is back when the
       suffixes are done, however this function returns */
    struct scope_restore {
        entity_index &scope;
        entity_index saved;
        ~scope_restore()
        {
            scope = saved;
        }
    } restore = {current_scope, scope_before};
    owned_array<suffix> suffixes;
    while (i < d.size()) {
        auto c = d[i];
        if (c.gnu_attribute_specifier()) {
            VTRY(r, gnu_attribute_specifier(c, m));
            i++;
            continue;
        }
        if (c.is_attribute()) {
            if (attributes_without_effect(c)) {
                i++;
                continue;
            }
            return not_supported(n, "dcl.attr");
        }
        if (c.parameters_and_qualifiers()) {
            /* [ext:gnu_attributes] after the parameters */
            for (auto a : c.elements())
                if (a.is_node() && only_gnu_attributes(tree, a)) {
                    VTRY(r, gnu_attribute_specifier(a, m));
                }
            suffixes.append({true, i, false});
            i++;
        } else if (c.is(punct::LEFT_BRACKET)) {
            bool has_bound = d[i + 1] && !d[i + 1].is(punct::RIGHT_BRACKET);
            suffixes.append({false, i + 1, has_bound});
            i += 2;
            if (has_bound)
                i++;
        } else {
            return not_supported(n, "dcl.decl");
        }
    }
    for (size_t k = suffixes.size(); k-- > 0;) {
        const suffix &x = suffixes[k];
        analysis_result r;
        if (x.function) {
            scoped_value<bool> parameters(variable_length_declarator, false);
            r = function(d[x.child], type);
        } else if (x.has_bound)
            r = array(n, d[x.child], type);
        else
            VTRY_SET(r, array(n, {}, type));
        /* a reference the suffix's operand was is no longer the
           declarator's outermost type */
        m.reference_made = false;
    }
    in_member_declarator = false;
    current_scope = scope_before;
    if (!has_head)
        return analysis_result::well_formed;
    analysis_result r = declarator(declaration, head, type, m);
    if (r == analysis_result::well_formed)
        implicitly_non_throwing(m, type);
    return r;
}

/* [except.spec]/8: "The exception specification for an implicitly-
   declared destructor, or a destructor without a noexcept-specifier, is
   potentially-throwing if and only if any of the destructors for any of its
   potentially constructed subobjects is potentially-throwing" (none is, of
   the classes analysed); /12: "A deallocation function with no explicit
   noexcept-specifier has a non-throwing exception specification." */
void semantic_analysis::implicitly_non_throwing(const declarator_meaning &m,
                                                qualified_type &type)
{
    const struct type &f = type_table[type.unqualified()];
    bool deallocation = m.allocation == allocation_function::delete_ ||
                        m.allocation == allocation_function::delete_array;
    if (f.kind == type_kind::function && !f.non_throwing &&
        !function_exceptions_specified && (deallocation || m.destructor) &&
        tokens.files.behavior.has(
            language_feature::exception_specification_in_type)) {
        owned_array<qualified_type> parameters;
        const qualified_type *list = type_table.parameters(type.unqualified());
        for (uint32_t i = 0; i < f.parameter_count; i++)
            parameters.append(list[i]);
        type = qualified_type::make(
            type_table.function(f.operand, parameters.data(), parameters.size(),
                                f.variadic, true, f.function_cv,
                                f.function_ref),
            type.cv());
    }
}

} // namespace cplusplus
