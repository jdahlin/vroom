/*
 * mangling/symbols.cpp: [abi:mangling-structure] a symbol's <mangled-name> and <encoding>, and
 * [abi:mangling-special]'s special names; the entry points (sema.h) (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:mangling-structure]:
 *
 *     <mangled-name> ::= _Z <encoding>
 *
 * The section exempts two kinds of names from mangling: those of entities
 * with C language linkage, and those of variables of the global namespace;
 * their symbol is the identifier.  main is not mangled either: its symbol
 * is the one the C run-time calls ([basic.start.main]/1 makes it the
 * program's start, and every Itanium implementation spells it main).
 */
bool itanium_mangler::mangled_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    if (x.kind == entity_kind::none)
        return refuse("an entity the analysis no longer knows (its name "
                      "held by a declaration it did not analyse)");
    if (x.kind != entity_kind::variable && x.kind != entity_kind::function)
        return refuse("an entity that is neither a function nor a variable");
    if (x.typeinfo_of)
        return typeinfo_name(x.typeinfo_of);
    /* (a closure type's constructor or destructor has its class's name:
       none) */
    /* (a structured binding declaration's variable e: its bindings'
       names, unqualified_name_without_tags) */
    bool binding = x.kind == entity_kind::variable && x.binding_count;
    if (!x.name && x.special == special_member::none && !binding)
        return false;
    size_t length = 0;
    const char *identifier = "";
    if (x.name)
        identifier = spelling(x.name, &length);
    /* an entity of the global namespace: a variable of another namespace
       is mangled, as is a function named main there */
    bool global = x.enclosing == 0;
    bool is_main = x.kind == entity_kind::function && length == 4 &&
                   !memcmp(identifier, "main", 4);
    /* [impl.clang]: a variable of the global namespace with internal
       linkage is mangled all the same (_ZL1x), so that it cannot collide
       with an extern "C" name of the translation unit; GCC writes x */
    bool internal_variable = x.kind == entity_kind::variable &&
                             x.linkage_of_name == linkage::internal;
    if (x.unnamed_type_linkage)
        internal_variable = false;
    /* [dcl.link]/7: "At most one function with a particular name can have
       C language linkage.  Two declarations for a function with C language
       linkage with the same function name ... that appear in different
       namespace scopes refer to the same function": its symbol is its
       name in any namespace, and so a variable's */
    /* (a variable template's specialization is mangled, _Z2piIdE) */
    bool specialization = x.kind == entity_kind::variable && x.instantiation;
    if (x.c_language_linkage ||
        (global && !internal_variable && !specialization && !binding &&
         (x.kind == entity_kind::variable || is_main))) {
        enc.unmangled(identifier, length);
        return true;
    }
    enc.write(terminal::underscore_Z_mangled_name);
    return encoding(e);
}

/*
 * [abi:mangling-structure]:
 *
 *     <encoding> ::= <function name> <bare-function-type>
 *                ::= <data name>
 *
 * A variable's encoding has no type.
 */
bool itanium_mangler::encoding(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    if ((x.kind == entity_kind::function || x.kind == entity_kind::variable) &&
        x.instantiation)
        return encoding_of_specialization(e);
    /* [impl.clang] a function whose symbol is not mangled (C language
       linkage, main; mangled_name), named in an <expr-primary>'s L _Z
       <encoding> E: its unqualified name alone, no function type, in any
       namespace ([dcl.link]/7) */
    if (x.kind == entity_kind::function && x.name && !x.instantiation) {
        size_t length = 0;
        const char *identifier = spelling(x.name, &length);
        bool is_main =
            !x.enclosing && length == 4 && !memcmp(identifier, "main", 4);
        if (x.c_language_linkage || is_main)
            return source_name(x.name);
    }
    if (!name(e))
        return false;
    if (x.kind == entity_kind::variable)
        return true;
    if (!bare_function_type(x.type))
        return false;
    if (x.kind == entity_kind::function && x.first_declaration && x.enclosing &&
        analysis.entity_table[x.enclosing].instantiation) {
        SyntaxRef declaration =
            analysis.entity_table.declaration(x.first_declaration).node;
        for (SyntaxElement part : declaration.elements()) {
            if (!part.is(syntax_kind::requires_clause) || part.size() != 2)
                continue;
            entity_index pattern = analysis.specialized_template(x.enclosing);
            if (!pattern)
                return false;
            enc.write(terminal::Q_requires_clause);
            return expression_of_constraint(pattern, part[1]);
        }
    }
    return true;
}

