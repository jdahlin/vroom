/*
 * tree-check.h: -fcxx-dump=check-tree (doc/PLAN-cxx-sema.md 3.2, S1d): the
 * semantic tree of every well-formed definition is complete enough to be
 * lowered, and agrees with what analysis recorded.
 *
 *   - every expression analysis recorded in a well-formed declaration has
 *     a node, of the expression's type and value category;
 *   - every definition of a well-formed declaration has a tree, and every
 *     node in it has the children its kind needs (semantic_kind's
 *     comments), of the right sort: a statement where a statement goes, an
 *     expression where an expression goes;
 *   - the operands of arithmetic, the conditions and the converted values
 *     are prvalues ([conv]/1's conversions all explicit), a condition of
 *     bool type ([stmt.pre]/6);
 *   - an lvalue-to-rvalue conversion's operand is a glvalue, an assignment's
 *     left operand an lvalue.
 *
 * A line per problem:
 *
 *     tree FILE:LINE:COLUMN: KIND: PROBLEM
 *
 * which bench/cxx-sema.py counts as a failure of the run.
 */
#ifndef VCC_CPLUSPLUS_DUMP_TREE_CHECK_H
#define VCC_CPLUSPLUS_DUMP_TREE_CHECK_H

namespace
{

enum class tree_sort { expression, initializer, statement };

struct tree_checker {
    const semantic_analysis &analysis;
    token_stream &tokens;
    vbuffer *out;
    size_t problems = 0;

    void problem(uint32_t n, const char *what)
    {
        const semantic_node &node = analysis.semantics[n];
        source_position at = tokens.position(node.at);
        vbuffer_format_append(out, "tree %s:%u:%u: %s: %s\n", at.file_name,
                              at.line, at.column, semantic_kind_name(node.kind),
                              what);
        problems++;
    }

    static bool is_expression(semantic_kind k)
    {
        return (k >= semantic_kind::constant &&
                k <= semantic_kind::member_pointer_access) ||
               k == semantic_kind::condition_declaration ||
               /* T(args), a prvalue of class type ([expr.type.conv]/2) */
               k == semantic_kind::construct ||
               (k >= semantic_kind::new_object &&
                k <= semantic_kind::variable_size);
    }
    static bool is_initializer(semantic_kind k)
    {
        return is_expression(k) || k == semantic_kind::zero ||
               k == semantic_kind::initializer_list ||
               k == semantic_kind::bind_reference ||
               k == semantic_kind::construct || k == semantic_kind::destroy;
    }
    static bool is_statement(semantic_kind k)
    {
        return k >= semantic_kind::block && k <= semantic_kind::null_statement;
    }

    /* a class prvalue an initializer list builds where an expression is
       wanted: [expr.prim.lambda.closure]/2's closure object, which its
       captures initialize, and an aggregate's T{...} bound to a reference
       ([dcl.init.ref]/5.4: held in a temporary) (class_object) */
    bool class_object(uint32_t c) const
    {
        const semantic_node &node = analysis.semantics[c];
        if (node.kind != semantic_kind::initializer_list)
            return false;
        const struct type &record =
            analysis.type_table[node.type.unqualified()];
        return record.kind == type_kind::class_type;
    }

    bool is_bool(qualified_type t) const
    {
        const struct type &record = analysis.type_table[t.unqualified()];
        return record.kind == type_kind::fundamental &&
               record.fundamental == fundamental_type::bool_;
    }

    /* the child of parent in field what, of sort, 0 allowed if optional */
    void child(uint32_t parent, uint32_t c, tree_sort sort, bool optional,
               const char *what)
    {
        if (!c) {
            if (!optional) {
                char message[64];
                snprintf(message, sizeof message, "no %s", what);
                problem(parent, message);
            }
            return;
        }
        if (c >= analysis.semantics.size()) {
            problem(parent, "a child out of the tree");
            return;
        }
        semantic_kind k = analysis.semantics[c].kind;
        bool right = is_statement(k);
        if (sort == tree_sort::expression)
            right = is_expression(k) || class_object(c);
        else if (sort == tree_sort::initializer)
            right = is_initializer(k);
        if (!right) {
            char message[96];
            snprintf(message, sizeof message, "the %s is a %s", what,
                     semantic_kind_name(k));
            problem(parent, message);
            return;
        }
        walk(c);
    }

