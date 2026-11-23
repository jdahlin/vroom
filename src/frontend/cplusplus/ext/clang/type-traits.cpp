/*
 * type-traits.cpp: [ext:clang_type_traits] (clang Language Extensions,
 * Type Trait Primitives): the compiler's answers to <type_traits>'
 * questions, which libstdc++ and libc++ use when __has_builtin says they
 * exist.
 *
 *     primary-expression:
 *         clang-type-trait-expression
 *
 *     clang-type-trait-expression:
 *         clang-type-trait ( clang-type-trait-argument-list )
 *
 *     clang-type-trait-argument-list:
 *         clang-type-trait-argument
 *         clang-type-trait-argument-list , clang-type-trait-argument
 *
 *     clang-type-trait-argument:
 *         type-id ..._opt
 *         assignment-expression ..._opt
 *
 *     simple-type-specifier:
 *         clang-type-transformation
 *
 *     clang-type-transformation:
 *         clang-type-transformation-keyword ( type-id )
 *
 * The keywords are extension-keywords.inc's type_trait and
 * type_transformation rows.  Most traits take type-ids; __array_extent's
 * second argument, __is_lvalue_expr's and __is_rvalue_expr's are
 * expressions: an argument is a type-id when the tokens can be one
 * ([dcl.ambig.res]/2's rule).  How many arguments each takes is not
 * grammar.  With clang_type_trait_names the keyword is an identifier when
 * no '(' follows (parse/token-stream.cpp).
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_type_trait_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* the trait */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after the type trait", "ext.clang");
    flag_set greater(greater_is_operator, true);
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        for (;;) {
            bool type = false;
            {
                tentative_parse attempt(*this);
                type_id();
                if (!failed() &&
                    (at(preprocessing_op_or_punc::COMMA) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     at(preprocessing_op_or_punc::ELLIPSIS))) {
                    attempt.commit();
                    type = true;
                }
            }
            if (!type) {
                assignment_expression();
                if (failed())
                    return 0;
            }
            accept(preprocessing_op_or_punc::ELLIPSIS);
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the type trait's arguments", "ext.clang");
    return finish_node(syntax_kind::clang_type_trait_expression, mark, first);
}

node_index parser::clang_type_transformation()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* the transformation */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after the type transformation", "ext.clang");
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the type transformation", "ext.clang");
    return finish_node(syntax_kind::clang_type_transformation, mark, first);
}

/* the transformations: clang's rules for each (Language Extensions,
   "Type Trait Primitives"; libc++ spells <type_traits> with them) */
enum class transformation : uint8_t {
    none,
    remove_cv,
    remove_const,
    remove_volatile,
    remove_cvref,
    remove_reference,
    remove_pointer,
    remove_extent,
    remove_all_extents,
    add_pointer,
    add_lvalue_reference,
    add_rvalue_reference,
    decay,
    make_signed,
    make_unsigned,
    underlying_type,
};

/* whether the n bytes at s spell the keyword k */
static bool spelled(const char *s, size_t n, const char *k)
{
    return strlen(k) == n && !memcmp(k, s, n);
}

static transformation transformation_named(const char *s, size_t n)
{
    static const struct {
        const char *name;
        transformation which;
    } names[] = {
        {"__remove_cv", transformation::remove_cv},
        {"__remove_const", transformation::remove_const},
        {"__remove_volatile", transformation::remove_volatile},
        {"__remove_cvref", transformation::remove_cvref},
        {"__remove_reference_t", transformation::remove_reference},
        {"__remove_pointer", transformation::remove_pointer},
        {"__remove_extent", transformation::remove_extent},
        {"__remove_all_extents", transformation::remove_all_extents},
        {"__add_pointer", transformation::add_pointer},
        {"__add_lvalue_reference", transformation::add_lvalue_reference},
        {"__add_rvalue_reference", transformation::add_rvalue_reference},
        {"__decay", transformation::decay},
        {"__make_signed", transformation::make_signed},
        {"__make_unsigned", transformation::make_unsigned},
        {"__underlying_type", transformation::underlying_type},
    };
    for (const auto &k : names)
        if (spelled(s, n, k.name))
            return k.which;
    return transformation::none;
}

/* [ext:clang_type_traits] clang-type-transformation: the type it
   denotes; one of a dependent operand is a dependent type, the same for
   the same keyword and operand ([temp.over.link]), and a template's
   instantiation analyses it again with its arguments */
