/*
 * dcl.name.cpp: [dcl.name] Type names.
 *
 * [dcl.name]/1: "To specify type conversions explicitly, and as an argument
 * of sizeof, alignof, new, or typeid, the name of a type shall be
 * specified."
 *
 *     type-id:
 *         type-specifier-seq abstract-declarator_opt
 *
 * The abstract-declarator is parse/dcl.decl.cpp's declarator in its
 * abstract form.  The type-specifier-seq is parsed as a
 * defining-type-specifier-seq: [dcl.type]/3's rule that a type-specifier-seq
 * has no class-specifier or enum-specifier is a semantic one.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::type_id()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    /* [temp.res]/5: a type-id-only context is this type-id's, not the
       type-ids nested in it */
    specifiers.implicit_typename =
        type_id_only &&
        tokens.files.behavior.has(language_feature::implicit_typename);
    {
        flag_set nested(type_id_only, false);
        decl_specifier_seq(specifiers, true);
    }
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq", "dcl.name");
    declarator_facts facts;
    declarator(declarator_form::abstract, facts);
    if (failed())
        return 0;
    return finish_node(syntax_kind::type_id, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [dcl.name]/1: "It is possible to identify uniquely the location in the
 * abstract-declarator where the identifier would appear if the
 * construction were a declarator in a declaration.  The named type is then
 * the same as the type of the hypothetical identifier."
 */
analysis_result semantic_analysis::type_id(TypeId n, qualified_type &type)
{
    auto node = n;
    if (!node.size() || !node[0].is(syntax_kind::type_specifier_seq) ||
        node.size() > 2)
        return not_supported(n, "dcl.name");
    /* [temp.variadic]/5: a template-argument's pack expansion, T ...
       (type_id_expansion: its pattern analysed) */
    uint32_t count = node.size();
    bool expansion = type_id_expansion;
    type_id_expansion = false;
    if (expansion && count == 2 &&
        node[1].is(preprocessing_op_or_punc::ELLIPSIS))
        count = 1;
    specified s;
    analysis_result r = decl_specifier_seq(node[0].index(), s, false);
    if (r != analysis_result::well_formed)
        return r;
    if (!s.has_type)
        return not_supported(n, "dcl.name");
    type = s.type;
    if (count == 1)
        return analysis_result::well_formed;
    declarator_meaning m;
    r = declarator(current_declaration, node[1], type, m);
    if (r == analysis_result::well_formed && m.has_id)
        return not_supported(n, "dcl.name");
    return r;
}

} // namespace cplusplus
