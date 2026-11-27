/*
 * mangling/scopes.cpp: [abi:mangling-scope] <local-name> and <discriminator>; [abi:closure-types] (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/* the function whose definition a block-scope entity is declared in: the
   enclosing block scopes end at its parameter scope, whose enclosing entity
   is the function (function_body); 0 for an entity not in a function */
entity_index enclosing_function(const entities &table, entity_index e)
{
    entity_index s = table[e].enclosing;
    while (s && table[s].kind == entity_kind::block_scope)
        s = table[s].enclosing;
    if (s && table[s].kind == entity_kind::function)
        return s;
    return 0;
}

entity_index next_enclosing_function(const entities &table, entity_index f)
{
    entity_index k = table[f].enclosing;
    if (!k || table[k].kind != entity_kind::class_type)
        return 0;
    while (table[k].enclosing &&
           table[table[k].enclosing].kind == entity_kind::class_type)
        k = table[k].enclosing;
    return enclosing_function(table, k);
}

bool visible_local_class(const entities &table, entity_index c)
{
    if (table[c].closure_variable)
        return true;
    /* (a class nested in a local class: as a member of it) */
    entity_index f = next_enclosing_function(table, c);
    if (!f)
        f = enclosing_function(table, c);
    if (!f)
        return false;
    for (entity_index g = next_enclosing_function(table, f); g;
         g = next_enclosing_function(table, g))
        f = g;
    /* (a static inline function, or one of an unnamed namespace, is no
       other unit's) */
    if (table[f].linkage_of_name == linkage::internal &&
        !table[f].instantiation)
        return false;
    return table[f].inline_ || table[f].instantiation;
}

/* Find the outermost class in the function's scope. */
entity_index itanium_mangler::local_function(entity_index e,
                                             entity_index *local_class) const
{
    const entities &table = analysis.entity_table;
    entity_index c = e;
    /* (a local enumeration is a local entity as a class is,
       [basic.scope.block]; one of a class goes with its class) */
    if (table[c].kind == entity_kind::enumeration) {
        if (table[table[c].enclosing].kind == entity_kind::class_type)
            c = table[c].enclosing;
    } else if (table[c].kind != entity_kind::class_type)
        c = table[c].enclosing;
    while (c && table[c].kind == entity_kind::class_type &&
           table[table[c].enclosing].kind == entity_kind::class_type)
        c = table[c].enclosing;
    if (!c || (table[c].kind != entity_kind::class_type &&
               table[c].kind != entity_kind::enumeration))
        return 0;
    entity_index function = enclosing_function(table, c);
    if (function && local_class)
        *local_class = c;
    return function;
}

/* [abi:mangling-scope]: count earlier classes and enumerations with the
   same name in the function. Unnamed ones have their own sequence, separate from lambdas. */
uint64_t itanium_mangler::local_class_ordinal(entity_index c,
                                              entity_index function) const
{
    const entities &table = analysis.entity_table;
    uint32_t name = table[c].name;
    if (!name)
        name = table[c].linkage_name;
    uint64_t ordinal = 0;
    for (entity_index other = 1; other < c; other++) {
        const entity &x = table[other];
        uint32_t other_name = x.name;
        if (!other_name)
            other_name = x.linkage_name;
        if ((x.kind == entity_kind::class_type ||
             x.kind == entity_kind::enumeration) &&
            !x.lambda && other_name == name &&
            enclosing_function(table, other) == function)
            ordinal++;
    }
    return ordinal;
}

void itanium_mangler::local_class_discriminator(entity_index c,
                                                entity_index function)
{
    const entity &x = analysis.entity_table[c];
    if (x.lambda || (!x.name && !x.linkage_name))
        return;
    if (uint64_t ordinal = local_class_ordinal(c, function))
        enc.discriminator(ordinal - 1);
}

/* [abi:mangling-scope]: Z <function encoding> E, the scope of a
   <local-name>; main's encoding is its name alone (local_name) */