analysis_result
semantic_analysis::clang_type_transformation(ClangTypeTransformation n,
                                             qualified_type &type)
{
    token_index keyword = n[0].token();
    owned_buffer written;
    tokens.spelling(keyword, written.get());
    transformation which = transformation_named(
        reinterpret_cast<const char *>(written.data()), written.size());
    if (which == transformation::none)
        return not_supported(n, "ext.clang");
    qualified_type t;
    VTRY(r, type_id(n[2], t));
    if (depends_on_template_parameter(t)) {
        /* (the keyword's name as an identifier: a keyword token has
           none of its own) */
        uint32_t name =
            tokens.pp.identifier(reinterpret_cast<const char *>(written.data()),
                                 written.size()) +
            1;
        /* the operand: its type and cv-qualifiers */
        uint64_t operand = (uint64_t(t.cv()) << 32) | t.unqualified();
        type = qualified_type::make(
            dependent_type(type_transformations,
                           entity_kind::type_transformation, name, operand),
            cv_none);
        return analysis_result::well_formed;
    }
    const struct type *k = &type_table[t.unqualified()];
    bool reference = k->kind == type_kind::lvalue_reference ||
                     k->kind == type_kind::rvalue_reference;
    bool is_void = k->kind == type_kind::fundamental &&
                   k->fundamental == fundamental_type::void_;
    switch (which) {
    case transformation::remove_cv:
        type = qualified_type::make(t.unqualified(), cv_none);
        break;
    case transformation::remove_const:
        type = qualified_type::make(t.unqualified(), t.cv() & ~cv_const);
        break;
    case transformation::remove_volatile:
        type = qualified_type::make(t.unqualified(), t.cv() & ~cv_volatile);
        break;
    case transformation::remove_reference:
    case transformation::remove_cvref:
        type = t;
        if (reference)
            type = k->operand;
        if (which == transformation::remove_cvref)
            type = qualified_type::make(type.unqualified(), cv_none);
        break;
    case transformation::remove_pointer:
        type = t;
        if (k->kind == type_kind::pointer)
            type = k->operand;
        break;
    case transformation::remove_extent:
        type = t;
        if (k->kind == type_kind::array)
            type = k->operand;
        break;
    case transformation::remove_all_extents:
        type = t;
        while (type_table[type.unqualified()].kind == type_kind::array)
            type = type_table[type.unqualified()].operand;
        break;
    case transformation::add_pointer:
        type = t;
        if (reference)
            type = qualified_type::make(type_table.pointer_to(k->operand),
                                        cv_none);
        else
            type = qualified_type::make(type_table.pointer_to(t), cv_none);
        break;
    case transformation::add_lvalue_reference:
        type = t;
        if (reference)
            type = qualified_type::make(
                type_table.lvalue_reference_to(k->operand), cv_none);
        else if (!is_void)
            type = qualified_type::make(type_table.lvalue_reference_to(t),
                                        cv_none);
        break;
    case transformation::add_rvalue_reference:
        type = t;
        if (!reference && !is_void)
            type = qualified_type::make(type_table.rvalue_reference_to(t),
                                        cv_none);
        break;
    case transformation::decay:
        type = decayed(t);
        break;
    case transformation::make_signed:
    case transformation::make_unsigned:
        return changed_signedness(n, t, which == transformation::make_signed,
                                  type);
    case transformation::underlying_type: {
        /* [meta.trans.other] underlying_type: "the underlying type of T"
           ([dcl.enum]/5, /7), cv-unqualified; "Mandates: T is a complete
           enumeration type" (clang: an error otherwise) */
        if (k->kind != type_kind::enumeration)
            return ill_formed(n,
                              "the operand of __underlying_type is an "
                              "enumeration type",
                              "meta.trans.other");
        fundamental_type u = entity_table[k->entity].underlying;
        if (u == fundamental_type::none)
            return not_supported(n, "dcl.enum");
        type = qualified_type::make(type_table.fundamental(u), cv_none);
        break;
    }
    case transformation::none:
        break;
    }
    return analysis_result::well_formed;
}

static int unary_trait_named(const char *s, size_t n)
{
    static const char *const names[] = {
#define UNARY_TRAIT(kind, spelling) spelling,
#include "unary-traits.inc"
#undef UNARY_TRAIT
    };
    /* clang's other spellings of a trait */
    if (spelled(s, n, "__is_sealed"))
        s = "__is_final", n = 10;
    if (spelled(s, n, "__is_literal_type"))
        s = "__is_literal", n = 12;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (spelled(s, n, names[i]))
            return int(i);
    return -1;
}

namespace
{
/* an array type T[0] ([ext:gnu_zero_length_array]) */
bool zero_length_array(const type &a)
{
    return a.bound_known && a.bound == 0 && !a.entity && !a.variable_bound;
}
} // namespace

