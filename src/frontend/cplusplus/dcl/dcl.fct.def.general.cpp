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
 * ([class.base.init]), and into a function-try-block's handlers.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

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
    bool function_try = at(standard_keyword::try_);
    if (member && collecting_members &&
        (at(preprocessing_op_or_punc::LEFT_BRACE) ||
         at(preprocessing_op_or_punc::COLON) || function_try)) {
        /* a complete-class context: the first pass over the
           member-specification (class.mem.cpp) keeps it as its tokens, a
           ctor-initializer's up to the compound-statement, which follows a
           mem-initializer's ')' or '}' (or its '...'), and a
           function-try-block's handlers */
        if (function_try)
            consume();
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
        }
        if (at(preprocessing_op_or_punc::LEFT_BRACE))
            position += group_end(0);
        while (function_try && at(standard_keyword::catch_)) {
            consume();
            if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
                position += group_end(0);
            if (at(preprocessing_op_or_punc::LEFT_BRACE))
                position += group_end(0);
        }
        add(tree.make(syntax_kind::unparsed, nullptr, 0, first, position));
        return finish_node(syntax_kind::function_body, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE) ||
        at(preprocessing_op_or_punc::COLON) || function_try) {
        /* in a class, a complete-class context ([class.mem]/6) */
        unsigned contexts = names.complete_contexts;
        if (member)
            names.complete_contexts++;
        scope_index enclosing = names.current();
        if (declarator.has_parameters)
            names.reenter(declarator.parameters);
        if (function_try)
            add_token(); /* try */
        if (at(preprocessing_op_or_punc::COLON))
            ctor_initializer();
        if (!failed()) {
            if (at(preprocessing_op_or_punc::LEFT_BRACE))
                compound_statement();
            else
                expected("'{' to begin the function-body",
                         "dcl.fct.def.general");
        }
        /* [except.handle]/11: "The scope and lifetime of the parameters
           of a function or constructor extend into the handlers of a
           function-try-block." */
        if (function_try && !failed())
            handler_seq();
        names.reenter(enclosing);
        names.complete_contexts = contexts;
        if (failed())
            return 0;
        /* function-body: function-try-block passes it through */
        if (function_try)
            return finish_node(syntax_kind::function_try_block, mark, first);
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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [dcl.fct.def.general]/1: the function-body of a function definition
 * whose declarator analysis has given its entity (last_declared) and its
 * parameters (function_parameters).  [basic.scope.param]/1: the parameters
 * have function parameter scope, which ends at the end of the outermost
 * block; [basic.funscope]/1: "Labels have function scope and may be used
 * anywhere in the function in which they are declared."
 */
analysis_result semantic_analysis::function_body(node_index definition,
                                                 syntax_child compound)
{
    entity_index f = last_declared;
    qualified_type returns =
        type_table[entity_table[f].type.unqualified()].operand;
    function_state.active = true;
    function_state.returns = returns;
    function_state.loops = 0;
    function_state.breakable = 0;
    function_state.labels.clear();
    function_state.gotos.clear();
    function_state.initialized.clear();
    function_state.switches.clear();
    function_state.case_values.clear();
    analysis_result result = analysis_result::well_formed;
    collect_labels(compound);
    /* [stmt.label]/1: "Labels shall not be redeclared within a function." */
    for (size_t i = 0; i < function_state.labels.size(); i++)
        for (size_t k = 0; k < i; k++)
            if (function_state.labels[k].name ==
                    function_state.labels[i].name &&
                result != analysis_result::ill_formed)
                result = ill_formed_at_token(
                    function_state.labels[i].point.at,
                    "labels shall not be redeclared within a function",
                    "stmt.label");
    entity_index parameters = open_scope(0, true);
    /* the scope the parameters are declared in is the function's
       (entity::enclosing), and its blocks' local names are the function's
       ([abi:mangling-scope]) */
    entity_table[parameters].enclosing = f;
    owned_array<uint32_t> parameter_entities;
    for (const parameter_meaning &parameter : function_parameters) {
        parameter_entities.append(0);
        if (!parameter.id)
            continue;
        uint32_t name = tokens.identifier(parameter.id);
        if (entity_table.find(parameters, name)) {
            /* [basic.def.odr]/1: each parameter of a function definition
               is a definition ([basic.def]/2) */
            result =
                worse(result, ill_formed_at_token(parameter.id,
                                                  "no translation unit shall "
                                                  "contain more than one "
                                                  "definition of any variable",
                                                  "basic.def.odr"));
            continue;
        }
        entity_index e =
            entity_table.make(entity_kind::variable, name, parameters);
        entity_table[e].type = parameter.type;
        entity_table[e].defined = true;
        entity_table[e].automatic = true;
        entity_table.add_declaration(e, definition);
        entity_table.bind(parameters, name, e);
        parameter_entities.back() = e;
    }
    open_scope(parameters, false);
    uint32_t block = 0;
    result = worse(result, block_items(compound.index(), block));
    /* the function's semantic tree: its outermost block and parameters */
    uint32_t body = statement_node(semantic_kind::function_body,
                                   first_token(compound), block);
    if (!parameter_entities.empty()) {
        semantics[body].first =
            semantics.list(&parameter_entities[0], parameter_entities.size());
        semantics[body].count =
            static_cast<uint32_t>(parameter_entities.size());
    }
    entity_table[f].definition = body;
    definition_record record;
    record.declaration = current_declaration;
    record.entity = f;
    definitions.append(record);
    for (const goto_record &g : function_state.gotos) {
        const label_record *target = nullptr;
        for (const label_record &l : function_state.labels)
            if (l.name == g.name && !target)
                target = &l;
        /* [stmt.goto]/1: "The identifier shall be a label located in the
           current function." */
        if (!target) {
            result = worse(result, ill_formed(g.node,
                                              "the identifier shall be a label "
                                              "located in the current function",
                                              "stmt.goto"));
            continue;
        }
        if (!target->analysed) {
            result = worse(result, not_supported(g.node, "stmt.dcl"));
            continue;
        }
        if (jump_bypasses(g.point, target->point))
            result = worse(result,
                           ill_formed(g.node,
                                      "it is possible to transfer into a "
                                      "block, but not in a way that bypasses "
                                      "declarations with initialization",
                                      "stmt.dcl"));
    }
    current_scope = 0;
    claim_place = 0;
    claim_scope = 0;
    function_state.active = false;
    return result;
}

} // namespace cplusplus
