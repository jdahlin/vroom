/*
 * predefined-names.cpp: [dcl.fct.def.general]/8 __func__ and
 * [ext:clang_predefined_names] __FUNCTION__ and __PRETTY_FUNCTION__.
 *
 *     primary-expression:
 *         __func__
 *         __FUNCTION__
 *         __PRETTY_FUNCTION__
 *
 * [dcl.fct.def.general]/8: "The function-local predefined variable __func__
 * is defined as if a definition of the form
 *     static const char __func__[] = "function-name";
 * had been provided, where function-name is an implementation-defined
 * string."  [impl.clang]: the name is the function's unqualified name
 * (__func__, __FUNCTION__); outside a function body (clang warns) the
 * string is empty.
 *
 * __PRETTY_FUNCTION__ is [impl.clang]'s signature: "int ns::S::g(int) const",
 * "static void ns::S::h()", "virtual int B::v()", and a function template's
 * specialization as declared, with its arguments after:
 * "T tr(T) [T = int]".  NOT CLANG'S YET: a member of a class template's
 * specialization prints the substituted types (clang prints the pattern's
 * with [T = int]); a type's typedef-name and a local class's scope are not
 * kept (clang prints the sugar), and a lambda's closure is "(lambda)".
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

static void append_identifier(const semantic_analysis &a, uint32_t id,
                              owned_buffer &out)
{
    size_t length = 0;
    const char *s = a.identifier_text(id, &length);
    vbuffer_append(out.get(), s, length);
}

/* the name a function is declared with (a constructor's class name, ~ and
   the class name for a destructor, operator + its token) */
static void unqualified_function_name(const semantic_analysis &a,
                                      entity_index f, owned_buffer &out)
{
    const entity &x = a.entity_table[f];
    if (x.special == special_member::destructor)
        vbuffer_string_append(out.get(), "~");
    if (x.special != special_member::none) {
        const entity &c = a.entity_table[x.enclosing];
        if (c.name)
            append_identifier(a, c.name, out);
        return;
    }
    if (x.operator_function) {
        vbuffer_string_append(out.get(), "operator");
        /* the call and subscript operators are two tokens */
        if (x.implemented == preprocessing_op_or_punc::LEFT_PARENTHESIS)
            vbuffer_string_append(out.get(), "()");
        else if (x.implemented == preprocessing_op_or_punc::LEFT_BRACKET)
            vbuffer_string_append(out.get(), "[]");
        else
            vbuffer_string_append(
                out.get(), preprocessing_op_or_punc_spelling(x.implemented));
        return;
    }
    if (x.conversion_function) {
        /* the target type is spelled for a fundamental type or a class */
        vbuffer_string_append(out.get(), "operator ");
        const type &r = a.type_table[a.type_table[x.type.unqualified()]
                                         .operand.unqualified()];
        if (r.kind == type_kind::fundamental)
            vbuffer_string_append(out.get(),
                                  fundamental_type_name(r.fundamental));
        return;
    }
    if (x.name)
        append_identifier(a, x.name, out);
}

static void spell_arguments(const semantic_analysis &a, entity_index spec,
                            owned_buffer &out);

static void qualified_prefix(const semantic_analysis &a, entity_index e,
                             owned_buffer &out)
{
    for (; e; e = a.entity_table[e].enclosing) {
        const entity &x = a.entity_table[e];
        if (x.kind != entity_kind::namespace_entity &&
            x.kind != entity_kind::class_type)
            continue;
        if (x.inline_)
            continue;
        qualified_prefix(a, x.enclosing, out);
        if (x.name)
            append_identifier(a, x.name, out);
        else if (x.lambda)
            vbuffer_string_append(out.get(), "(lambda)");
        else if (x.kind == entity_kind::class_type) {
            if (x.union_)
                vbuffer_string_append(out.get(), "(unnamed union)");
            else
                vbuffer_string_append(out.get(), "(unnamed struct)");
        } else
            vbuffer_string_append(out.get(), "(anonymous namespace)");
        if (x.kind == entity_kind::class_type && x.instantiation &&
            a.specialized_template(e)) {
            vbuffer_string_append(out.get(), "<");
            spell_arguments(a, e, out);
            vbuffer_string_append(out.get(), ">");
        }
        vbuffer_string_append(out.get(), "::");
        return;
    }
}