bool semantic_analysis::unary_trait(int which, qualified_type t, int64_t &value)
{
    const struct type *record = &type_table[t.unqualified()];
    type_kind kind = record->kind;
    fundamental_type f = fundamental_of(t);
    if (kind == type_kind::atomic || kind == type_kind::complex ||
        kind == type_kind::none || kind == type_kind::placeholder ||
        kind == type_kind::pack_expansion ||
        kind == type_kind::template_parameter || kind == type_kind::dependent ||
        f == fundamental_type::float128 ||
        f == fundamental_type::builtin_va_list ||
        f == fundamental_type::builtin_va_list_tag)
        return false;
    bool is_void =
        kind == type_kind::fundamental && f == fundamental_type::void_;
    bool reference = kind == type_kind::lvalue_reference ||
                     kind == type_kind::rvalue_reference;
    bool is_union =
        kind == type_kind::class_type && entity_table[record->entity].union_;
    bool member_pointer = kind == type_kind::pointer_to_member;
    /* [basic.fundamental]/15: void, std::nullptr_t, the arithmetic types */
    bool fundamental = kind == type_kind::fundamental;
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool answer = false;
    switch (static_cast<unary_trait_kind>(which)) {
    case unary_trait_kind::is_void:
        answer = is_void;
        break;
    case unary_trait_kind::is_integral:
        answer = is_integral(t);
        break;
    case unary_trait_kind::is_floating_point:
        answer = is_floating(t);
        break;
    case unary_trait_kind::is_arithmetic:
        answer = is_arithmetic(t);
        break;
    /* [ext:gnu_zero_length_array]: clang answers false for T[0], which is
       no array of [dcl.array]/1 (its bound is greater than zero); a bound
       that is a template-parameter's (entity) is not 0 */
    case unary_trait_kind::is_array:
        answer = kind == type_kind::array && !zero_length_array(*record);
        break;
    case unary_trait_kind::is_bounded_array:
        answer = kind == type_kind::array && record->bound_known &&
                 !zero_length_array(*record);
        break;
    case unary_trait_kind::is_unbounded_array:
        answer = kind == type_kind::array && !record->bound_known;
        break;
    case unary_trait_kind::is_pointer:
        answer = kind == type_kind::pointer;
        break;
    case unary_trait_kind::is_lvalue_reference:
        answer = kind == type_kind::lvalue_reference;
        break;
    case unary_trait_kind::is_rvalue_reference:
        answer = kind == type_kind::rvalue_reference;
        break;
    case unary_trait_kind::is_reference:
        answer = reference;
        break;
    case unary_trait_kind::is_function:
        answer = kind == type_kind::function;
        break;
    case unary_trait_kind::is_enum:
        answer = kind == type_kind::enumeration;
        break;
    case unary_trait_kind::is_scoped_enum:
        answer = kind == type_kind::enumeration &&
                 entity_table[record->entity].scoped;
        break;
    case unary_trait_kind::is_class:
        answer = kind == type_kind::class_type && !is_union;
        break;
    case unary_trait_kind::is_union:
        answer = is_union;
        break;
    case unary_trait_kind::is_member_pointer:
        answer = member_pointer;
        break;
    case unary_trait_kind::is_member_function_pointer:
        answer = is_member_function_pointer(t);
        break;
    case unary_trait_kind::is_member_object_pointer:
        answer = member_pointer && !is_member_function_pointer(t);
        break;
    case unary_trait_kind::is_fundamental:
        answer = fundamental;
        break;
    /* [basic.types]/9: "Arithmetic types, enumeration types,
       pointer types, pointer-to-member types, std::nullptr_t, and
       cv-qualified versions of these types are collectively called scalar
       types." */
    case unary_trait_kind::is_scalar:
        answer = is_arithmetic(t) || kind == type_kind::enumeration ||
                 kind == type_kind::pointer || member_pointer ||
                 f == fundamental_type::std_nullptr_t;
        break;
    /* [basic.compound]/1 */
    case unary_trait_kind::is_compound:
        answer = !fundamental;
        break;
    /* [basic.types]/8: "An object type is a (possibly
       cv-qualified) type that is not a function type, not a reference
       type, and not cv void." */
    case unary_trait_kind::is_object:
        answer = kind != type_kind::function && !reference && !is_void;
        break;
    /* an array's cv-qualifiers are its elements' ([basic.type.qualifier]/3) */
    case unary_trait_kind::is_const:
        answer = (element_type(t).cv() & cv_const) != 0;
        break;
    case unary_trait_kind::is_volatile:
        answer = (element_type(t).cv() & cv_volatile) != 0;
        break;
    /* [meta.unary.prop] is_signed: "If is_arithmetic_v<T> is true, the
       same result as T(-1) < T(0); otherwise, false" */
    case unary_trait_kind::is_signed:
        answer =
            is_floating(t) || (is_integral(t) && f != fundamental_type::bool_ &&
                               fundamental_type_is_signed(b, f));
        break;
    case unary_trait_kind::is_unsigned:
        answer = is_integral(t) && (f == fundamental_type::bool_ ||
                                    !fundamental_type_is_signed(b, f));
        break;
    /* ext/clang/class-traits.cpp: questions about a class's declaration */
    case unary_trait_kind::is_abstract:
    case unary_trait_kind::is_polymorphic:
    case unary_trait_kind::is_aggregate:
    case unary_trait_kind::is_empty:
    case unary_trait_kind::is_trivial:
    case unary_trait_kind::is_trivially_copyable:
    case unary_trait_kind::is_trivially_destructible:
    case unary_trait_kind::has_trivial_destructor:
    case unary_trait_kind::has_virtual_destructor:
    case unary_trait_kind::is_destructible:
    case unary_trait_kind::is_nothrow_destructible:
    case unary_trait_kind::is_final:
    case unary_trait_kind::is_standard_layout:
    case unary_trait_kind::is_pod:
    case unary_trait_kind::is_literal:
    case unary_trait_kind::has_trivial_constructor:
    case unary_trait_kind::has_trivial_copy:
    case unary_trait_kind::has_trivial_assign:
    case unary_trait_kind::has_unique_object_representations:
    case unary_trait_kind::is_trivially_relocatable:
    case unary_trait_kind::is_cpp_trivially_relocatable:
    case unary_trait_kind::is_trivially_equality_comparable:
        if (!class_property_trait(which, t, answer))
            return false;
        break;
    /* [meta.unary.prop.query] rank: the number of dimensions */
    case unary_trait_kind::array_rank:
        value = 0;
        while (type_table[t.unqualified()].kind == type_kind::array) {
            t = type_table[t.unqualified()].operand;
            value++;
        }
        return true;
    }
    value = answer;
    return true;
}

/* [dcl.init]/17.6.2: the same class initialized from a value of
   category c: the copy or move constructor chosen, [over.match.ctor];
   a deleted one, or the implicit one deleted ([class.copy.ctor]/10), makes
   it ill-formed.  known is false for what is not analysed (constructor
   templates) */
bool semantic_analysis::copy_constructible_from(entity_index c,
                                                value_category category,
                                                bool &known)
{
    if (entity_table[c].constructor_template ||
        entity_table[c].constructor_templates) {
        known = false;
        return false;
    }
    bool movable = false, copy_declared = false, copy_usable = false;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        if (f.parameter_count == 0)
            continue;
        qualified_type p =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        const struct type &r = type_table[p.unqualified()];
        if (r.kind != type_kind::lvalue_reference &&
            r.kind != type_kind::rvalue_reference)
            continue;
        const struct type &o = type_table[r.operand.unqualified()];
        if (o.kind != type_kind::class_type || o.entity != c)
            continue;
        if (r.kind == type_kind::rvalue_reference) {
            if (!entity_table[k].deleted)
                movable = true;
            continue;
        }
        copy_declared = true;
        /* [class.access]/1: a private or protected copy constructor is
           no candidate the trait's context can use */
        if (!entity_table[k].deleted && !entity_table[k].access)
            copy_usable = true;
    }
    if (category != value_category::lvalue && movable)
        return true;
    if (copy_declared)
        return copy_usable;
    /* [class.copy.ctor]/8: a declared move constructor deletes the
       implicit copy constructor */
    if (movable)
        return false;
    return !entity_table[c].implicit_copy_deleted;
}

/* [meta.rel] is_convertible<From, To>: "the return expression in the
   following code would be well-formed": To test() { return
   declval<From>(); }.  "Both are void" is true; a void To or From
   otherwise, and a To that is an array, a function or an abstract class
   (no object to return), is false.  declval<From>() is From&&: an xvalue,
   a function's lvalue.  unknown when the analysis cannot tell. */