/*
 * A function or variable template's specialization (a variable's has no
 * type, as above).  [abi:mangle.function-type]: a
 * template function's return type is encoded: the template's return type,
 * then its parameter types, as the template declares them (a template
 * parameter as <template-param>, type()).  [abi:mangle.name]:
 *
 *     <name> ::= <unscoped-template-name> <template-args>
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <template-prefix> <template-args> E
 *     <template-prefix> ::= <prefix> <template unqualified-name>
 *
 * The template's name is a substitution candidate after its prefix,
 * before the arguments.  [abi:mangle.template-arg]:
 *
 *     <template-args> ::= I <template-arg>+ E
 *     <template-arg> ::= <type>
 */
bool itanium_mangler::encoding_of_specialization(entity_index e)
{
    const entities &table = analysis.entity_table;
    /* (a specialization named within another's arguments: the enclosing
       one's template parameters are resolved again after it) */
    scoped_value<entity_index> outer_template(encoding_template,
                                              encoding_template);
    entity_index pattern_of = analysis.specialized_template(e);
    entity_index scope = table[e].enclosing;
    bool nested = scope && !is_std(scope);
    entity_index local_class = 0;
    entity_index local = 0;
    if (nested) {
        /* (a member function template's specialization, [temp.mem]: the
           nested-name's CV-qualifiers its own, as nested_name's) */
        const entity &x = table[e];
        bool member = table[scope].kind == entity_kind::class_type &&
                      (x.kind == entity_kind::function ||
                       x.kind == entity_kind::variable);
        if (table[scope].kind != entity_kind::namespace_entity && !member)
            return false;
        /* (a member of a local class: its function scope first, as
           name()'s) */
        local = local_function(e, &local_class);
        if (member && local && !local_name_scope(local))
            return false;
        enc.write(terminal::N_nested_name);
        if (member && x.nonstatic_member)
            member_cv_ref_qualifiers(x);
    }
    /* [abi:mangling-compression]: a template named before (a local type's
       function, itself the specialization being named, in a parameter of
       its lambda) is its substitution, its prefix with it */
    if (!substitution(pattern_of)) {
        if (nested) {
            if (!prefix(scope))
                return false;
        } else if (scope) {
            enc.write(terminal::St_std_prefix);
        }
        if (!unqualified_name(e))
            return false;
        candidate c;
        c.scope = pattern_of;
        enc.add_candidate(c);
    }
    /* [impl.clang]: only a function template's specialization has the
       parameters' types ("Tn") before its arguments; a variable's has not */
    entity_index typed_parameters = pattern_of;
    if (table[e].kind != entity_kind::function)
        typed_parameters = 0;
    /* (the parameters' types name the template's parameters, which
       their expressions resolve through it) */
    /* [abi:mangle.template-arg]'s template-args, with [impl.clang]'s
       requires-clause: I, the template-args, Q and the requires-clause's
       expression, E; the requires-clause of the template-head */
    SyntaxElement constraint = analysis.template_constraint(pattern_of);
    bool head_constraint = constraint && !table[scope].lambda;
    bool arguments = false;
    {
        entity_index arguments_template = encoding_template;
        if (typed_parameters)
            arguments_template = pattern_of;
        scoped_value<entity_index> in_arguments(encoding_template,
                                                arguments_template);
        arguments = template_args(e, typed_parameters, true, UINT32_MAX,
                                  !head_constraint);
    }
    if (!arguments)
        return false;
    if (head_constraint) {
        enc.write(terminal::Q_requires_clause);
        if (!expression_of_constraint(pattern_of, constraint))
            return false;
        enc.write(terminal::E_end);
    }
    if (nested)
        enc.write(terminal::E_end);
    if (local)
        local_class_discriminator(local_class, local);
    /* a variable template's specialization: <data name> */
    if (table[e].kind == entity_kind::variable)
        return true;
    qualified_type pattern = table[pattern_of].type;
    encoding_template = pattern_of;
    /* (a constructor or conversion function template's has none:
       [abi:mangle.function-type]'s exception for constructors and conversion
       operators) */
    if (table[e].special == special_member::none &&
        !table[e].conversion_function) {
        scoped_value<const char *> in_return(mangling_part, "the return type");
        if (!type(record(pattern).operand))
            return false;
    }
    if (!bare_function_type(pattern))
        return false;
    /* [impl.clang] a generic lambda's or a function template's trailing
       requires-clause: Q and its expression, after the parameters (one after its template-parameters
       is the closure type's, closure_type_name) */
    bool trailing = false;
    if (table[scope].lambda)
        constraint = analysis.lambda_constraint(table[scope].lambda, &trailing);
    else if ((constraint = analysis.template_trailing_constraint(pattern_of)))
        trailing = true;
    if (constraint && trailing) {
        enc.write(terminal::Q_requires_clause);
        return expression_of_constraint(pattern_of, constraint);
    }
    return true;
}

