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
    specifiers.implicit_typename = type_id_only && tokens.files.behavior.has(language_feature::implicit_typename);
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

} // namespace cplusplus