/* set when a type's class template specialization was spelled in a
   parameter or return type: clang prints the typedef-name or alias the
   source used there (std::string), which a canonical type does not keep */
static thread_local bool sugar_lost;

static void append(owned_buffer &out, const char *text)
{
    vbuffer_string_append(out.get(), text);
}

static void append_number(owned_buffer &out, uint64_t v)
{
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = char('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        vbuffer_append(out.get(), &digits[--n], 1);
}

static void spell_type(const semantic_analysis &a, qualified_type t,
                       owned_buffer &decl, owned_buffer &out);
static void spell_argument(const semantic_analysis &a, entity_index spec,
                           uint32_t i, uint32_t element, owned_buffer &out);

/* the arguments of a specialization: "<int, 3>" (the pack's elements
   flat); a function template's own go in brackets, named */
static void spell_arguments(const semantic_analysis &a, entity_index spec,
                            owned_buffer &out)
{
    bool first = true;
    uint32_t count = a.specialization_argument_count(spec);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pack = a.specialization_pack(spec, i);
        uint32_t n = 1;
        if (pack)
            n = pack - 1;
        for (uint32_t k = 0; k < n; k++) {
            if (!first)
                append(out, ", ");
            first = false;
            if (pack)
                spell_argument(a, spec, i, k + 1, out);
            else
                spell_argument(a, spec, i, 0, out);
        }
    }
}

static void spell_argument(const semantic_analysis &a, entity_index spec,
                           uint32_t i, uint32_t element, owned_buffer &out)
{
    uint64_t v = 0;
    bool negative = false;
    uint32_t parameter = 0;
    if (!a.specialization_value(spec, i, v, negative, parameter, element)) {
        owned_buffer none;
        spell_type(a, a.specialization_argument(spec, i, element), none, out);
        return;
    }
    qualified_type t = a.specialization_argument(spec, i, element);
    const type &r = a.type_table[t.unqualified()];
    if (r.kind == type_kind::fundamental &&
        r.fundamental == fundamental_type::bool_) {
        if (v)
            append(out, "true");
        else
            append(out, "false");
        return;
    }
    if (negative)
        append(out, "-");
    append_number(out, v);
    if (r.kind == type_kind::fundamental) {
        switch (r.fundamental) {
        case fundamental_type::unsigned_int:
            append(out, "U");
            break;
        case fundamental_type::long_int:
            append(out, "L");
            break;
        case fundamental_type::unsigned_long_int:
            append(out, "UL");
            break;
        case fundamental_type::long_long_int:
            append(out, "LL");
            break;
        case fundamental_type::unsigned_long_long_int:
            append(out, "ULL");
            break;
        default:
            break;
        }
    }
}

static void spell_class(const semantic_analysis &a, entity_index e,
                        owned_buffer &out)
{
    const entity &x = a.entity_table[e];
    qualified_prefix(a, x.enclosing, out);
    if (x.name)
        append_identifier(a, x.name, out);
    else if (x.lambda)
        append(out, "(lambda)");
    else if (x.union_)
        append(out, "(unnamed union)");
    else
        append(out, "(unnamed struct)");
    if (x.instantiation && a.specialized_template(e)) {
        append(out, "<");
        spell_arguments(a, e, out);
        append(out, ">");
    }
}

/* [impl.clang]: t as a declaration of `decl`: the declarator text so far
   (a name, "*", "(&)[3]", ...), which the type completes */
