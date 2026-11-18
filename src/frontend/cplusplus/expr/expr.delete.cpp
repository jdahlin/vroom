/*
 * expr.delete.cpp: [expr.delete] Delete.
 *
 *     delete-expression:
 *         ::_opt delete cast-expression
 *         ::_opt delete [ ] cast-expression
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.delete]/1: "Whenever the delete keyword is immediately followed by
 * empty square brackets, it shall be interpreted as the second
 * alternative."  A '[' with anything in it begins the operand (a
 * lambda-expression).
 */
node_index parser::delete_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    accept(preprocessing_op_or_punc::COLON_COLON);
    add_token(); /* delete */
    if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
        at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)) {
        add_token();
        add_token();
    }
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::delete_expression, mark, first);
}

/* ---- semantic analysis ---- */

/*
 * [expr.delete]/1: "The operand shall be of pointer to object type or of
 * class type. ... The delete-expression's result has type void."  /6: "If
 * the value of the operand of the delete-expression is not a null pointer
 * value ..., the delete-expression will invoke the destructor (if any) for
 * the object or the elements of the array being deleted."  /7: "the
 * delete-expression shall call a deallocation function".
 *
 * Not analysed: an operand of class type (/2), a pointer to void or to an
 * incomplete class (/5), the array delete of a class with a destructor
 * (its cookie, [expr.new]/15), a destroying operator delete and
 * new-extended alignment (/10).
 */
