/* definitions.cpp: definitions: functions, variables, the translation unit (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

bool c_lowering::definition_body(entity_index e, uint32_t s, int variant)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    labels.clear();
    label_paths.clear();
    label_paths_made = false;
    function_root = body.a;
    in_function = true;
    /* every parameter has a symbol, an unnamed one too */
    owned_array<uint32_t> parameters;
    const struct type &f = record(x.type);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    const uint32_t *items = analysis.semantics.items(body.first);
    /* a member function's this comes first */
    uint32_t implicit = 0;
    if (body.count > f.parameter_count)
        implicit = body.count - f.parameter_count;
    for (uint32_t i = 0; i < body.count; i++) {
        uint32_t p = 0;
        if (items[i])
            p = symbol(items[i], body.at);
        else if (i >= implicit)
            p = c_local(c, parameter_type(types[i - implicit], body.at));
        if (!p)
            return fail("a parameter", body.at);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        parameters.append(p);
    }
    structor_variant = variant;
    structor_class = 0;
    structor_this = 0;
    vtt_parameter = 0;
    if (variant) {
        structor_class = x.enclosing;
        structor_this = items[0];
    }
    if (variant == 2) {
        /* the VTT parameter after this */
        vtt_parameter = c_local(c, pointer_to(ast_builder_type_pointer(c, v)));
        ast_builder_symbol_flags_add(c, vtt_parameter,
                                     AST_SYMBOL_FLAG_PARAMETER);
        owned_array<uint32_t> with_vtt;
        for (uint32_t i = 0; i < parameters.size(); i++) {
            with_vtt.append(parameters[i]);
            if (!i)
                with_vtt.append(vtt_parameter);
        }
        parameters.clear();
        for (uint32_t p : with_vtt)
            parameters.append(p);
    }
    live.clear();
    named_return = 0;
    if (passed_indirectly(f.operand)) {
        bool mixed = false;
        entity_index found = 0;
        find_named_return(body.a, found, mixed);
        if (!mixed)
            named_return = found;
    }
    if (body.c) {
        scope_record r;
        r.kind = scope_record::subobjects;
        r.symbol = body.c;
        live.append(r);
    }
    uint32_t prefix = 0;
    if (body.b)
        prefix = statement(body.b);
    uint32_t block_node = scoped_block(node(body.a), prefix);
    if (body.c) {
        /* [class.dtor]/13: after the body, the members and bases */
        uint32_t items[2] = {block_node, statement(body.c)};
        block_node = block(items, 2);
    }
    in_function = false;
    structor_variant = 0;
    structor_class = 0;
    structor_this = 0;
    vtt_parameter = 0;
    if (failure.what)
        return false;
    struct node &b = ast_builder_ast(c)->nodes[block_node];
    uint32_t count = static_cast<uint32_t>(parameters.size());
    b.a = ast_builder_list(c, parameters.data(), count);
    b.aux = static_cast<uint16_t>(count);
    ast_builder_symbol_body(c, s, block_node);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::function_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    const semantic_node &body = node(x.definition);
    if (with_virtual_bases(e)) {
        /* [abi:mangling-special-ctor-dtor]: the base object variant (the
           VTT after this, no virtual base), then the complete object one,
           each from the body; the second lowering makes its own symbols
           of the automatic variables */
        uint32_t base = symbol(e, body.at, 2);
        if (!base)
            return false;
        owned_array<entity_index> before;
        for (entity_index i = 0; i < symbols.size(); i++)
            if (!symbols[i])
                before.append(i);
        if (!definition_body(e, base, 2))
            return false;
        for (entity_index i : before)
            if (analysis.entity_table[i].kind == entity_kind::variable &&
                analysis.entity_table[i].automatic)
                symbols[i] = 0;
        if (x.inline_ && !complete_symbols[e]) {
            /* [abi:vague-inline]: made where called (complete_variants) */
            inline_structors.append(e);
        } else if (!complete_variant(e)) {
            return false;
        }
        if (x.virtual_ && !thunk_definitions(e, body.at))
            return false;
        return !(x.virtual_ && x.special == special_member::destructor &&
                 !deleting_destructor(e, body.at, true));
    }
    uint32_t s = symbol(e, body.at);
    if (!s || !definition_body(e, s, 0))
        return false;
    const struct type &f = record(x.type);
    const qualified_type *types =
        analysis.type_table.parameters(x.type.unqualified());
    if (x.virtual_ && !thunk_definitions(e, body.at))
        return false;
    if (x.virtual_ && x.special == special_member::destructor &&
        !deleting_destructor(e, body.at, true))
        return false;
    if (x.special == special_member::none || x.inline_)
        return true;
    /* [abi:mangling-special-ctor-dtor]: the complete object variant, which
       for a class without virtual bases does what the base object one does
       (clang makes it an alias): a call of it */
    uint32_t complete = symbol(e, body.at, 1);
    if (!complete)
        return false;
    owned_array<uint32_t> forwarded, arguments;
    owned_array<ast_qualified_type> parameter_types;
    parameter_types.append(
        pointer_to(type(analysis.entity_table[x.enclosing].type, body.at)));
    for (uint32_t i = 0; i < f.parameter_count; i++)
        parameter_types.append(parameter_type(types[i], body.at));
    for (uint32_t i = 0; i < body.count; i++) {
        ast_qualified_type t = parameter_types[i];
        uint32_t p = c_local(c, t);
        ast_builder_symbol_flags_add(c, p, AST_SYMBOL_FLAG_PARAMETER);
        forwarded.append(p);
        arguments.append(c_variable(c, p, t));
    }
    ast_qualified_type function = entity_type(e, body.at);
    uint32_t callee = cast(make(AST_NODE_VARIABLE, function, s, 0, 0, 0, 0),
                           pointer_to(function), AST_CAST_FUNCTION_DECAY);
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
    uint32_t call = make(AST_NODE_CALL, v, callee,
                         ast_builder_list(c, arguments.data(), body.count),
                         body.count, 0, 0);
    uint32_t statement_node = c_statement(c, call);
    uint32_t thunk = block(&statement_node, 1);
    struct node &t = ast_builder_ast(c)->nodes[thunk];
    t.a = ast_builder_list(c, forwarded.data(), body.count);
    t.aux = static_cast<uint16_t>(body.count);
    ast_builder_symbol_body(c, complete, thunk);
    ast_builder_symbol_flags_add(c, complete, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::variable_definition(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    uint32_t s = symbol(e, 0);
    if (!s)
        return false;
    if (!static_zero_initialization(x, s, 0))
        return false;
    if (x.definition) {
        /* [basic.start.static]/2: constant initialization; anything else is
           dynamic initialization, which needs code before main */
        if (!constant_initializer(x.definition) &&
            !needs_run_time(x.definition))
            return fail("a constant whose value analysis does not know",
                        node(x.definition).at);
        qualified_type object;
        entity_index destructor = element_destructor(x.type, object);
        bool constant = constant_initializer(x.definition);
        /* clang: "initializer for thread-local variable must be a constant
           expression", "type of thread-local variable has non-trivial
           destruction" */
        if (x.c11_thread_local && (!constant || destructor))
            return fail("a _Thread_local variable initialized or destroyed "
                        "at run time",
                        node(x.definition).at);
        if (x.inline_ && (!constant || destructor))
            /* [basic.start.dynamic]/1: partially-ordered, under a guard in
               a COMDAT group ([abi:guards]) */
            return fail("an inline variable with dynamic initialization or a "
                        "destructor",
                        node(x.definition).at);
        if (!constant) {
            /* [basic.start.static]/2: zero-initialized, then initialized
               by dynamic_initialization */
            ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
            return global_initializer(e, false);
        }
        uint32_t init = expression(x.definition);
        if (!init)
            return false;
        ast_builder_symbol_body(c, s, init);
        if (destructor) {
            /* [basic.start.term]/1: constant-initialized, its destructor
               registered at start-up */
            if (!global_initializer(e, true))
                return false;
        }
    } else {
        qualified_type object;
        if (element_destructor(x.type, object)) {
            /* [basic.start.term]/1: zero-initialized (no non-trivial
               default constructor), its destructor registered */
            if (!global_initializer(e, true))
                return false;
        }
    }
    ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_DEFINED);
    return true;
}

