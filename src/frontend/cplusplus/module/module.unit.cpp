/*
 * module.unit.cpp: [module] Modules: [module.unit] Module units and
 * purviews, [module.interface] Export declaration, [module.import] Import
 * declaration, [module.global.frag] and [module.private.frag].
 *
 * The keywords: [lex.pptoken] export-keyword, import-keyword and
 * module-keyword are the preprocessing tokens phase 4 makes of export,
 * import and module at the start of a line that [cpp.module] and
 * [cpp.import] recognize; phase 7 gives them no token kind, so the parser
 * reads them by their preprocessing kind (at_phase4_keyword).  export
 * anywhere else is the keyword of [lex.key].  P1103R3 (C++20).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

bool parser::at_phase4_keyword(preprocessing_token_kind kind, unsigned ahead)
{
    if (tokens.at_end(position + ahead))
        return false;
    return peek(ahead).preprocessing.kind == kind;
}

bool parser::at_module_declaration()
{
    if (at_phase4_keyword(preprocessing_token_kind::export_keyword))
        return at_phase4_keyword(preprocessing_token_kind::module_keyword, 1);
    return at_phase4_keyword(preprocessing_token_kind::module_keyword) &&
           !at(preprocessing_op_or_punc::SEMICOLON, 1) &&
           !at(preprocessing_op_or_punc::COLON, 1);
}

bool parser::at_private_module_fragment()
{
    return at_phase4_keyword(preprocessing_token_kind::module_keyword) &&
           at(preprocessing_op_or_punc::COLON, 1) &&
           at(standard_keyword::private_, 2);
}

/*
 * [module.unit]/1:
 *
 *     module-name:
 *         module-name-qualifier_opt identifier
 *
 *     module-name-qualifier:
 *         identifier .
 *         module-name-qualifier identifier .
 *
 * "The identifiers module and import shall not appear as identifiers in a
 * module-name or module-partition."  A module-name-qualifier and a
 * module-name of one identifier pass through.
 */
node_index parser::module_name()
{
    size_t mark = begin_node();
    token_index first = position;
    size_t qualifier_mark = begin_node();
    unsigned identifiers = 0;
    for (;;) {
        if (!at_identifier())
            return expected("an identifier in the module-name", "module.unit");
        if (at_word("module") || at_word("import"))
            return violates("no module or import in a module-name",
                            "module.unit", position);
        add_token();
        identifiers++;
        if (!at(preprocessing_op_or_punc::DOT))
            break;
        add_token();
        finish_node(syntax_kind::module_name_qualifier, qualifier_mark, first);
    }
    if (identifiers == 1)
        return 0;
    return finish_node(syntax_kind::module_name, mark, first);
}

/*
 *     module-partition:
 *         : module-name-qualifier_opt identifier
 */
node_index parser::module_partition()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* : */
    module_name();
    if (failed())
        return 0;
    return finish_node(syntax_kind::module_partition, mark, first);
}

/*
 * [module.unit]/1:
 *
 *     module-declaration:
 *         export-keyword_opt module-keyword module-name module-partition_opt attribute-specifier-seq_opt ;
 */
node_index parser::module_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at_phase4_keyword(preprocessing_token_kind::export_keyword))
        add_token();
    add_token(); /* module-keyword */
    module_name();
    if (failed())
        return 0;
    if (at(preprocessing_op_or_punc::COLON)) {
        module_partition();
        if (failed())
            return 0;
    }
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the module-declaration", "module.unit");
    return finish_node(syntax_kind::module_declaration, mark, first);
}

/*
 * [module.import]:
 *
 *     module-import-declaration:
 *         import-keyword module-name attribute-specifier-seq_opt ;
 *         import-keyword module-partition attribute-specifier-seq_opt ;
 *         import-keyword header-name attribute-specifier-seq_opt ;
 *
 * /1: "A module-import-declaration shall only appear at global namespace
 * scope."
 */