    void prvalue(uint32_t parent, uint32_t c, const char *what)
    {
        if (!c || c >= analysis.semantics.size())
            return;
        if (analysis.semantics[c].category != value_category::prvalue) {
            char message[64];
            snprintf(message, sizeof message, "the %s is not a prvalue", what);
            problem(parent, message);
        }
    }

    void condition(uint32_t parent, uint32_t c)
    {
        if (!c || c >= analysis.semantics.size())
            return;
        prvalue(parent, c, "condition");
        if (!is_bool(analysis.semantics[c].type))
            problem(parent, "the condition is not a bool");
    }

    void list(uint32_t parent, tree_sort sort, const char *what)
    {
        const semantic_node &node = analysis.semantics[parent];
        const uint32_t *items = analysis.semantics.items(node.first);
        for (uint32_t i = 0; i < node.count; i++)
            child(parent, items[i], sort, false, what);
    }

    void walk(uint32_t n)
    {
        const semantic_node &node = analysis.semantics[n];
        if (is_expression(node.kind) && !node.type.unqualified())
            problem(n, "no type");
        switch (node.kind) {
        case semantic_kind::none:
            problem(n, "a node of no kind");
            break;
        case semantic_kind::constant:
        case semantic_kind::string_literal:
        case semantic_kind::zero:
            break;
        case semantic_kind::entity:
            if (!node.a)
                problem(n, "no entity");
            break;
        case semantic_kind::conversion:
            child(n, node.a, tree_sort::expression, false, "operand");
            if (node.category != value_category::prvalue)
                problem(n, "a conversion's value is not a prvalue");
            if (node.op ==
                    static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue) &&
                node.a &&
                analysis.semantics[node.a].category == value_category::prvalue)
                problem(n, "the operand of an lvalue-to-rvalue conversion is "
                           "a prvalue");
            if ((node.op ==
                     static_cast<uint8_t>(conversion_kind::derived_to_base) ||
                 node.op ==
                     static_cast<uint8_t>(conversion_kind::base_to_derived)) &&
                (!node.b || !analysis.entity_table[node.b].base_subobject))
                problem(n, "no base subobject");
            /* the conversions of a glvalue, and the discarding of any
               value ([expr.static.cast]/6) */
            if (node.op !=
                    static_cast<uint8_t>(conversion_kind::lvalue_to_rvalue) &&
                node.op !=
                    static_cast<uint8_t>(conversion_kind::array_to_pointer) &&
                node.op != static_cast<uint8_t>(
                               conversion_kind::function_to_pointer) &&
                node.op != static_cast<uint8_t>(conversion_kind::to_void))
                prvalue(n, node.a, "operand");
            break;
        case semantic_kind::unary:
            child(n, node.a, tree_sort::expression, false, "operand");
            prvalue(n, node.a, "operand");
            break;
        case semantic_kind::address:
        case semantic_kind::increment:
            child(n, node.a, tree_sort::expression, false, "operand");
            break;
        case semantic_kind::indirection:
            child(n, node.a, tree_sort::expression, false, "operand");
            prvalue(n, node.a, "operand");
            break;
        case semantic_kind::binary:
        case semantic_kind::subscript:
            child(n, node.a, tree_sort::expression, false, "left operand");
            child(n, node.b, tree_sort::expression, false, "right operand");
            prvalue(n, node.a, "left operand");
            prvalue(n, node.b, "right operand");
            break;
        case semantic_kind::logical_and:
        case semantic_kind::logical_or:
            child(n, node.a, tree_sort::expression, false, "left operand");
            child(n, node.b, tree_sort::expression, false, "right operand");
            condition(n, node.a);
            condition(n, node.b);
            break;
        case semantic_kind::assignment:
            child(n, node.a, tree_sort::expression, false, "left operand");
            child(n, node.b, tree_sort::expression, false, "right operand");
            if (node.a &&
                analysis.semantics[node.a].category != value_category::lvalue)
                problem(n, "the left operand is not an lvalue");
            prvalue(n, node.b, "right operand");
            break;
        case semantic_kind::conditional:
            child(n, node.a, tree_sort::expression, false, "condition");
            child(n, node.b, tree_sort::expression, !!node.d, "second operand");
            /* [ext:gnu_conditional_omitted] its value */
            child(n, node.d, tree_sort::expression, !!node.b, "first operand");
            child(n, node.c, tree_sort::expression, false, "third operand");
            condition(n, node.a);
            break;
        case semantic_kind::comma:
            child(n, node.a, tree_sort::expression, false, "left operand");
            child(n, node.b, tree_sort::expression, false, "right operand");
            break;
        case semantic_kind::call:
            child(n, node.a, tree_sort::expression, false, "function");
            child(n, node.b, tree_sort::expression, true, "object address");
            if (node.b)
                prvalue(n, node.b, "object address");
            list(n, tree_sort::initializer, "argument");
            break;
        case semantic_kind::member:
            child(n, node.a, tree_sort::expression, false, "object");
            if (!node.b)
                problem(n, "no member");
            break;
        case semantic_kind::member_pointer:
            if (!node.a || !analysis.entity_table[node.a].nonstatic_member)
                problem(n, "no non-static member");
            break;
        case semantic_kind::member_pointer_access:
            child(n, node.a, tree_sort::expression, false, "object address");
            prvalue(n, node.a, "object address");
            child(n, node.b, tree_sort::expression, false, "pointer to member");
            prvalue(n, node.b, "pointer to member");
            break;
        case semantic_kind::bound_member_function:
            /* only a call's function, which the call replaces */
            problem(n, "a bound member function outside a call");
            break;
        case semantic_kind::compound_literal:
            child(n, node.a, tree_sort::initializer, false, "initializer");
            break;
        case semantic_kind::vector_shuffle:
            child(n, node.a, tree_sort::expression, false, "first vector");
            child(n, node.b, tree_sort::expression, false, "second vector");
            list(n, tree_sort::expression, "index");
            break;
        case semantic_kind::initializer_list:
            list(n, tree_sort::initializer, "element");
            break;
        case semantic_kind::bind_reference:
            child(n, node.a, tree_sort::expression, false, "referent");
            break;
        case semantic_kind::construct:
            child(n, node.a, tree_sort::expression, true, "object");
            if (!node.b || analysis.entity_table[node.b].special !=
                               special_member::constructor)
                problem(n, "no constructor");
            list(n, tree_sort::initializer, "argument");
            break;
        case semantic_kind::va_arg_:
            child(n, node.a, tree_sort::expression, false, "va_list");
            break;
        case semantic_kind::gnu_asm_operand:
            child(n, node.a, tree_sort::expression, false, "expression");
            if (node.value && node.a && node.a < analysis.semantics.size() &&
                analysis.semantics[node.a].category != value_category::lvalue)
                problem(n, "an asm output is not an lvalue");
            break;
        case semantic_kind::source_location:
        case semantic_kind::label_address:
        case semantic_kind::variable_size:
            break;
        case semantic_kind::default_argument:
            child(n, node.a, tree_sort::initializer, false, "argument");
            break;
        case semantic_kind::vtable_pointer_store:
            child(n, node.a, tree_sort::expression, false, "object address");
            prvalue(n, node.a, "object address");
            if (!node.b || !analysis.entity_table[node.b].dynamic)
                problem(n, "no dynamic class");
            break;
        case semantic_kind::destroy:
            child(n, node.a, tree_sort::expression, false, "object");
            if (!node.b || analysis.entity_table[node.b].special !=
                               special_member::destructor)
                problem(n, "no destructor");
            break;
        case semantic_kind::new_object:
            child(n, node.a, tree_sort::expression, false, "allocation");
            child(n, node.b, tree_sort::initializer, true, "initialization");
            child(n, node.d, tree_sort::expression, true, "count");
            prvalue(n, node.a, "allocation");
            break;
        case semantic_kind::allocated:
        case semantic_kind::array_count:
            break;
        case semantic_kind::array_size:
            child(n, node.a, tree_sort::expression, false, "count");
            prvalue(n, node.a, "count");
            break;
        case semantic_kind::delete_object:
            child(n, node.a, tree_sort::expression, false, "operand");
            child(n, node.b, tree_sort::initializer, true, "destruction");
            child(n, node.c, tree_sort::expression, false, "deallocation");
            prvalue(n, node.a, "operand");
            break;
        case semantic_kind::throw_exception:
            child(n, node.a, tree_sort::expression, true, "operand");
            break;
        case semantic_kind::type_identification:
            break;
        case semantic_kind::block:
        case semantic_kind::declarations:
            list(n, tree_sort::statement, "statement");
            break;
        case semantic_kind::expression_statement:
            /* a destructor's call of a subobject's destructor
               ([class.dtor]/13) is a statement of its own */
            if (node.a && node.a < analysis.semantics.size() &&
                analysis.semantics[node.a].kind == semantic_kind::destroy)
                child(n, node.a, tree_sort::initializer, false, "expression");
            else
                child(n, node.a, tree_sort::expression, false, "expression");
            break;
        case semantic_kind::declaration_statement:
            if (!node.a)
                problem(n, "no variable");
            child(n, node.b, tree_sort::initializer, true, "initializer");
            break;
        case semantic_kind::if_statement:
            child(n, node.d, tree_sort::statement, true, "init-statement");
            child(n, node.a, tree_sort::expression, false, "condition");
            condition(n, node.a);
            child(n, node.b, tree_sort::statement, false, "substatement");
            child(n, node.c, tree_sort::statement, true, "else");
            break;
        case semantic_kind::while_statement:
            child(n, node.a, tree_sort::expression, false, "condition");
            condition(n, node.a);
            child(n, node.b, tree_sort::statement, false, "substatement");
            break;
        case semantic_kind::do_statement:
            child(n, node.a, tree_sort::statement, false, "substatement");
            child(n, node.b, tree_sort::expression, false, "condition");
            condition(n, node.b);
            break;
        case semantic_kind::for_statement:
            /* none in the loop [stmt.ranged]/1 expands to: its declarations
               precede it in a block */
            child(n, node.a, tree_sort::statement, true, "init-statement");
            child(n, node.b, tree_sort::expression, true, "condition");
            condition(n, node.b);
            child(n, node.c, tree_sort::expression, true, "expression");
            child(n, node.d, tree_sort::statement, false, "substatement");
            break;
        case semantic_kind::switch_statement:
            child(n, node.d, tree_sort::statement, true, "init-statement");
            child(n, node.a, tree_sort::expression, false, "condition");
            prvalue(n, node.a, "condition");
            child(n, node.b, tree_sort::statement, false, "substatement");
            break;
        case semantic_kind::case_label:
        case semantic_kind::default_label:
        case semantic_kind::identifier_label:
            child(n, node.a, tree_sort::statement, false, "statement");
            break;
        case semantic_kind::return_statement:
            child(n, node.a, tree_sort::initializer, true, "operand");
            break;
        case semantic_kind::indirect_goto_statement:
            child(n, node.a, tree_sort::expression, false, "address");
            prvalue(n, node.a, "address");
            break;
        case semantic_kind::goto_statement:
        case semantic_kind::break_statement:
        case semantic_kind::continue_statement:
        case semantic_kind::null_statement:
            break;
        case semantic_kind::gnu_asm_statement:
            list(n, tree_sort::expression, "operand");
            break;
        case semantic_kind::condition_declaration:
            child(n, node.a, tree_sort::statement, false, "declaration");
            child(n, node.b, tree_sort::expression, false, "value");
            break;
        case semantic_kind::function_body:
            if (!node.a ||
                analysis.semantics[node.a].kind != semantic_kind::block)
                problem(n, "no outermost block");
            else
                walk(node.a);
            child(n, node.b, tree_sort::statement, true,
                  "member initializations");
            child(n, node.c, tree_sort::statement, true,
                  "subobject destructions");
            break;
        }
    }
};