bool itanium_mangler::local_name_scope(entity_index function)
{
    const entity &f = analysis.entity_table[function];
    size_t length = 0;
    const char *identifier = spelling(f.name, &length);
    bool is_main =
        !f.enclosing && length == 4 && !memcmp(identifier, "main", 4);

    enc.write(terminal::Z_local_name);
    if (is_main) {
        if (!unqualified_name(function))
            return false;
    } else if (!encoding(function))
        return false;
    enc.write(terminal::E_end);
    return true;
}

/*
 * [abi:mangling-scope]:
 *
 *     <local-name> ::= Z <function encoding> E <entity name> [<discriminator>]
 *
 * The entity's name is mangled as if the function were the global scope:
 * an unscoped name for a variable declared directly in it.  When several
 * entities of the function have the same name (in different blocks), the
 * section numbers them in lexical order and gives the second and later a
 * discriminator, the n-th one's number being n - 2.  The ones numbered
 * here are the function's static local variables, the only local entities
 * with a symbol in S1.
 */
bool itanium_mangler::local_name(entity_index e, entity_index function)
{
    const entities &table = analysis.entity_table;
    const entity &x = table[e];
    if (x.kind != entity_kind::variable || x.automatic)
        return false;
    if (!local_name_scope(function))
        return false;
    if (!unqualified_name(e))
        return false;
    uint64_t earlier = 0;
    for (entity_index other = 1; other < e; other++)
        if (table[other].kind == entity_kind::variable &&
            !table[other].automatic && table[other].name == x.name &&
            table[other].enclosing &&
            table[table[other].enclosing].kind == entity_kind::block_scope &&
            enclosing_function(table, other) == function)
            earlier++;
    if (earlier)
        enc.discriminator(earlier - 1);
    return true;
}

/*
 * [abi:closure-types]: "In the following contexts, however, the
 * one-definition rule requires closure types in different translation
 * units to "correspond": ... the bodies of inline or templated functions"
 * (a lambda's function call operator is inline: one in a lambda's body
 * too); a closure type elsewhere is "unique to the translation unit".
 */
bool itanium_mangler::closure_numbered(entity_index c) const
{
    const entities &table = analysis.entity_table;
    if (table[c].closure_variable)
        return true;
    /* (a member function of a local class: its enclosing function's
       too) */
    for (entity_index f = enclosing_function(table, c); f;
         f = next_enclosing_function(table, f))
        if (table[f].inline_ || table[f].instantiation)
            return true;
    return false;
}

/* [impl.clang]: the name of a closure type, or of an unnamed local class or
   enumeration, that is unique to the translation unit: the <source-name>
   $_N, N its number among those of its function (or of namespace scope,
   which counts others of functions too) in the order their names are first mangled (as clang's code generator mangles them) */
bool itanium_mangler::translation_unit_number(entity_index c)
{
    const entities &table = analysis.entity_table;
    if (!analysis.number_closures)
        return false;
    entity_index context = enclosing_function(table, c);
    size_t n = 0;
    bool seen = false;
    for (entity_index k : analysis.unnumbered_closures) {
        if (k == c) {
            seen = true;
            break;
        }
        /* ([impl.clang] a generic lambda's closure of a function takes
           a number of the translation unit's too, and an unnamed class or
           enumeration of namespace scope counts every one before it) */
        if (enclosing_function(table, k) == context ||
            (!context &&
             (!table[c].lambda ||
              (table[k].lambda && table[table[k].call_operator].kind ==
                                      entity_kind::template_entity))))
            n++;
    }
    if (!seen)
        analysis.unnumbered_closures.append(c);
    owned_buffer written;
    vbuffer_format_append(written.get(), "$_%llu",
                          static_cast<unsigned long long>(n));
    enc.source_name(written.data(), written.size());
    return true;
}

