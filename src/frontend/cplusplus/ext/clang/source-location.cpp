/*
 * source-location.cpp: [ext:clang_source_location] (clang Language
 * Extensions, Source location builtins): __builtin_source_location(), the
 * primitive of libc++'s std::source_location::current
 * ([support.srcloc.cons]/1), and __builtin_FILE(), __builtin_FILE_NAME(),
 * __builtin_FUNCTION(), __builtin_LINE() and __builtin_COLUMN(), the
 * values it is made of.
 *
 *     primary-expression:
 *         clang-source-location-expression
 *
 *     clang-source-location-expression:
 *         __builtin_source_location ( )
 *         __builtin_FILE ( )
 *         __builtin_FILE_NAME ( )
 *         __builtin_FUNCTION ( )
 *         __builtin_LINE ( )
 *         __builtin_COLUMN ( )
 *
 * The values are those of the place the builtin is written at, except in
 * a default argument ([dcl.fct.default]/5, "evaluated each time the
 * function is called"), where they are those of the call that uses it (as
 * std::source_location::current's default argument is written), and in a
 * default member initializer, where they are those of the constructor that
 * uses it (not of an aggregate initialization's: declaration/initializer.cpp
 * analyses those, and gives them their own).  __builtin_LINE and
 * __builtin_COLUMN are unsigned int; the others const char*.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_source_location_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_source_location, __builtin_FILE, ... */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("'()' after the builtin's name", "ext.clang");
    return finish_node(syntax_kind::clang_source_location_expression, mark,
                       first);
}

/* [ext:clang_source_location] a prvalue of type "pointer to const
   std::source_location::__impl", which must be declared before; "a
   pointer to a global object holding the file name, function name, line
   and column" of the place it is evaluated at.  Only its type is
   analysed (decltype of it, libc++'s __bsl_ty): the object it points to
   is not made yet */
analysis_result
semantic_analysis::clang_source_location_object(SyntaxRef n,
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

/* [ext:clang_source_location] __builtin_LINE() and the others.  Where it is
   written, the place is known: a line and a column are constants, and a
   file name and a function name a node lowering makes the string of.  In a
   default argument ([dcl.fct.default]/5) the place is the call's, which
   is not known until a call uses the argument: a node for each, which
   bind_call_site gives that call's place */
analysis_result semantic_analysis::clang_source_location(SyntaxRef n,
                                                         expression_meaning &m)
{
    token_index first = n.first_token();
    owned_buffer spelling;
    tokens.spelling(first, spelling.get());
    static const struct {
        const char *name;
        source_location_builtin builtin;
    } builtins[] = {
        {"__builtin_FILE", source_location_builtin::file},
        {"__builtin_FILE_NAME", source_location_builtin::file_name},
        {"__builtin_FUNCTION", source_location_builtin::function},
        {"__builtin_LINE", source_location_builtin::line},
        {"__builtin_COLUMN", source_location_builtin::column},
    };
    bool found = false;
    source_location_builtin which = source_location_builtin::file;
    for (const auto &b : builtins)
        if (spelling.size() == strlen(b.name) &&
            !memcmp(spelling.data(), b.name, spelling.size())) {
            found = true;
            which = b.builtin;
        }
    if (!found)
        return clang_source_location_object(n, m);
    /* [cpp.replace]: in a macro's argument the place clang gives is the end
       of the outermost invocation, which a token does not know: a
       builtin on another line than the macro's name is not analysed (one
       in the macro's replacement list, or on its line, has the
       invocation's place) */
    const preprocessing_token &written = tokens[first].preprocessing;
    if (written.expansion && written.location > written.expansion) {
        source_position in_argument = tokens.files.position(written.location);
        source_position of_macro = tokens.files.position(written.expansion);
        if (!strcmp(in_argument.file_name, of_macro.file_name) &&
            in_argument.line != of_macro.line)
            return not_supported(n, "cpp.replace");
    }
    bool numeric = which == source_location_builtin::line ||
                   which == source_location_builtin::column;
    m = expression_meaning();
    m.category = value_category::prvalue;
    if (numeric) {
        m.type = qualified_type::make(
            type_table.fundamental(fundamental_type::unsigned_int), cv_none);
    } else {
        qualified_type character = qualified_type::make(
            type_table.fundamental(fundamental_type::char_), cv_const);
        m.type =
            qualified_type::make(type_table.pointer_to(character), cv_none);
    }
    /* a string's address is no constant analysis folds */
    m.evaluation = constness::unknown;
    if (!call_site_depth && numeric) {
        token_index at = first;
        if (source_location_at)
            at = source_location_at;
        source_position p = tokens.position(at);
        m.evaluation = constness::constant;
        m.value = p.line;
        if (which == source_location_builtin::column)
            m.value = p.column;
        m.node = expression_node(semantic_kind::constant, m, first);
        return analysis_result::well_formed;
    }
    token_index at = first;
    if (!call_site_depth && source_location_at)
        at = source_location_at;
    if (call_site_depth)
        call_site_nodes++;
    m.node =
        expression_node(semantic_kind::source_location, m, at, 0,
                        function_state.function, static_cast<uint8_t>(which));
    return analysis_result::well_formed;
}

/* [dcl.fct.default]/5: a default argument that made source_location nodes
   from `nodes_before` on is wrapped, so that each call may bind them */
uint32_t semantic_analysis::wrap_call_site(uint32_t node, token_index at,
                                           uint32_t nodes_before)
{
    if (call_site_nodes == nodes_before)
        return node;
    semantic_node wrapper = semantics[node];
    wrapper.kind = semantic_kind::default_argument;
    wrapper.evaluation = constness::unknown;
    wrapper.at = at;
    wrapper.a = node;
    wrapper.b = 0;
    wrapper.c = wrapper.d = 0;
    wrapper.first = wrapper.count = 0;
    wrapper.value = 0;
    wrapper.op = 0;
    return semantics.add(wrapper);
}

/* a call's use of a default argument: the argument bound to the call (the
   function the call is in, and where) */
uint32_t semantic_analysis::bind_call_site(uint32_t node, token_index call)
{
    if (semantics[node].kind != semantic_kind::default_argument)
        return node;
    semantic_node bound = semantics[node];
    bound.at = call;
    bound.b = function_state.function;
    return semantics.add(bound);
}

} // namespace cplusplus
