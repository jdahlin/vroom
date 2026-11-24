/*
 * semantic-analysis.cpp: the analysis of a translation unit, declaration
 * by declaration, and the result for what has no rules yet
 * (doc/PLAN-cxx-sema.md).
 *
 * The rules themselves are in the file of their subclause, beside the
 * grammar: a function of semantic_analysis named as the nonterminal.
 * Here is only what calls them.  A nonterminal without such a function is
 * `unsupported` with the subclause that defines it; nothing is concluded
 * about its declaration, and no diagnostic is issued.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

const char *analysis_result_name(analysis_result result)
{
    switch (result) {
    case analysis_result::well_formed:
        return "well-formed";
    case analysis_result::ill_formed:
        return "ill-formed";
    case analysis_result::unsupported:
        return "unsupported";
    case analysis_result::not_parsed:
        return "not-parsed";
    }
    return "";
}

semantic_analysis::semantic_analysis(const syntax_tree &tree,
                                     token_stream &tokens, const parser &p,
                                     diagnostic_consumer &diagnostics)
    : tree(tree), tokens(tokens), p(p), diagnostics(diagnostics),
      syntax_cx(tree, tokens, p)
{
    unsupported.append(unsupported_construct());
}

analysis_result semantic_analysis::not_supported(SyntaxRef n, call_site where)
{
    return not_supported(n, syntax_kind_subclause(n.kind()), where);
}

analysis_result semantic_analysis::not_supported(SyntaxRef n,
                                                 const char *subclause,
                                                 call_site where)
{
    unsupported_construct u;
    u.node = n;
    u.subclause = subclause;
    u.file = where.file;
    u.line = where.line;
    u.function = where.function;
    /* an extension's construct is named by its source text (the first
       tokens), so the error says what it is */
    if (!strncmp(subclause, "ext.", 4) && n.index()) {
        const syntax_node &s = tree[n];
        owned_buffer text;
        size_t tokens_named = 0;
        for (token_index t = s.first_token;
             t < s.end_token && tokens_named < 16 && text.size() < 100; t++) {
            if (text.size())
                vbuffer_string_append(text.get(), " ");
            tokens.spelling(t, text.get());
            tokens_named++;
        }
        if (text.size() && tokens_named == 16)
            vbuffer_string_append(text.get(), " ...");
        if (text.size()) {
            owned_string &named = detail_texts.make(text.data(), text.size());
            u.detail = named.c_str();
        }
    }
    unsupported.append(u);
    return analysis_result::unsupported;
}

void semantic_analysis::name_lookup_detail(token_index t,
                                           const lookup_result &found)
{
    if (found.unknown && found.unknown_by != held_cause::refused) {
        name_held_detail(t, found.unknown_by);
        last_unsupported_held_by(found.unknown_entity);
    } else if (found.unknown_entity &&
               entity_table[found.unknown_entity].instantiation &&
               !entity_table[found.unknown_entity].held_by_template)
        /* [temp.dep.constexpr]/2: in a template's own declaration, only
           the parameter's bare name as an array bound is represented
           (value_parameter_named) */
        name_unsupported_detail(t, "is a non-type template parameter in an "
                                   "expression of a declarator's type");
    else if (found.unknown_entity) {
        name_unsupported_detail(t, "is declared by a declaration with no "
                                   "rules yet");
        last_unsupported_held_by(found.unknown_entity);
    } else if (found.unknown)
        name_unsupported_detail(t, "has no declaration the analysis knows: a "
                                   "scope it was searched in has names "
                                   "analysis did not follow");
    else
        name_unsupported_detail(t, "has no declaration the analysis knows: no "
                                   "scope has it");
}

/* the last refusal's name was held by a declaration that was not
   analysed for another reason than a missing rule */
void semantic_analysis::name_held_detail(token_index t, held_cause by)
{
    if (by == held_cause::ill_formed)
        name_unsupported_detail(
            t, "is declared by a declaration that is ill-formed");
    else if (by == held_cause::not_parsed)
        name_unsupported_detail(
            t, "is declared by a declaration that is not parsed");
}

namespace
{
/* what a template-id's name is when it is no template of the kind used
   ([temp.names]/1), for the detail of the refusal */
const char *template_name_names(const entity &e)
{
    switch (e.kind) {
    case entity_kind::template_entity:
        return "names a template of another kind than the use needs";
    case entity_kind::template_specialization:
        return "names a template specialization";
    case entity_kind::class_type:
        return "names a class that is not a template";
    case entity_kind::function:
        return "names a function";
    case entity_kind::variable:
        return "names a variable";
    case entity_kind::typedef_name:
        return "names a typedef-name";
    case entity_kind::enumeration:
        return "names an enumeration";
    case entity_kind::enumerator:
        return "names an enumerator";
    case entity_kind::namespace_entity:
        return "names a namespace";
    case entity_kind::pack:
        return "names a pack";
    default:
        break;
    }
    return "names something that is not a template";
}
} // namespace

