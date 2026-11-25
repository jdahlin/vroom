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
 * "T tr(T) [T = int]".  The return and parameter types are the
 * declaration's as written (clang prints the pattern's types with their
 * sugar: typedef-names, a class template's injected-class-name as
 * "C<T>"), the const and volatile of a type first, an array parameter as a
 * pointer, no parameter name or default argument (a trailing return type
 * is the type it denotes); the function's scope is
 * its classes' canonical specializations ("ns::C<int>"), a lambda's
 * "f(int)::(lambda)::operator()(int) const"; then the template arguments
 * of the enclosing specializations, outermost first ([B = int, T = long]).
 * A type in the template arguments is canonical: a class's trailing
 * arguments equal to their defaults left out ("std::vector<int>",
 * "nlohmann::basic_json<>"), an inline namespace left out where it is
 * redundant (std::__1::vector is "std::vector"), std::nullptr_t, and an
 * instantiated specialization's preferred_name typedef ("std::string").  A
 * template's trailing return type, a parameter of function type and an
 * unusual declarator are not followed.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

static void append_identifier(semantic_analysis &a, uint32_t id,
                              owned_buffer &out)
{
    size_t length = 0;
    const char *s = a.identifier_text(id, &length);
    vbuffer_append(out.get(), s, length);
}

/* the name a function is declared with (a constructor's class name, ~ and
   the class name for a destructor, operator + its token) */
static void unqualified_function_name(semantic_analysis &a, entity_index f,
                                      owned_buffer &out)
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

static void spell_arguments(semantic_analysis &a, entity_index spec,
                            owned_buffer &out);

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

static void spell_type(semantic_analysis &a, qualified_type t,
                       owned_buffer &decl, owned_buffer &out);
static void spell_argument(semantic_analysis &a, entity_index spec, uint32_t i,
                           uint32_t element, owned_buffer &out);
static void type_scope_prefix(semantic_analysis &a, entity_index e,
                              entity_index child, owned_buffer &out);

/* the arguments of a specialization: "<int, 3>" (the pack's elements
   flat); a function template's own go in brackets, named */
