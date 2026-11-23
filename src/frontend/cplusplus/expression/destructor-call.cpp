/*
 * destructor-call.cpp: [expr.prim.id.dtor] Destruction, the explicit call of
 * a destructor and of a pseudo-destructor.
 *
 *     unqualified-id:
 *         ~ type-name
 *         ~ decltype-specifier
 *
 * The call's postfix-expression is a class member access ([expr.ref]) whose
 * id-expression is ~type-name or nested-name-specifier type-name :: ~type-name
 * (the grammar is postfix-expression.cpp's and id-expression.cpp's).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * The unqualified-id ~type-name of the id-expression id of a class member
 * access, the last part of a qualified-id included; nothing for another
 * name.
 */
SyntaxElement semantic_analysis::destructor_id(SyntaxElement id) const
{
    if (id.is(syntax_kind::qualified_id) && id.size() == 2)
        id = id[1];
    if (id.is(syntax_kind::unqualified_id) && id.size() == 2 &&
        id[0].is(preprocessing_op_or_punc::TILDE))
        return id;
    return SyntaxElement{};
}

/* [basic.types]/9: "Arithmetic types, enumeration types, pointer
   types, pointer-to-member types, std::nullptr_t, and cv-qualified versions
   of these types are collectively called scalar types." */
bool semantic_analysis::scalar_type(type_index t) const
{
    const struct type &x = type_table[t];
    if (x.kind == type_kind::fundamental)
        return x.fundamental != fundamental_type::void_;
    return x.kind == type_kind::pointer || x.kind == type_kind::enumeration ||
           x.kind == type_kind::pointer_to_member;
}

/*
 * The type the type-name of ~type-name names ([basic.lookup.classref]/3,
 * [basic.lookup.qual]/6): looked up in a scope (in_scope: the class's or
 * namespace's, else the context of the whole expression), a typedef-name's synonym; found is false when
 * the name finds no type.
 */
analysis_result
semantic_analysis::destructor_type_name(SyntaxRef n, SyntaxElement name,
                                        bool in_scope, entity_index scope,
                                        type_index &type, bool &found)
{
    found = false;
    type = 0;
    /* (clang's references of a destructor's name are the class's: the
       name's use is not recorded) */
    size_t uses_before = name_uses.size();
    SyntaxElement written = name[1];
    if (written.is(syntax_kind::simple_template_id)) {
        uint32_t cv = 0;
        VTRY(r, template_type(written, scope, in_scope, type, &cv));
        found = true;
        name_uses.resize(uses_before);
        return analysis_result::well_formed;
    }
    if (!written.is_identifier())
        /* ~ decltype-specifier */
        return not_supported(n, "expr.prim.id.dtor");
    token_index t = written.token();
    uint32_t id = tokens.identifier(t);
    if (in_scope && scope &&
        entity_table[scope].kind == entity_kind::class_type &&
        entity_table[scope].name == id) {
        /* [class.pre]/2: the injected-class-name, the class's own name in
           its scope */
        type = entity_table[scope].type.unqualified();
        found = true;
        return analysis_result::well_formed;
    }
    lookup_result x;
    if (in_scope)
        x = lookup_qualified(scope, id, lookup_filter::types);
    else
        x = lookup_unqualified(id, lookup_filter::types);
    if (x.ambiguous || x.unknown)
        return not_supported(n, "basic.lookup.classref");
    if (!x.entity)
        return analysis_result::well_formed;
    entity_kind k = entity_table[x.entity].kind;
    if (k != entity_kind::class_type && k != entity_kind::enumeration &&
        k != entity_kind::typedef_name)
        /* (a class template's name, which the injected-class-name of the
           class hides in its scope) */
        return analysis_result::well_formed;
    uint32_t cv = 0;
    VTRY(r,
         named_member_type(n, t, x.entity, "basic.lookup.classref", type, cv));
    name_uses.resize(uses_before);
    found = true;
    return analysis_result::well_formed;
}