bool c_lowering::translation_unit()
{
    if (!cplusplus::analysis_complete(analysis, failure))
        return false;
    /* the namespace-scope functions and variables, in the order they are
       first declared */
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        if (x.kind != entity_kind::function && x.kind != entity_kind::variable)
            continue;
        if (x.automatic || enclosing_function(d.entity))
            continue;
        /* a function with a placeholder return type never defined: its
           name is in no expression ([dcl.spec.auto]/11), so it needs no
           symbol (clang emits none) */
        if (x.kind == entity_kind::function && x.placeholder_return &&
            !x.defined)
            continue;
        /* a deleted function: no symbol ([dcl.fct.def.delete]) */
        if (x.kind == entity_kind::function && x.deleted)
            continue;
        /* a function declared, not defined, whose symbol is not mangled
           yet (a specialization of a template whose signature names a
           dependent decltype): a use of it fails where it is.
           [impl.clang] a specialization for a closure unique to the
           translation unit: its symbol made at its first use, which
           numbers the closure (itanium_mangler) */
        if ((x.kind == entity_kind::function && !x.defined) ||
            x.instantiation) {
            owned_buffer probe;
            analysis.number_closures = false;
            bool mangled = itanium_symbol_name(analysis, tokens.pp, d.entity,
                                               probe.get(), 0);
            analysis.number_closures = true;
            if (!mangled)
                continue;
        }
        declaring_symbols = true;
        bool made = symbol(d.entity, 0);
        declaring_symbols = false;
        if (!made)
            return false;
    }
    owned_array<entity_index> done;
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &x = analysis.entity_table[d.entity];
        /* a deleted function has no code ([dcl.fct.def.delete]) */
        if (!x.defined || x.automatic || enclosing_function(d.entity) ||
            x.deleted)
            continue;
        /* [temp.explicit]/11: an explicit instantiation declaration's
           specialization is defined in another translation unit (clang
           emits none, not even an inline one) */
        if (analysis.explicitly_instantiated(d.entity) ==
            instantiation_kind::declaration)
            continue;
        bool again = false;
        for (entity_index s : done)
            if (s == d.entity)
                again = true;
        if (again)
            continue;
        done.append(d.entity);
        if (x.kind == entity_kind::function) {
            if (!function_definition(d.entity))
                return false;
        } else if (x.kind == entity_kind::variable) {
            if (!variable_definition(d.entity))
                return false;
        }
    }
    if (!initializers.empty() && !dynamic_initialization())
        return false;
    /* [abi:vague-vtable]: the virtual table of each class whose key
       function this translation unit defines */
    for (entity_index k = 1; k < analysis.entity_table.size(); k++) {
        const dynamic_class *d = analysis.dynamic_of(k);
        if (d && d->key_function &&
            analysis.entity_table[d->key_function].defined && !vtable(k, 0))
            return false;
    }
    /* the member functions defined, in the order of their definitions,
       and the functions friend declarations define ([class.friend]/6) */
    for (const semantic_analysis::definition_record &d : analysis.definitions) {
        const entity &x = analysis.entity_table[d.entity];
        bool friend_ = analysis.defined_by_friend(d.entity);
        if (!x.defined && !friend_)
            continue;
        if (x.kind != entity_kind::function ||
            (!friend_ &&
             (!x.enclosing || analysis.entity_table[x.enclosing].kind !=
                                  entity_kind::class_type)))
            continue;
        if (analysis.explicitly_instantiated(d.entity) ==
            instantiation_kind::declaration)
            continue;
        if (!function_definition(d.entity))
            return false;
    }
    if (!complete_variants())
        return false;
    return !failure.what;
}

} // namespace cplusplus::lowering