static void spell_arguments(semantic_analysis &a, entity_index spec,
                            owned_buffer &out)
{
    bool first = true;
    uint32_t count = a.shown_template_arguments(spec);
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

static void spell_argument(semantic_analysis &a, entity_index spec, uint32_t i,
                           uint32_t element, owned_buffer &out)
{
    uint64_t v = 0;
    bool negative = false;
    uint32_t parameter = 0;
    entity_index named = 0;
    if (a.specialization_template_name(spec, i, named, parameter, element)) {
        /* a template template-argument: the template's name */
        if (!named) {
            sugar_lost = true;
            return;
        }
        type_scope_prefix(a, a.entity_table[named].enclosing, named, out);
        append_identifier(a, a.entity_table[named].name, out);
        return;
    }
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

/* the scopes of a type's class (or of a template) spelled in a template
   argument, child the entity in it named next: every namespace but an
   inline one that is redundant (clang leaves one out where its enclosing
   namespace's lookup of the next name finds no other entity), classes with
   their arguments */
static void type_scope_prefix(semantic_analysis &a, entity_index e,
                              entity_index child, owned_buffer &out)
{
    for (; e; e = a.entity_table[e].enclosing) {
        const entity &x = a.entity_table[e];
        if (x.kind != entity_kind::namespace_entity &&
            x.kind != entity_kind::class_type)
            continue;
        /* (an inline namespace is redundant where its enclosing namespace
           finds the next name to the same entity, or to none other) */
        if (x.inline_ && x.kind == entity_kind::namespace_entity) {
            entity_index found = 0;
            if (child && a.entity_table[child].name)
                found = a.entity_table.find(x.enclosing,
                                            a.entity_table[child].name);
            if (!found || found == child)
                continue;
        }
        type_scope_prefix(a, x.enclosing, e, out);
        if (x.name)
            append_identifier(a, x.name, out);
        else if (x.lambda)
            vbuffer_string_append(out.get(), "(lambda)");
        else if (x.kind == entity_kind::class_type)
            vbuffer_string_append(out.get(), "(unnamed struct)");
        else
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

static void spell_class(semantic_analysis &a, entity_index e, owned_buffer &out)
{
    const entity &x = a.entity_table[e];
    /* a preferred name of its template that denotes it, once the
       specialization is instantiated (clang's specialization takes the
       attribute from its pattern then; one only named keeps its own) */
    if (x.instantiation && x.kind == entity_kind::class_type && x.size &&
        a.specialized_template(e))
        for (const semantic_analysis::preferred_name &p : a.preferred_names) {
            if (p.template_entity != a.specialized_template(e))
                continue;
            const entity &n = a.entity_table[p.name];
            const type &r = a.type_table[n.type.unqualified()];
            if (n.type.cv() || r.kind != type_kind::class_type || r.entity != e)
                continue;
            type_scope_prefix(a, n.enclosing, p.name, out);
            append_identifier(a, n.name, out);
            return;
        }
    type_scope_prefix(a, x.enclosing, e, out);
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
static void spell_type(semantic_analysis &a, qualified_type t,
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
        /* [impl.clang] std::nullptr_t, as clang prints decltype(nullptr) */
        if (r.fundamental == fundamental_type::std_nullptr_t)
            append(base, "std::nullptr_t");
        else
            append(base, fundamental_type_name(r.fundamental));
        break;
    case type_kind::class_type:
    case type_kind::enumeration:
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
static void spell_bindings(semantic_analysis &a, entity_index tmpl,
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

/* clang's spacing of a printed type: a piece joins the text so far with
   a space between words, before a * or & that follows a word or a closing
   >, and after a comma */
static bool word_character(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static void join(owned_buffer &out, const char *s, size_t n)
{
    if (!n)
        return;
    if (out.size()) {
        char p = static_cast<char>(out.data()[out.size() - 1]);
        char q = s[0];
        bool pw = word_character(p), qw = word_character(q);
        bool space = (pw && qw) ||
                     ((pw || p == '>') && (q == '*' || q == '&')) || p == ',' ||
                     (p == '>' && qw);
        if (space)
            vbuffer_append(out.get(), " ", 1);
    }
    vbuffer_append(out.get(), s, n);
}

static void join_token(semantic_analysis &a, token_index t, owned_buffer &out)
{
    owned_buffer spelled;
    a.token_spelling(t, spelled.get());
    join(out, reinterpret_cast<const char *>(spelled.data()), spelled.size());
}

static bool spelled_token(semantic_analysis &a, token_index t, const char *w)
{
    owned_buffer spelled;
    a.token_spelling(t, spelled.get());
    size_t n = strlen(w);
    return spelled.size() == n && !memcmp(spelled.data(), w, n);
}

/* the tokens of element e, joined */
static void join_element(semantic_analysis &a, SyntaxElement e,
                         owned_buffer &out)
{
    if (e.is_token()) {
        join_token(a, e.token(), out);
        return;
    }
    for (token_index t = e.first_token(); t < e.end_token(); t++)
        join_token(a, t, out);
}

/* the class template whose specialization encloses f (its
   injected-class-name, which clang prints with the template's parameters,
   C<T, U>), 0 if none */
static entity_index enclosing_specialization(semantic_analysis &a,
                                             entity_index f)
{
    for (entity_index e = a.entity_table[f].enclosing; e;
         e = a.entity_table[e].enclosing)
        if (a.entity_table[e].kind == entity_kind::class_type &&
            a.entity_table[e].instantiation && a.specialized_template(e))
            return e;
    return 0;
}

/* a decl-specifier-seq as a type: the specifiers that are no type's left
   out, const and volatile first; false for one not followed */
static bool written_base(semantic_analysis &a, SyntaxElement seq,
                         entity_index f, owned_buffer &out)
{
    static const char *const dropped[] = {
        "static",    "inline",     "constexpr",     "consteval",
        "constinit", "virtual",    "explicit",      "friend",
        "extern",    "mutable",    "register",      "thread_local",
        "__inline",  "__inline__", "__extension__", "typedef",
    };
    bool is_const = false, is_volatile = false;
    owned_buffer text;
    entity_index injected = enclosing_specialization(a, f);
    for (uint32_t i = 0; i < seq.size(); i++) {
        SyntaxElement c = seq[i];
        token_index first = 0;
        if (c.is_token())
            first = c.token();
        else
            first = c.first_token();
        if (spelled_token(a, first, "__attribute__") ||
            spelled_token(a, first, "[") || spelled_token(a, first, "[[") ||
            spelled_token(a, first, "alignas") ||
            spelled_token(a, first, "__declspec") ||
            spelled_token(a, first, "explicit"))
            continue;
        if (c.is_token()) {
            bool drop = false;
            for (const char *d : dropped)
                if (spelled_token(a, first, d))
                    drop = true;
            if (drop)
                continue;
            if (spelled_token(a, first, "const")) {
                is_const = true;
                continue;
            }
            if (spelled_token(a, first, "volatile")) {
                is_volatile = true;
                continue;
            }
            join_token(a, first, text);
            /* C alone in a member of C<T>: C<T> */
            if (injected && c.is_identifier() &&
                a.token_identifier(first) == a.entity_table[injected].name &&
                (i + 1 >= seq.size() || !seq[i + 1].is_token() ||
                 !spelled_token(a, seq[i + 1].token(), "<"))) {
                entity_index t = a.specialized_template(injected);
                uint32_t count = a.specialization_argument_count(injected);
                vbuffer_string_append(text.get(), "<");
                for (uint32_t k = 0; k < count; k++) {
                    if (k)
                        vbuffer_string_append(text.get(), ", ");
                    uint32_t name = a.template_parameter_name(t, k);
                    if (!name)
                        return false;
                    append_identifier(a, name, text);
                    if (a.specialization_pack(injected, k))
                        vbuffer_string_append(text.get(), "...");
                }
                vbuffer_string_append(text.get(), ">");
            }
            continue;
        }
        join_element(a, c, text);
    }
    if (!text.size())
        return false;
    if (is_const)
        append(out, "const ");
    if (is_volatile)
        append(out, "volatile ");
    vbuffer_append(out.get(), text.data(), text.size());
    return true;
}

static bool written_parameters(semantic_analysis &a, SyntaxElement list,
                               entity_index f, owned_buffer &out);

/* the declarator of a parameter (or a return type's ptr-operators), its
   name left out; array: set for a declarator whose outermost part is an
   array (a parameter's, which is adjusted to a pointer) */
static bool written_declarator(semantic_analysis &a, SyntaxElement d,
                               entity_index f, owned_buffer &out, bool top,
                               bool &array)
{
    if (d.is_token()) {
        if (d.is_identifier())
            return true;
        if (spelled_token(a, d.token(), "..."))
            join(out, "...", 3);
        else
            join_token(a, d.token(), out);
        return true;
    }
    if (d.is(syntax_kind::ptr_operator)) {
        join_element(a, d, out);
        return true;
    }
    if (d.is(syntax_kind::parameters_and_qualifiers)) {
        /* a function type's parameters ((*)(int)); a parameter of function
           type, adjusted to a pointer, is not followed */
        if (top)
            return false;
        owned_buffer list;
        if (!written_parameters(a, d, f, list))
            return false;
        vbuffer_append(out.get(), "(", 1);
        vbuffer_append(out.get(), list.data(), list.size());
        vbuffer_append(out.get(), ")", 1);
        return true;
    }
    if (d.is(syntax_kind::qualified_id))
        return true;
    bool brackets = false;
    /* (a group, (&a)[3]: the brackets after it are the type's, not the
       parameter's array) */
    bool grouped = false;
    for (uint32_t k = 0; k < d.size(); k++)
        if (d[k].is_token() && spelled_token(a, d[k].token(), "("))
            grouped = true;
    for (uint32_t i = 0; i < d.size(); i++) {
        SyntaxElement c = d[i];
        if (c.is_token() && spelled_token(a, c.token(), "[")) {
            brackets = true;
            if (top && d.is(syntax_kind::noptr_declarator) && !grouped) {
                array = true;
                /* the bound is dropped with the adjustment */
                while (i < d.size() && !(d[i].is_token() &&
                                         spelled_token(a, d[i].token(), "]")))
                    i++;
                continue;
            }
            join(out, "[", 1);
            continue;
        }
        if (c.is_token() && spelled_token(a, c.token(), "]")) {
            brackets = false;
            join(out, "]", 1);
            continue;
        }
        if (brackets) {
            join_element(a, c, out);
            continue;
        }
        if (c.is_token() && spelled_token(a, c.token(), "(")) {
            vbuffer_append(out.get(), "(", 1);
            continue;
        }
        if (c.is_token() && spelled_token(a, c.token(), ")")) {
            vbuffer_append(out.get(), ")", 1);
            continue;
        }
        /* (the outermost part is the noptr-declarator of the name, under
           ptr-operators: a group's inner declarator, or the function type
           after it, is not) */
        bool inner_top = top && !c.is(syntax_kind::ptr_operator) &&
                         (d.is(syntax_kind::ptr_declarator) ||
                          (d.is(syntax_kind::noptr_declarator) && !grouped));
        if (!written_declarator(a, c, f, out, inner_top, array))
            return false;
    }
    return true;
}

/* one parameter-declaration as clang prints its type */
static bool written_parameter(semantic_analysis &a, SyntaxElement p,
                              entity_index f, owned_buffer &out)
{
    if (!p.is(syntax_kind::parameter_declaration))
        return false;
    SyntaxElement seq;
    SyntaxElement declarator;
    for (uint32_t i = 0; i < p.size(); i++) {
        SyntaxElement c = p[i];
        if (c.is(syntax_kind::decl_specifier_seq)) {
            seq = c;
            continue;
        }
        if (c.is_token() && spelled_token(a, c.token(), "="))
            break;
        if (!declarator)
            declarator = c;
    }
    if (!seq)
        return false;
    owned_buffer base;
    if (!written_base(a, seq, f, base))
        return false;
    owned_buffer d;
    bool array = false;
    if (declarator && !written_declarator(a, declarator, f, d, true, array))
        return false;
    if (array)
        join(d, "*", 1);
    vbuffer_append(out.get(), base.data(), base.size());
    if (d.size()) {
        char q = static_cast<char>(d.data()[0]);
        if (q != '.')
            vbuffer_append(out.get(), " ", 1);
        vbuffer_append(out.get(), d.data(), d.size());
    }
    return true;
}

/* the parameter types of a parameters-and-qualifiers (or lambda-declarator)
   list, "(int, const char *)" without the parentheses */
static bool written_parameters(semantic_analysis &a, SyntaxElement list,
                               entity_index f, owned_buffer &out)
{
    bool first = true;
    /* (a ... right after a parameter-declaration expands it, R...; one
       after a comma, or alone, is the ellipsis) */
    bool after_parameter = false;
    for (uint32_t i = 0; i < list.size(); i++) {
        SyntaxElement c = list[i];
        if (c.is_token()) {
            if (spelled_token(a, c.token(), "...")) {
                /* (R... with no comma, of a function that is not
                   variadic: the pack's expansion) */
                bool expands =
                    after_parameter ||
                    (!first && c.token() > 0 &&
                     !spelled_token(a, c.token() - 1, ",") &&
                     !a.type_table[a.entity_table[f].type.unqualified()]
                          .variadic);
                if (expands)
                    append(out, "...");
                else {
                    if (!first)
                        append(out, ", ");
                    append(out, "...");
                }
                first = false;
            }
            after_parameter = false;
            continue;
        }
        after_parameter = c.is(syntax_kind::parameter_declaration);
        if (c.is(syntax_kind::parameter_declaration_clause) ||
            c.is(syntax_kind::parameter_declaration_list)) {
            owned_buffer inner;
            if (!written_parameters(a, c, f, inner))
                return false;
            if (inner.size()) {
                if (!first)
                    append(out, ", ");
                vbuffer_append(out.get(), inner.data(), inner.size());
                first = false;
            }
            continue;
        }
        if (c.is(syntax_kind::parameter_declaration)) {
            if (!first)
                append(out, ", ");
            if (!written_parameter(a, c, f, out))
                return false;
            first = false;
            continue;
        }
    }
    /* (void): no parameter */
    if (out.size() == 4 && !memcmp(out.data(), "void", 4))
        out.get()->n = 0;
    return true;
}

/* the declaration of function f as written: its decl-specifier-seq, the
   ptr-operators before its name and its parameters-and-qualifiers (or a
   lambda's lambda-declarator); false when it has a trailing return type or
   is not found */
static bool written_signature(semantic_analysis &a, entity_index f,
                              SyntaxElement &seq, owned_buffer &operators,
                              SyntaxElement &parameters, bool &trailing)
{
    trailing = false;
    const entity &x = a.entity_table[f];
    entity_index closure = x.enclosing;
    if (closure && a.entity_table[closure].kind == entity_kind::class_type &&
        a.entity_table[closure].lambda &&
        a.entity_table[closure].call_operator == f) {
        SyntaxRef l = a.entity_table[closure].lambda;
        for (uint32_t i = 0; i < l.size(); i++)
            if (SyntaxElement(l[i]).is(syntax_kind::lambda_declarator)) {
                SyntaxElement d = l[i];
                for (uint32_t k = 0; k < d.size(); k++)
                    if (d[k].is(syntax_kind::trailing_return_type))
                        trailing = true;
                parameters = d;
            }
        return true;
    }
    if (!x.first_declaration)
        return false;
    SyntaxElement n = a.entity_table.declaration(x.first_declaration).node;
    if (n.is(syntax_kind::template_declaration)) {
        for (uint32_t i = 0; i < n.size(); i++)
            if (n[i].is(syntax_kind::function_definition) ||
                n[i].is(syntax_kind::member_declaration) ||
                n[i].is(syntax_kind::simple_declaration))
                n = n[i];
    }
    SyntaxElement declarator;
    for (uint32_t i = 0; i < n.size(); i++) {
        SyntaxElement c = n[i];
        if (c.is(syntax_kind::decl_specifier_seq))
            seq = c;
        else if (c.is(syntax_kind::ptr_declarator) ||
                 c.is(syntax_kind::noptr_declarator) ||
                 c.is(syntax_kind::declarator))
            declarator = c;
        else if ((c.is(syntax_kind::member_declarator_list) ||
                  c.is(syntax_kind::member_declarator) ||
                  c.is(syntax_kind::init_declarator_list) ||
                  c.is(syntax_kind::init_declarator)) &&
                 !declarator) {
            SyntaxElement d = c;
            while (d.is_node() &&
                   (d.is(syntax_kind::member_declarator_list) ||
                    d.is(syntax_kind::member_declarator) ||
                    d.is(syntax_kind::init_declarator_list) ||
                    d.is(syntax_kind::init_declarator)) &&
                   d.size())
                d = d[0];
            declarator = d;
        }
    }
    if (!declarator)
        return false;
    /* noptr-declarator parameters-and-qualifiers trailing-return-type */
    if (declarator.is(syntax_kind::declarator)) {
        SyntaxElement inner;
        for (uint32_t i = 0; i < declarator.size(); i++) {
            if (declarator[i].is(syntax_kind::trailing_return_type))
                trailing = true;
            else if (declarator[i].is(syntax_kind::ptr_declarator) ||
                     declarator[i].is(syntax_kind::noptr_declarator))
                inner = declarator[i];
        }
        if (!inner)
            return false;
        declarator = inner;
    }
    while (declarator.is(syntax_kind::ptr_declarator)) {
        uint32_t last = declarator.size() - 1;
        for (uint32_t i = 0; i < last; i++) {
            if (!declarator[i].is(syntax_kind::ptr_operator))
                return false;
            join_element(a, declarator[i], operators);
        }
        declarator = declarator[last];
    }
    if (!declarator.is(syntax_kind::noptr_declarator))
        return false;
    for (uint32_t i = 0; i < declarator.size(); i++) {
        SyntaxElement c = declarator[i];
        if (c.is(syntax_kind::trailing_return_type))
            trailing = true;
        /* a function returning a pointer to function, (*f(int))(char) */
        if (c.is_token() && spelled_token(a, c.token(), "("))
            return false;
        if (c.is(syntax_kind::parameters_and_qualifiers)) {
            for (uint32_t k = 0; k < c.size(); k++)
                if (c[k].is(syntax_kind::trailing_return_type))
                    trailing = true;
            parameters = c;
        }
    }
    return static_cast<bool>(parameters);
}

/* the scope f is declared in, as the prefix of its qualified name: no
   inline namespace, classes with their arguments, a lambda's closure as
   "(lambda)" after its function, "f(int)" */
static bool function_prefix(semantic_analysis &a, entity_index e,
                            owned_buffer &out)
{
    for (; e; e = a.entity_table[e].enclosing) {
        const entity &x = a.entity_table[e];
        if (x.kind == entity_kind::function) {
            if (!function_prefix(a, x.enclosing, out))
                return false;
            unqualified_function_name(a, e, out);
            SyntaxElement seq, parameters;
            owned_buffer operators;
            bool trailing = false;
            if (!written_signature(a, e, seq, operators, parameters, trailing))
                return false;
            owned_buffer list;
            if (!written_parameters(a, parameters, e, list))
                return false;
            append(out, "(");
            vbuffer_append(out.get(), list.data(), list.size());
            append(out, ")::");
            return true;
        }
        if (x.kind != entity_kind::namespace_entity &&
            x.kind != entity_kind::class_type)
            continue;
        if (x.inline_)
            continue;
        if (!function_prefix(a, x.enclosing, out))
            return false;
        if (x.name)
            append_identifier(a, x.name, out);
        else if (x.lambda)
            append(out, "(lambda)");
        else if (x.kind == entity_kind::class_type) {
            if (x.union_)
                append(out, "(unnamed union)");
            else
                append(out, "(unnamed struct)");
        } else
            append(out, "(anonymous namespace)");
        if (x.kind == entity_kind::class_type && x.instantiation &&
            a.specialized_template(e)) {
            append(out, "<");
            spell_arguments(a, e, out);
            append(out, ">");
        }
        append(out, "::");
        return true;
    }
    return true;
}

static bool pretty_function(semantic_analysis &a, entity_index f,
                            owned_buffer &out)
{
    /* (a copy: the types analysed below may add entities, and move the
       table) */
    const entity x = a.entity_table[f];
    if (!x.nonstatic_member && x.kind == entity_kind::function && x.enclosing &&
        a.entity_table[x.enclosing].kind == entity_kind::class_type)
        append(out, "static ");
    if (x.virtual_)
        append(out, "virtual ");
    SyntaxElement seq, parameters;
    owned_buffer operators;
    bool trailing = false;
    if (!written_signature(a, f, seq, operators, parameters, trailing))
        return false;
    /* a trailing return type: clang prints the type it denotes (int for
       -> decltype(42)); of a template's specialization or a member of a
       class template's, which may name the parameters, not followed */
    bool templated = false;
    for (entity_index e = f; e; e = a.entity_table[e].enclosing)
        if (a.entity_table[e].instantiation &&
            (a.entity_table[e].kind == entity_kind::class_type ||
             a.entity_table[e].kind == entity_kind::function))
            templated = true;
    if (trailing && templated)
        return false;
    owned_buffer name;
    if (!function_prefix(a, x.enclosing, name))
        return false;
    unqualified_function_name(a, f, name);
    owned_buffer list;
    if (!written_parameters(a, parameters, f, list))
        return false;
    append(name, "(");
    vbuffer_append(name.get(), list.data(), list.size());
    append(name, ")");
    if (x.member_cv & cv_const)
        append(name, " const");
    if (x.member_cv & cv_volatile)
        append(name, " volatile");
    if (x.member_cv & cv_ref_lvalue)
        append(name, " &");
    if (x.member_cv & cv_ref_rvalue)
        append(name, " &&");
    /* the return type: none for a constructor or destructor; a conversion
       function's is its target; a lambda's call operator's is auto */
    if (x.special == special_member::none) {
        owned_buffer result;
        if (x.conversion_function || trailing) {
            owned_buffer none;
            spell_type(a, a.type_table[x.type.unqualified()].operand, none,
                       result);
        } else if (!seq) {
            append(result, "auto");
        } else if (!written_base(a, seq, f, result)) {
            return false;
        }
        if (operators.size()) {
            append(result, " ");
            vbuffer_append(result.get(), operators.data(), operators.size());
        } else {
            append(result, " ");
        }
        vbuffer_append(out.get(), result.data(), result.size());
    }
    vbuffer_append(out.get(), name.data(), name.size());
    /* the template arguments of the enclosing specializations, outermost
       first */
    owned_array<entity_index> chain;
    for (entity_index e = f; e; e = a.entity_table[e].enclosing) {
        const entity &y = a.entity_table[e];
        if (y.instantiation &&
            (y.kind == entity_kind::class_type ||
             y.kind == entity_kind::function) &&
            a.specialized_template(e))
            chain.append(e);
    }
    bool any = false;
    for (size_t i = chain.size(); i-- > 0;)
        spell_bindings(a, a.specialized_template(chain[i]), chain[i], any, out);
    if (any)
        append(out, "]");
    return true;
}

bool semantic_analysis::note_preferred_names(entity_index t, token_index first,
                                             token_index end)
{
    owned_array<preferred_name> found_names;
    for (token_index i = first; i + 2 < end; i++) {
        if (!spelled_token(*this, i, "__preferred_name__") &&
            !spelled_token(*this, i, "preferred_name"))
            continue;
        if (!spelled_token(*this, i + 1, "("))
            return false;
        token_index k = i + 2;
        entity_index found = 0;
        token_index close = 0;
        /* N or ns::N, then ) */
        if (tokens[k].kind != token_kind::identifier)
            return false;
        if (k + 3 < end && spelled_token(*this, k + 1, "::") &&
            tokens[k + 2].kind == token_kind::identifier) {
            entity_index scope =
                lookup_unqualified(tokens.identifier(k), lookup_filter::any)
                    .entity;
            if (!scope || !is_namespace(scope))
                return false;
            found = lookup_qualified(scope, tokens.identifier(k + 2),
                                     lookup_filter::any)
                        .entity;
            close = k + 3;
        } else {
            found = lookup_unqualified(tokens.identifier(k), lookup_filter::any)
                        .entity;
            close = k + 1;
        }
        if (close >= end || !spelled_token(*this, close, ")"))
            return false;
        /* clang: "argument ... is not a typedef for a specialization of"
           the template (cv-qualified, another type) */
        if (!found || entity_table[found].kind != entity_kind::typedef_name)
            return false;
        qualified_type named = entity_table[found].type;
        const type &r = type_table[named.unqualified()];
        if (named.cv() || r.kind != type_kind::class_type ||
            !entity_table[r.entity].instantiation ||
            specialized_template(r.entity) != t)
            return false;
        preferred_name p;
        p.template_entity = t;
        p.name = found;
        found_names.append(p);
        i = close;
    }
    for (const preferred_name &p : found_names)
        preferred_names.append(p);
    return !found_names.empty();
}

bool semantic_analysis::predefined_name_text(entity_index f,
                                             predefined_name_form form,
                                             owned_buffer &out) const
{
    if (!f)
        return true;
    if (form == predefined_name_form::pretty) {
        for (const pretty_text &p : pretty_texts)
            if (p.function == f) {
                vbuffer_append(out.get(), pretty_characters.data() + p.first,
                               p.length);
                return true;
            }
        return false;
    }
    unqualified_function_name(const_cast<semantic_analysis &>(*this), f, out);
    return true;
}

/* the characters of __PRETTY_FUNCTION__ in f, kept for the lowering
   (predefined_name_text); false when they are not clang's */
bool semantic_analysis::keep_pretty_function(entity_index f)
{
    for (const pretty_text &p : pretty_texts)
        if (p.function == f)
            return true;
    owned_buffer text;
    sugar_lost = false;
    size_t unsupported_before = unsupported.size();
    bool made;
    {
        diagnostics_suppressed_if hush(diagnostics, true);
        made = pretty_function(*this, f, text);
    }
    unsupported.resize(unsupported_before);
    if (!made || sugar_lost)
        return false;
    pretty_text p;
    p.function = f;
    p.first = static_cast<uint32_t>(pretty_characters.size());
    p.length = static_cast<uint32_t>(text.size());
    vbuffer_append(pretty_characters.get(), text.data(), text.size());
    pretty_texts.append(p);
    return true;
}

/* [temp.arg]/4 with [impl.clang]: the template arguments of class
   specialization c clang prints: those before the trailing ones that are
   the parameters' default template-arguments for the arguments before them
   (clang leaves out the arguments a default gave) */
uint32_t semantic_analysis::shown_template_arguments(entity_index c)
{
    uint32_t count = specialization_argument_count(c);
    entity_index t = specialized_template(c);
    /* (a template entity's record: function_templates' index plus one) */
    uint32_t ct = entity_table[t].instantiation;
    if (!ct || entity_table[c].kind != entity_kind::class_type ||
        !function_templates[ct - 1].is_class ||
        function_templates[ct - 1].parameter_count != count)
        return count;
    /* (an explicit specialization's arguments are written, none taken
       from a default: clang prints them all, std::less<void>) */
    if (explicitly_specialized_class(c))
        return count;
    uint32_t shown = count;
    while (shown > 0) {
        owned_array<template_argument> arguments;
        owned_array<bool> set;
        for (uint32_t i = 0; i < count; i++) {
            arguments.append(specialization_template_argument(c, i));
            set.append(i < shown - 1);
        }
        size_t unsupported_before = unsupported.size();
        analysis_result r;
        {
            diagnostics_suppressed_if hush(diagnostics, true);
            r = default_template_arguments(SyntaxRef(), ct, arguments, set);
        }
        unsupported.resize(unsupported_before);
        if (r != analysis_result::well_formed)
            break;
        bool same = true;
        for (uint32_t i = shown - 1; i < count && same; i++)
            same = arguments[i] == specialization_template_argument(c, i);
        if (!same)
            break;
        shown--;
    }
    return shown;
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
    if (f && form == predefined_name_form::pretty && !keep_pretty_function(f))
        return not_supported_because(current_declaration, "ext.clang",
                                     "__PRETTY_FUNCTION__ in a function whose "
                                     "printed name cannot be formed");
    owned_buffer text;
    if (!predefined_name_text(f, form, text))
        return not_supported_because(
            current_declaration, "ext.clang",
            "a predefined function name whose text is not available");
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