/*
 * [expr.prim.id.dtor]/1: the destructor's name, /2: for a pseudo-destructor
 * "T shall be a scalar type and the id-expression shall appear as the right
 * operand of a class member access that forms the postfix-expression of a
 * function call"; /3's examples.  [expr.ref]/3: "If the object expression is
 * of scalar type, E2 shall name the pseudo-destructor of that same type
 * (ignoring cv-qualifications)".  [basic.lookup.classref]/3: "If the
 * unqualified-id is ~type-name, the type-name is looked up in the context of
 * the entire postfix-expression.  If the type T of the object expression is
 * of a class type C, the type-name is also looked up in the scope of class
 * C.  At least one of the lookups shall find a name that refers to cv T";
 * [basic.lookup.qual]/6: "In a qualified-id of the form
 * nested-name-specifier_opt type-name :: ~ type-name the second type-name is
 * looked up in the same scope as the first."
 *
 * The call evaluates the object expression and, of a class with a
 * non-trivial destructor, calls it: [class.virtual]/16 "Explicit
 * qualification with the scope operator suppresses the virtual call
 * mechanism", so an unqualified p->~T() of a virtual destructor calls the
 * final overrider through the virtual table.  Of a scalar or of a class
 * with a trivial destructor it only evaluates the object expression
 * (the lifetime ends, [basic.life]/1); /5 of [expr.call]: the call has type
 * void.
 *
 * Not analysed: a destructor of a base class named through a derived
 * class's object (p->B::~B()), ~decltype(...), a temporary object of class
 * type.
 */