static void spell_type(const semantic_analysis &a, qualified_type t,
                       owned_buffer &decl, owned_buffer &out)
{
    const type &r = a.type_table[t.unqualified()];
    owned_buffer next;
    auto cv = [&](owned_buffer &to) {
        if (t.cv() & cv_const)
            append(to, "const");
        if (t.cv() & cv_volatile) {
            if (t.cv() & cv_const)
                append(to, " ");
            append(to, "volatile");
        }
    };
    auto join = [&](owned_buffer &to, const char *head) {
        append(to, head);
        if (decl.size()) {
            if (t.cv() && head[0])
                append(to, " ");
            vbuffer_append(to.get(), decl.data(), decl.size());
        }
    };
    switch (r.kind) {
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference: {
        const char *mark = "*";
        if (r.kind == type_kind::lvalue_reference)
            mark = "&";
        if (r.kind == type_kind::rvalue_reference)
            mark = "&&";
        const type &to = a.type_table[r.operand.unqualified()];
        bool wrap =
            to.kind == type_kind::array || to.kind == type_kind::function;
        if (wrap)
            append(next, "(");
        append(next, mark);
        cv(next);
        if (t.cv() && decl.size())
            append(next, " ");
        vbuffer_append(next.get(), decl.data(), decl.size());
        if (wrap)
            append(next, ")");
        spell_type(a, r.operand, next, out);
        return;
    }
    case type_kind::pointer_to_member: {
        spell_class(a, r.entity, next);
        append(next, "::*");
        if (decl.size())
            vbuffer_append(next.get(), decl.data(), decl.size());
        spell_type(a, r.operand, next, out);
        return;
    }
    case type_kind::array:
        vbuffer_append(next.get(), decl.data(), decl.size());
        append(next, "[");
        if (r.bound_known)
            append_number(next, r.bound);
        append(next, "]");
        spell_type(a, r.operand, next, out);
        return;
    case type_kind::function: {
        vbuffer_append(next.get(), decl.data(), decl.size());
        append(next, "(");
        const qualified_type *p = a.type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++) {
            owned_buffer none;
            if (i)
                append(next, ", ");
            spell_type(a, p[i], none, next);
        }
        if (r.variadic && r.parameter_count)
            append(next, ", ...");
        else if (r.variadic)
            append(next, "...");
        append(next, ")");
        spell_type(a, r.operand, next, out);
        return;
    }
    default:
        break;
    }
    owned_buffer base;
    if (t.cv() & cv_const)
        append(base, "const ");
    if (t.cv() & cv_volatile)
        append(base, "volatile ");
    switch (r.kind) {
    case type_kind::fundamental:
        append(base, fundamental_type_name(r.fundamental));
        break;
    case type_kind::class_type:
    case type_kind::enumeration:
        if (a.entity_table[r.entity].instantiation)
            sugar_lost = true;
        spell_class(a, r.entity, base);
        break;
    case type_kind::template_parameter:
        append_identifier(a, a.entity_table[r.entity].name, base);
        break;
    case type_kind::pack_expansion:
        spell_type(a, r.operand, next, base);
        append(base, "...");
        break;
    case type_kind::placeholder:
        append(base, "auto");
        break;
    default:
        append(base, "auto");
        break;
    }
    (void)join;
    vbuffer_append(out.get(), base.data(), base.size());
    if (decl.size()) {
        append(out, " ");
        vbuffer_append(out.get(), decl.data(), decl.size());
    }
}

/* "[T = int, N = 3]" for the template template_ whose specialization spec
   is (a pack as "Ts = <int, double>") */
static void spell_bindings(const semantic_analysis &a, entity_index tmpl,
                           entity_index spec, bool &any, owned_buffer &out)
{
    uint32_t count = a.specialization_argument_count(spec);
    for (uint32_t i = 0; i < count; i++) {
        /* [impl.clang] clang leaves an unnamed template-parameter out */
        if (!a.template_parameter_name(tmpl, i))
            continue;
        if (any)
            append(out, ", ");
        else
            append(out, " [");
        any = true;
        append_identifier(a, a.template_parameter_name(tmpl, i), out);
        append(out, " = ");
        uint32_t pack = a.specialization_pack(spec, i);
        if (!pack) {
            spell_argument(a, spec, i, 0, out);
            continue;
        }
        append(out, "<");
        for (uint32_t k = 1; k < pack; k++) {
            if (k > 1)
                append(out, ", ");
            spell_argument(a, spec, i, k, out);
        }
        append(out, ">");
    }
}

