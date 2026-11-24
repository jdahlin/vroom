/*
 * try-block.cpp: [except.pre] Exception handling: the try-block and its
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
 * a function-body's (function-definition.cpp).  [basic.scope.block]/1:
 * "Each ... handler ... introduces a block scope that includes that
 * statement or handler", so the exception-declaration's name is declared
 * in a scope around the handler's compound-statement.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [except.pre]/1: "The optional attribute-specifier-seq in an
 * exception-declaration appertains to the formal parameter of the
 * handler."  The try-block's compound-statement and each handler are
 * block scopes ([basic.scope.block]/1).
 */
/* a labeled-statement (a label, case or default) anywhere in n: a jump may enter the try-block or a handler
   ([stmt.dcl]/3, [except.handle]/2), which is not analysed */
static bool has_label(SyntaxElement n)
{
    if (n.is_token())
        return false;
    if (n.is(syntax_kind::labeled_statement))
        return true;
    for (auto c : n.elements())
        if (has_label(c))
            return true;
    return false;
}

analysis_result semantic_analysis::try_block(TryBlock n, uint32_t &built)
{
    if (has_label(n))
        return not_supported(n, "except.handle");
    /* try compound-statement handler-seq (one handler alone is a child) */
    if (n.size() < 3 || !n[1].is(syntax_kind::compound_statement))
        return not_supported(n, "except.pre");
    uint32_t body = 0;
    analysis_result result;
    {
        scoped_value<size_t> inside(try_entities, entity_table.size());
        result = statement(n[1], body);
    }
    owned_array<uint32_t> handlers;
    owned_array<SyntaxElement> items;
    for (uint32_t i = 2; i < n.size(); i++) {
        if (n[i].is(syntax_kind::handler_seq)) {
            for (auto h : n[i].elements())
                items.append(h);
        } else {
            items.append(n[i]);
        }
    }
    for (size_t i = 0; i < items.size(); i++) {
        if (!items[i].is(syntax_kind::handler))
            return worse(result, not_supported(n, "except.pre"));
        uint32_t one = 0;
        /* [except.handle]/5: "A handler for a ... catch (...) ... shall be the last handler" */
        result = worse(result, handler(Handler(items[i]), one));
        handlers.append(one);
    }
    built = statement_node(semantic_kind::try_statement, n.first_token(), body);
    semantics.set_list(built, handlers.data(), handlers.size());
    return result;
}

/*
 * [except.handle]/1: "The exception-declaration shall not denote an
 * incomplete type, an abstract class type, or an rvalue reference type";
 * "A handler of type 'array of T' or function type T is adjusted to be of
 * type 'pointer to T'" (/3 matches by the type without references and
 * top-level cv).  /10: "the variable declared by the exception-declaration,
 * of type cv T or cv T&, is initialized from the exception object, of type
 * E, as follows" (copy-initialization from an lvalue of the object's type).
 */
analysis_result semantic_analysis::handler(Handler n, uint32_t &built)
{
    if (n.size() != 5 || !n[2].is(syntax_kind::exception_declaration) ||
        !n[4].is(syntax_kind::compound_statement))
        return not_supported(n, "except.pre");
    SyntaxElement declaration = n[2];
    entity_index outer = current_scope;
    open_scope(0, false);
    analysis_result result = analysis_result::well_formed;
    entity_index variable = 0;
    uint32_t declared = 0;
    qualified_type caught;
    bool catch_all = declaration.size() == 1 && declaration[0].is_token();
    if (!catch_all) {
        uint32_t spec = 0, name = 0;
        for (uint32_t i = 0; i < declaration.size(); i++) {
            if (declaration[i].is(syntax_kind::decl_specifier_seq,
                                  syntax_kind::type_specifier_seq))
                spec = i + 1;
            else if (spec && !name)
                name = i + 1;
        }
        if (!spec) {
            current_scope = outer;
            return not_supported(n, "except.pre");
        }
        specified s;
        result = decl_specifier_seq(declaration[spec - 1], s, false);
        if (result == analysis_result::well_formed && name) {
            size_t first = block_variables.size();
            bool claimable = declarations_claimable;
            declarations_claimable = false;
            declarator_meaning probe;
            qualified_type t = s.type;
            result = declarator(declaration, declaration[name - 1], t, probe);
            if (result == analysis_result::well_formed && probe.has_id) {
                /* [except.handle]/10: initialized from an lvalue of the exception object's type */
                caught = decayed(t);
                expression_meaning m;
                m.type = qualified_type::make(caught.unqualified(), cv_none);
                m.category = value_category::lvalue;
                m.evaluation = constness::not_constant;
                m.node = expression_node(semantic_kind::exception_object, m,
                                         n.first_token());
                result = declare_with_initializer(
                    declaration, declaration[name - 1], declaration[name - 1],
                    true, s, false, &m);
                if (result == analysis_result::well_formed) {
                    variable = last_declared;
                    declared = declaration_statements(first, n.first_token());
                } else {
                    block_variables.resize(first);
                }
            } else {
                caught = t;
            }
            declarations_claimable = claimable;
        } else if (result == analysis_result::well_formed) {
            caught = s.type;
        }
        /* [except.handle]/10: "if the exception-declaration does not specify a name, a temporary is
           copy-initialized from the exception object": a class type by value is not analysed */
        if (result == analysis_result::well_formed && !variable &&
            type_table[caught.unqualified()].kind == type_kind::class_type) {
            current_scope = outer;
            return not_supported(n, "except.handle");
        }
        if (result == analysis_result::well_formed &&
            abstract_class(decayed(caught)))
            result = ill_formed(declaration,
                                "the exception-declaration shall not denote "
                                "an abstract class type",
                                "except.handle");
        if (result == analysis_result::well_formed &&
            type_table[caught.unqualified()].kind ==
                type_kind::rvalue_reference)
            result = ill_formed(declaration,
                                "the exception-declaration shall not denote "
                                "an rvalue reference type",
                                "except.handle");
        caught = decayed(caught);
    }
    uint32_t body = 0;
    if (result == analysis_result::well_formed) {
        /* [basic.scope.block]/2: the handler's outermost block shall not redeclare the exception-declaration's name */
        entity_index handler_scope = current_scope;
        current_scope = handler_scope;
        result = worse(result, compound_statement(CompoundStatement(n[4]),
                                                  handler_scope, body));
    }
    current_scope = outer;
    (void)variable;
    built = statement_node(semantic_kind::handler, n.first_token(), declared, 0,
                           body);
    semantics[built].computation = caught;
    return result;
}

} // namespace cplusplus
