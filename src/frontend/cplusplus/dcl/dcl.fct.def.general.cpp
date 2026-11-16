/*
 * dcl.fct.def.general.cpp: [dcl.fct.def.general] Function definitions: the
 * function-body.  The function-definition itself is parsed with the
 * simple-declaration it shares its beginning with (dcl.pre.cpp).
 *
 *     function-body:
 *         ctor-initializer_opt compound-statement
 *         function-try-block
 *         = default ;
 *         = delete ;
 *
 * [basic.scope.param]/1: the parameters' potential scope goes on through
 * the compound-statement ("If the nearest enclosing function declarator is
 * not the declarator of a function definition, the potential scope ends at
 * the end of that function declarator"), the ctor-initializer's included
 * ([class.base.init]).  The function-try-block is P6's.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [dcl.fct.def.default]/1: "A function that is explicitly defaulted shall
 * (1.1) be a special member function or a comparison operator function
 * ([over.binary]), and (1.2) not have default arguments."  What the
 * declarator-id alone shows: a special member function is a constructor
 * (the class's name, [class.ctor]/1), a destructor (~), a copy or move
 * assignment operator (operator=); a comparison operator function is
 * operator==, !=, <, >, <=, >= or <=> (C++20's P0515R3 and P0905R1; in an
 * earlier edition a later edition's).  Any other identifier, operator or
 * conversion function cannot be defaulted.  Whether a constructor or
 * operator= is a special member function depends on its parameters'
 * types: a semantic rule.
 */
void parser::defaultable_declarator(const declarator_facts &declarator,
                                    bool member)
{
    if (!declarator.has_id || declarator.qualified_id)
        return;
    token_index at_id = declarator.id_first;
    if (declarator.name) {
        if (member && declarator.name == member_class_name)
            return;
        violates("only a special member function or a comparison operator "
                 "function is explicitly defaulted",
                 "dcl.fct.def.default", at_id);
        return;
    }
    if (keyword_of(tokens[at_id]) != standard_keyword::operator_)
        return; /* ~ class-name: a destructor */
    static const preprocessing_op_or_punc comparisons[] = {
        preprocessing_op_or_punc::EQUAL_EQUAL,
        preprocessing_op_or_punc::EXCLAMATION_EQUAL,
        preprocessing_op_or_punc::LESS,
        preprocessing_op_or_punc::GREATER,
        preprocessing_op_or_punc::LESS_EQUAL,
        preprocessing_op_or_punc::GREATER_EQUAL,
        preprocessing_op_or_punc::SPACESHIP};
    const token &op = tokens[at_id + 1];
    if (is_operator(op, preprocessing_op_or_punc::EQUAL))
        return;
    for (preprocessing_op_or_punc c : comparisons) {
        if (!is_operator(op, c))
            continue;
        if (!tokens.files.behavior.has(
                language_feature::three_way_comparison_operator))
            beyond_this_step();
        return;
    }
    violates("only a special member function or a comparison operator "
             "function is explicitly defaulted",
             "dcl.fct.def.default", at_id);
}

node_index parser::function_body(const declarator_facts &declarator,
                                 bool member)
{
    size_t mark = begin_node();
    token_index first = position;
    if (member && collecting_members &&
        (at(preprocessing_op_or_punc::LEFT_BRACE) ||
         at(preprocessing_op_or_punc::COLON))) {
        /* a complete-class context: the first pass over the
           member-specification (class.mem.cpp) keeps it as its tokens, a
           ctor-initializer's up to the compound-statement, which follows a
           mem-initializer's ')' or '}' (or its '...') */
        if (at(preprocessing_op_or_punc::COLON)) {
            consume();
            while (!at_end() && !at(preprocessing_op_or_punc::SEMICOLON)) {
                bool after_group =
                    is_operator(tokens[position - 1],
                                preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                    is_operator(tokens[position - 1],
                                preprocessing_op_or_punc::RIGHT_BRACE) ||
                    is_operator(tokens[position - 1],
                                preprocessing_op_or_punc::ELLIPSIS);
                if (at(preprocessing_op_or_punc::LEFT_BRACE) && after_group)
                    break;
                if (at(preprocessing_op_or_punc::RIGHT_BRACE) ||
                    at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                    break;
                unsigned end = group_end(0);
                if (end)
                    position += end;
                else
                    consume();
            }
            add(tree.make(syntax_kind::unparsed, nullptr, 0, first, position));
        }
        if (at(preprocessing_op_or_punc::LEFT_BRACE))
            unparsed_group();
        return finish_node(syntax_kind::function_body, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE) ||
        at(preprocessing_op_or_punc::COLON)) {
        /* in a class, a complete-class context ([class.mem]/6) */
        unsigned contexts = names.complete_contexts;
        if (member)
            names.complete_contexts++;
        scope_index enclosing = names.current();
        if (declarator.has_parameters)
            names.reenter(declarator.parameters);
        if (at(preprocessing_op_or_punc::COLON))
            ctor_initializer();
        if (!failed()) {
            if (at(preprocessing_op_or_punc::LEFT_BRACE))
                compound_statement();
            else
                expected("'{' to begin the function-body",
                         "dcl.fct.def.general");
        }
        names.reenter(enclosing);
        names.complete_contexts = contexts;
        if (failed())
            return 0;
    } else {
        if (at(standard_keyword::default_, 1)) {
            defaultable_declarator(declarator, member);
            if (failed())
                return 0;
        }
        add_token(); /* = */
        bool deleted = at(standard_keyword::delete_);
        add_token(); /* default or delete */
        /* C++26's
               = delete deleted-function-message_opt ;   (P2573R2)
           with deleted-function-message: ( string-literal ); in an earlier
           edition [ext.clang] clang_deleted_function_message */
        const implementation_defined_behavior &b = tokens.files.behavior;
        if (deleted && at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            (b.has(language_feature::deleted_function_message) ||
             b.has(extension::clang_deleted_function_message))) {
            add_token();
            if (peek().literal != literal_kind::string_literal)
                return expected("a string-literal", "dcl.fct.def.delete");
            add_token();
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')'", "dcl.fct.def.delete");
        }
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the function-body",
                            "dcl.fct.def.general");
    }
    return finish_node(syntax_kind::function_body, mark, first);
}

} // namespace cplusplus