bool semantic_analysis::convertible_types(qualified_type from,
                                          qualified_type to, bool &known)
{
    known = true;
    const struct type f = type_table[from.unqualified()];
    const struct type t = type_table[to.unqualified()];
    bool from_void = f.kind == type_kind::fundamental &&
                     f.fundamental == fundamental_type::void_;
    bool to_void = t.kind == type_kind::fundamental &&
                   t.fundamental == fundamental_type::void_;
    if (from_void || to_void)
        return from_void && to_void;
    if (t.kind == type_kind::array || t.kind == type_kind::function)
        return false;
    if (abstract_class(to))
        return false;
    expression_meaning value;
    value.type = from;
    value.category = value_category::xvalue;
    if (f.kind == type_kind::function)
        value.category = value_category::lvalue;
    /* a reference From: declval<From&>() is an lvalue, From&& an xvalue */
    if (f.kind == type_kind::lvalue_reference ||
        f.kind == type_kind::rvalue_reference) {
        value.type = f.operand;
        value.category = value_category::lvalue;
        if (f.kind == type_kind::rvalue_reference &&
            type_table[f.operand.unqualified()].kind != type_kind::function)
            value.category = value_category::xvalue;
    }
    conversion_sequence s = implicit_conversion_sequence(value, to, true);
    if (s.kind == conversion_sequence::form::unknown)
        known = false;
    if (!known)
        return false;
    if (s.kind == conversion_sequence::form::bad || s.ambiguous)
        return false;
    /* [over.match]/4: a deleted function selected makes the
       initialization ill-formed; [class.access.base]/6: so does an
       inaccessible base ([conv.ptr]/3, [conv.mem]/2) */
    if (s.function && entity_table[s.function].deleted)
        return false;
    /* [meta.rel]/5: "Access checking is performed in a context unrelated
       to To and From": a converting function that is not public */
    if (s.function && entity_table[s.function].access)
        return false;
    if (s.base_from && s.base_to) {
        owned_array<entity_index> steps;
        if (nonvirtual_path(s.base_from, s.base_to, steps)) {
            for (uint32_t i = 0; i < steps.size(); i++)
                if (entity_table[steps[i]].access)
                    return false;
        }
    }
    if (!s.function && t.kind == type_kind::class_type &&
        type_table[value.type.unqualified()].kind == type_kind::class_type &&
        type_table[value.type.unqualified()].entity == t.entity)
        return copy_constructible_from(t.entity, value.category, known);
    return true;
}

/* [ext:clang_type_traits] clang-type-trait-expression: a prvalue of type
   bool, a constant.  The traits analysis answers so far:
     __is_convertible(From, To) (and __is_convertible_to): [meta.rel]
     is_convertible, by convertible_types;
     __is_same(T, U) (GCC's spelling __is_same_as): [meta.rel] is_same,
     "T and U name the same type with the same cv-qualifications";
     unary-traits.inc's traits of one type, by its category.
   A template's own operands are dependent: the template is analysed with
   its arguments when instantiated, as sizeof's operand is */
analysis_result semantic_analysis::clang_type_trait(SyntaxRef n,
                                                    expression_meaning &m)
{
    owned_buffer written;
    tokens.spelling(n[0].token(), written.get());
    const char *name = reinterpret_cast<const char *>(written.data());
    bool same = spelled(name, written.size(), "__is_same") ||
                spelled(name, written.size(), "__is_same_as");
    bool convertible = spelled(name, written.size(), "__is_convertible") ||
                       spelled(name, written.size(), "__is_convertible_to");
    if (spelled(name, written.size(), "__is_constructible"))
        return clang_constructible_trait(n, false, false, m);
    if (spelled(name, written.size(), "__is_nothrow_constructible"))
        return clang_constructible_trait(n, true, false, m);
    if (spelled(name, written.size(), "__is_trivially_constructible"))
        return clang_constructible_trait(n, false, true, m);
    if (n.size() == 6 && n[2].is(syntax_kind::type_id) &&
        n[4].is(syntax_kind::type_id)) {
        if (spelled(name, written.size(), "__is_base_of"))
            return clang_base_of_trait(n, false, m);
        if (spelled(name, written.size(), "__is_layout_compatible"))
            return clang_layout_compatible_trait(n, m);
        if (spelled(name, written.size(), "__builtin_is_virtual_base_of"))
            return clang_base_of_trait(n, true, m);
        if (spelled(name, written.size(), "__is_assignable"))
            return clang_assignable_trait(n, 0, m);
        if (spelled(name, written.size(), "__is_nothrow_assignable"))
            return clang_assignable_trait(n, 1, m);
        if (spelled(name, written.size(), "__is_trivially_assignable"))
            return clang_assignable_trait(n, 2, m);
    }
    /* ( type-id ): a trait of the type's category */
    if (!same && !convertible && n.size() == 4 &&
        n[2].is(syntax_kind::type_id)) {
        int which = unary_trait_named(name, written.size());
        if (which < 0)
            return not_supported(n, "ext.clang");
        qualified_type t;
        VTRY(r, type_id(n[2], t));
        if (depends_on_template_parameter(t))
            return not_supported(n, "temp.dep.constexpr");
        int64_t value = 0;
        if (!unary_trait(which, t, value))
            return not_supported(n, "ext.clang");
        fundamental_type result = fundamental_type::bool_;
        if (which == int(unary_trait_kind::array_rank))
            result = fundamental_type::unsigned_long_int;
        m = integer_constant(result, value, n.first_token());
        return analysis_result::well_formed;
    }
    /* ( type-id , type-id ) */
    if ((!same && !convertible) || n.size() != 6 ||
        !n[2].is(syntax_kind::type_id) || !n[4].is(syntax_kind::type_id))
        return not_supported(n, "ext.clang");
    qualified_type t, u;
    VTRY(r, type_id(n[2], t));
    VTRY_SET(r, type_id(n[4], u));
    if (depends_on_template_parameter(t) || depends_on_template_parameter(u))
        return not_supported(n, "temp.dep.constexpr");
    bool answer = t == u;
    if (convertible) {
        bool known = true;
        answer = convertible_types(t, u, known);
        if (!known)
            return not_supported(n, "over.best.ics");
    }
    m = integer_constant(fundamental_type::bool_, answer, n.first_token());
    return analysis_result::well_formed;
}