/*
 * [abi:closure-types]:
 *
 *     <closure-type-name> ::= Ul <lambda-sig> E [ <nonnegative number> ] _
 *     <lambda-sig> ::= <parameter type>+
 *
 * "The number is omitted for the first closure type with a given
 * <lambda-sig> in a given context; it is n-2 for the nth closure type (in
 * lexical order) with that same <lambda-sig> and context."  A closure type
 * unique to the translation unit has no encoding in the ABI
 * (translation_unit_number).
 */
bool itanium_mangler::closure_type_name(entity_index c)
{
    const entities &table = analysis.entity_table;
    entity_index call = table[c].call_operator;
    if (!call)
        return false;
    if (!closure_numbered(c))
        return translation_unit_number(c);
    /* [impl.clang] the context of a closure type in an inline variable's
       initializer (the ABI's later data-member-prefix): the variable's
       <source-name> and M */
    entity_index variable = table[c].closure_variable;
    if (variable) {
        if (!source_name(table[variable].name))
            return false;
        enc.write(terminal::M_closure_prefix);
        candidate x;
        x.scope = variable;
        enc.add_candidate(x);
    }
    enc.write(terminal::Ul_closure_type);
    /* [impl.clang] (the ABI's later template-param-decl) before the
       parameter types, one per template-parameter of a lambda's
       template-parameter-list: Ty for a type-parameter (the only ones
       analysed) */
    if (table[call].kind == entity_kind::template_entity) {
        /* ([impl.clang] the invented ones of an auto parameter have none) */
        uint32_t explicit_count =
            analysis.template_parameter_count(call) -
            analysis.invented_parameter_count(table[c].lambda);
        /* the template-param-decls: Ty, Tn and the type, Tp before a pack's */
        for (uint32_t i = 0; i < explicit_count; i++) {
            bool pack = false;
            qualified_type value_type;
            semantic_analysis::template_parameter_form_kind kind =
                analysis.template_parameter_kind(call, i, pack, value_type);
            if (kind ==
                semantic_analysis::template_parameter_form_kind::template_)
                return false;
            if (pack)
                enc.write(terminal::Tp_pack_parameter_decl);
            if (kind == semantic_analysis::template_parameter_form_kind::type) {
                enc.write(terminal::Ty_type_parameter_decl);
            } else {
                enc.write(terminal::Tn_non_type_parameter_decl);
                if (!type(value_type))
                    return false;
            }
        }
        /* [impl.clang] the template-head's requires-clause: Q and its
           expression */
        bool trailing = false;
        SyntaxElement constraint =
            analysis.lambda_constraint(table[c].lambda, &trailing);
        if (constraint && !trailing) {
            enc.write(terminal::Q_requires_clause);
            if (!expression_of_constraint(call, constraint))
                return false;
        }
    }
    qualified_type signature = table[call].type;
    if (!bare_function_type(signature))
        return false;
    enc.write(terminal::E_end);
    entity_index context = enclosing_function(table, c);
    uint64_t earlier = 0;
    for (entity_index other : analysis.closure_entities) {
        if (other >= c)
            break;
        const entity &k = table[other];
        if (k.kind != entity_kind::class_type || !k.lambda ||
            !k.call_operator || enclosing_function(table, other) != context ||
            k.closure_variable != variable ||
            k.default_argument_parameter != table[c].default_argument_parameter)
            continue;
        /* (a default member initializer analysed in each constructor: the
           one closure type of its lambda-expression) */
        if (variable && !table[variable].inline_ && k.lambda == table[c].lambda)
            continue;
        const struct type &a = record(table[k.call_operator].type);
        const struct type &b = record(signature);
        if (a.variadic == b.variadic &&
            same_parameters(analysis.type_table,
                            table[k.call_operator].type.unqualified(),
                            signature.unqualified()))
            earlier++;
    }
    if (earlier)
        enc.number(earlier - 1);
    enc.write(terminal::underscore);
    return true;
}

} // namespace cplusplus