analysis_result semantic_analysis::delete_expression(node_index n,
                                                     expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    token_index at = node.first_token;
    uint32_t count = node.child_count;
    uint32_t i = 0;
    bool global = false;
    if (children[0].is_token() && tokens[children[0].index()].kind ==
                                      token_kind::operator_or_punctuator) {
        global = true;
        i++;
    }
    i++; /* delete */
    if (count < i + 1)
        return not_supported(n, "expr.delete");
    /* delete [ ] cast-expression */
    bool array = count - i == 3;
    expression_meaning operand;
    analysis_result r = expression(children[count - 1], operand);
    if (r != analysis_result::well_formed)
        return r;
    if (type_table[operand.type.unqualified()].kind == type_kind::class_type)
        /* /2: "the operand is converted to a pointer type by calling the
           above-mentioned conversion function" */
        return not_supported(n, "expr.delete");
    r = operand_value(n, operand);
    if (r != analysis_result::well_formed)
        return r;
    const struct type &pointer = type_table[operand.type.unqualified()];
    if (pointer.kind != type_kind::pointer)
        return ill_formed(n,
                          "the operand shall be of pointer to object type or "
                          "of class type",
                          "expr.delete");
    qualified_type object_type = pointer.operand;
    const struct type &object = type_table[object_type.unqualified()];
    if (object.kind == type_kind::function)
        return ill_formed(n,
                          "the operand shall be of pointer to object type or "
                          "of class type",
                          "expr.delete");
    if (object.kind == type_kind::fundamental &&
        object.fundamental == fundamental_type::void_)
        /* clang: a warning, "cannot delete expression with pointer-to-'void'
           type" */
        return not_supported(n, "expr.delete");
    entity_index c = 0;
    if (object.kind == type_kind::class_type) {
        c = object.entity;
        if (entity_table[c].kind != entity_kind::class_type ||
            !entity_table[c].size)
            /* /5: "If the object being deleted has incomplete class type at
               the point of deletion and the complete class has a
               non-trivial destructor or a deallocation function, the
               behavior is undefined." */
            return not_supported(n, "expr.delete");
    }
    uint64_t size = size_of_type(object_type);
    if (!size || alignment_of_type(object_type) > 16)
        return not_supported(n, "expr.delete");
    /* the operand's value, which the destructor and the deallocation
       function are given; /11 (a note): "Any cv-qualifiers in the type of
       the deleted object are ignored when forming this argument." */
    qualified_type unqualified =
        qualified_type::make(object_type.unqualified(), cv_none);
    expression_meaning deleted;
    deleted.type =
        qualified_type::make(type_table.pointer_to(unqualified), cv_none);
    deleted.category = value_category::prvalue;
    deleted.evaluation = constness::not_constant;
    deleted.node = expression_node(semantic_kind::allocated, deleted, at);
    uint32_t destroyed = 0;
    if (c && destroys(c)) {
        entity_index destructor = entity_table[c].destructor;
        if (array || !destructor)
            return not_supported(n, "expr.delete");
        r = deleted_use(at, destructor);
        if (r != analysis_result::well_formed)
            return r;
        expression_meaning target;
        target.type = unqualified;
        target.category = value_category::lvalue;
        target.evaluation = constness::not_constant;
        target.node = expression_node(semantic_kind::indirection, target, at,
                                      deleted.node);
        expression_meaning call;
        call.type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
        call.category = value_category::prvalue;
        call.evaluation = constness::not_constant;
        destroyed = expression_node(semantic_kind::destroy, call, at,
                                    target.node, destructor);
    }
    /* /9: "When the keyword delete in a delete-expression is preceded by
       the unary :: operator, the deallocation function's name is looked up
       in global scope.  Otherwise, the lookup considers class-specific
       deallocation functions ([class.free]).  If no class-specific
       deallocation function is found, the deallocation function's name is
       looked up in global scope." */
    allocation_function kind = allocation_function::delete_;
    if (array)
        kind = allocation_function::delete_array;
    owned_array<entity_index> set;
    bool in_class = false;
    entity_index scope = c;
    if (global)
        scope = 0;
    r = allocation_functions(n, kind, scope, set, in_class);
    if (r != analysis_result::well_formed)
        return r;
    /* [basic.stc.dynamic.deallocation]/3: "A usual deallocation function
       is a deallocation function whose parameters after the first are ...
       optionally, a parameter of type std::size_t" (the other forms are
       not declared: see implicit_allocation_functions) */
    qualified_type size_type = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    entity_index unsized = 0, sized = 0;
    for (entity_index f : set) {
        const struct type &ft = type_table[entity_table[f].type.unqualified()];
        const qualified_type *parameters =
            type_table.parameters(entity_table[f].type.unqualified());
        if (ft.variadic)
            continue;
        if (ft.parameter_count == 1)
            unsized = f;
        else if (ft.parameter_count == 2 && parameters[1] == size_type)
            sized = f;
    }
    /* [expr.delete]/10: "If deallocation function lookup finds more than
       one usual deallocation function, the function to be called is
       selected as follows: ... If exactly one function remains, that
       function is selected ... If the deallocation functions have class
       scope, the one without a parameter of type std::size_t is selected.
       If the type is complete and if, for an array delete expression only,
       the operand is a pointer to a class type with a non-trivial
       destructor or a (possibly multi-dimensional) array thereof, the
       function with a parameter of type std::size_t is selected.
       Otherwise, it is unspecified whether a deallocation function with a
       parameter of type std::size_t is selected."  As clang: the sized one
       for a single object, the unsized one for an array. */
    entity_index function = unsized;
    if (!unsized || (sized && !in_class && !array))
        function = sized;
    if (!function)
        /* a deallocation function that is no usual one: clang's "no
           suitable member 'operator delete'" */
        return not_supported(n, "expr.delete");
    owned_array<expression_meaning> arguments;
    arguments.append(deleted);
    if (function == sized)
        /* /11: "If a deallocation function with a parameter of type
           std::size_t is used, the size of the deleted object in a
           single-object delete expression ... is passed as the
           corresponding argument." */
        arguments.append(
            integer_constant(fundamental_type::unsigned_long_int, size, at));
    expression_meaning call;
    r = call_function(n, at, function, arguments, call);
    if (r != analysis_result::well_formed)
        return r;
    m = expression_meaning();
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::delete_object, m, at, operand.node,
                             destroyed);
    semantics[m.node].c = call.node;
    semantics[m.node].computation = deleted.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