/* [meta.trans.other] decay_t<T>: the type of a T passed by value */
qualified_type semantic_analysis::decayed(qualified_type t)
{
    const struct type *k = &type_table[t.unqualified()];
    if (k->kind == type_kind::lvalue_reference ||
        k->kind == type_kind::rvalue_reference)
        t = k->operand;
    k = &type_table[t.unqualified()];
    if (k->kind == type_kind::array)
        return qualified_type::make(type_table.pointer_to(k->operand), cv_none);
    if (k->kind == type_kind::function)
        return qualified_type::make(type_table.pointer_to(t), cv_none);
    return qualified_type::make(t.unqualified(), cv_none);
}

/*
 * [ext:clang_builtin_templates] (clang's BuiltinTemplates.td):
 *
 *     __builtin_common_type<Template, TypeIdentity, Empty, T...>
 *
 * is TypeIdentity<C> when the types T have the common type C of
 * [meta.trans.other]/3 (common_type's rules, the user's specializations
 * of it reached through Template<T1, T2>), Empty when they have none.
 * libc++'s common_type derives from it with
 * Template = __common_type_t (common_type<T...>::type) when __has_builtin
 * says it exists.  Template and TypeIdentity are written as names of
 * templates; T... may expand an instantiation's pack.
 */
bool semantic_analysis::is_builtin_common_type(uint32_t name) const
{
    static const char spelled[] = "__builtin_common_type";
    return name == tokens.pp.identifier(spelled, sizeof spelled - 1) + 1;
}

/* a builtin template's template-arguments as written, each with whether
   it is a pack expansion */
analysis_result semantic_analysis::builtin_template_arguments(
    SyntaxRef id, owned_array<SyntaxElement> &written,
    owned_array<bool> &expansion)
{
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        if (id[i].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported(id, "temp.variadic");
            expansion.back() = true;
            continue;
        }
        if (id[i].is(syntax_kind::template_argument_list)) {
            if (!expansion_list(id[i], written, expansion))
                return not_supported(id, "temp.variadic");
        } else {
            written.append(id[i]);
            expansion.append(false);
        }
    }
    for (size_t w = 0; w < written.size(); w++)
        if (written[w].is(syntax_kind::type_id) && written[w].size() == 2 &&
            written[w][1].is(preprocessing_op_or_punc::ELLIPSIS))
            expansion[w] = true;
    return analysis_result::well_formed;
}

/* the types of the template-arguments written[from ...), a pack
   expansion's once per element; dependent when one is, or when it expands
   a template's own pack not bound to elements */