bool itanium_symbol_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index e, vbuffer *out,
                         int variant)
{
    itanium_mangler m(analysis, pp, out, variant);
    return m.mangled_name(e);
}

bool itanium_symbol_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index e, vbuffer *out,
                         int variant, vbuffer *why)
{
    itanium_mangler m(analysis, pp, out, variant);
    if (m.mangled_name(e))
        return true;
    if (!m.refused) {
        vbuffer_string_append(why, "a construct it has no rule for");
        return false;
    }
    vbuffer_string_append(why, m.refused);
    vbuffer_string_append(why, " in ");
    vbuffer_string_append(why, m.refused_in);
    return false;
}

bool itanium_reference_temporary_name(const semantic_analysis &analysis,
                                      const preprocessor &pp, entity_index e,
                                      uint32_t index, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.reference_temporary_name(e, index);
}

bool itanium_typeinfo_name(const semantic_analysis &analysis,
                           const preprocessor &pp, type_index t, int what,
                           vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.typeinfo_name(t, what);
}

bool itanium_vtable_name(const semantic_analysis &analysis,
                         const preprocessor &pp, entity_index c, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.vtable_name(c);
}

bool itanium_vtt_name(const semantic_analysis &analysis, const preprocessor &pp,
                      entity_index c, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.vtt_name(c);
}

bool itanium_construction_vtable_name(const semantic_analysis &analysis,
                                      const preprocessor &pp, entity_index c,
                                      uint64_t offset, entity_index base,
                                      vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    return m.construction_vtable_name(c, offset, base);
}

bool itanium_guard_variable_name(const semantic_analysis &analysis,
                                 const preprocessor &pp, entity_index e,
                                 vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 0);
    return m.guard_variable_name(e);
}

bool itanium_thunk_name(const semantic_analysis &analysis,
                        const preprocessor &pp, entity_index f, int variant,
                        int64_t adjustment, int64_t vcall, vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, variant);
    return m.thunk_name(f, adjustment, vcall);
}

void itanium_runtime_vtable_name(const semantic_analysis &analysis,
                                 const preprocessor &pp, const char *name,
                                 vbuffer *out)
{
    itanium_mangler m(analysis, pp, out, 2);
    m.runtime_vtable_name(name);
}

} // namespace cplusplus