namespace cplusplus
{

using lowering::c_lowering;

bool analysis_complete(const semantic_analysis &analysis, lowering_failure &why)
{
    for (const analysed_declaration &d : analysis.declarations)
        if (d.result != analysis_result::well_formed) {
            why.at = d.node.first_token();
            if (d.result == analysis_result::unsupported && d.has_unsupported) {
                const unsupported_construct &u =
                    analysis.unsupported[d.first_unsupported];
                why.subclause = u.subclause;
                why.file = u.file;
                why.line = u.line;
                why.function = u.function;
                why.at = u.node.first_token();
            }
            why.what = "a declaration that is not well-formed";
            why.front_end = true;
            if (d.result == analysis_result::not_parsed) {
                why.what = "a declaration the parser kept as tokens";
                /* the first part of it the parser kept, and why */
                auto node = d.node;
                for (const parser::unparsed_reason &r :
                     analysis.parsed().unparsed_reasons)
                    if (r.first >= node.first_token() &&
                        r.first < node.end_token() && r.expected) {
                        why.parse_at = r.at;
                        why.expected = r.expected;
                        why.parse_subclause = r.subclause;
                        why.later_step = r.later_step;
                        break;
                    }
            }
            return false;
        }
    return true;
}

bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why, const char *main_path)
{
    c_lowering lowering(analysis, tokens, c, target, main_path);
    /* [impl.clang]: the closures unique to the translation unit numbered
       in the order their symbols are made (itanium_mangler) */
    analysis.unnumbered_closures.clear();
    analysis.number_closures = true;
    bool lowered = lowering.translation_unit();
    why = lowering.failure;
    return lowered;
}

} // namespace cplusplus
