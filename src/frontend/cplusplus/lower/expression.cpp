/* expression.cpp: expressions: each kind of semantic node as C (lower-internal.h) */
#include "lower-internal.h"

namespace cplusplus::lowering
{

uint32_t c_lowering::expression(uint32_t index)
{
    if (failure.what)
        return 0;
    at_result_object = index == result_object;
    if (at_result_object)
        result_object = 0;
    const semantic_node &n = node(index);
    switch (n.kind) {
    case semantic_kind::constant:
        return constant(n);
    case semantic_kind::string_literal:
        return string_literal(n);
    case semantic_kind::entity: {
        entity_index e = n.a;
        const entity &x = analysis.entity_table[e];
        uint32_t s = symbol(e, n.at);
        if (!s)
            return 0;
        ast_builder_symbol_flags_add(c, s, AST_SYMBOL_FLAG_USED);
        if (x.kind == entity_kind::function)
            return make(AST_NODE_VARIABLE, type(x.type, n.at), s, 0, 0, 0, 0);
        if (indirect_parameter(e))
            /* the object at the address the caller passed */
            return dereference(c_variable(c, s, parameter_type(x.type, n.at)),
                               type(x.type, n.at));
        ast_qualified_type t = type(x.type, n.at);
        uint32_t var = c_variable(c, s, t);
        if (!is_reference(x.type))
            return var;
        /* [dcl.ref]: a name of a reference is the object it refers to */
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), var, 0, 0, 0,
                    flags);
    }
    case semantic_kind::conversion:
        return conversion(n);
    case semantic_kind::unary: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        int op = c_operator(static_cast<preprocessing_op_or_punc>(n.op));
        if (op == LEX_TOKEN_EXCLAMATION)
            return to_bool(make(AST_NODE_UNARY,
                                ast_builder_type_basic(AST_TYPE_INT), truth(a),
                                0, 0, LEX_TOKEN_EXCLAMATION, 0));
        if (!op)
            return fail("a unary operator", n.at);
        return make(AST_NODE_UNARY,
                    AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at)), a, 0, 0,
                    static_cast<uint16_t>(op), 0);
    }
    case semantic_kind::address: {
        if (!is_glvalue(node(n.a)))
            /* the implicit object argument of a prvalue: [basic.lval]/7
               the temporary it is materialized into */
            return bind_reference(n);
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        return address_of(a, type(n.type, n.at));
    }
    case semantic_kind::indirection: {
        uint32_t a = expression(n.a);
        if (!a)
            return 0;
        uint8_t flags = AST_NODE_FLAG_LVALUE;
        if (record(n.type).kind == type_kind::function)
            flags = 0;
        return make(AST_NODE_DEREFERENCE, type(n.type, n.at), a, 0, 0, 0,
                    flags);
    }
    case semantic_kind::increment: {
        uint32_t a = object(n.a);
        if (!a)
            return 0;
        int op = AST_NODE_PRE_INCREMENT;
        switch (static_cast<increment_kind>(n.op)) {
        case increment_kind::pre_increment:
            op = AST_NODE_PRE_INCREMENT;
            break;
        case increment_kind::pre_decrement:
            op = AST_NODE_PRE_DECREMENT;
            break;
        case increment_kind::post_increment:
            op = AST_NODE_POST_INCREMENT;
            break;
        case increment_kind::post_decrement:
            op = AST_NODE_POST_DECREMENT;
            break;
        }
        return make(op, AST_QUALIFIED_TYPE_UNQUALIFIED(type_of(a)), a, 0, 0, 0,
                    0);
    }
    case semantic_kind::binary:
        return binary(n);
    case semantic_kind::logical_and:
    case semantic_kind::logical_or: {
        uint32_t a = expression(n.a);
        conditional_depth++;
        uint32_t b = expression(n.b);
        conditional_depth--;
        if (!a || !b)
            return 0;
        int op = AST_NODE_LOGICAL_AND;
        if (n.kind == semantic_kind::logical_or)
            op = AST_NODE_LOGICAL_OR;
        return to_bool(make(op, ast_builder_type_basic(AST_TYPE_INT), truth(a),
                            truth(b), 0, 0, 0));
    }
    case semantic_kind::assignment:
        return assignment(n);
    case semantic_kind::conditional: {
        conditional_depth++;
        uint32_t lowered = conditional(n);
        conditional_depth--;
        return lowered;
    }
    case semantic_kind::comma: {
        ast_qualified_type t = type(n.type, n.at);
        uint32_t a = discard(n.a);
        if (!is_glvalue(n))
            return comma(AST_QUALIFIED_TYPE_UNQUALIFIED(t), a, expression(n.b));
        ast_qualified_type pointer = pointer_to(t);
        uint32_t b = object(n.b);
        if (!b)
            return 0;
        uint32_t selected = comma(pointer, a, address_of(b, pointer));
        return dereference(selected, t);
    }
    case semantic_kind::call:
        /* [expr.const]/13: an immediate invocation is its value */
        if (n.evaluation == constness::constant)
            return constant(n);
        /* a class result returned by address that initializes no object:
           materialized ([class.temporary]/2) */
        if (!at_result_object && !is_glvalue(n) && passed_indirectly(n.type))
            return temporary_object(index);
        return call(n);
    case semantic_kind::subscript: {
        if (record(node(n.a).type).kind == type_kind::vector)
            return vector_lane(n);
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        return make(AST_NODE_INDEX, type(n.type, n.at), a, to_ptrdiff(b), 0, 0,
                    AST_NODE_FLAG_LVALUE);
    }
    case semantic_kind::member: {
        /* [expr.ref]/6.2: the member subobject */
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        const entity &x = analysis.entity_table[n.b];
        if (!is_reference(x.type) || is_reference(n.type))
            /* a reference member being initialized ([class.base.init]) is
               the pointer itself */
            return ast_builder_member(c, object, c_fields[n.b],
                                      type(n.type, n.at));
        /* [expr.ref]/6.1: a reference member is the object it refers to */
        uint32_t pointer =
            ast_builder_member(c, object, c_fields[n.b], type(x.type, n.at));
        return dereference(pointer, type(n.type, n.at));
    }
    case semantic_kind::member_pointer:
        return member_pointer(index);
    case semantic_kind::member_pointer_access:
        return member_pointer_object(n);
    case semantic_kind::zero:
        if (record(n.type).kind == type_kind::pointer_to_member)
            return null_member_pointer(n.type, n.at);
        if (holds_data_member_pointer(n.type))
            return fail("a zero-initialized object with a pointer to data "
                        "member in it",
                        n.at);
        return make(AST_NODE_ZERO, type(n.type, n.at), 0, 0, 0, 0, 0);
    case semantic_kind::initializer_list:
        return initializer_list(n);
    case semantic_kind::compound_literal:
        return compound_literal(n.a);
    case semantic_kind::vector_shuffle: {
        uint32_t a = expression(n.a), b = expression(n.b);
        if (!a || !b)
            return 0;
        if (n.op)
            return ast_builder_vector_minmax(c, n.op == 3, a, b);
        const uint32_t *items = analysis.semantics.items(n.first);
        int32_t indices[64];
        for (uint32_t i = 0; i < n.count && i < 64; i++)
            indices[i] = static_cast<int32_t>(node(items[i]).value);
        return ast_builder_shuffle_vector(
            c, AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at)), a, b,
            indices, n.count);
    }
    case semantic_kind::bind_reference:
        return bind_reference(n);
    case semantic_kind::construct:
        /* [class.temporary]/4: one with a destructor is destroyed at the
           end of the full-expression */
        if (!n.a && !at_result_object &&
            analysis.entity_table[record(n.type).entity].destructor)
            return temporary_object(index);
        return construct(n);
    case semantic_kind::new_object:
        return new_object(n);
    case semantic_kind::array_size:
        return array_size(n);
    case semantic_kind::delete_object:
        return delete_object(n);
    case semantic_kind::throw_exception:
        return fail("a throw-expression (no exception support)", n.at);
    case semantic_kind::type_identification:
        return fail("a typeid expression (no run-time type information)", n.at);
    case semantic_kind::array_count:
        if (!count_symbol)
            return fail("an array count outside new or delete", n.at);
        return c_variable(c, count_symbol, type(n.type, n.at));
    case semantic_kind::allocated:
        if (!allocated_symbol)
            return fail("an allocated pointer outside new or delete", n.at);
        return c_variable(c, allocated_symbol, allocated_type);
    case semantic_kind::va_arg_: {
        uint32_t list = expression(n.a);
        if (!list)
            return 0;
        return ast_builder_node(
            c, AST_NODE_VA_ARG,
            AST_QUALIFIED_TYPE_UNQUALIFIED(type(n.type, n.at)), list, 0, 0, 0,
            0);
    }
    case semantic_kind::vtable_pointer_store:
        return vtable_pointer_stores(n);
    case semantic_kind::destroy: {
        uint32_t object = expression(n.a);
        if (!object)
            return 0;
        uint32_t vtt = 0;
        if (entity_index b = base_subobject_of(n.a))
            vtt = vtt_argument(b, n.at);
        if (failure.what)
            return 0;
        return special_call(n.b,
                            address_of(object, pointer_to(type_of(object))),
                            nullptr, 0, n.at, vtt);
    }
    default:
        return fail("an expression of this kind", n.at);
    }
}

} // namespace cplusplus::lowering
