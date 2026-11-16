/*
 * except.pre.cpp: [except.pre] Exception handling: the try-block and its
 * handlers.
 *
 *     try-block:
 *         try compound-statement handler-seq
 *
 *     function-try-block:
 *         try ctor-initializer_opt compound-statement handler-seq
 *
 *     handler-seq:
 *         handler handler-seq_opt
 *
 *     handler:
 *         catch ( exception-declaration ) compound-statement
 *
 *     exception-declaration:
 *         attribute-specifier-seq_opt type-specifier-seq declarator
 *         attribute-specifier-seq_opt type-specifier-seq abstract-declarator_opt
 *         ...
 *
 * [stmt.pre]/1 makes the try-block a statement; the function-try-block is
 * a function-body's (dcl.fct.def.general.cpp).  [basic.scope.block]/1:
 * "Each ... handler ... introduces a block scope that includes that
 * statement or handler", so the exception-declaration's name is declared
 * in a scope around the handler's compound-statement.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::try_block()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* try */
    if (!at(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("'{' to begin the try-block's compound-statement",
                        "except.pre");
    compound_statement();
    if (failed())
        return 0;
    handler_seq();
    if (failed())
        return 0;
    return finish_node(syntax_kind::try_block, mark, first);
}

/* one handler or more; one alone passes through */
node_index parser::handler_seq()
{
    size_t mark = begin_node();
    token_index first = position;
    if (!at(standard_keyword::catch_))
        return expected("a handler (catch) after the compound-statement",
                        "except.pre");
    unsigned count = 0;
    while (at(standard_keyword::catch_)) {
        handler();
        if (failed())
            return 0;
        count++;
    }
    if (count == 1)
        return 0;
    return finish_node(syntax_kind::handler_seq, mark, first);
}

node_index parser::handler()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* catch */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after catch", "except.pre");
    declarative_region scope(names, scope_kind::block);
    exception_declaration();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the exception-declaration", "except.pre");
    if (!at(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("'{' to begin the handler's compound-statement",
                        "except.pre");
    compound_statement();
    if (failed())
        return 0;
    return finish_node(syntax_kind::handler, mark, first);
}

/*
 * A type-specifier-seq, so no storage class or other decl-specifier
 * ([dcl.type]/1), then a declarator or an abstract-declarator: whether it
 * declares a name is the declarator's to say ([dcl.name]/1: an
 * abstract-declarator is a declarator without the declarator-id).
 */
node_index parser::exception_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    if (accept(preprocessing_op_or_punc::ELLIPSIS))
        return finish_node(syntax_kind::exception_declaration, mark, first);
    attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq", "except.pre");
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        declarator_facts facts;
        declarator(declarator_form::either, facts);
        if (failed())
            return 0;
        if (facts.name && !facts.qualified_id)
            names.declare(facts.name, name_kind::other);
    }
    return finish_node(syntax_kind::exception_declaration, mark, first);
}

} // namespace cplusplus