static void pretty_function(const semantic_analysis &a, entity_index f,
                            owned_buffer &out)
{
    const entity &x = a.entity_table[f];
    entity_index pattern = f;
    if (x.instantiation && x.kind == entity_kind::function &&
        a.specialized_template(f))
        pattern = a.specialized_template(f);
    const entity &p = a.entity_table[pattern];
    if (!x.nonstatic_member && x.kind == entity_kind::function && x.enclosing &&
        a.entity_table[x.enclosing].kind == entity_kind::class_type)
        append(out, "static ");
    if (x.virtual_)
        append(out, "virtual ");
    owned_buffer decl;
    qualified_prefix(a, x.enclosing, decl);
    /* a class template's specialization names its arguments */
    unqualified_function_name(a, f, decl);
    const type &ft = a.type_table[p.type.unqualified()];
    append(decl, "(");
    const qualified_type *params =
        a.type_table.parameters(p.type.unqualified());
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        owned_buffer none;
        if (i)
            append(decl, ", ");
        spell_type(a, params[i], none, decl);
    }
    if (ft.variadic && ft.parameter_count)
        append(decl, ", ...");
    else if (ft.variadic)
        append(decl, "...");
    append(decl, ")");
    if (x.member_cv & cv_const)
        append(decl, " const");
    if (x.member_cv & cv_volatile)
        append(decl, " volatile");
    if (x.member_cv & cv_ref_lvalue)
        append(decl, " &");
    if (x.member_cv & cv_ref_rvalue)
        append(decl, " &&");
    if (x.special != special_member::none)
        vbuffer_append(out.get(), decl.data(), decl.size());
    else
        spell_type(a, ft.operand, decl, out);
    bool any = false;
    if (x.enclosing && a.entity_table[x.enclosing].instantiation &&
        a.entity_table[x.enclosing].kind == entity_kind::class_type &&
        a.specialized_template(x.enclosing))
        spell_bindings(a, a.specialized_template(x.enclosing), x.enclosing, any,
                       out);
    if (pattern != f)
        spell_bindings(a, pattern, f, any, out);
    if (any)
        append(out, "]");
}

bool semantic_analysis::predefined_name_text(entity_index f,
                                             predefined_name_form form,
                                             owned_buffer &out) const
{
    if (!f)
        return true;
    if (form == predefined_name_form::pretty) {
        sugar_lost = false;
        pretty_function(*this, f, out);
        return !sugar_lost;
    }
    unqualified_function_name(*this, f, out);
    return true;
}

analysis_result semantic_analysis::predefined_name(token_index t,
                                                   expression_meaning &m)
{
    predefined_name_form form = predefined_name_form::func;
    owned_buffer spelling;
    tokens.spelling(t, spelling.get());
    if (spelling.size() == 12)
        form = predefined_name_form::function;
    if (spelling.size() == 19)
        form = predefined_name_form::pretty;
    entity_index f = function_state.function;
    owned_buffer text;
    if (!predefined_name_text(f, form, text))
        return not_supported(current_declaration, "ext.clang");
    qualified_type element = qualified_type::make(
        type_table.fundamental(fundamental_type::char_), cv_const);
    m = expression_meaning();
    m.type = qualified_type::make(
        type_table.array_of(element, uint64_t(text.size()) + 1), cv_none);
    m.category = value_category::lvalue;
    m.evaluation = constness::unknown;
    m.node = expression_node(semantic_kind::predefined_name, m, t, 0, f,
                             static_cast<uint8_t>(form));
    return analysis_result::well_formed;
}

} // namespace cplusplus
