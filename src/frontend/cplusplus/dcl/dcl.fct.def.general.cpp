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

/*
 * [dcl.fct.def.general]/1's function-body, and before C++26
 * [ext:clang_deleted_function_message]:
 *
 *     function-body:
 *         = delete ( string-literal ) ;
 */
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
           edition [ext:clang_deleted_function_message] (above) */
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
namespace
{

/* what a constexpr function's body encloses that the rules of its edition
   ask about: a return statement, and the first iteration statement, which
   C++11 does not allow (0: none) */
struct constexpr_body {
    bool returns = false;
    token_index not_in_cplusplus_11 = 0;
    /* the declaration statements of automatic variables */
    owned_array<uint32_t> variables;
};

void walk_constexpr_body(const semantic_tree &tree, uint32_t n,
                         constexpr_body &found)
{
    if (!n)
        return;
    const semantic_node &x = tree[n];
    switch (x.kind) {
    case semantic_kind::block:
    case semantic_kind::declarations: {
        const uint32_t *items = tree.items(x.first);
        for (uint32_t i = 0; i < x.count; i++)
            walk_constexpr_body(tree, items[i], found);
        return;
    }
    case semantic_kind::return_statement:
        found.returns = true;
        return;
    case semantic_kind::declaration_statement:
        found.variables.append(n);
        return;
    /* (clang allows the other statements as extensions, expression
       statements too; an evaluation that modifies an object is what C++11
       cannot run, its "never produces a constant expression") */
    case semantic_kind::while_statement:
    case semantic_kind::do_statement:
    case semantic_kind::for_statement:
        if (!found.not_in_cplusplus_11)
            found.not_in_cplusplus_11 = x.at;
        break;
    default:
        break;
    }
    switch (x.kind) {
    case semantic_kind::if_statement:
        walk_constexpr_body(tree, x.b, found);
        walk_constexpr_body(tree, x.c, found);
        return;
    case semantic_kind::while_statement:
    case semantic_kind::switch_statement:
        walk_constexpr_body(tree, x.b, found);
        return;
    case semantic_kind::do_statement:
    case semantic_kind::case_label:
    case semantic_kind::default_label:
    case semantic_kind::identifier_label:
        walk_constexpr_body(tree, x.a, found);
        return;
    case semantic_kind::for_statement:
        walk_constexpr_body(tree, x.d, found);
        return;
    default:
        return;
    }
}

} // namespace