analysis_result semantic_analysis::destructor_call(SyntaxRef n,
                                                   expression_meaning &m)
{
    SyntaxElement access = n[0];
    bool arrow = access[1].is(preprocessing_op_or_punc::ARROW);
    bool qualified = access[2].is(syntax_kind::qualified_id);
    SyntaxElement name = destructor_id(access[2]);
    token_index at = n.first_token();
    VTRY(r, member_object(access, arrow, true, m));
    const struct type object = type_table[m.type.unqualified()];
    type_index object_type = m.type.unqualified();
    bool scalar = scalar_type(object_type);
    if (object.kind != type_kind::class_type && !scalar) {
        /* (void: a Microsoft extension, a pseudo-destructor on type void) */
        if ((fundamental_of(m.type) == fundamental_type::none &&
             object.kind != type_kind::pointer) ||
            fundamental_of(m.type) == fundamental_type::void_)
            return not_supported(n, "expr.ref");
        return ill_formed(n,
                          "the object expression of a class member access "
                          "shall be of class type",
                          "expr.ref");
    }
    entity_index c = 0;
    if (!scalar) {
        c = object.entity;
        if (entity_table[c].kind != entity_kind::class_type)
            /* a class whose definition was not analysed */
            return not_supported(n, "class.pre");
        require_complete(c);
        if (!entity_table[c].size)
            return ill_formed(n,
                              "the class type shall be complete unless the "
                              "class member access appears in the "
                              "definition of that class",
                              "expr.ref");
    }
    /* the type-names: found, and naming the object's type */
    bool found = false;
    type_index named = 0;
    if (qualified) {
        NestedNameSpecifier specifier = access[2][0];
        uint32_t parts = 0;
        SyntaxElement first;
        for (auto part : specifier.parts()) {
            if (!parts)
                first = part.name;
            parts++;
        }
        entity_index scope = 0;
        bool in_scope = specifier.global();
        type_index qualifier = 0;
        bool names = true;
        for (auto part : specifier.parts())
            if (!part.name.is_identifier() || !part.scoped || part.has_template)
                names = false;
        if (names) {
            /* [basic.lookup.qual]/6: the nested-name-specifier's last name
               is a type-name, a typedef-name of a scalar type included
               (which no nominated scope is), and "the second type-name is
               looked up in the same scope as the first" */
            uint32_t seen = 0;
            for (auto part : specifier.parts()) {
                seen++;
                token_index t = part.name.token();
                uint32_t id = tokens.identifier(t);
                bool last = seen == parts;
                lookup_result x;
                lookup_filter filter = lookup_filter::scopes;
                if (last)
                    filter = lookup_filter::types;
                if (in_scope)
                    x = lookup_qualified(scope, id, filter);
                else
                    x = lookup_unqualified(id, filter);
                if (x.ambiguous || x.unknown || !x.entity)
                    return not_supported(n, "basic.lookup.qual");
                if (last) {
                    uint32_t cv = 0;
                    VTRY(r,
                         named_member_type(n, t, x.entity, "basic.lookup.qual",
                                           qualifier, cv));
                    break;
                }
                entity_index next = x.entity;
                if (entity_table[next].kind == entity_kind::typedef_name) {
                    const struct type &synonym =
                        type_table[entity_table[next].type.unqualified()];
                    if (synonym.kind != type_kind::class_type)
                        return not_supported(n, "basic.lookup.qual");
                    next = synonym.entity;
                }
                if (!is_namespace(next) &&
                    entity_table[next].kind != entity_kind::class_type)
                    return not_supported(n, "basic.lookup.qual");
                scope = next;
                in_scope = true;
            }
        } else {
            nominated_scope nominated;
            VTRY(r, nested_name_specifier(specifier, nominated));
            if (!nominated.entity ||
                entity_table[nominated.entity].kind != entity_kind::class_type)
                return not_supported(n, "basic.lookup.qual");
            scope = nominated.entity;
            in_scope = true;
            qualifier = entity_table[scope].type.unqualified();
        }
        VTRY(r, destructor_type_name(n, name, in_scope, scope, named, found));
        if (!found || named != qualifier)
            return ill_formed(n,
                              "the second type-name of a destructor's "
                              "qualified name shall name the first's type",
                              "basic.lookup.qual");
        if (qualifier != object_type) {
            if (scalar)
                return ill_formed(n,
                                  "a pseudo-destructor shall name the "
                                  "object expression's type",
                                  "expr.ref");
            /* a base class's destructor */
            return not_supported(n, "expr.ref");
        }
    } else {
        bool matches = false;
        if (c) {
            VTRY(r, destructor_type_name(n, name, true, c, named, found));
            matches = found && named == object_type;
        }
        if (!matches) {
            VTRY(r, destructor_type_name(n, name, false, 0, named, found));
            matches = found && named == object_type;
        }
        if (!matches)
            return ill_formed(n,
                              "the type-name of a destructor's name shall "
                              "name the type of the object expression",
                              "basic.lookup.classref");
    }
    if (n.size() == 4)
        return ill_formed(n,
                          "to be a viable function, a candidate function "
                          "shall have enough parameters to agree in number "
                          "with the arguments in the list",
                          "over.match.viable");
    /* the call's value */
    expression_meaning result;
    result.type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    result.category = value_category::prvalue;
    result.evaluation = constness::not_constant;
    entity_index destructor = 0;
    if (c && destroys(c)) {
        destructor = entity_table[c].destructor;
        if (!destructor)
            return not_supported(n, "class.dtor");
        VTRY(r, deleted_use(at, destructor));
        if (!accessible(destructor, c))
            return ill_formed_at_token(at,
                                       "a private or protected member's name "
                                       "can be used only by members and "
                                       "friends of the class in which it is "
                                       "declared",
                                       "class.access");
    }
    if (!destructor) {
        /* a pseudo-destructor, a trivial destructor: the object expression
           is evaluated (the pointer of p->~T(): *p is no access) and
           discarded */
        if (aggregate_prvalue(m))
            return not_supported(n, "class.temporary");
        uint32_t operand = m.node;
        if (arrow)
            operand = semantics[operand].a;
        m = result;
        m.node = operand;
        converted(m, conversion_kind::to_void);
        return analysis_result::well_formed;
    }
    if (aggregate_prvalue(m))
        return not_supported(n, "class.temporary");
    if (entity_table[destructor].virtual_ && !qualified) {
        /* [class.virtual]/16: the final overrider */
        expression_meaning address;
        address.type =
            qualified_type::make(type_table.pointer_to(m.type), cv_none);
        address.category = value_category::prvalue;
        address.evaluation = constness::not_constant;
        address.node =
            expression_node(semantic_kind::address, address, at, m.node);
        expression_meaning named_function;
        named_function.type = entity_table[destructor].type;
        named_function.category = value_category::lvalue;
        named_function.evaluation = constness::constant;
        named_function.entity = destructor;
        uint32_t callee = expression_node(semantic_kind::entity, named_function,
                                          at, destructor);
        result.node = expression_node(semantic_kind::call, result, at, callee,
                                      address.node);
        semantics[result.node].c = 1;
        semantics.set_list(result.node, nullptr, 0);
        m = result;
        return analysis_result::well_formed;
    }
    result.node =
        expression_node(semantic_kind::destroy, result, at, m.node, destructor);
    m = result;
    return analysis_result::well_formed;
}

} // namespace cplusplus