/* the last refusal of a template-id whose template-name at t is no
   template the use wants: why lookup found nothing, or what it found */
void semantic_analysis::template_name_detail(token_index t,
                                             const lookup_result &found,
                                             entity_index named)
{
    if (!found.entity) {
        name_lookup_detail(t, found);
        return;
    }
    name_unsupported_detail(t, template_name_names(entity_table[named]));
}

/* the last refusal recorded names the identifier at t: "NAME what" */
void semantic_analysis::name_unsupported_detail(token_index t, const char *what)
{
    owned_buffer text;
    tokens.spelling(t, text.get());
    vbuffer_string_append(text.get(), " ");
    vbuffer_string_append(text.get(), what);
    last_unsupported_detail(text.data(), text.size());
}

analysis_result semantic_analysis::not_supported_because(SyntaxRef n,
                                                         const char *subclause,
                                                         const char *what,
                                                         call_site where)
{
    analysis_result r = not_supported(n, subclause, where);
    last_unsupported_detail(what, strlen(what));
    return r;
}

void semantic_analysis::last_unsupported_detail(const char *text, size_t size)
{
    owned_string &detail = detail_texts.make(text, size);
    unsupported[unsupported.size() - 1].detail = detail.c_str();
}

void semantic_analysis::last_unsupported_held_by(entity_index e)
{
    if (e)
        unsupported[unsupported.size() - 1].holder = entity_table[e].holder;
}

void semantic_analysis::instantiation_detail(const unsupported_construct &in)
{
    char text[512];
    snprintf(text, sizeof text, "instantiation refused: [%s] %s:%u %s",
             in.subclause, in.file, static_cast<unsigned>(in.line), in.detail);
    last_unsupported_detail(text, strlen(text));
    /* (a held name the specialization's analysis met) */
    unsupported[unsupported.size() - 1].holder = in.holder;
}

analysis_result semantic_analysis::not_supported_builtin(SyntaxRef n,
                                                         const char *subclause,
                                                         entity_index called,
                                                         const char *what,
                                                         call_site where)
{
    size_t length = 0;
    const char *spelling =
        tokens.pp.identifier_spelling(entity_table[called].name - 1, &length);
    char text[256];
    snprintf(text, sizeof text, "%.*s %s", static_cast<int>(length), spelling,
             what);
    owned_string &detail = detail_texts.make(text, strlen(text));
    analysis_result r = not_supported(n, subclause, where);
    unsupported[unsupported.size() - 1].detail = detail.c_str();
    return r;
}

analysis_result semantic_analysis::ill_formed(SyntaxRef at, const char *rule,
                                              const char *subclause)
{
    return ill_formed_at_token(at.first_token(), rule, subclause);
}

analysis_result semantic_analysis::ill_formed_at_token(token_index at,
                                                       const char *rule,
                                                       const char *subclause)
{
    diagnostics.issue(diagnostic_id::sema_violation, tokens.location(at))
        << rule << subclause;
    return analysis_result::ill_formed;
}

analysis_result semantic_analysis::extension_used(diagnostic_id id,
                                                  token_index at,
                                                  const char *argument)
{
    diagnostic_severity severity = diagnostics.severity_now(id);
    diagnostic_message &m = diagnostics.issue(id, tokens.location(at));
    if (argument)
        m << argument;
    if (severity == diagnostic_severity::error ||
        severity == diagnostic_severity::fatal)
        return analysis_result::ill_formed;
    return analysis_result::well_formed;
}

/* extension_used for an error after which clang goes on as if the rule had
   been met: a well-formed result, the declaration judged ill-formed when it
   ends (declaration()), its entities declared */
analysis_result semantic_analysis::recoverable_extension_used(diagnostic_id id,
                                                              token_index at)
{
    analysis_result r = extension_used(id, at, nullptr);
    if (r == analysis_result::ill_formed) {
        record_recovered_error();
        return analysis_result::well_formed;
    }
    return r;
}

analysis_result semantic_analysis::recovered_error_at(token_index at,
                                                      const char *rule,
                                                      const char *subclause)
{
    ill_formed_at_token(at, rule, subclause);
    record_recovered_error();
    return analysis_result::well_formed;
}

void semantic_analysis::record_recovered_error()
{
    if (analysing)
        analysing->recovered = true;
}