/* the result of each declaration of declarations, by its node */
analysis_result declaration_result(const semantic_analysis &analysis,
                                   node_index declaration)
{
    for (const analysed_declaration &d : analysis.declarations)
        if (d.node == declaration)
            return d.result;
    return analysis_result::unsupported;
}

size_t check_tree(const semantic_analysis &analysis, const syntax_tree &tree,
                  token_stream &tokens, vbuffer *out)
{
    tree_checker checker{analysis, tokens, out};
    size_t j = 0;
    for (const analysed_expression &x : analysis.expressions) {
        while (j < analysis.declarations.size() &&
               analysis.declarations[j].node != x.declaration)
            j++;
        if (j == analysis.declarations.size())
            break;
        if (analysis.declarations[j].result != analysis_result::well_formed)
            continue;
        uint32_t n = x.meaning.node;
        if (!n) {
            token_index at = x.at.token();
            if (!x.at.is_token())
                at = tree[x.at.index()].first_token;
            source_position p = tokens.position(at);
            vbuffer_format_append(out,
                                  "tree %s:%u:%u: an expression without "
                                  "a node\n",
                                  p.file_name, p.line, p.column);
            checker.problems++;
            continue;
        }
        const semantic_node &node = analysis.semantics[n];
        if (!(node.type == x.meaning.type))
            checker.problem(n, "the node's type is not the expression's");
        if (node.category != x.meaning.category)
            checker.problem(n, "the node's value category is not the "
                               "expression's");
    }
    for (const semantic_analysis::definition_record &d : analysis.definitions) {
        if (declaration_result(analysis, d.declaration) !=
            analysis_result::well_formed)
            continue;
        uint32_t root = analysis.entity_table[d.entity].definition;
        if (!root) {
            source_position p =
                tokens.position(tree[d.declaration].first_token);
            vbuffer_format_append(out,
                                  "tree %s:%u:%u: a definition without "
                                  "a tree\n",
                                  p.file_name, p.line, p.column);
            checker.problems++;
            continue;
        }
        checker.walk(root);
    }
    return checker.problems;
}