node_index parser::module_import_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    if (names.current() != declared_names::global_namespace)
        return violates("a module-import-declaration only at global "
                        "namespace scope",
                        "module.import", position);
    add_token(); /* import-keyword */
    if (at(preprocessing_op_or_punc::COLON)) {
        module_partition();
    } else if (at_phase4_keyword(preprocessing_token_kind::header_name)) {
        add_token();
    } else {
        module_name();
    }
    if (failed())
        return 0;
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the module-import-declaration",
                        "module.import");
    return finish_node(syntax_kind::module_import_declaration, mark, first);
}

/*
 * [module.interface]:
 *
 *     export-declaration:
 *         export declaration
 *         export { declaration-seq_opt }
 *         export-keyword module-import-declaration
 *
 * /1: "An export-declaration shall appear only at namespace scope ... An
 * export-declaration does not establish a scope and its declaration or
 * declaration-seq shall not contain an export-declaration or
 * module-import-declaration."  Before C++20 export is the keyword of
 * C++98's exported templates ([diff.cpp03.temp]/1: removed in C++11), and
 * reserved after: clang's extension of C++98, P9's.
 */
node_index parser::export_declaration()
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    if (!b.has(language_feature::modules)) {
        if (at(standard_keyword::template_, 1))
            return beyond_this_step();
        return expected("template after export", "diff.cpp03.temp");
    }
    size_t mark = begin_node();
    token_index first = position;
    if (in_export_declaration)
        return violates("no export-declaration in an export-declaration",
                        "module.interface", position);
    bool keyword = at_phase4_keyword(preprocessing_token_kind::export_keyword);
    add_token(); /* export */
    flag_set inside(in_export_declaration, true);
    if (keyword) {
        if (!at_phase4_keyword(preprocessing_token_kind::import_keyword))
            return expected("import after export", "module.interface");
        module_import_declaration();
    } else if (at_phase4_keyword(preprocessing_token_kind::import_keyword) ||
               at_phase4_keyword(preprocessing_token_kind::module_keyword)) {
        return expected("a declaration after export", "module.interface");
    } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        add_token();
        declaration_seq(true);
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
            return expected("'}' to end the export-declaration",
                            "module.interface");
    } else {
        declaration_alternative();
    }
    if (failed())
        return 0;
    return finish_node(syntax_kind::export_declaration, mark, first);
}

/*
 * [basic.link]/2 (the second alternative of translation-unit), with
 * [module.global.frag]:
 *
 *     global-module-fragment:
 *         module-keyword ; declaration-seq_opt
 *
 * and [module.private.frag]:
 *
 *     private-module-fragment:
 *         module-keyword : private ; declaration-seq_opt
 *
 * The declarations of the global module fragment end at the
 * module-declaration, those of the purview at module : private ;.
 */
node_index parser::module_unit_rest(bool global_fragment)
{
    if (global_fragment) {
        size_t mark = begin_node();
        token_index first = position;
        add_token();
        add_token(); /* module ; */
        declaration_seq_until_module(false);
        finish_node(syntax_kind::global_module_fragment, mark, first);
        /* no module-declaration: the declarations run to the end */
        if (!at_module_declaration())
            return 0;
    }
    recover_declaration(&parser::module_declaration);
    declaration_seq_until_module(true);
    if (at_private_module_fragment()) {
        size_t mark = begin_node();
        token_index first = position;
        add_token();
        add_token();
        add_token();
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            expected("';' after module : private", "module.private.frag");
        else
            declaration_seq_until_module(false);
        finish_node(syntax_kind::private_module_fragment, mark, first);
    }
    return 0;
}

/* declarations until the end, a module-declaration, or (with private_)
   a private-module-fragment */
void parser::declaration_seq_until_module(bool private_)
{
    size_t mark = begin_node();
    token_index first = position;
    while (!at_end() && !at_module_declaration() &&
           !(private_ && at_private_module_fragment())) {
        declaration();
    }
    if (position != first)
        finish_node(syntax_kind::declaration_seq, mark, first);
}

} // namespace cplusplus