bool semantic_analysis::in_system_header(token_index at) const
{
    if (!diagnostics.files)
        return false;
    return diagnostics.files->in_system_header(tokens.location(at) +
                                               diagnostics.location_base);
}

bool semantic_analysis::parsed_completely(SyntaxRef n) const
{
    /* the nodes under n, without recursion: a tree is as deep as its
       source is nested */
    owned_array<SyntaxRef> work;
    work.append(n);
    while (!work.empty()) {
        SyntaxRef next = work.back();
        work.resize(work.size() - 1);
        if (next.is(syntax_kind::erroneous, syntax_kind::unparsed))
            return false;
        for (uint32_t i = 0; i < next.size(); i++) {
            SyntaxElement c = next[i];
            if (!c.is_token())
                work.append(c);
        }
    }
    return true;
}

/* whether the node is a namespace-definition ([namespace.def]) */
static bool is_namespace_definition(const syntax_tree &tree, SyntaxRef n)
{
    syntax_kind kind = tree[n].kind;
    return kind == syntax_kind::named_namespace_definition ||
           kind == syntax_kind::unnamed_namespace_definition ||
           kind == syntax_kind::nested_namespace_definition;
}

analysis_result semantic_analysis::declaration(SyntaxRef n)
{
    analysed_declaration d;
    d.node = n;
    current_declaration = n;
    size_t unsupported_before = unsupported.size();
    size_t outer_refusals = declaration_refusals;
    declaration_refusals = unsupported_before;
    size_t held_before = held_pending.size();
    size_t scopes_before = unknown_scopes_pending.size();
    analysed_declaration *outer_analysing = analysing;
    analysing = &d;
    if (is_namespace_definition(tree, n)) {
        /* the declarations of its body are analysed as declarations of
           their own: one of them not parsed does not make the namespace
           so, and the definition has no record unless it is not
           well-formed itself */
        d.result = namespace_definition(n);
        if (d.result == analysis_result::well_formed && !d.recovered) {
            analysing = outer_analysing;
            return d.result;
        }
    } else if (tree[n].kind == syntax_kind::linkage_specification) {
        /* [dcl.link]: a linkage-specification is no scope of its own, its
           declarations are the enclosing scope's: one of them not parsed
           does not stop the others (linkage_specification() checks each) */
        d.result = linkage_specification(n);
    } else if (!parsed_completely(n)) {
        d.result = analysis_result::not_parsed;
        /* what it declares is not known (a name it declares is no name
           "no scope has") */
        declaration_not_parsed_seen = true;
    } else
        d.result = declaration_by_kind(n);
    analysing = outer_analysing;
    /* a recovered error: ill-formed, what it declares declared all the
       same (hold_names does not run) */
    bool declares = d.recovered && d.result == analysis_result::well_formed;
    if (declares)
        d.result = analysis_result::ill_formed;
    if (d.result == analysis_result::unsupported)
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
    d.has_unsupported = unsupported.size() > unsupported_before;
    /* (a linkage-specification holds the names of its own declarations) */
    if (d.result != analysis_result::well_formed && !declares &&
        tree[n].kind != syntax_kind::linkage_specification) {
        /* [basic.scope.declarative]: a function redeclared ill-formed is
           still the function its first declaration declared */
        hold_ill_formed = d.result == analysis_result::ill_formed &&
                          !d.has_unsupported &&
                          tree[n].kind != syntax_kind::function_definition;
        hold_names(n);
        hold_ill_formed = false;
    }
    /* the names a template-declaration held while it was refused say why */
    held_cause cause = held_cause::refused;
    if (!d.has_unsupported && d.result == analysis_result::ill_formed)
        cause = held_cause::ill_formed;
    else if (!d.has_unsupported && d.result == analysis_result::not_parsed)
        cause = held_cause::not_parsed;
    for (size_t i = held_before; i < held_pending.size(); i++) {
        entity &held = entity_table[held_pending[i]];
        if (d.has_unsupported && !held.held_because)
            held.held_because = unsupported[unsupported_before].subclause;
        else if (!d.has_unsupported && !held.held_because)
            held.held_by = cause;
        /* (a well-formed declaration that holds a name, a definition of a
           class template whose first declaration was refused, keeps the
           holder that declaration was; it is the holder when there is no
           other, a deduction guide) */
        if (d.result != analysis_result::well_formed || !held.holder)
            held.holder = n;
    }
    held_pending.resize(held_before);
    for (size_t i = scopes_before; i < unknown_scopes_pending.size(); i++) {
        entity &scope = entity_table[unknown_scopes_pending[i]];
        if (d.has_unsupported && !scope.unknown_because)
            scope.unknown_because = unsupported[unsupported_before].subclause;
        else if (!d.has_unsupported && !scope.unknown_because)
            scope.unknown_by = cause;
    }
    unknown_scopes_pending.resize(scopes_before);
    declaration_refusals = outer_refusals;
    declarations.append(d);
    return d.result;
}