/*
 * -dump-tree: each definition's tree, a node per line, indented by depth:
 *
 *     KIND[/OP] LINE:COLUMN [TYPE CATEGORY] [=VALUE] [entity #N]
 *
 * with the type in the claims' encoding (type-claims.h).
 */
void dump_node(const semantic_analysis &analysis, token_stream &tokens,
               uint32_t n, unsigned depth, vbuffer *out)
{
    const semantic_node &node = analysis.semantics[n];
    for (unsigned i = 0; i < depth; i++)
        vbuffer_string_append(out, "  ");
    source_position at = tokens.position(node.at);
    vbuffer_format_append(out, "%s", semantic_kind_name(node.kind));
    if (node.op)
        vbuffer_format_append(out, "/%u", node.op);
    vbuffer_format_append(out, " %u:%u", at.line, at.column);
    bool expression = tree_checker::is_initializer(node.kind);
    if (expression) {
        vbuffer_string_append(out, " ");
        if (!write_expression_claim(analysis.type_table, node.type,
                                    node.category, out))
            vbuffer_string_append(out, "?");
    }
    if (node.kind == semantic_kind::constant ||
        node.kind == semantic_kind::case_label)
        vbuffer_format_append(out, " =%llu",
                              static_cast<unsigned long long>(node.value));
    bool a_is_entity = node.kind == semantic_kind::entity ||
                       node.kind == semantic_kind::member_pointer ||
                       node.kind == semantic_kind::declaration_statement;
    if (a_is_entity)
        vbuffer_format_append(out, " entity #%u", node.a);
    vbuffer_string_append(out, "\n");
    uint32_t children[4] = {node.a, node.b, node.c, node.d};
    /* a member's and a constructor call's b is an entity */
    if (node.kind == semantic_kind::member ||
        node.kind == semantic_kind::construct)
        children[1] = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (i == 0 && a_is_entity)
            continue;
        if (children[i] && children[i] < analysis.semantics.size())
            dump_node(analysis, tokens, children[i], depth + 1, out);
    }
    if (node.kind == semantic_kind::function_body) {
        for (unsigned i = 0; i <= depth; i++)
            vbuffer_string_append(out, "  ");
        vbuffer_string_append(out, "parameters");
        for (uint32_t i = 0; i < node.count; i++)
            vbuffer_format_append(out, " #%u",
                                  analysis.semantics.items(node.first)[i]);
        vbuffer_string_append(out, "\n");
        return;
    }
    for (uint32_t i = 0; i < node.count; i++)
        dump_node(analysis, tokens, analysis.semantics.items(node.first)[i],
                  depth + 1, out);
}

void dump_tree(const semantic_analysis &analysis, token_stream &tokens,
               vbuffer *out)
{
    for (const semantic_analysis::definition_record &d : analysis.definitions) {
        uint32_t root = analysis.entity_table[d.entity].definition;
        vbuffer_format_append(out, "definition of entity #%u\n", d.entity);
        if (root)
            dump_node(analysis, tokens, root, 1, out);
    }
}

} // namespace

#endif
