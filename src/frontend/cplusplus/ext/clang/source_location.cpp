/*
 * source_location.cpp: [ext:clang_source_location] (clang Language
 * Extensions, Source location builtins): __builtin_source_location(), the
 * primitive of libc++'s std::source_location::current
 * ([support.srcloc.cons]/1).
 *
 *     primary-expression:
 *         clang-source-location-expression
 *
 *     clang-source-location-expression:
 *         __builtin_source_location ( )
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_source_location_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_source_location */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("'()' after __builtin_source_location", "ext.clang");
    return finish_node(syntax_kind::clang_source_location_expression, mark,
                       first);
}

/* [ext:clang_source_location] a prvalue of type "pointer to const
   std::source_location::__impl", which must be declared before; "a
   pointer to a global object holding the file name, function name, line
   and column" of the place it is evaluated at.  Only its type is
   analysed (decltype of it, libc++'s __bsl_ty): the object it points to
   is not made yet */
analysis_result semantic_analysis::clang_source_location(node_index n,
                                                         expression_meaning &m)
{
    entity_index impl = 0;
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    if (std_ && is_namespace(std_)) {
        entity_index named =
            lookup_in_namespace(std_,
                                tokens.pp.identifier("source_location", 15) + 1,
                                lookup_filter::types)
                .entity;
        if (named && entity_table[named].kind == entity_kind::class_type)
            impl = declared_in(named, tokens.pp.identifier("__impl", 6) + 1,
                               lookup_filter::types);
    }
    if (!impl || entity_table[impl].kind != entity_kind::class_type)
        return ill_formed(n,
                          "'std::source_location::__impl' must be defined "
                          "before '__builtin_source_location' is called",
                          "ext.clang");
    if (!unevaluated)
        return not_supported(n, "ext.clang");
    qualified_type object = qualified_type::make(
        type_table.of_entity(type_kind::class_type, impl), cv_const);
    m.type = qualified_type::make(type_table.pointer_to(object), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::unknown;
    return analysis_result::well_formed;
}

} // namespace cplusplus