analysis_result semantic_analysis::declaration_by_kind(SyntaxRef n)
{
    /* (a node the grammar does not place among declarations, (unparsed)
       or an extension's, goes to the default case) */
    return Declaration(n).visit(Overloaded{
        [&](FunctionDefinition f) {
            /* [dcl.fct]/18: an abbreviated function template */
            if (abbreviated_function(f) && !out_of_class_member(f))
                return function_template_definition(f, f);
            return function_definition(f);
        },
        [&](TemplateDeclaration t) { return template_declaration(t); },
        [&](SimpleDeclaration d) { return simple_declaration(d); },
        [&](AliasDeclaration a) { return alias_declaration(a); },
        [&](StaticAssertDeclaration a) { return static_assert_declaration(a); },
        [&](EmptyDeclaration e) { return empty_declaration(e); },
        [&](ExplicitSpecialization s) { return explicit_specialization(s); },
        [&](ExplicitInstantiation i) { return explicit_instantiation(i); },
        [&](DeductionGuide g) { return deduction_guide(g); },
        [&](LinkageSpecification l) { return linkage_specification(l); },
        [&](NamespaceDefinition d) {
            /* in a linkage-specification */
            return namespace_definition(d);
        },
        [&](UsingDirective u) { return using_directive(u); },
        [&](NamespaceAliasDefinition a) {
            return namespace_alias_definition(a);
        },
        [&](UsingDeclaration u) { return using_declaration(u); },
        [&](UsingEnumDeclaration u) { return using_enum_declaration(u); },
        [&](OpaqueEnumDeclaration e) {
            type_index type = 0;
            return enum_specifier(e, type);
        },
        [&](GnuExtensionDeclaration e) {
            /* [ext:gnu_extension_keyword]: __extension__ changes no
                   meaning */
            return declaration_by_kind(e[1]);
        },
        [&](Declaration d) {
            /* [dcl.pre]/4: "An attribute-declaration has no effect" unless
               an attribute says otherwise: the ones that change nothing
               vcc generates, and those clang does not know (it ignores them,
               [dcl.attr.grammar]/6), are passed over */
            SyntaxRef r = d;
            if (r.is(syntax_kind::attribute_declaration)) {
                bool ignored = true;
                for (SyntaxElement c : r.elements())
                    if (!c.is_token() && !attributes_without_effect(c))
                        ignored = false;
                if (ignored)
                    return analysis_result::well_formed;
            }
            return not_supported(d);
        }});
}

void semantic_analysis::translation_unit()
{
    /* [impl.clang] a typedef-name every translation unit starts with
       (the parser's names know it too) */
    {
        static const char name[] = "__builtin_va_list";
        uint32_t id = tokens.pp.identifier(name, sizeof name - 1) + 1;
        entity_index e = entity_table.make(entity_kind::typedef_name, id, 0);
        if (tokens.files.behavior.va_list_is_array) {
            /* x86-64 System V: struct __va_list_tag[1] */
            qualified_type tag = qualified_type::make(
                type_table.fundamental(fundamental_type::builtin_va_list_tag),
                cv_none);
            entity_table[e].type =
                qualified_type::make(type_table.array_of(tag, 1), cv_none);
        } else {
            entity_table[e].type = qualified_type::make(
                type_table.fundamental(fundamental_type::builtin_va_list),
                cv_none);
        }
        entity_table[e].defined = true;
        entity_table.bind(0, id, e);
    }
    /* [impl.clang] __int128_t and __uint128_t, as the parser's names */
    static const struct {
        const char *name;
        fundamental_type type;
    } int128_names[] = {{"__int128_t", fundamental_type::int128},
                        {"__uint128_t", fundamental_type::unsigned_int128}};
    for (const auto &t : int128_names) {
        uint32_t id = tokens.pp.identifier(t.name, strlen(t.name)) + 1;
        entity_index e = entity_table.make(entity_kind::typedef_name, id, 0);
        entity_table[e].type =
            qualified_type::make(type_table.fundamental(t.type), cv_none);
        entity_table[e].defined = true;
        entity_table.bind(0, id, e);
    }
    owned_array<node_index> all;
    tree.declarations(&all);
    for (node_index n : all)
        declaration(n);
    instantiate_pending();
}

} // namespace cplusplus