analysis_result semantic_analysis::builtin_template_types(
    SyntaxRef id, const owned_array<SyntaxElement> &written,
    const owned_array<bool> &expansion, size_t from,
    owned_array<qualified_type> &types, bool &dependent)
{
    for (size_t w = from; w < written.size(); w++) {
        SyntaxElement a = written[w];
        if (!a.is(syntax_kind::type_id))
            return not_supported(id, "ext.clang");
        uint32_t times = 1;
        if (expansion[w]) {
            token_index first = first_token(a);
            token_index end = a.end_token();
            if (end > first + 1 && SyntaxElement::from_token(end - 1).is(
                                       preprocessing_op_or_punc::ELLIPSIS))
                end--;
            /* a template's own pack, not bound to elements: a dependent
               template-id */
            if (!expansion_count(first, end, times)) {
                dependent = true;
                continue;
            }
        }
        for (uint32_t k = 0; k < times; k++) {
            qualified_type t;
            analysis_result r = analysis_result::well_formed;
            {
                expansion_scope scope(*this, expansion[w], k, true);
                r = type_id(a, t);
            }
            if (r != analysis_result::well_formed)
                return r;
            if (depends_on_template_parameter(t))
                dependent = true;
            types.append(t);
        }
    }
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::builtin_common_type(SyntaxRef id,
                                                       type_index &type)
{
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    VTRY(r, builtin_template_arguments(id, written, expansion));
    if (written.size() < 3 || expansion[0] || expansion[1] || expansion[2])
        return not_supported(id, "ext.clang");
    /* the two templates, by name */
    entity_index templates[2] = {0, 0};
    for (uint32_t i = 0; i < 2; i++) {
        SyntaxElement a = written[i];
        token_index first = first_token(a);
        token_index end = first + 1;
        if (!a.is_token())
            end = a.end_token();
        if (end != first + 1 || tokens[first].kind != token_kind::identifier)
            return not_supported(id, "ext.clang");
        lookup_result x =
            lookup_unqualified(tokens.identifier(first), lookup_filter::any);
        entity_index e = x.entity;
        if (!e)
            e = x.unknown_entity;
        uint32_t t = class_template_of(e);
        if (!t)
            t = alias_template_of(e);
        if (!t || function_templates[t - 1].is_parameter ||
            function_templates[t - 1].primary)
            return not_supported(id, "ext.clang");
        templates[i] = e;
    }
    qualified_type empty;
    if (!written[2].is(syntax_kind::type_id))
        return not_supported(id, "ext.clang");
    VTRY_SET(r, type_id(written[2], empty));
    owned_array<qualified_type> types;
    bool dependent = depends_on_template_parameter(empty);
    VTRY_SET(
        r, builtin_template_types(id, written, expansion, 3, types, dependent));
    if (dependent) {
        /* [temp.dep.type]/9: a template-id of dependent arguments */
        type = dependent_written_type(id);
        return analysis_result::well_formed;
    }
    common_found found = common_found::none;
    qualified_type c;
    VTRY_SET(r, common_type_of(id, templates[0], types.data(),
                               static_cast<uint32_t>(types.size()), found, c));
    if (found == common_found::none) {
        if (empty.cv())
            return not_supported(id, "ext.clang");
        type = empty.unqualified();
        return analysis_result::well_formed;
    }
    return template_with_types(id, templates[1], &c, 1, type);
}

/*
 * [ext:clang_builtin_templates] __type_pack_element<N, T...> is the N-th
 * type of T (from 0); N an integral constant expression, less than
 * sizeof...(T), else ill-formed (clang's errors).  libc++'s tuple_element
 * and __type_list use it.
 */
bool semantic_analysis::is_builtin_type_pack_element(uint32_t name) const
{
    static const char spelled[] = "__type_pack_element";
    return name == tokens.pp.identifier(spelled, sizeof spelled - 1) + 1;
}

analysis_result semantic_analysis::builtin_type_pack_element(SyntaxRef id,
                                                             type_index &type)
{
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    VTRY(r, builtin_template_arguments(id, written, expansion));
    if (written.empty() || expansion[0] || written[0].is(syntax_kind::type_id))
        return not_supported(id, "ext.clang");
    owned_array<qualified_type> types;
    bool dependent = false;
    VTRY_SET(
        r, builtin_template_types(id, written, expansion, 1, types, dependent));
    /* N: value-dependent when it names a template's own parameter
       ([temp.dep.constexpr]/2) */
    SyntaxElement n = written[0];
    token_index first = first_token(n);
    token_index end = first + 1;
    if (!n.is_token())
        end = n.end_token();
    if (dependent || names_dependent_entity(first, end)) {
        /* [temp.dep.type]/9: a template-id of dependent arguments */
        type = dependent_written_type(id);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    r = expression(n, m);
    if (r == analysis_result::well_formed)
        VTRY_SET(r, operand_value(id, m));
    bool constant = false;
    if (is_integral(m.type)) {
        VTRY_SET(r, converted_constant_expression(
                        id, m, fundamental_type::unsigned_long_int, constant));
    }
    if (!constant)
        return ill_formed(id,
                          "the index of __type_pack_element is an integral "
                          "constant expression",
                          "ext.clang");
    if (m.value >= types.size())
        return ill_formed(id,
                          "the index of __type_pack_element is less than the "
                          "number of types",
                          "ext.clang");
    if (types[m.value].cv())
        return not_supported(id, "ext.clang");
    type = types[m.value].unqualified();
    return analysis_result::well_formed;
}

/*
 * [ext:clang_builtin_templates] __make_integer_seq<B, T, N> is
 * B<T, 0, 1, ..., N - 1>; N a converted constant expression of type T, not
 * negative (clang's errors).  libc++'s make_integer_sequence uses it.
 */
bool semantic_analysis::is_builtin_make_integer_seq(uint32_t name) const
{
    static const char spelled[] = "__make_integer_seq";
    return name == tokens.pp.identifier(spelled, sizeof spelled - 1) + 1;
}

analysis_result semantic_analysis::builtin_make_integer_seq(SyntaxRef id,
                                                            type_index &type)
{
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    VTRY(r, builtin_template_arguments(id, written, expansion));
    if (written.size() != 3 || expansion[0] || expansion[1] || expansion[2] ||
        written[2].is(syntax_kind::type_id))
        return not_supported(id, "ext.clang");
    /* B, by name */
    token_index first = first_token(written[0]);
    if ((!written[0].is_token() && written[0].end_token() != first + 1) ||
        tokens[first].kind != token_kind::identifier)
        return not_supported(id, "ext.clang");
    lookup_result x =
        lookup_unqualified(tokens.identifier(first), lookup_filter::any);
    entity_index b = x.entity;
    if (!b)
        b = x.unknown_entity;
    uint32_t t = class_template_of(b);
    if (!t)
        t = alias_template_of(b);
    if (!t)
        return not_supported(id, "ext.clang");
    if (!written[1].is(syntax_kind::type_id))
        return not_supported(id, "ext.clang");
    qualified_type element;
    VTRY_SET(r, type_id(written[1], element));
    /* N: value-dependent when it names a template's own parameter
       ([temp.dep.constexpr]/2) */
    SyntaxElement n = written[2];
    token_index end = first_token(n) + 1;
    if (!n.is_token())
        end = n.end_token();
    if (function_templates[t - 1].is_parameter ||
        depends_on_template_parameter(element) ||
        names_dependent_entity(first_token(n), end)) {
        /* [temp.dep.type]/9: a template-id of dependent arguments */
        type = dependent_written_type(id);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    r = expression(n, m);
    if (r == analysis_result::well_formed)
        VTRY_SET(r, operand_value(id, m));
    bool constant = false;
    fundamental_type f = fundamental_of(element);
    if (!element.cv() && is_integral(element) && is_integral(m.type)) {
        VTRY_SET(r, converted_constant_expression(id, m, f, constant));
    }
    if (!constant)
        return ill_formed(id,
                          "the length of __make_integer_seq is an integral "
                          "constant expression of its type",
                          "ext.clang");
    if (fundamental_type_is_signed(tokens.files.behavior, f) &&
        static_cast<int64_t>(m.value) < 0)
        return ill_formed(id,
                          "the length of __make_integer_seq is not negative",
                          "ext.clang");
    const function_template ft = function_templates[t - 1];
    if (ft.parameter_count != 2 ||
        !template_parameter_is_pack(ft.first_parameter + 1))
        return not_supported(id, "ext.clang");
    owned_array<template_argument> arguments, values;
    template_argument a;
    a.type = element;
    arguments.append(a);
    a.kind = template_argument::kind_t::value;
    for (uint64_t i = 0; i < m.value; i++) {
        a.value = i;
        values.append(a);
    }
    arguments.append(
        make_pack(values.data(), static_cast<uint32_t>(values.size())));
    if (ft.is_alias)
        return alias_template_specialization(id, t, arguments, type);
    entity_index c = 0;
    VTRY_SET(r, instantiate_class(id, t, arguments.data(),
                                  static_cast<uint32_t>(arguments.size()), c));
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/* [meta.trans.other]/3: the common type of count types */
analysis_result
semantic_analysis::common_type_of(SyntaxRef id, entity_index base,
                                  const qualified_type *types, uint32_t count,
                                  common_found &found, qualified_type &out)
{
    found = common_found::none;
    /* /3.1: "If sizeof...(T) is zero, there shall be no member type." */
    if (!count)
        return analysis_result::well_formed;
    /* /3.2: "If sizeof...(T) is one, ... common_type_t<T0, T0>" */
    if (count == 1) {
        qualified_type two[2] = {types[0], types[0]};
        type_index t = 0;
        substitution_mark mark = mark_substitution();
        substituting++;
        analysis_result r = template_with_types(id, base, two, 2, t);
        substituting--;
        if (r == analysis_result::ill_formed && undo_substitution(mark))
            return analysis_result::well_formed;
        if (r != analysis_result::well_formed)
            return r;
        found = common_found::found;
        out = qualified_type::make(t, cv_none);
        return analysis_result::well_formed;
    }
    /* /3.4: "If sizeof...(T) is greater than two, ... Let C denote the
       same type, if any, as common_type_t<T1, T2>. If there is such a
       type C, the member typedef-name type shall denote the same type, if
       any, as common_type_t<C, R...>." (each through Template, which
       reaches the user's specializations) */
    qualified_type c = types[0];
    for (uint32_t i = 1; i < count; i++) {
        qualified_type two[2] = {c, types[i]};
        if (count == 2)
            return common_type_pair(id, base, two[0], two[1], found, out);
        type_index t = 0;
        {
            substitution_mark mark = mark_substitution();
            substituting++;
            analysis_result r = template_with_types(id, base, two, 2, t);
            substituting--;
            if (r == analysis_result::ill_formed && undo_substitution(mark))
                return analysis_result::well_formed;
            if (r != analysis_result::well_formed)
                return r;
        }
        c = qualified_type::make(t, cv_none);
    }
    found = common_found::found;
    out = c;
    return analysis_result::well_formed;
}

/* [meta.trans.other]/3.3: the common type of two types */
analysis_result
semantic_analysis::common_type_pair(SyntaxRef id, entity_index base,
                                    qualified_type a, qualified_type b,
                                    common_found &found, qualified_type &out)
{
    found = common_found::none;
    qualified_type d1 = decayed(a), d2 = decayed(b);
    /* /3.3.1: "If is_same_v<T1, D1> is false or is_same_v<T2, D2> is
       false, let C denote the same type, if any, as
       common_type_t<D1, D2>." */
    if (!(d1 == a) || !(d2 == b)) {
        qualified_type two[2] = {d1, d2};
        type_index t = 0;
        substitution_mark mark = mark_substitution();
        substituting++;
        analysis_result r = template_with_types(id, base, two, 2, t);
        substituting--;
        if (r == analysis_result::ill_formed && undo_substitution(mark))
            return analysis_result::well_formed;
        if (r != analysis_result::well_formed)
            return r;
        found = common_found::found;
        out = qualified_type::make(t, cv_none);
        return analysis_result::well_formed;
    }
    /* /3.3.3: "if decay_t<decltype(false ? declval<D1>() :
       declval<D2>())> denotes a valid type, let C denote that type" */
    qualified_type c;
    analysis_result r = common_conditional(id, d1, d2, found, c);
    if (r != analysis_result::well_formed || found == common_found::found) {
        out = decayed(c);
        return r;
    }
    /* /3.3.4: "if COND-RES(CREF(D1), CREF(D2)) denotes a type, let C
       denote the type decay_t<COND-RES(CREF(D1), CREF(D2))>" (C++20) */
    if (tokens.files.behavior.edition < standard_edition::cplusplus_20)
        return analysis_result::well_formed;
    qualified_type r1 = qualified_type::make(
        type_table.lvalue_reference_to(
            qualified_type::make(d1.unqualified(), d1.cv() | cv_const)),
        cv_none);
    qualified_type r2 = qualified_type::make(
        type_table.lvalue_reference_to(
            qualified_type::make(d2.unqualified(), d2.cv() | cv_const)),
        cv_none);
    r = common_conditional(id, r1, r2, found, c);
    if (r == analysis_result::well_formed && found == common_found::found)
        out = decayed(c);
    return r;
}

/* declval<T>(): add_rvalue_reference_t<T>'s call, an lvalue of a function
   or of what an lvalue reference refers to, an xvalue of an object type, a
   prvalue of void */
expression_meaning semantic_analysis::declval_meaning(qualified_type given)
{
    qualified_type t = given;
    const struct type &k = type_table[t.unqualified()];
    value_category c = value_category::xvalue;
    if (k.kind == type_kind::lvalue_reference) {
        t = k.operand;
        c = value_category::lvalue;
    } else if (k.kind == type_kind::rvalue_reference) {
        t = k.operand;
    }
    if (type_table[t.unqualified()].kind == type_kind::function)
        c = value_category::lvalue;
    if (fundamental_of(t) == fundamental_type::void_)
        c = value_category::prvalue;
    expression_meaning e;
    e.type = t;
    e.category = c;
    e.evaluation = constness::not_constant;
    return e;
}

/* decltype(false ? declval<A>() : declval<B>()), if it is a type */
analysis_result semantic_analysis::common_conditional(SyntaxRef id,
                                                      qualified_type a,
                                                      qualified_type b,
                                                      common_found &found,
                                                      qualified_type &out)
{
    found = common_found::none;
    expression_meaning e[2] = {declval_meaning(a), declval_meaning(b)};
    /* [expr.cond]/2: both void: void; one: ill-formed (no throw) */
    bool v0 = fundamental_of(e[0].type) == fundamental_type::void_;
    bool v1 = fundamental_of(e[1].type) == fundamental_type::void_;
    if (v0 || v1) {
        if (v0 && v1) {
            found = common_found::found;
            out = qualified_type::make(e[0].type.unqualified(), cv_none);
        }
        return analysis_result::well_formed;
    }
    expression_meaning condition;
    condition.type = qualified_type::make(
        type_table.fundamental(fundamental_type::bool_), cv_none);
    condition.evaluation = constness::constant;
    expression_meaning m;
    substitution_mark mark = mark_substitution();
    substituting++;
    unevaluated++;
    analysis_result r = conditional_operands(id, condition, e[0], e[1], m);
    unevaluated--;
    substituting--;
    if (r == analysis_result::ill_formed && undo_substitution(mark))
        return analysis_result::well_formed;
    if (r != analysis_result::well_formed)
        return r;
    /* [dcl.type.decltype]/1.3-1.5 */
    found = common_found::found;
    out = m.type;
    if (m.category == value_category::lvalue)
        out = qualified_type::make(type_table.lvalue_reference_to(m.type),
                                   cv_none);
    else if (m.category == value_category::xvalue)
        out = qualified_type::make(type_table.rvalue_reference_to(m.type),
                                   cv_none);
    return analysis_result::well_formed;
}

/* the type the template named (a class or alias template) denotes with
   type arguments, its last parameter a pack taking the rest */
analysis_result
semantic_analysis::template_with_types(SyntaxRef id, entity_index named,
                                       const qualified_type *types,
                                       uint32_t count, type_index &type)
{
    uint32_t t = class_template_of(named);
    bool alias = false;
    if (!t) {
        t = alias_template_of(named);
        alias = true;
    }
    const function_template ft = function_templates[t - 1];
    uint32_t parameters = ft.parameter_count;
    bool pack = parameters &&
                template_parameter_is_pack(ft.first_parameter + parameters - 1);
    uint32_t fixed = parameters;
    if (pack)
        fixed--;
    if (count < fixed || (!pack && count != fixed))
        return not_supported(id, "ext.clang");
    owned_array<template_argument> arguments;
    for (uint32_t i = 0; i < fixed; i++) {
        if (entity_table[template_parameter_entities[ft.first_parameter + i]]
                .kind != entity_kind::typedef_name)
            return not_supported(id, "ext.clang");
        template_argument a;
        a.type = types[i];
        arguments.append(a);
    }
    if (pack) {
        if (entity_table[template_parameter_entities[ft.first_parameter +
                                                     fixed]]
                .kind != entity_kind::typedef_name)
            return not_supported(id, "ext.clang");
        owned_array<template_argument> elements;
        for (uint32_t i = fixed; i < count; i++) {
            template_argument a;
            a.type = types[i];
            elements.append(a);
        }
        arguments.append(
            make_pack(elements.data(), static_cast<uint32_t>(elements.size())));
    }
    if (alias)
        return alias_template_specialization(id, t, arguments, type);
    entity_index c = 0;
    VTRY(r, instantiate_class(id, t, arguments.data(),
                              static_cast<uint32_t>(arguments.size()), c));
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [meta.trans.sign] make_signed / make_unsigned, as clang's __make_signed
 * and __make_unsigned: "If T names a (possibly cv-qualified) signed
 * [unsigned] integer type then the member typedef type names the type T;
 * otherwise, if T names a (possibly cv-qualified) unsigned [signed]
 * integer type then type names the corresponding signed [unsigned]
 * integer type, with the same cv-qualifiers as T; otherwise, type names
 * the signed [unsigned] integer type with smallest rank for which sizeof(T)
 * == sizeof(type), with the same cv-qualifiers as T."  "Mandates: T is an
 * integral or enumeration type other than cv bool."  (char's
 * corresponding types are signed char and unsigned char; the character
 * types and enumerations take the third rule.)
 */
analysis_result semantic_analysis::changed_signedness(SyntaxRef n,
                                                      qualified_type t,
                                                      bool to_signed,
                                                      qualified_type &type)
{
    static const fundamental_type pairs[][2] = {
        {fundamental_type::signed_char, fundamental_type::unsigned_char},
        {fundamental_type::short_int, fundamental_type::unsigned_short_int},
        {fundamental_type::int_, fundamental_type::unsigned_int},
        {fundamental_type::long_int, fundamental_type::unsigned_long_int},
        {fundamental_type::long_long_int,
         fundamental_type::unsigned_long_long_int},
        {fundamental_type::int128, fundamental_type::unsigned_int128},
    };
    unsigned side = 1;
    if (to_signed)
        side = 0;
    const struct type &k = type_table[t.unqualified()];
    fundamental_type f = fundamental_type::none;
    if (k.kind == type_kind::fundamental)
        f = k.fundamental;
    if (f == fundamental_type::char_) {
        type = qualified_type::make(type_table.fundamental(pairs[0][side]),
                                    t.cv());
        return analysis_result::well_formed;
    }
    for (const auto &p : pairs)
        if (f == p[0] || f == p[1]) {
            type =
                qualified_type::make(type_table.fundamental(p[side]), t.cv());
            return analysis_result::well_formed;
        }
    bool by_size =
        k.kind == type_kind::enumeration || f == fundamental_type::wchar_t_ ||
        f == fundamental_type::char8_t_ || f == fundamental_type::char16_t_ ||
        f == fundamental_type::char32_t_;
    if (!by_size) {
        /* (_BitInt(N): no rule here) */
        if (f == fundamental_type::bit_precise_int ||
            f == fundamental_type::unsigned_bit_precise_int)
            return not_supported(n, "ext.clang");
        return ill_formed(n,
                          "the operand of __make_signed and __make_unsigned "
                          "is an integral or enumeration type other than "
                          "bool",
                          "meta.trans.sign");
    }
    uint64_t size =
        size_of_type(qualified_type::make(t.unqualified(), cv_none));
    if (!size)
        return not_supported(n, "ext.clang");
    for (const auto &p : pairs)
        if (size_of_type(qualified_type::make(type_table.fundamental(p[side]),
                                              cv_none)) == size) {
            type =
                qualified_type::make(type_table.fundamental(p[side]), t.cv());
            return analysis_result::well_formed;
        }
    return not_supported(n, "ext.clang");
}

} // namespace cplusplus
