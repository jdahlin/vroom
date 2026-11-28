/*
 * mangling/names.cpp: [abi:mangle.name] <name>, <nested-name>, <prefix>, <unscoped-name>,
 * <unqualified-name> and <source-name>; [abi:mangling-operator] and
 * [abi:mangling-special-ctor-dtor]'s names of operators, constructors and
 * destructors (doc/PLAN-mangling.md).
 */
#include "mangling-internal.h"

namespace cplusplus
{

/*
 * [abi:mangle.name]:
 *
 *     <name> ::= <nested-name>
 *            ::= <unscoped-name>
 *            ::= <local-name>
 *
 * No template scope yet.  An entity of the global namespace or of ::std
 * has an <unscoped-name>; a member of another namespace or of a class a
 * <nested-name>; an entity of a function's block scope a <local-name>
 * ([abi:mangling-scope]).
 */
bool itanium_mangler::name(entity_index e)
{
    const entities &table = analysis.entity_table;
    entity_index scope = table[e].enclosing;
    /* (a closure type with a <data-member-prefix> is a <nested-name>) */
    if ((scope == 0 || is_std(scope)) && !table[e].closure_variable)
        return unscoped_name(e);
    entity_index local_class = 0;
    entity_index local = local_function(e, &local_class);
    if (local && table[scope].kind == entity_kind::class_type) {
        /* a member of a local class: Z <function encoding> E
           <nested-name> */
        if (!local_name_scope(local))
            return false;
        /* [abi:mangling-scope] a default argument's scope:
           d [<number>] _ */
        if (uint32_t parameter =
                table[local_class].default_argument_parameter) {
            enc.write(terminal::d_default_argument);
            if (parameter > 1)
                enc.number(parameter - 2);
            enc.write(terminal::underscore);
        }
        if (!nested_name(e))
            return false;
        local_class_discriminator(local_class, local);
        return true;
    }
    if (table[scope].kind == entity_kind::class_type ||
        table[scope].kind == entity_kind::namespace_entity)
        return nested_name(e);
    entity_index function = enclosing_function(table, e);
    if (!function && table[scope].kind == entity_kind::none)
        return refuse("a member of a class the analysis no longer knows "
                      "(its name held by a declaration it did not "
                      "analyse)");
    if (!function)
        return refuse("an entity whose scope is neither a class, a "
                      "namespace nor a function");
    return local_name(e, function);
}

/*
 * [abi:mangle.qualified-type]:
 *
 *     <CV-qualifiers> ::= [r] [V] [K]
 *     <ref-qualifier> ::= R  # &
 *                     ::= O  # &&
 *
 * A non-static member function's <nested-name> places these before the
 * prefix ([abi:mangle.name]). The member qualifiers here are volatile,
 * const, and the optional ref-qualifier.
 */
void itanium_mangler::member_cv_ref_qualifiers(const entity &x)
{
    cv_qualifiers(x.member_cv & (cv_volatile | cv_const));
    int ref = 0;
    if (x.member_cv & cv_ref_lvalue)
        ref = 1;
    if (x.member_cv & cv_ref_rvalue)
        ref = 2;
    ref_qualifier(ref);
}

/*
 * [abi:mangle.name]:
 *
 *     <nested-name> ::= N [<CV-qualifiers>] [<ref-qualifier>] <prefix> <unqualified-name> E
 *
 * The CV-qualifiers and ref-qualifier are a non-static member function's
 * (the ones of its declarator).
 */
bool itanium_mangler::nested_name(entity_index e)
{
    const entity &x = analysis.entity_table[e];
    enc.write(terminal::N_nested_name);
    if (x.kind == entity_kind::function && x.nonstatic_member)
        member_cv_ref_qualifiers(x);
    if (!prefix(x.enclosing))
        return false;
    if (!unqualified_name(e))
        return false;
    enc.write(terminal::E_end);
    return true;
}

/*
 * [abi:mangle.name]:
 *
 *     <prefix> ::= <unqualified-name>
 *     <prefix> ::= <prefix> <unqualified-name>
 *     <prefix> ::= <substitution>
 *
 * A class or a namespace: of the global namespace its name, of another
 * scope that scope's prefix and its name.  [abi:mangling-compression]: a
 * prefix is a substitution candidate, a class's being its type's (the
 * class named again as a prefix or a type is the same candidate), a
 * namespace's its own; ::std as a prefix is the substitution St, which is
 * no candidate.  A local class starts the prefix: the <local-name>'s
 * function scope is written before the <nested-name> (local_name_scope).
 */
bool itanium_mangler::prefix(entity_index scope)
{
    const entities &table = analysis.entity_table;
    const entity &c = table[scope];
    bool is_class = c.kind == entity_kind::class_type;
    if (!is_class && c.kind != entity_kind::namespace_entity)
        return false;
    if (!is_class && is_std(scope)) {
        enc.write(terminal::St_std_prefix);
        return true;
    }
    if (is_class && substitution(c.type))
        return true;
    /* ::std::string and the char streams as a prefix: their abbreviation,
       no candidate (_ZNSs4sizeEv); ::std::allocator<T> and another
       ::std::basic_string Sa or Sb and the arguments, a candidate as a
       whole */
    if (is_class && c.name && c.instantiation) {
        terminal whole = std_substitution(scope);
        if (whole != terminal::E_end) {
            enc.write(whole);
            return true;
        }
        size_t length = 0;
        const char *identifier = spelling(c.name, &length);
        terminal code = terminal::E_end;
        if (length == 9 && !memcmp(identifier, "allocator", 9))
            code = terminal::Sa_std_allocator;
        if (length == 12 && !memcmp(identifier, "basic_string", 12))
            code = terminal::Sb_std_basic_string;
        if (c.instantiation && is_std(c.enclosing) &&
            !in_inline_namespace(scope) && code != terminal::E_end) {
            enc.write(code);
            if (!template_args(scope))
                return false;
            add_candidate(c.type);
            return true;
        }
    }
    if (is_class && c.instantiation &&
        substitution(analysis.specialized_template(scope))) {
        if (!template_args(scope))
            return false;
        add_candidate(c.type);
        return true;
    }
    if (!is_class && substitution(scope))
        return true;
    /* a local class: the function scope (local_name_scope) came before the
       <nested-name>, whose prefix starts at the class */
    bool local = is_class && c.enclosing &&
                 table[c.enclosing].kind != entity_kind::class_type &&
                 table[c.enclosing].kind != entity_kind::namespace_entity;
    if (c.enclosing && !local && !prefix(c.enclosing))
        return false;
    if (!unqualified_name(scope))
        return false;
    if (is_class && !class_template_args(scope))
        return false;
    if (is_class) {
        add_candidate(c.type);
    } else {
        candidate x;
        x.scope = scope;
        enc.add_candidate(x);
    }
    return true;
}

/* [abi:mangling-compression]: ::std, which St abbreviates: the namespace
   std of the global namespace */
bool itanium_mangler::is_std(entity_index scope) const
{
    return analysis.std_namespace(scope);
}

bool itanium_mangler::in_inline_namespace(entity_index e) const
{
    entity_index s = analysis.entity_table[e].enclosing;
    return s &&
           analysis.entity_table[s].kind == entity_kind::namespace_entity &&
           analysis.entity_table[s].inline_;
}

/* whether e is declared, directly or not, within an unnamed namespace */
bool itanium_mangler::in_unnamed_namespace(entity_index e) const
{
    const entities &table = analysis.entity_table;
    for (entity_index s = table[e].enclosing; s; s = table[s].enclosing)
        if (table[s].kind == entity_kind::namespace_entity && !table[s].name)
            return true;
    return false;
}

/*
 * [abi:mangle.name]:
 *
 *     <unscoped-name> ::= <unqualified-name>
 *                     ::= St <unqualified-name>
 *
 * The second for a member of ::std.
 */
bool itanium_mangler::unscoped_name(entity_index e)
{
    if (analysis.entity_table[e].enclosing)
        enc.write(terminal::St_std_prefix);
    return unqualified_name(e);
}

/*
 * [abi:mangle.name]:
 *
 *     <unqualified-name> ::= <source-name>
 *
 * [impl.gnu] [impl.clang] Both put L before the source name of a function
 * or variable of namespace scope with internal linkage (a static
 * function), which the ABI does not have, so that it is not the symbol of
 * an external entity of that name: _ZL2f2v, _ZN1NL1fEv.  Not within an
 * unnamed namespace, whose name already keeps the symbol apart: both name
 * an unnamed namespace _GLOBAL__N_1 (_ZN12_GLOBAL__N_11fEv).
 */
bool itanium_mangler::unqualified_name(entity_index e, bool as_type)
{
    if (!unqualified_name_without_tags(e, as_type))
        return false;
    abi_tags(e);
    /* [impl.clang] enable_if(true, ...): the vendor qualifier
       "U <source-name> <template-args>" of its condition */
    if (analysis.entity_table[e].gnu_flags & gnu_enable_if) {
        enc.write(terminal::U_vendor_qualifier);
        enc.write(terminal::a9enable_if_attribute);
        enc.write(terminal::I_template_args);
        expr_primary_value(fundamental_type::bool_, false, 1);
        enc.write(terminal::E_end);
    }
    return true;
}

/* [abi:mangle.abi-tag]:

       <abi-tags> ::= <abi-tag> [<abi-tags>]
       <abi-tag> ::= B <source-name>

   after the name, from GNU's __attribute__((abi_tag("tag"))) (one tag:
   the string literal's characters) */
void itanium_mangler::abi_tags(entity_index e)
{
    token_index tag = analysis.entity_table[e].abi_tag;
    if (!tag)
        return;
    owned_buffer written;
    analysis.token_spelling(tag, written.get());
    enc.write(terminal::B_abi_tag);
    enc.source_name(written.data() + 1, written.size() - 2);
}

bool itanium_mangler::unqualified_name_without_tags(entity_index e,
                                                    bool as_type)
{
    const entities &table = analysis.entity_table;
    const entity &x = table[e];
    if (x.kind == entity_kind::namespace_entity && !x.name) {
        enc.source_name("_GLOBAL__N_1");
        return true;
    }
    if (x.kind == entity_kind::variable && x.binding_count) {
        /* [abi:mangle.name] "<unqualified-name> ::= DC <source-name>+ E":
           a structured binding declaration's variable e ([dcl.struct.bind]/1),
           by its structured bindings' names; [impl.clang] without L for
           internal linkage */
        enc.write(terminal::DC_structured_binding);
        for (uint32_t k = 0; k < x.binding_count; k++)
            if (!source_name(table[x.first_binding + k].name))
                return false;
        enc.write(terminal::E_end);
        return true;
    }
    bool namespace_scope =
        x.enclosing == 0 ||
        table[x.enclosing].kind == entity_kind::namespace_entity;
    /* [impl.clang]: L for a name declared with internal linkage, not for
       a specialization whose internal linkage is its arguments' */
    linkage declared = x.linkage_of_name;
    if (x.instantiation && x.kind == entity_kind::function)
        declared = table[analysis.specialized_template(e)].linkage_of_name;
    if (declared == linkage::internal && namespace_scope &&
        (x.kind == entity_kind::function || x.kind == entity_kind::variable) &&
        !in_unnamed_namespace(e) && !x.unnamed_type_linkage &&
        !x.operator_function)
        enc.write(terminal::L_internal_linkage);
    if (x.special != special_member::none) {
        /* [impl.clang] an inherited default constructor is named as
           the class's own */
        entity_index inherited = x.inherited_constructor;
        if (!analysis.type_table[x.type.unqualified()].parameter_count)
            inherited = 0;
        return ctor_dtor_name(x.special, inherited);
    }
    if (x.operator_function)
        return operator_name(x);
    if (x.conversion_function) {
        /* [abi:mangling-operator]: "<operator-name> ::= cv <type> # (cast)" */
        enc.write(terminal::cv_cast);
        if (x.instantiation) {
            entity_index tmpl = analysis.specialized_template(e);
            scoped_value<entity_index> in_template(encoding_template, tmpl);
            return type(
                analysis
                    .type_table[analysis.entity_table[tmpl].type.unqualified()]
                    .operand);
        }
        return type(analysis.type_table[x.type.unqualified()].operand);
    }
    if (x.kind == entity_kind::function && x.name) {
        /* [abi:mangling-operator]: "<operator-name> ::= li <source-name>
           # operator \"\"": a literal operator, named operator""X */
        size_t length = 0;
        const char *bytes = spelling(x.name, &length);
        if (length > 10 && !memcmp(bytes, "operator\"\"", 10)) {
            enc.write(terminal::li_literal_operator);
            enc.source_name(bytes + 10, length - 10);
            return true;
        }
    }
    if (x.kind == entity_kind::class_type && x.lambda)
        return closure_type_name(e);
    bool unnamed = !x.name && !x.linkage_name &&
                   (x.kind == entity_kind::class_type ||
                    x.kind == entity_kind::enumeration);
    if (unnamed) {
        if (entity_index function = enclosing_function(table, e)) {
            /* [impl.clang]: as a closure type, one of a function that is
               neither inline nor a specialization is unique to the
               translation unit, $_N */
            if (!closure_numbered(e))
                return translation_unit_number(e);
            /* [abi:mangling.named]: <unnamed-type-name> ::= Ut [ <nonnegative number> ] _ */
            enc.write(terminal::Ut_unnamed_type);
            if (uint64_t ordinal = local_class_ordinal(e, function))
                enc.number(ordinal - 1);
            enc.write(terminal::underscore);
            return true;
        }
    }
    if (unnamed && x.enclosing &&
        table[x.enclosing].kind == entity_kind::class_type) {
        /* [abi:mangling.named]: an unnamed class of a class (clang's
           _ZN1OUt0_C2Ev): Ut, the number of the unnamed classes and
           enumerations of the class before it, less one, and _ */
        uint64_t before = 0;
        for (entity_index other{1}; other < e; other++) {
            const entity &k = table[other];
            if (k.enclosing == x.enclosing && !k.name && !k.linkage_name &&
                !k.lambda &&
                (k.kind == entity_kind::class_type ||
                 k.kind == entity_kind::enumeration))
                before++;
        }
        enc.write(terminal::Ut_unnamed_type);
        if (before)
            enc.number(before - 1);
        enc.write(terminal::underscore);
        return true;
    }
    /* [impl.clang]: one of namespace scope with no name for linkage
       purposes is unique to the translation unit too */
    if (unnamed && (!x.enclosing ||
                    table[x.enclosing].kind == entity_kind::namespace_entity))
        return translation_unit_number(e);
    /* [dcl.typedef]/9: an unnamed class's name for linkage purposes */
    if (!x.name && (x.kind == entity_kind::class_type ||
                    x.kind == entity_kind::enumeration)) {
        if (x.linkage_name)
            return source_name(x.linkage_name);
        return false;
    }
    return source_name(x.name);
}

/*
 * [abi:mangling-special-ctor-dtor]:
 *
 *     <ctor-dtor-name> ::= C1
 *                      ::= C2
 *                      ::= D0
 *                      ::= D1
 *                      ::= D2
 *
 * C1 and D1 are the complete object's, C2 and D2 the base object's, D0
 * the deleting destructor (variant 0).  The complete object constructor
 * of a class without virtual bases does what the base object constructor
 * does ([abi:mangling-special-ctor-dtor]); there are no allocating
 * constructors yet.
 */
bool itanium_mangler::ctor_dtor_name(special_member which,
                                     entity_index inherited)
{
    /* [abi:mangling-special-ctor-dtor]: "<ctor-dtor-name> ::= CI1 <type>
       ::= CI2 <type>": an inheriting constructor, by the base class whose
       constructor it was inherited from */
    bool complete = ctor_dtor_variant == 1;
    if (which != special_member::constructor) {
        if (complete)
            enc.write(terminal::D1_complete_destructor);
        else if (ctor_dtor_variant == 0)
            enc.write(terminal::D0_deleting_destructor);
        else
            enc.write(terminal::D2_base_destructor);
    } else if (inherited && complete) {
        enc.write(terminal::CI1_complete_inheriting_constructor);
    } else if (inherited) {
        enc.write(terminal::CI2_base_inheriting_constructor);
    } else if (complete) {
        enc.write(terminal::C1_complete_constructor);
    } else {
        enc.write(terminal::C2_base_constructor);
    }
    if (inherited) {
        /* [impl.clang] one inherited through another class's inheriting
           constructors is named by the class that declares the original;
           the class is no substitution candidate here */
        const entities &table = analysis.entity_table;
        while (table[inherited].inherited_constructor)
            inherited = table[inherited].inherited_constructor;
        return class_enum_type(qualified_type::make(
            table[table[inherited].enclosing].type.unqualified(), cv_none));
    }
    return true;
}

/*
 * [abi:mangle.name]:
 *
 *     <source-name> ::= <positive length number> <identifier>
 *     <identifier> ::= <unqualified source code identifier>
 *
 * The length is of the identifier's bytes (UTF-8 for one with a universal
 * character name, as clang and GCC write it).
 */
bool itanium_mangler::source_name(uint32_t identifier)
{
    if (!identifier)
        return false;
    size_t length = 0;
    const char *bytes = spelling(identifier, &length);
    enc.source_name(bytes, length);
    return true;
}

/*
 * [abi:mangling-operator]:
 *
 *     <operator-name> ::= nw  # new
 *                     ::= na  # new[]
 *                     ::= dl  # delete
 *                     ::= da  # delete[]
 *                     ::= ps  # + (unary)
 *                     ::= ng  # - (unary)
 *                     ::= ad  # & (unary)
 *                     ::= de  # * (unary)
 *                     ::= co  # ~
 *                     ::= pl  # +
 *                     ::= mi  # -
 *                     ::= ml  # *
 *                     ::= dv  # /
 *                     ::= rm  # %
 *                     ::= an  # &
 *                     ::= or  # |
 *                     ::= eo  # ^
 *                     ::= aS  # =
 *                     ::= pL  # +=
 *                     ::= mI  # -=
 *                     ::= mL  # *=
 *                     ::= dV  # /=
 *                     ::= rM  # %=
 *                     ::= aN  # &=
 *                     ::= oR  # |=
 *                     ::= eO  # ^=
 *                     ::= ls  # <<
 *                     ::= lS  # <<=
 *                     ::= eq  # ==
 *                     ::= ne  # !=
 *                     ::= lt  # <
 *                     ::= le  # <=
 *                     ::= ss  # <=>
 *                     ::= nt  # !
 *                     ::= aa  # &&
 *                     ::= oo  # ||
 *                     ::= pp  # ++
 *                     ::= mm  # --
 *                     ::= cm  # ,
 *                     ::= pm  # ->*
 *                     ::= pt  # ->
 *                     ::= cl  # ()
 *                     ::= ix  # []
 *
 * and rs (>>), rS (>>=), gt (>), and ge (>=) (operators.inc).  The
 * operand count includes the implicit object ([over.unary]/1).
 */
bool itanium_mangler::operator_name(const entity &x)
{
    switch (x.allocation) {
    case allocation_function::new_:
        enc.write(itanium_operator::nw_new_);
        return true;
    case allocation_function::new_array:
        enc.write(itanium_operator::na_new_array);
        return true;
    case allocation_function::delete_:
        enc.write(itanium_operator::dl_delete_);
        return true;
    case allocation_function::delete_array:
        enc.write(itanium_operator::da_delete_array);
        return true;
    default:
        break;
    }
    /* the operator of the function's operand count, else the one row of
       its token (++, ->, (), [] whatever their count) */
    const struct type &function = analysis.type_table[x.type.unqualified()];
    unsigned operands = function.parameter_count + unsigned(x.nonstatic_member);
    unsigned arity = 2;
    if (operands == 1)
        arity = 1;
    itanium_operator o = operator_written(x.implemented, arity);
    for (unsigned other = 0; o == itanium_operator::none && other < 4; other++)
        o = operator_written(x.implemented, other);
    if (o == itanium_operator::none)
        return false;
    enc.write(o);
    return true;
}

} // namespace cplusplus