analysis_result semantic_analysis::function_body(node_index definition,
                                                 syntax_child compound,
                                                 syntax_child ctor_init)
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
    entity_index outer_scope = current_scope;
    entity_index parameters = open_scope(0, true);
    /* the scope the parameters are declared in is the function's
       (entity::enclosing), and its blocks' local names are the function's
       ([abi:mangling-scope]) */
    entity_table[parameters].enclosing = f;
    owned_array<uint32_t> parameter_entities;
    function_state.function = f;
    function_state.this_entity = 0;
    if (entity_table[f].nonstatic_member) {
        /* [class.this]/1: "The type of this in a member function whose
           type has a cv-qualifier-seq cv and whose class is X is "pointer
           to cv X"."  An unnamed variable lowering makes the first
           parameter */
        entity_index c = entity_table[f].enclosing;
        qualified_type object = qualified_type::make(
            entity_table[c].type.unqualified(), entity_table[f].member_cv);
        entity_index t =
            entity_table.make(entity_kind::variable, 0, parameters);
        entity_table[t].type =
            qualified_type::make(type_table.pointer_to(object), cv_none);
        entity_table[t].defined = true;
        entity_table[t].automatic = true;
        parameter_entities.append(t);
        function_state.this_entity = t;
    }
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
    /* [class.base.init]/2: "Lookup for an unqualified name in a
       mem-initializer-id ignores the constructor's function parameter
       scope" (the name is looked up in the class); the expressions are in
       the function parameter scope */
    current_scope = parameters;
    uint32_t members = 0;
    /* [class.base.init]/9: a constructor without a ctor-initializer
       default-initializes its bases and members as well */
    if (ctor_init.index() ||
        entity_table[f].special == special_member::constructor)
        result = worse(result, ctor_initializer(ctor_init, members));
    else if (entity_table[f].special == special_member::destructor)
        /* [class.cdtor]/4: in the destructor, a virtual call finds this
           class's final overriders: its virtual table pointer stored on
           entry ([abi:vtable-ctor]) */
        members = vtable_pointer_store(entity_table[f].enclosing,
                                       first_token(compound));
    open_scope(parameters, false);
    uint32_t block = 0;
    result = worse(result, block_items(compound.index(), block));
    uint32_t destructions = 0;
    if (entity_table[f].special == special_member::destructor)
        destructions = subobject_destructions(entity_table[f].enclosing,
                                              first_token(compound));
    /* the function's semantic tree: its outermost block and parameters */
    uint32_t body =
        statement_node(semantic_kind::function_body, first_token(compound),
                       block, members, destructions);
    if (!parameter_entities.empty()) {
        semantics[body].first =
            semantics.list(&parameter_entities[0], parameter_entities.size());
        semantics[body].count =
            static_cast<uint32_t>(parameter_entities.size());
    }
    entity_table[f].definition = body;
    /* a body analysed in part has a tree the evaluator cannot run */
    if (result != analysis_result::well_formed)
        semantics[body].evaluation = constness::not_constant;
    /* [dcl.constexpr]/7: "If the instantiated template specialization of
       a constexpr function template ... would fail to satisfy the
       requirements for a constexpr function, that specialization is still
       a constexpr function, even though a call to such a function cannot
       appear in a constant expression." */
    bool specialization = entity_table[f].instantiation != 0;
    if (result == analysis_result::well_formed && entity_table[f].constexpr_ &&
        specialization) {
        const implementation_defined_behavior &b = tokens.files.behavior;
        bool literal = true;
        if (!b.has(language_feature::constexpr_without_constant_result)) {
            if (literal_type(returns) == constness::not_constant)
                literal = false;
            type_index ft = entity_table[f].type.unqualified();
            const qualified_type *parameters = type_table.parameters(ft);
            for (uint32_t i = 0; i < type_table[ft].parameter_count; i++)
                if (literal_type(parameters[i]) == constness::not_constant)
                    literal = false;
        }
        if (!literal)
            semantics[body].evaluation = constness::not_constant;
    }
    if (result == analysis_result::well_formed && entity_table[f].constexpr_ &&
        !specialization && entity_table[f].special == special_member::none) {
        const implementation_defined_behavior &b = tokens.files.behavior;
        constexpr_body found;
        walk_constexpr_body(semantics, block, found);
        bool returns_void = fundamental_of(returns) == fundamental_type::void_;
        /* C++11's [dcl.constexpr]/3: the function-body "shall be ... a
           compound-statement that contains only" null statements,
           static_assert-declarations, typedefs, using-declarations and
           -directives "and exactly one return statement", the return type
           a literal type (void is not one there); clang allows the
           other statements as extensions */
        if (!b.has(language_feature::relaxed_constexpr)) {
            if (found.not_in_cplusplus_11)
                result = worse(
                    result,
                    ill_formed_at_token(
                        found.not_in_cplusplus_11,
                        "before C++14 the body of a constexpr function shall "
                        "contain no iteration statement",
                        "dcl.constexpr"));
            if (returns_void)
                result = worse(
                    result,
                    ill_formed_at_token(
                        first_token(compound),
                        "before C++14 the return type of a constexpr function "
                        "shall be a literal type, which void is not",
                        "dcl.constexpr"));
        }
        /* [dcl.constexpr]/3 until C++23: "its return type (if any) shall
           be a literal type; each of its parameter types shall be a
           literal type"; and (/3.5.3) its body shall not contain "a
           definition of a variable of non-literal type" (P2448R2,
           P2242R3) */
        if (!b.has(language_feature::constexpr_without_constant_result)) {
            if (literal_type(returns) == constness::not_constant)
                result = worse(result, ill_formed_at_token(
                                           first_token(compound),
                                           "until C++23 the return type of a "
                                           "constexpr function shall be a "
                                           "literal type",
                                           "dcl.constexpr"));
            type_index ft = entity_table[f].type.unqualified();
            const qualified_type *parameters = type_table.parameters(ft);
            for (uint32_t i = 0; i < type_table[ft].parameter_count; i++)
                if (literal_type(parameters[i]) == constness::not_constant)
                    result = worse(
                        result,
                        ill_formed_at_token(first_token(compound),
                                            "until C++23 each parameter type "
                                            "of a constexpr function shall "
                                            "be a literal type",
                                            "dcl.constexpr"));
        }
        if (!b.has(language_feature::non_literal_constexpr_variables))
            for (uint32_t v : found.variables) {
                const entity &e = entity_table[semantics[v].a];
                if (e.automatic &&
                    literal_type(e.type) == constness::not_constant)
                    result = worse(
                        result,
                        ill_formed_at_token(
                            semantics[v].at,
                            "until C++23 the body of a constexpr function "
                            "shall not define a variable of non-literal type",
                            "dcl.constexpr"));
            }
        /* [dcl.constexpr]/6: "if no argument values exist such that an
           invocation of the function ... could be an evaluated
           subexpression of a core constant expression ..., the program is
           ill-formed, no diagnostic required": a function that returns a
           value and has no return statement flows off its end
           ([stmt.return]/2), which clang diagnoses until C++23 */
        if (!b.has(language_feature::constexpr_without_constant_result) &&
            !returns_void && !found.returns)
            result =
                worse(result,
                      ill_formed_at_token(first_token(compound),
                                          "a constexpr function that returns a "
                                          "value shall have a return statement "
                                          "(no invocation of it is a constant "
                                          "expression)",
                                          "dcl.constexpr"));
        /* the same rule for every argument value: the body evaluated with
           the parameters unknown fails before it reads one (clang's
           "constexpr function never produces a constant expression"; not
           in a system header, where libstdc++ declares constexpr functions
           on builtins clang does not fold) */
        else if (!b.has(language_feature::constexpr_without_constant_result) &&
                 !in_system_header(first_token(compound)) &&
                 potential_constant(body) == constness::not_constant)
            result = worse(
                result,
                extension_used(diagnostic_id::sema_constexpr_never_constant,
                               first_token(compound), nullptr));
    }
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
    current_scope = outer_scope;
    claim_place = 0;
    claim_scope = outer_scope;
    function_state.active = false;
    function_state.function = 0;
    function_state.this_entity = 0;
    return result;
}

} // namespace cplusplus
