/*
 * type_traits.cpp: [ext:clang_type_traits] (clang Language Extensions,
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
 * no '(' follows (parse/token_stream.cpp).
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
};

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
    };
    for (const auto &k : names)
        if (strlen(k.name) == n && !memcmp(k.name, s, n))
            return k.which;
    return transformation::none;
}

/* [ext:clang_type_traits] clang-type-transformation: the type it
   denotes; one of a dependent operand is a dependent type, the same for
   the same keyword and operand ([temp.over.link]), and a template's
   instantiation analyses it again with its arguments */
analysis_result
semantic_analysis::clang_type_transformation(node_index n, qualified_type &type)
{
    const syntax_child *children = tree.children(n);
    token_index keyword = children[0].index();
    owned_buffer written;
    tokens.spelling(keyword, written.get());
    transformation which = transformation_named(
        reinterpret_cast<const char *>(written.data()), written.size());
    if (which == transformation::none)
        return not_supported(n, "ext.clang");
    qualified_type t;
    analysis_result r = type_id(children[2].index(), t);
    if (r != analysis_result::well_formed)
        return r;
    if (depends_on_template_parameter(t)) {
        uint32_t name = tokens.identifier(keyword);
        /* the operand: its type and cv-qualifiers */
        uint64_t operand = (uint64_t(t.cv()) << 32) | t.unqualified();
        for (entity_index e : type_transformations)
            if (entity_table[e].value == operand &&
                entity_table[e].name == name) {
                type = entity_table[e].type;
                return analysis_result::well_formed;
            }
        entity_index e =
            entity_table.make(entity_kind::type_transformation, name, 0);
        entity_table[e].value = operand;
        entity_table[e].type = qualified_type::make(
            type_table.of_entity(type_kind::dependent, e), cv_none);
        type_transformations.append(e);
        type = entity_table[e].type;
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
        type = t;
        if (reference)
            type = k->operand;
        k = &type_table[type.unqualified()];
        if (k->kind == type_kind::array)
            type = qualified_type::make(type_table.pointer_to(k->operand),
                                        cv_none);
        else if (k->kind == type_kind::function)
            type = qualified_type::make(type_table.pointer_to(type), cv_none);
        else
            type = qualified_type::make(type.unqualified(), cv_none);
        break;
    case transformation::none:
        break;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
