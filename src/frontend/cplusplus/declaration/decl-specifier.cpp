/*
 * decl-specifier.cpp: [dcl.spec] Specifiers, with [dcl.stc], [dcl.fct.spec],
 * [dcl.typedef], [dcl.constexpr], [dcl.constinit], [dcl.inline] (each a
 * keyword) and [dcl.type] with [dcl.type.simple], [dcl.type.elab],
 * [dcl.type.decltype], [dcl.spec.auto]: the type-specifiers.
 *
 *     decl-specifier:
 *         storage-class-specifier
 *         defining-type-specifier
 *         function-specifier
 *         friend
 *         typedef
 *         constexpr
 *         consteval
 *         constinit
 *         inline
 *
 *     decl-specifier-seq:
 *         decl-specifier attribute-specifier-seq_opt
 *         decl-specifier decl-specifier-seq
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

name_kind lookup_qualified_or_specialization(const parser &p, scope_index scope,
                                             uint32_t name, scope_index *found,
                                             bool *dependent)
{
    name_kind kind = p.names.lookup_qualified(scope, name, found, dependent);
    if (kind != name_kind::not_declared)
        return kind;
    for (size_t s = p.specialization_regions.size(); s-- > 0;) {
        const auto &r = p.specialization_regions[s];
        if (r.primary != scope)
            continue;
        kind = p.names.lookup_qualified(r.region, name, found, dependent);
        if (kind != name_kind::not_declared)
            return kind;
    }
    return name_kind::not_declared;
}

/*
 * The name at tokens[ahead ...]: ::_opt (name ::)* name, where a name is an
 * identifier or a simple-template-id (template_opt after a '::'), looked
 * up as [basic.lookup.unqual] and [basic.lookup.qual] say.  *length is how
 * many tokens it has.  A name after a class's nested-name-specifier is
 * looked up in the class when the parser knows its members
 * ([class.qual]/1); after a type that depends on a template parameter it
 * is a member of an unknown specialization ([temp.dep.type]/6); after any
 * other type it is unknown.
 *
 * A name followed by < is a template-id when [temp.names]/2 says it names a
 * template (names_template): its template-argument-list is skipped by its
 * brackets, not parsed.  [temp.names]/4: a < after a member of an unknown
 * specialization begins a template-argument-list only after the keyword
 * template.  With template_ids, a < after any other name makes the name
 * unknown (a declaration's caller cannot tell a type from what follows).
 */
/* [temp.dep.type]/1: whether a template-argument-list, the tokens from
   first to end (its '<' and '>' included), names a template parameter or
   a dependent name: the specialization is then dependent, and so are the
   members it has ([temp.res]/4: not types without typename) */
static bool arguments_dependent(parser &p, unsigned first, unsigned end)
{
    for (unsigned i = first + 1; i + 1 < end; i++) {
        if (!p.at_identifier(i) ||
            p.at(preprocessing_op_or_punc::COLON_COLON, i - 1))
            continue;
        scope_index found = 0;
        bool found_dependent = false;
        p.names.lookup_unqualified(p.tokens.identifier(p.position + i), &found,
                                   &found_dependent);
        if (found_dependent)
            return true;
    }
    return false;
}

name_class classify(parser &p, unsigned ahead, unsigned *length,
                    bool template_ids)
{
    unsigned i = ahead;
    scope_index scope = p.names.current();
    bool qualified = false;
    if (p.at(preprocessing_op_or_punc::COLON_COLON, i)) {
        scope = declared_names::global_namespace;
        qualified = true;
        i++;
    }
    /* the class the nested-name-specifier so far names, whether the parser
       knows its members (its region is scope), and whether it is a
       dependent type */
    uint32_t class_name = 0;
    bool class_known = false, dependent = false;
    for (;;) {
        bool template_keyword = false;
        if (qualified && p.at(standard_keyword::template_, i)) {
            template_keyword = true;
            i++;
        }
        if (!p.at_identifier(i)) {
            *length = i - ahead;
            if (p.at(preprocessing_op_or_punc::TILDE, i) && class_name)
                return name_class::constructor;
            if (qualified && p.at(standard_keyword::operator_, i))
                return name_class::not_a_type;
            if (qualified)
                return name_class::unknown;
            return name_class::none;
        }
        uint32_t name = p.tokens.identifier(p.position + i);
        if (class_name) {
            /* [class.qual]/2: "the name is instead considered to name the
               constructor of class C" */
            *length = i + 1 - ahead;
            if (name == class_name &&
                (p.at(preprocessing_op_or_punc::LEFT_PARENTHESIS, i + 1) ||
                 p.at_attribute_specifier(i + 1)))
                return name_class::constructor;
        }
        bool opaque = class_name && !class_known;
        scope_index found = 0;
        name_kind kind = name_kind::not_declared;
        bool found_dependent = false;
        if (!opaque) {
            /* [basic.lookup.qual]/1: before a :: only namespaces, types
               and templates count */
            bool nested = p.at(preprocessing_op_or_punc::COLON_COLON, i + 1);
            if (qualified && nested)
                kind = p.names.lookup_qualified_nested(scope, name, &found,
                                                       &found_dependent);
            else if (qualified)
                kind = lookup_qualified_or_specialization(
                    p, scope, name, &found, &found_dependent);
            else if (nested)
                kind = p.names.lookup_unqualified_nested(name, &found,
                                                         &found_dependent);
            else
                kind =
                    p.names.lookup_unqualified(name, &found, &found_dependent);
            /* [ext:gnu_library_builtins] a builtin function clang declares
               at its first use: a function's name, no type */
            if (!qualified && kind == name_kind::not_declared &&
                p.names_compiler_builtin(name))
                kind = name_kind::function;
        }
        unsigned after = i + 1;
        bool arguments = false;
        if (p.at(preprocessing_op_or_punc::LESS, after) &&
            ((opaque && template_keyword) ||
             (!opaque &&
              p.names_template(kind, qualified, template_keyword)))) {
            unsigned end = p.angle_group_end(after);
            if (!end) {
                *length = after - ahead;
                return name_class::unknown;
            }
            if (p.at(preprocessing_op_or_punc::COLON_COLON, end) &&
                arguments_dependent(p, after, end))
                dependent = true;
            after = end;
            arguments = true;
        }
        if (p.at(preprocessing_op_or_punc::COLON_COLON, after)) {
            if (opaque) {
                /* [temp.res]/4: "In a nested-name-specifier that
                   immediately contains a nested-name-specifier that depends
                   on a template parameter, the identifier or
                   simple-template-id is implicitly assumed to name a type" */
                if (!dependent) {
                    *length = after - ahead;
                    return name_class::unknown;
                }
                class_name = name;
            } else if (kind == name_kind::namespace_name && !arguments) {
                scope = found;
                class_name = 0;
            } else if (kind == name_kind::type_name ||
                       kind == name_kind::template_name) {
                class_name = name;
                class_known =
                    !found_dependent && found &&
                    (p.names.kind_of(found) == scope_kind::class_ ||
                     p.names.kind_of(found) == scope_kind::enumeration);
                if (class_known)
                    scope = found;
                dependent = dependent || found_dependent;
            } else if (kind == name_kind::not_declared && template_keyword) {
                class_name = name;
                class_known = false;
            } else {
                *length = after - ahead;
                return name_class::unknown;
            }
            qualified = true;
            i = after + 1;
            continue;
        }
        *length = after - ahead;
        if (opaque) {
            if (!dependent)
                return name_class::unknown;
            return name_class::dependent;
        }
        if (arguments) {
            switch (kind) {
            case name_kind::template_name:
                return name_class::type;
            case name_kind::concept_name:
                return name_class::concept_;
            case name_kind::not_declared:
                return name_class::unknown;
            default:
                return name_class::not_a_type;
            }
        }
        /* a variable's or an enumerator's name followed by < is no
           template-name ([temp.names]/2): (v < 2) is an expression */
        if (template_ids && p.at(preprocessing_op_or_punc::LESS, after) &&
            kind != name_kind::other)
            return name_class::unknown;
        /* nested-name-specifier template identifier with no
           template-argument-list names a template, not a type: a template
           template argument's id-expression ([temp.arg.template]/1,
           [temp.names]/5) */
        if (template_keyword)
            return name_class::not_a_type;
        switch (kind) {
        case name_kind::type_name:
        /* a class template's name alone: its injected-class-name in its
           scope ([temp.local]/1), or a placeholder for a deduced class type
           ([dcl.type.class.deduct]) */
        case name_kind::template_name:
            return name_class::type;
        case name_kind::concept_name:
            return name_class::concept_;
        case name_kind::not_declared:
            /* [temp.dep.type]/6: a member of a dependent specialization the
               parser finds nothing for (the primary template is incomplete,
               or the member is a base's) is a member of an unknown
               specialization: no type without typename ([temp.res]/4) */
            if (class_name && dependent)
                return name_class::dependent;
            return name_class::unknown;
        default:
            return name_class::not_a_type;
        }
    }
}

/* the decl-specifiers that are no type-specifiers: storage-class-specifier
   ([dcl.stc]), function-specifier ([dcl.fct.spec]: virtual,
   explicit-specifier) and the keywords of [dcl.spec]/1 (friend among them) */
bool is_non_type_decl_specifier(standard_keyword k)
{
    switch (k) {
    case standard_keyword::static_:
    case standard_keyword::thread_local_:
    case standard_keyword::extern_:
    case standard_keyword::mutable_:
    case standard_keyword::typedef_:
    case standard_keyword::constexpr_:
    case standard_keyword::consteval_:
    case standard_keyword::constinit_:
    case standard_keyword::inline_:
    case standard_keyword::friend_:
    case standard_keyword::virtual_:
    case standard_keyword::explicit_:
        return true;
    default:
        return false;
    }
}

/*
 * [dcl.type]/2: "As a general rule, at most one defining-type-specifier is
 * allowed in the complete decl-specifier-seq of a declaration or in a
 * defining-type-specifier-seq, and at most one type-specifier is allowed in
 * a type-specifier-seq. The only exceptions to this rule are the
 * following:
 *   - const can be combined with any type specifier except itself.
 *   - volatile can be combined with any type specifier except itself.
 *   - signed or unsigned can be combined with char, long, short, or int.
 *   - short or long can be combined with int.
 *   - long can be combined with double.
 *   - long can be combined with long."
 */
struct type_specifier_count {
    unsigned const_ = 0, volatile_ = 0, sign = 0, short_ = 0, long_ = 0;
    /* char, int, double, and every other defining-type-specifier */
    unsigned char_ = 0, int_ = 0, double_ = 0, other = 0;

    /* [ext:gnu_int128]: __int128, with signed or unsigned only */
    unsigned int128 = 0;
    /* [ext:gnu_float128]: __float128, alone */
    unsigned float128 = 0;

    /* adds the specifier: false if the sequence now breaks the rule */
    bool add(standard_keyword k, extension_keyword ext)
    {
        /* [ext:gnu_complex]: a modifier, which analysis checks */
        if (ext == extension_keyword::complex)
            return true;
        if (ext == extension_keyword::float128) {
            float128++;
            return float128 == 1 && !char_ && !int_ && !double_ && !other &&
                   !short_ && !long_ && !sign && !int128;
        }
        if (ext == extension_keyword::int128) {
            int128++;
            return int128 == 1 && !char_ && !int_ && !double_ && !other &&
                   !short_ && !long_ && !float128;
        }
        switch (k) {
        case standard_keyword::const_:
            return ++const_ == 1;
        case standard_keyword::volatile_:
            return ++volatile_ == 1;
        case standard_keyword::signed_:
        case standard_keyword::unsigned_:
            sign++;
            break;
        case standard_keyword::short_:
            short_++;
            break;
        case standard_keyword::long_:
            long_++;
            break;
        case standard_keyword::char_:
            char_++;
            break;
        case standard_keyword::int_:
            int_++;
            break;
        case standard_keyword::double_:
            double_++;
            break;
        default:
            other++;
            break;
        }
        unsigned base = char_ + int_ + double_ + other + int128 + float128;
        if (base > 1 || sign > 1 || short_ > 1 || long_ > 2 ||
            (short_ && long_))
            return false;
        if (sign && (double_ || other || float128))
            return false;
        if ((short_ || long_) && (char_ || other || int128 || float128))
            return false;
        if (short_ && double_)
            return false;
        if (double_ && long_ > 1)
            return false;
        return true;
    }
};

} // namespace

VPRIV bool is_simple_type_keyword(standard_keyword k)
{
    switch (k) {
    case standard_keyword::char_:
    case standard_keyword::char8_t_:
    case standard_keyword::char16_t_:
    case standard_keyword::char32_t_:
    case standard_keyword::wchar_t_:
    case standard_keyword::bool_:
    case standard_keyword::short_:
    case standard_keyword::int_:
    case standard_keyword::long_:
    case standard_keyword::signed_:
    case standard_keyword::unsigned_:
    case standard_keyword::float_:
    case standard_keyword::double_:
    case standard_keyword::void_:
        return true;
    default:
        return false;
    }
}

name_class parser::classify_name(unsigned ahead, unsigned *length,
                                 bool template_ids)
{
    return classify(*this, ahead, length, template_ids);
}

bool parser::type_name_follows(unsigned ahead)
{
    unsigned length = 0;
    return classify_name(ahead, &length) == name_class::type;
}

/* whether a decl-specifier-seq starts at the current token (for
   [dcl.ambig.res]: whether a parameter-declaration-clause could start) */
bool parser::at_decl_specifier(bool unknown_names)
{
    standard_keyword k = keyword_of(peek());
    if (is_simple_type_keyword(k) || is_non_type_decl_specifier(k))
        return true;
    switch (k) {
    case standard_keyword::const_:
    case standard_keyword::volatile_:
    case standard_keyword::auto_:
    case standard_keyword::decltype_:
    case standard_keyword::class_:
    case standard_keyword::struct_:
    case standard_keyword::union_:
    case standard_keyword::enum_:
    case standard_keyword::typename_:
        return true;
    case standard_keyword::extension:
        /* a keyword that is a literal (__null) is an operand, and so is
           one that only an expression begins (__builtin_offsetof) */
        return peek().literal == literal_kind::none &&
               !at_expression_extension_keyword();
    default:
        break;
    }
    unsigned length = 0;
    name_class c = classify_name(0, &length);
    /* a type-constraint before auto: a placeholder-type-specifier */
    if (c == name_class::concept_)
        return at(standard_keyword::auto_, length) ||
               at(standard_keyword::decltype_, length);
    return c == name_class::type || (unknown_names && c == name_class::unknown);
}

/*
 * decl-specifier-seq, or with type_specifiers_only a
 * defining-type-specifier-seq ([dcl.type]/1):
 *
 *     defining-type-specifier-seq:
 *       defining-type-specifier attribute-specifier-seq_opt
 *       defining-type-specifier defining-type-specifier-seq
 *
 *     defining-type-specifier:
 *         type-specifier
 *         class-specifier
 *         enum-specifier
 *
 *     type-specifier:
 *       simple-type-specifier
 *       elaborated-type-specifier
 *       typename-specifier
 *       cv-qualifier
 *
 * [dcl.spec]/3: "If a type-name is encountered while parsing a
 * decl-specifier-seq, it is interpreted as part of the decl-specifier-seq
 * if and only if there is no previous defining-type-specifier other than a
 * cv-qualifier in the decl-specifier-seq."  Which combinations of
 * specifiers are allowed ([dcl.type]/2, [dcl.spec]/2) is a semantic rule,
 * not checked here.  Nothing (0, no failure) when there is no specifier.
 */
node_index parser::decl_specifier_seq(decl_specifier_facts &facts,
                                      bool type_specifiers_only)
{
    size_t mark = begin_node();
    token_index first = position;
    type_specifier_count count;
    token_index other_specifier = 0;
    bool has_other_specifier = false, has_auto = false;
    uint64_t dependent_before = names.dependent_found;
    for (;;) {
        /* [ext.gnu] GCC manual 6.39: "An attribute specifier list may
           appear immediately before a declarator ... and among the
           declaration specifiers"; not a specifier of its own */
        if (at_gnu_attribute()) {
            gnu_attribute_specifier();
            if (failed())
                return 0;
            continue;
        }
        /* [ext:gnu_restrict] a cv-qualifier ([dcl.decl]) */
        if (accept(extension_keyword::restrict) ||
            accept(extension_keyword::nullability))
            continue;
        /* [ext:clang_c11_thread_local]
         *     storage-class-specifier:
         *         _Thread_local
         * [ext:gnu_thread_local]
         *     storage-class-specifier:
         *         __thread
         */
        if (!type_specifiers_only && at(extension_keyword::c11_thread_local)) {
            other_specifier = position;
            has_other_specifier = true;
            add_token();
            continue;
        }
        standard_keyword k = keyword_of(peek());
        token_index specifier = position;
        /* before C++11 auto is a storage-class-specifier
           ([diff.cpp03.dcl.dcl]/1), so a type-name may follow it */
        bool storage_class_auto =
            k == standard_keyword::auto_ &&
            !tokens.files.behavior.has(language_feature::auto_type_specifier);
        bool type_specifier =
            !is_non_type_decl_specifier(k) && !storage_class_auto;
        if (storage_class_auto) {
            /* where only type-specifiers can be, auto is C++11's
               placeholder: [ext.clang] clang's extension of C++03, P9 */
            if (type_specifiers_only || facts.type_specifier_seq)
                return beyond_this_step();
            add_token();
        } else if (is_non_type_decl_specifier(k)) {
            if (type_specifiers_only)
                break;
            if (k == standard_keyword::typedef_) {
                facts.typedef_ = true;
                facts.typedef_at = position;
            } else {
                other_specifier = position;
                has_other_specifier = true;
                if (k == standard_keyword::friend_) {
                    facts.friend_ = true;
                    facts.friend_at = position;
                }
                if (k == standard_keyword::static_)
                    facts.static_ = true;
            }
            /* [dcl.typedef]/1: "The typedef specifier shall not be
               combined in a decl-specifier-seq with any other kind of
               specifier except a defining-type-specifier" */
            if (facts.typedef_ && has_other_specifier) {
                token_index at = position;
                if (k != standard_keyword::typedef_)
                    at = other_specifier;
                return violates("typedef combined only with "
                                "defining-type-specifiers",
                                "dcl.typedef", at);
            }
            if (k == standard_keyword::explicit_ &&
                at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1)) {
                explicit_specifier();
                if (failed())
                    return 0;
            } else {
                add_token();
            }
        } else if (k == standard_keyword::const_ ||
                   k == standard_keyword::volatile_) {
            add_token();
        } else if (is_simple_type_keyword(k)) {
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::auto_) {
            /* placeholder-type-specifier: type-constraint_opt auto; the
               type-constraint is P7's */
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::decltype_) {
            /* decltype-specifier :: is a nested-name-specifier
               ([expr.prim.id.qual]): of a declarator's ptr-operator, or of
               a type-name that is a class's member (P5) */
            if (at(preprocessing_op_or_punc::COLON_COLON, group_end(1))) {
                if (facts.has_defining_type_specifier)
                    break;
                /* decltype-specifier :: type-name: no declarator-id is
                   qualified by a decltype-specifier ([dcl.meaning.general]
                   /1), so the name is a type's */
                simple_type_specifier_with_name();
            } else {
                decltype_specifier();
            }
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::enum_) {
            enum_specifier_or_elaborated(facts);
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::class_ ||
                   k == standard_keyword::struct_ ||
                   k == standard_keyword::union_) {
            elaborated_or_class_specifier(facts);
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (k == standard_keyword::typename_ &&
                   !at(preprocessing_op_or_punc::ELLIPSIS, 1)) {
            if (facts.has_defining_type_specifier)
                break;
            typename_specifier();
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::type_transformation)) {
            /* [ext.clang] a simple-type-specifier */
            if (facts.has_defining_type_specifier)
                break;
            clang_type_transformation();
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::atomic) &&
                   !at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1)) {
            /* [ext:clang_atomic] C's atomic type qualifier, not a
               defining-type-specifier */
            add_token();
            facts.count++;
            continue;
        } else if (at(extension_keyword::atomic) ||
                   at(extension_keyword::bit_int)) {
            /* [ext:clang_atomic], [ext:clang_bit_int] a
               simple-type-specifier */
            if (facts.has_defining_type_specifier)
                break;
            if (at(extension_keyword::atomic))
                clang_atomic_type_specifier();
            else
                clang_bit_int_type_specifier();
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::typeof_)) {
            /* [ext:gnu_typeof] a simple-type-specifier */
            if (facts.has_defining_type_specifier)
                break;
            gnu_typeof_specifier();
            if (failed())
                return 0;
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::int128)) {
            /* [ext:gnu_int128] a simple-type-specifier */
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::float128) ||
                   at(extension_keyword::float16) ||
                   at(extension_keyword::bfloat16)) {
            /* [ext:gnu_float128], [ext:clang_float16] a simple-type-specifier */
            add_token();
            facts.has_defining_type_specifier = true;
        } else if (at(extension_keyword::complex)) {
            /* [ext:gnu_complex] a modifier of float and double, as signed
               is of int; what it combines with is analysis's */
            add_token();
        } else if (k == standard_keyword::extension &&
                   peek().literal == literal_kind::none &&
                   !at_expression_extension_keyword()) {
            /* an extension's keyword (P9); one that is a literal, __null,
               is an operand, no decl-specifier */
            return beyond_this_step();
        } else if (at_identifier() ||
                   at(preprocessing_op_or_punc::COLON_COLON)) {
            if (facts.has_defining_type_specifier)
                break; /* [dcl.spec]/3 */
            /* [class.ctor]/1: "In a constructor declaration, each
               decl-specifier in the optional decl-specifier-seq shall be
               friend, inline, constexpr, or an explicit-specifier", and
               its declarator-id is the injected-class-name */
            if (facts.constructor_name && at_identifier() &&
                tokens.identifier(position) == facts.constructor_name &&
                constructor_parameters_follow(1))
                break;
            unsigned length = 0;
            name_class c = classify_name(0, &length);
            if (c == name_class::concept_ &&
                (at(standard_keyword::auto_, length) ||
                 at(standard_keyword::decltype_, length))) {
                /* placeholder-type-specifier: type-constraint auto,
                   type-constraint decltype(auto) ([dcl.spec.auto]) */
                size_t placeholder_mark = begin_node();
                token_index placeholder_first = position;
                type_constraint();
                if (failed())
                    return 0;
                if (at(standard_keyword::decltype_)) {
                    decltype_specifier();
                    if (failed())
                        return 0;
                } else {
                    add_token();
                }
                finish_node(syntax_kind::placeholder_type_specifier,
                            placeholder_mark, placeholder_first);
                facts.has_defining_type_specifier = true;
                k = standard_keyword::auto_;
            } else if (c == name_class::type || (c == name_class::dependent &&
                                                 facts.implicit_typename)) {
                last_type_region = 0;
                simple_type_specifier_with_name();
                if (failed())
                    return 0;
                facts.type_region = last_type_region;
                facts.has_defining_type_specifier = true;
            } else if (c == name_class::unknown && unknown_names_are_types &&
                       !at(preprocessing_op_or_punc::LESS, length) &&
                       !at(preprocessing_op_or_punc::COLON_COLON, length)) {
                /* the reading in which the name is a type-name */
                for (unsigned i = 0; i < length; i++)
                    add_token();
                facts.has_defining_type_specifier = true;
            } else if (c == name_class::unknown) {
                /* a name the parser cannot classify where a type could be:
                   T x; with T undeclared is ill-formed, but the parser
                   cannot tell it from a type a later step declares */
                if (!facts.has_defining_type_specifier &&
                    (at_identifier(length) ||
                     at(preprocessing_op_or_punc::STAR, length) ||
                     at(preprocessing_op_or_punc::AMPERSAND, length) ||
                     at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND,
                        length) ||
                     at(preprocessing_op_or_punc::LEFT_PARENTHESIS, length) ||
                     at(preprocessing_op_or_punc::LESS, length) ||
                     at(preprocessing_op_or_punc::COLON_COLON, length) ||
                     at(standard_keyword::const_, length) ||
                     at(standard_keyword::auto_, length) ||
                     at(standard_keyword::decltype_, length) ||
                     at_attribute_specifier(length) ||
                     at(preprocessing_op_or_punc::COMMA, length) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, length) ||
                     at(preprocessing_op_or_punc::SEMICOLON, length)))
                    return beyond_this_step();
                /* in an expression's type-id (a cast, new T[n]), what
                   follows is read by what the name is */
                if (facts.type_specifier_seq)
                    unknown_name = true;
                break;
            } else {
                break; /* a declarator-id, or a constructor's */
            }
        } else {
            break;
        }
        facts.count++;
        if (k == standard_keyword::auto_ && !storage_class_auto)
            has_auto = true;
        if (type_specifier &&
            !count.add(k, extension_keyword_of(tokens[specifier]))) {
            /* auto int x: C++03's storage-class-specifier auto
               ([diff.cpp03.dcl.dcl]/1), in a later edition [ext.clang]
               clang's extension: P9 */
            if (has_auto)
                return beyond_this_step();
            return violates("at most one defining-type-specifier, except "
                            "the combinations [dcl.type]/2 lists",
                            "dcl.type", specifier);
        }
        /* decl-specifier attribute-specifier-seq_opt: the
           attribute-specifier-seq ends the sequence ([dcl.spec]/1: it
           "appertains to the type determined by the preceding
           decl-specifiers") */
        if (at_attribute_specifier() && !at_gnu_attribute()) {
            attribute_specifier_seq(true);
            if (failed())
                return 0;
            facts.trailing_attributes = true;
            break;
        }
    }
    if (position == first)
        return 0;
    facts.dependent = names.dependent_found != dependent_before;
    /* [dcl.type.elab]/1: "An attribute-specifier-seq shall not appear in
       an elaborated-type-specifier unless the latter is the sole
       constituent of a declaration." */
    if (facts.elaborated_attributes &&
        (explicit_instantiation_ ||
         !(facts.count == 1 && !facts.trailing_attributes &&
           !type_specifiers_only && at(preprocessing_op_or_punc::SEMICOLON))))
        return violates("an attribute-specifier-seq in an "
                        "elaborated-type-specifier only when it is the sole "
                        "constituent of a declaration",
                        "dcl.type.elab", facts.elaborated_attributes_at);
    syntax_kind kind = syntax_kind::decl_specifier_seq;
    if (type_specifiers_only)
        kind = syntax_kind::defining_type_specifier_seq;
    if (facts.type_specifier_seq)
        kind = syntax_kind::type_specifier_seq;
    facts.node = finish_node(kind, mark, first);
    return facts.node;
}

/*
 * [dcl.type.simple]:
 *
 *     simple-type-specifier:
 *         nested-name-specifier_opt type-name
 *         nested-name-specifier template simple-template-id
 *         decltype-specifier
 *         placeholder-type-specifier
 *         nested-name-specifier_opt template-name
 *         ...
 *
 *     type-name:
 *         class-name
 *         enum-name
 *         typedef-name
 *
 * The first alternative: a type-name alone passes through as its token.
 */
node_index parser::simple_type_specifier_with_name()
{
    size_t mark = begin_node();
    token_index first = position;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    bool template_keyword = qualifier && accept(standard_keyword::template_);
    if (!at_identifier())
        return expected("a type-name", "dcl.type.simple");
    uint32_t name = tokens.identifier(position);
    size_t name_mark = begin_node();
    token_index name_first = position;
    add_token();
    name_kind kind = name_kind::not_declared;
    bool dependent = false;
    scope_index region = 0;
    if (!qualifier)
        kind = names.lookup_unqualified(name, &region, &dependent);
    else if (known)
        kind = lookup_qualified_or_specialization(*this, scope, name, &region,
                                                  &dependent);
    if (kind == name_kind::template_name && !at(preprocessing_op_or_punc::LESS))
        tree.template_names_as_types.append(name_first);
    if (at(preprocessing_op_or_punc::LESS) && qualifier &&
        qualifier_dependent && !template_keyword)
        return violates("template before a dependent template-name",
                        "temp.names", name_first);
    if (at(preprocessing_op_or_punc::LESS)) {
        bool is_template_id;
        if (qualifier && !known)
            is_template_id = template_keyword;
        else
            is_template_id =
                names_template(kind, qualifier != 0, template_keyword);
        if (is_template_id) {
            template_id_rest(name_mark, name_first,
                             syntax_kind::simple_template_id);
            if (failed())
                return 0;
        }
    } else if (template_keyword) {
        return expected("a simple-template-id after template",
                        "dcl.type.simple");
    }
    /* the class the type-name names (a class template's, for one of its
       specializations): a typedef-name of it names its members too.  Set
       after the template-argument-list, whose own type-names set it too
       ([dcl.typedef]/2: typedef duration<long, ratio<3>> hours;
       hours::rep). */
    last_type_region = region;
    if (!qualifier)
        return 0;
    return finish_node(syntax_kind::simple_type_specifier, mark, first);
}

/*
 * [dcl.type.elab]:
 *
 *     elaborated-type-specifier:
 *         class-key attribute-specifier-seq_opt nested-name-specifier_opt identifier
 *         class-key simple-template-id
 *         class-key nested-name-specifier template_opt simple-template-id
 *         elaborated-enum-specifier
 *
 * and, when a '{' or a base-clause follows the head, [class.pre]'s
 *
 *     class-specifier:
 *         class-head { member-specification_opt }
 *
 *     class-head:
 *         class-key attribute-specifier-seq_opt class-head-name class-virt-specifier_opt base-clause_opt
 *         class-key attribute-specifier-seq_opt base-clause_opt
 *
 *     class-head-name:
 *         nested-name-specifier_opt class-name
 *
 *     class-virt-specifier:
 *         final
 *
 *     class-name:
 *         identifier
 *         simple-template-id
 *
 *     class-key:
 *         class
 *         struct
 *         union
 *
 * Templates are P7's; elaborated-enum-specifier is enum-specifier.cpp's.  A
 * class-specifier makes the class's region in declared_names: its members'
 * scope ([basic.scope.class]), which the class-name carries.
 * [basic.scope.pdecl]/3: "The point of declaration for a class or class
 * template first declared by a class-specifier is immediately after the
 * identifier or simple-template-id (if any) in its class-head", so before
 * its base-clause.  [basic.scope.pdecl]/7: an elaborated-type-specifier
 * that names nothing declared yet declares the class, in the current scope
 * when it is the declaration "class-key attribute-specifier-seq_opt
 * identifier ;", else in "the smallest namespace or block scope that
 * contains the declaration", and as a friend nowhere lookup finds it
 * ([basic.scope.pdecl]/13).
 */
node_index parser::elaborated_or_class_specifier(decl_specifier_facts &facts)
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* class-key */
    token_index attributes_at = position;
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = 0;
    size_t name_mark = begin_node();
    token_index name_first = position;
    if (nested_name_specifier_follows(0)) {
        qualifier = nested_name_specifier(&scope, &known);
        if (failed())
            return 0;
    }
    uint32_t name = 0;
    bool template_id = false;
    scope_index primary = 0;
    token_index template_id_first = 0, template_id_end = 0;
    if (qualifier)
        accept(standard_keyword::template_);
    if (at_identifier()) {
        name = tokens.identifier(position);
        size_t id_mark = begin_node();
        token_index id_first = position;
        add_token();
        if (at(preprocessing_op_or_punc::LESS)) {
            /* class-name: simple-template-id, a class template's partial or
               explicit specialization, or an elaborated name of one.
               [temp.names]/2: its name is a template-name, and
               [dcl.type.elab]/1 a class template's (one the parser cannot
               look up after a qualifier is taken as one) */
            if (!qualifier || known) {
                name_kind kind;
                if (qualifier)
                    kind = names.lookup_qualified(scope, name, &primary);
                else
                    kind = names.lookup_unqualified(name, &primary);
                if (kind != name_kind::template_name)
                    return violates("a class template's name before the "
                                    "template-argument-list",
                                    "dcl.type.elab", id_first);
            }
            template_id_rest(id_mark, id_first,
                             syntax_kind::simple_template_id);
            if (failed())
                return 0;
            template_id = true;
            template_id_first = id_first;
            template_id_end = position;
        }
    } else if (qualifier) {
        return expected("an identifier after the nested-name-specifier",
                        "dcl.type.elab");
    }
    /* [dcl.type]/1: a type-specifier-seq (a type-id's) has no
       class-specifier: a '{' after its elaborated-type-specifier is the
       next production's */
    bool final_ = name && at_word("final") &&
                  (at(preprocessing_op_or_punc::LEFT_BRACE, 1) ||
                   at(preprocessing_op_or_punc::COLON, 1));
    bool definition = !facts.type_specifier_seq &&
                      (final_ || at(preprocessing_op_or_punc::LEFT_BRACE) ||
                       at(preprocessing_op_or_punc::COLON));
    if (definition) {
        /* [class.friend]/2: "A class shall not be defined in a friend
           declaration." */
        if (facts.friend_)
            return violates("no class defined in a friend declaration",
                            "class.friend", facts.friend_at);
        if (qualifier)
            finish_node(syntax_kind::class_head_name, name_mark, name_first);
        if (final_) {
            /* N3272 (C++11); before it [ext.clang] clang's extension: P9 */
            if (!tokens.files.behavior.has(language_feature::override_control))
                return beyond_this_step();
            add_token();
        }
        /* the class's region: in the namespace or class a
           nested-name-specifier names ([class.mem]/2: a class nested in
           another is its member), else in the current scope */
        scope_index outer = names.current();
        if (qualifier && known)
            names.reenter(scope);
        scope_index region = names.enter(scope_kind::class_);
        names.reenter(outer);
        if (name && !qualifier && !template_id) {
            names.define_class(names.declaring_scope(), name, region);
            names.declare(name, declared_kind(name_kind::type_name), region);
        } else if (name && known && !template_id) {
            names.define_class(scope, name, region);
            names.declare_in(scope, name, declared_kind(name_kind::type_name),
                             region);
        }
        if (at(preprocessing_op_or_punc::COLON)) {
            base_clause(region);
            if (failed())
                return 0;
        }
        finish_node(syntax_kind::class_head, mark, first);
        if (!at(preprocessing_op_or_punc::LEFT_BRACE))
            return expected("'{' to begin the member-specification",
                            "class.pre");
        /* [temp.expl.spec]/7, [temp.class.spec.mfunc]/1: an explicit or a
           partial specialization has its own members, which a member
           defined outside it (a mem-initializer's names) finds */
        if (template_id && primary)
            specialization_regions.append(
                {primary, region, template_id_first, template_id_end});
        class_body(name, region, template_id);
        if (failed())
            return 0;
        /* [class.union.anon]/1: "The names of the members of an anonymous
           union are bound in the scope inherited from the scope of the
           anonymous union declaration"; an unnamed struct so is clang's
           extension: a name in the enclosing class finds them */
        if (!name && !qualifier && at(preprocessing_op_or_punc::SEMICOLON) &&
            names.kind_of(names.declaring_scope()) == scope_kind::class_)
            names.add_base(names.declaring_scope(), region);
        facts.declares_type = true;
        return finish_node(syntax_kind::class_specifier, mark, first);
    }
    if (!name)
        return expected("an identifier or '{'", "dcl.type.elab");
    facts.declares_type = true;
    facts.elaborated = true;
    facts.elaborated_at = first;
    facts.elaborated_declaring_form = !qualifier;
    /* [ext.gnu] GCC's attributes are not the standard's
       attribute-specifier-seq this rule is about */
    if (attributes && !only_gnu_attributes(tree, attributes)) {
        facts.elaborated_attributes = true;
        facts.elaborated_attributes_at = attributes_at;
    }
    name_kind found = name_kind::not_declared;
    if (!qualifier && !template_id)
        found = names.lookup_elaborated(name);
    if (!qualifier && !template_id && !facts.friend_ &&
        found != name_kind::type_name && found != name_kind::template_name) {
        /* the class's region, empty until a class-specifier defines it
           (names.define_class): a typedef-name of the class made before
           the definition finds its members */
        scope_index outer = names.current();
        scope_index region = names.enter(scope_kind::class_);
        names.reenter(outer);
        if (facts.count == 0 && at(preprocessing_op_or_punc::SEMICOLON))
            names.declare(name, declared_kind(name_kind::type_name), region);
        else
            names.declare_in(names.nearest_namespace_or_block(), name,
                             name_kind::type_name, region);
    }
    return finish_node(syntax_kind::elaborated_type_specifier, mark, first);
}

/*
 * [dcl.type.decltype]:
 *
 *     decltype-specifier:
 *       decltype ( expression )
 *
 * [dcl.spec.auto]:
 *
 *     placeholder-type-specifier:
 *       type-constraint_opt auto
 *       type-constraint_opt decltype ( auto )
 *
 * [dcl.type.decltype]/1: "For an expression E, the type denoted by
 * decltype(E) is defined as follows": whether E is an unparenthesized
 * id-expression or class member access is what the tree says (a
 * parenthesized one is a primary-expression node).
 */
node_index parser::decltype_specifier()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after decltype", "dcl.type.decltype");
    syntax_kind kind = syntax_kind::decltype_specifier;
    if (at(standard_keyword::auto_, 1) &&
        at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 2)) {
        kind = syntax_kind::placeholder_type_specifier;
        add_token();
        add_token();
        add_token();
    } else {
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the operand of decltype",
                            "dcl.type.decltype");
    }
    return finish_node(kind, mark, first);
}

namespace
{

/* the subclause whose rules a decl-specifier keyword needs, for the ones
   analysis has none for yet */
const char *specifier_subclause(standard_keyword k)
{
    switch (k) {
    case standard_keyword::auto_:
        return "dcl.spec.auto";
    case standard_keyword::constexpr_:
    case standard_keyword::consteval_:
        return "dcl.constexpr";
    case standard_keyword::constinit_:
        return "dcl.constinit";
    case standard_keyword::thread_local_:
    case standard_keyword::register_:
    case standard_keyword::mutable_:
        return "dcl.stc";
    case standard_keyword::virtual_:
    case standard_keyword::explicit_:
        return "dcl.fct.spec";
    case standard_keyword::friend_:
        return "class.friend";
    case standard_keyword::extension:
        return "ext";
    default:
        return "dcl.spec";
    }
}

} // namespace

/* a class-specifier among the decl-specifiers of seq before its i-th */
bool semantic_analysis::class_specifier_before(SyntaxRef seq, uint32_t i) const
{
    for (uint32_t k = 0; k < i; k++)
        if (seq[k].is(syntax_kind::class_specifier))
            return true;
    return false;
}

/*
 * [dcl.spec]/1: the decl-specifiers of a declaration (or of a
 * parameter-declaration, when parameter): its type ([dcl.meaning]/4:
 * "First, the decl-specifier-seq determines a type") and the specifiers
 * that apply to the names it declares.
 */
analysis_result semantic_analysis::decl_specifier_seq(SyntaxRef seq,
                                                      specified &s,
                                                      bool parameter)
{
    /* [dcl.spec.auto]/6: "A program that uses a placeholder type in a
       context not explicitly allowed in this subclause is ill-formed": the
       contexts analysed allow it (placeholder_allowed) */
    bool allow_placeholder = placeholder_allowed && !parameter;
    placeholder_allowed = false;
    simple_type_keywords k;
    uint32_t cv = cv_none;
    unsigned const_ = 0, volatile_ = 0, typedef_ = 0, static_ = 0, extern_ = 0,
             mutable_ = 0, inline_ = 0, constexpr_ = 0, virtual_ = 0,
             friend_ = 0, explicit_ = 0, consteval_ = 0, c11_thread_local = 0,
             atomic_qualifier = 0, constinit_ = 0, thread_local_ = 0;
    type_index named = 0;
    bool declares_class = false, declares_enumeration = false;
    token_index named_at = 0, duplicate = 0;
    const char *duplicate_spelling = nullptr;
    auto count = [&](unsigned &n, token_index t, const char *spelling) {
        if (n++ && !duplicate) {
            duplicate = t;
            duplicate_spelling = spelling;
        }
    };
    /* a specifier with no rules yet (an attribute after a class-specifier
       may change the class's layout) before any class is defined */
    trailing_class_alignment = 0;
    trailing_class_flags = 0;
    for (uint32_t i = 0; i < seq.size(); i++) {
        SyntaxElement c = seq[i];
        if (c.is_token() || c.is(syntax_kind::class_specifier,
                                 syntax_kind::elaborated_type_specifier,
                                 syntax_kind::enum_specifier,
                                 syntax_kind::elaborated_enum_specifier,
                                 syntax_kind::simple_type_specifier,
                                 syntax_kind::typename_specifier,
                                 syntax_kind::simple_template_id,
                                 syntax_kind::decltype_specifier,
                                 syntax_kind::gnu_typeof_specifier,
                                 syntax_kind::clang_type_transformation,
                                 syntax_kind::clang_atomic_type_specifier,
                                 syntax_kind::clang_bit_int_type_specifier,
                                 syntax_kind::placeholder_type_specifier))
            continue;
        /* an attribute specifier has rules unless a class-specifier is
           among the specifiers */
        bool defines_class = false;
        for (auto child : seq.elements())
            if (child.is(syntax_kind::class_specifier))
                defines_class = true;
        if (!defines_class && c.is(syntax_kind::gnu_attribute_specifier))
            continue;
        /* [ext:gnu_attributes] aligned after the class-specifier's closing
           brace: GCC raises the class's alignment (clang's layout too) */
        if (c.is(syntax_kind::gnu_attribute_specifier) &&
            class_specifier_before(seq, i)) {
            gnu_attributes g;
            VTRY(r, gnu_attribute_specifier(c, g));
            if (!(g.flags & ~gnu_packed) && !g.section && !g.abi_tag &&
                !g.mode && !g.vector_bytes) {
                if (g.aligned > trailing_class_alignment)
                    trailing_class_alignment = static_cast<uint32_t>(g.aligned);
                trailing_class_flags |= g.flags;
                continue;
            }
        }
        /* the class names the declaration would define are held, as
           hold_names holds its declarators' */
        for (auto spec : seq.elements()) {
            if (!spec.is(syntax_kind::class_specifier) || !spec.size() ||
                !spec[0].is(syntax_kind::class_head))
                continue;
            auto head = spec[0];
            for (SyntaxElement t : head.elements()) {
                if (!t.is_token() ||
                    tokens[t.token()].kind != token_kind::identifier)
                    continue;
                uint32_t name = tokens.identifier(t.token());
                entity_index found = entity_table.find(current_scope, name);
                if (found) {
                    entity_table[found].kind = entity_kind::none;
                } else {
                    found = entity_table.make(entity_kind::none, name,
                                              current_scope);
                    entity_table.bind(current_scope, name, found);
                }
                note_held(found);
                break;
            }
        }
        /* [dcl.align]/2: "the constant-expression shall be an integral
           constant expression" */
        bool alignment = c.is(syntax_kind::alignment_specifier);
        if (c.is(syntax_kind::attribute_specifier_seq))
            for (auto child : c.elements())
                if (child.is(syntax_kind::alignment_specifier))
                    alignment = true;
        if (alignment)
            return not_supported(c.index(), "dcl.align");
        return not_supported(c.index());
    }
    for (uint32_t i = 0; i < seq.size(); i++) {
        SyntaxElement c = seq[i];
        if (!c.is_token()) {
            /* [dcl.type.class]... a class-specifier or an
               elaborated-type-specifier: the class is the type */
            type_index class_type = 0;
            analysis_result r;
            if (c.is(syntax_kind::class_specifier))
                r = class_specifier(c, class_type);
            else if (c.is(syntax_kind::elaborated_type_specifier))
                r = elaborated_type_specifier(c, class_type);
            else if (c.is(syntax_kind::enum_specifier)) {
                r = enum_specifier(c, class_type);
                declares_enumeration = true;
            } else if (c.is(syntax_kind::elaborated_enum_specifier))
                r = elaborated_enum_specifier(c, class_type);
            else if (c.is(syntax_kind::simple_template_id)) {
                uint32_t named_cv = 0;
                r = template_type(c, 0, false, class_type, &named_cv);
                cv |= named_cv;
            } else if (c.is(syntax_kind::simple_type_specifier)) {
                uint32_t named_cv = 0;
                r = qualified_type_name(c, class_type, named_cv);
                cv |= named_cv;
            } else if (c.is(syntax_kind::typename_specifier)) {
                uint32_t named_cv = 0;
                r = typename_type(c, class_type, named_cv);
                cv |= named_cv;
            } else if (c.is(syntax_kind::decltype_specifier)) {
                /* [dcl.type.decltype] */
                qualified_type denoted;
                r = decltype_type(c, denoted);
                class_type = denoted.unqualified();
                cv |= denoted.cv();
            } else if (c.is(syntax_kind::gnu_typeof_specifier)) {
                /* [ext:gnu_typeof] */
                qualified_type denoted;
                r = gnu_typeof_type(c, denoted);
                class_type = denoted.unqualified();
                cv |= denoted.cv();
            } else if (c.is(syntax_kind::clang_type_transformation)) {
                /* [ext:clang_type_traits] */
                qualified_type denoted;
                r = clang_type_transformation(c, denoted);
                class_type = denoted.unqualified();
                cv |= denoted.cv();
            } else if (c.is(syntax_kind::clang_atomic_type_specifier)) {
                /* [ext:clang_atomic] */
                qualified_type denoted;
                r = clang_atomic_type(c, denoted);
                class_type = denoted.unqualified();
            } else if (c.is(syntax_kind::clang_bit_int_type_specifier)) {
                /* [ext:clang_bit_int] */
                qualified_type denoted;
                r = clang_bit_int_type(c, denoted);
                class_type = denoted.unqualified();
            } else if (c.is(syntax_kind::placeholder_type_specifier)) {
                /* [dcl.spec.auto]: decltype(auto); a type-constraint is
                   [temp.param]'s */
                qualified_type invented;
                if (parameter && invented_parameter_type(seq, invented)) {
                    /* [dcl.fct]/22: C auto's invented template-parameter */
                    class_type = invented.unqualified();
                    cv |= invented.cv();
                    r = analysis_result::well_formed;
                } else {
                    /* [dcl.spec.auto]/1: type-constraint_opt auto,
                       type-constraint_opt decltype(auto); a type-constraint
                       of a concept-name alone, C<U> ([temp.param]/4) */
                    entity_index named_concept = 0;
                    uint32_t last = c.size() - 1;
                    if (c[0].is(syntax_kind::type_constraint)) {
                        auto q = c[0];
                        if (c[0].size() == 1 && q[0].is_token())
                            named_concept =
                                constraint_concept(current_scope, q[0].token());
                        if (!named_concept ||
                            function_templates[concept_of(named_concept) - 1]
                                    .parameter_count != 1)
                            return not_supported(c[0].index(), "temp.param");
                    }
                    bool decltype_auto =
                        !c[last].is_token() ||
                        c[last].keyword() != standard_keyword::auto_;
                    if (!allow_placeholder ||
                        !tokens.files.behavior.has(
                            language_feature::return_type_deduction))
                        return not_supported(seq, "dcl.spec.auto");
                    class_type = placeholder_type(decltype_auto, named_concept)
                                     .unqualified();
                    r = analysis_result::well_formed;
                }
            } else if (c.is(syntax_kind::gnu_attribute_specifier)) {
                /* (after a class-specifier: the class's alignment, taken
                   above) */
                if (class_specifier_before(seq, i))
                    continue;
                /* [ext:gnu_attributes] a decl-specifier: it applies to
                   the declaration */
                VTRY_SET(r, gnu_attribute_specifier(c, s));
                continue;
            } else
                return not_supported(c.index());
            if (r != analysis_result::well_formed)
                return r;
            if (named || !k.first)
                k.first = c.first_token();
            if (named)
                k.alone++;
            named = class_type;
            named_at = c.first_token();
            if (!c.is(syntax_kind::simple_type_specifier,
                      syntax_kind::simple_template_id,
                      syntax_kind::typename_specifier,
                      syntax_kind::decltype_specifier,
                      syntax_kind::gnu_typeof_specifier,
                      syntax_kind::clang_type_transformation,
                      syntax_kind::clang_atomic_type_specifier,
                      syntax_kind::clang_bit_int_type_specifier,
                      syntax_kind::placeholder_type_specifier))
                declares_class = true;
            continue;
        }
        token_index t = c.token();
        const token &tok = tokens[t];
        if (tok.kind == token_kind::identifier) {
            /* a type-name ([dcl.type.simple]/1), found by unqualified
               lookup ([basic.lookup.unqual]) */
            lookup_result found =
                lookup_unqualified(tokens.identifier(t), lookup_filter::any);
            if (found.ambiguous)
                return ill_formed_at_token(
                    t,
                    "a name found in two different namespaces whose "
                    "declarations do not declare the same entity is "
                    "ill-formed",
                    "namespace.udir");
            entity_index e = found.entity;
            if (e && (entity_table[e].kind == entity_kind::class_type ||
                      entity_table[e].kind == entity_kind::enumeration ||
                      entity_table[e].kind == entity_kind::typedef_name))
                used(t, e);
            if (e && (entity_table[e].kind == entity_kind::class_type ||
                      entity_table[e].kind == entity_kind::enumeration)) {
                /* [class.name]/1: a class-name; [dcl.enum]/1: an
                   enum-name */
                if (named || !k.first)
                    k.first = t;
                if (named)
                    k.alone++;
                named = entity_table[e].type.unqualified();
                named_at = t;
                continue;
            }
            if (!e || entity_table[e].kind != entity_kind::typedef_name) {
                entity_index unknown = e;
                if (!unknown) {
                    analysis_result r = not_supported(
                        seq,
                        unknown_name_subclause(found, "basic.lookup.unqual"));
                    name_lookup_detail(t, found);
                    return r;
                }
                return not_supported(seq, unknown_name_subclause(
                                              unknown, "basic.lookup.unqual"));
            }
            if (named || !k.first)
                k.first = t;
            if (named)
                k.alone++;
            named = entity_table[e].type.unqualified();
            cv |= entity_table[e].type.cv();
            s.typedef_aligned = entity_table[e].alignment;
            named_at = t;
            continue;
        }
        /* [ext:clang_nullability]: no effect on the type */
        if (p.extension_keyword_of(tok) == extension_keyword::nullability)
            continue;
        /* [ext:clang_atomic] _Atomic without (: the qualifier */
        if (p.extension_keyword_of(tok) == extension_keyword::atomic) {
            count(atomic_qualifier, t, "_Atomic");
            continue;
        }
        /* [ext:clang_c11_thread_local] */
        if (p.extension_keyword_of(tok) ==
            extension_keyword::c11_thread_local) {
            count(c11_thread_local, t, "_Thread_local");
            continue;
        }
        if (p.extension_keyword_of(tok) == extension_keyword::complex) {
            /* [ext:gnu_complex] */
            if (!k.first)
                k.first = t;
            k.complex++;
            continue;
        }
        if (p.extension_keyword_of(tok) == extension_keyword::float128) {
            /* [ext:gnu_float128] */
            if (!k.first)
                k.first = t;
            k.float128++;
            continue;
        }
        if (p.extension_keyword_of(tok) == extension_keyword::float16 ||
            p.extension_keyword_of(tok) == extension_keyword::bfloat16) {
            /* [ext:clang_float16] */
            if (!k.first)
                k.first = t;
            if (p.extension_keyword_of(tok) == extension_keyword::float16)
                k.float16++;
            else
                k.bfloat16++;
            continue;
        }
        if (p.extension_keyword_of(tok) == extension_keyword::int128) {
            /* [ext:gnu_int128] */
            if (!k.first)
                k.first = t;
            k.int128++;
            continue;
        }
        standard_keyword kw = p.keyword_of(tok);
        if (!k.first && kw != standard_keyword::const_ &&
            kw != standard_keyword::volatile_ &&
            kw != standard_keyword::typedef_ &&
            kw != standard_keyword::static_ &&
            kw != standard_keyword::extern_ &&
            kw != standard_keyword::mutable_ &&
            kw != standard_keyword::inline_ &&
            kw != standard_keyword::virtual_ &&
            kw != standard_keyword::explicit_ &&
            kw != standard_keyword::constexpr_ &&
            kw != standard_keyword::consteval_)
            k.first = t;
        switch (kw) {
        case standard_keyword::const_:
            count(const_, t, "const");
            cv |= cv_const;
            break;
        case standard_keyword::volatile_:
            count(volatile_, t, "volatile");
            cv |= cv_volatile;
            break;
        case standard_keyword::signed_:
            k.signed_++;
            break;
        case standard_keyword::unsigned_:
            k.unsigned_++;
            break;
        case standard_keyword::short_:
            k.short_++;
            break;
        case standard_keyword::long_:
            k.long_++;
            break;
        case standard_keyword::char_:
            k.char_++;
            break;
        case standard_keyword::int_:
            k.int_++;
            break;
        case standard_keyword::double_:
            k.double_++;
            break;
        case standard_keyword::bool_:
            k.alone++;
            k.alone_type = fundamental_type::bool_;
            break;
        case standard_keyword::float_:
            k.alone++;
            k.alone_type = fundamental_type::float_;
            break;
        case standard_keyword::void_:
            k.alone++;
            k.alone_type = fundamental_type::void_;
            break;
        case standard_keyword::wchar_t_:
            k.alone++;
            k.alone_type = fundamental_type::wchar_t_;
            break;
        case standard_keyword::char8_t_:
            k.alone++;
            k.alone_type = fundamental_type::char8_t_;
            break;
        case standard_keyword::char16_t_:
            k.alone++;
            k.alone_type = fundamental_type::char16_t_;
            break;
        case standard_keyword::char32_t_:
            k.alone++;
            k.alone_type = fundamental_type::char32_t_;
            break;
        case standard_keyword::typedef_:
            count(typedef_, t, "typedef");
            break;
        case standard_keyword::static_:
            count(static_, t, "static");
            break;
        case standard_keyword::extern_:
            count(extern_, t, "extern");
            break;
        case standard_keyword::mutable_:
            /* [dcl.stc]/8: "The mutable specifier shall appear only in the
               declaration of a non-static data member": analysed on a
               member-declaration's decl-specifier-seq (member_declarator) */
            if (!mutable_allowed)
                return not_supported(seq, specifier_subclause(kw));
            count(mutable_, t, "mutable");
            break;
        case standard_keyword::inline_:
            count(inline_, t, "inline");
            break;
        case standard_keyword::constexpr_:
            count(constexpr_, t, "constexpr");
            break;
        case standard_keyword::consteval_:
            /* [dcl.constexpr]/2: "The consteval specifier shall be applied
               only to the declaration of a function or function template";
               an immediate function is a constexpr function */
            count(consteval_, t, "consteval");
            break;
        case standard_keyword::constinit_:
            /* [dcl.constinit]/1: applied only to a variable with static or
               thread storage duration (simple_declaration) */
            count(constinit_, t, "constinit");
            break;
        case standard_keyword::thread_local_:
            /* [dcl.stc]/3: only on a variable, a structured binding or a
               static data member (simple_declaration, member_declarator) */
            count(thread_local_, t, "thread_local");
            break;
        case standard_keyword::virtual_:
            /* [dcl.fct.spec]/2: "The virtual specifier shall be used only
               in the initial declaration of a non-static member
               function": analysed on a member-declaration
               (member_declarator) */
            if (!virtual_allowed)
                return not_supported(seq, specifier_subclause(kw));
            count(virtual_, t, "virtual");
            break;
        case standard_keyword::auto_: {
            /* [dcl.fct]/22: a generic parameter type placeholder, the type
               of its invented template-parameter */
            qualified_type invented;
            if (parameter && invented_parameter_type(seq, invented)) {
                if (named)
                    k.alone++;
                named = invented.unqualified();
                cv |= invented.cv();
                named_at = t;
                break;
            }
            /* [dcl.spec.auto]/1: a placeholder-type-specifier; where the
               context does not take one, the detail says which */
            if (!allow_placeholder) {
                const char *context = "in a declaration that takes none";
                if (parameter)
                    context = "in a parameter-declaration";
                else if (seq.is(syntax_kind::type_specifier_seq))
                    context = "in a type-id";
                return not_supported_because(seq, specifier_subclause(kw),
                                             context);
            }
            if (!tokens.files.behavior.has(
                    language_feature::auto_type_specifier)) {
                /* [ext.clang] before C++11: auto with no other type
                   specifier is the placeholder, as [dcl.spec.auto]/1;
                   with one it is [dcl.stc]'s (C++03) storage class */
                bool other_type = false;
                for (auto child : seq.elements()) {
                    if (!child.is_token()) {
                        if (!child.is(syntax_kind::gnu_attribute_specifier,
                                      syntax_kind::attribute_specifier_seq))
                            other_type = true;
                        continue;
                    }
                    const token &o = tokens[child.token()];
                    if (o.kind == token_kind::identifier ||
                        is_simple_type_keyword(p.keyword_of(o)))
                        other_type = true;
                }
                if (other_type)
                    return not_supported(seq, specifier_subclause(kw));
            }
            if (named)
                k.alone++;
            named = placeholder_type(false).unqualified();
            named_at = t;
            break;
        }
        case standard_keyword::explicit_:
            /* [dcl.fct.spec]/3: "An explicit-specifier shall be used only
               in the declaration of a constructor or conversion function
               within its class definition" */
            if (!explicit_allowed)
                return not_supported(seq, specifier_subclause(kw));
            count(explicit_, t, "explicit");
            break;
        case standard_keyword::friend_:
            if (!friend_allowed)
                return not_supported(seq, specifier_subclause(kw));
            count(friend_, t, "friend");
            break;
        default:
            return not_supported(seq, specifier_subclause(kw));
        }
    }
    /* [dcl.spec]/2: "At most one of the constexpr, consteval, and
       constinit keywords shall appear in a decl-specifier-seq." */
    if ((constexpr_ && consteval_) ||
        (constinit_ && (constexpr_ || consteval_)))
        return ill_formed(seq,
                          "at most one of the constexpr, consteval, and "
                          "constinit keywords shall appear in a "
                          "decl-specifier-seq",
                          "dcl.spec");
    constexpr_ += consteval_;
    if (parameter && (typedef_ || static_ || extern_ || inline_ || constexpr_ ||
                      c11_thread_local || constinit_ || thread_local_))
        return not_supported(seq, "dcl.fct");
    /* [dcl.stc]/1: "At most one storage-class-specifier shall appear in a
       given decl-specifier-seq, except that thread_local may appear with
       static or extern." */
    if ((static_ && extern_) ||
        (mutable_ &&
         (static_ || extern_ || c11_thread_local || thread_local_)) ||
        (c11_thread_local && thread_local_))
        return ill_formed(seq,
                          "at most one storage-class-specifier shall appear "
                          "in a given decl-specifier-seq",
                          "dcl.stc");
    /* [dcl.stc]/3: "When thread_local is applied to a variable of block
       scope the storage-class-specifier static is implied if no other
       storage-class-specifier appears in the decl-specifier-seq." */
    if (thread_local_ && !static_ && !extern_ && at_block_scope() &&
        entity_table[current_scope].kind != entity_kind::class_type)
        static_ = 1;
    /* [dcl.stc]/1: "If a storage-class-specifier appears in a
       decl-specifier-seq, there can be no typedef specifier in the same
       decl-specifier-seq" */
    if (typedef_ &&
        (static_ || extern_ || mutable_ || c11_thread_local || thread_local_))
        return ill_formed(seq,
                          "if a storage-class-specifier appears in a "
                          "decl-specifier-seq, there can be no typedef "
                          "specifier in the same decl-specifier-seq",
                          "dcl.stc");
    /* [dcl.typedef]/1: "The typedef specifier shall not be combined in a
       decl-specifier-seq with any other kind of specifier except a
       defining-type-specifier" */
    if (typedef_ && inline_)
        return ill_formed(seq,
                          "the typedef specifier shall not be combined in a "
                          "decl-specifier-seq with any other kind of "
                          "specifier except a defining-type-specifier",
                          "dcl.typedef");
    /* [dcl.constexpr]/1: "The constexpr specifier shall be applied only to
       the definition of a variable or variable template or the declaration
       of a function or function template." */
    if (typedef_ && constexpr_)
        return ill_formed(seq,
                          "the constexpr specifier shall be applied only to "
                          "the definition of a variable or the declaration "
                          "of a function",
                          "dcl.constexpr");
    type_index type = 0;
    bool keywords = k.signed_ || k.unsigned_ || k.short_ || k.long_ ||
                    k.char_ || k.int_ || k.double_ || k.int128 || k.float128 ||
                    k.float16 || k.bfloat16 || k.complex ||
                    k.alone_type != fundamental_type::none;
    if (named && keywords)
        /* [dcl.type]/2: "As a general rule, at most one
           defining-type-specifier is allowed in the complete
           decl-specifier-seq of a declaration" */
        return ill_formed_at_token(named_at,
                                   "at most one defining-type-specifier is "
                                   "allowed in the complete decl-specifier-seq "
                                   "of a declaration",
                                   "dcl.type");
    if (named && k.alone)
        return ill_formed_at_token(named_at,
                                   "at most one defining-type-specifier is "
                                   "allowed in the complete decl-specifier-seq "
                                   "of a declaration",
                                   "dcl.type");
    if (named) {
        type = named;
    } else if (keywords) {
        VTRY(r, simple_type(seq, k, type));
    }
    /* [dcl.spec]/2: "Each decl-specifier shall appear at most once in a
       complete decl-specifier-seq, except that long may appear twice." */
    if (duplicate) {
        VTRY(r, extension_used(diagnostic_id::sema_duplicate_specifier,
                               duplicate, duplicate_spelling));
    }
    s.has_type = type != 0;
    s.declares_class = declares_class;
    s.declares_enumeration = declares_enumeration;
    s.typedef_ = typedef_;
    s.static_ = static_;
    s.extern_ = extern_;
    /* [basic.stc.thread]: thread_local's thread storage duration is
       _Thread_local's (the entity's c11_thread_local) */
    s.c11_thread_local = c11_thread_local != 0 || thread_local_ != 0;
    s.cxx_thread_local = thread_local_ != 0;
    s.constinit_ = constinit_ != 0;
    s.mutable_ = mutable_ != 0;
    s.inline_ = inline_;
    s.virtual_ = virtual_ != 0;
    s.friend_ = friend_ != 0;
    s.explicit_ = explicit_ != 0;
    s.constexpr_ = constexpr_;
    s.consteval_ = consteval_;
    /* [ext:clang_atomic] C's _Atomic qualifier: the type the specifiers
       name made atomic, as _Atomic(T) (C23 6.7.4.1/3 "The type modified by
       the _Atomic qualifier shall not be an array type or a function
       type"); other cv-qualifiers stay on the atomic type */
    if (atomic_qualifier && type) {
        type_kind k = type_table[type].kind;
        if (k == type_kind::array || k == type_kind::function ||
            k == type_kind::atomic)
            return ill_formed(seq,
                              "_Atomic cannot be applied to an array, "
                              "function or atomic type",
                              "ext.clang");
        /* clang: "'_Atomic' qualifier on reference type ... has no
           effect" */
        if (k != type_kind::lvalue_reference &&
            k != type_kind::rvalue_reference) {
            VTRY(r,
                 atomic_value_check(seq, qualified_type::make(type, cv_none)));
            type = type_table.atomic_of(qualified_type::make(type, cv_none));
        }
    } else if (atomic_qualifier) {
        return not_supported(seq, "ext.clang");
    }
    if (type)
        s.type = add_cv(qualified_type::make(type, cv_none), cv);
    return analysis_result::well_formed;
}

/*
 * [dcl.type.simple]/1:
 *
 *     simple-type-specifier:
 *         nested-name-specifier_opt type-name
 *
 * [basic.lookup.qual]/1: the type-name is looked up in the scope the
 * nested-name-specifier nominates: a member of a namespace
 * ([namespace.qual]) or of a class that is a type, a nested class or
 * enumeration or a member typedef-name ([class.nest]/1, [class.mem]/3).
 */
analysis_result semantic_analysis::qualified_type_name(SyntaxRef n,
                                                       type_index &type,
                                                       uint32_t &cv)
{
    cv = 0;
    if (n.size() != 2 || !n[0].is(syntax_kind::nested_name_specifier) ||
        !n[1].is_identifier()) {
        /* [temp.names]: nested-name-specifier template_opt
           simple-template-id */
        if (n.size() == 2 && n[0].is(syntax_kind::nested_name_specifier) &&
            n[1].is(syntax_kind::simple_template_id)) {
            nominated_scope scope;
            VTRY(r, nested_name_specifier(n[0], scope));
            /* a namespace's member template or a class's (a member
               alias template, member_template_declaration) */
            if (!is_namespace(scope.entity) &&
                entity_table[scope.entity].kind != entity_kind::class_type)
                return not_supported(n, "temp.names");
            return template_type(n[1], scope.entity, true, type, &cv);
        }
        return not_supported(n, "dcl.type.simple");
    }
    nominated_scope scope;
    VTRY(r, nested_name_specifier(n[0], scope, true));
    token_index t = n[1].token();
    if (scope.dependent) {
        /* [temp.res]/5: a qualified name in a context the parser assumes
           names a type (C++20's implicit typename), with a dependent
           nested-name-specifier: typename Q::name */
        type = dependent_member_type(scope.dependent, tokens.identifier(t));
        return analysis_result::well_formed;
    }
    lookup_result found = lookup_qualified(scope.entity, tokens.identifier(t),
                                           lookup_filter::types);
    if (found.ambiguous)
        return ill_formed_at_token(t,
                                   "the use of a name is ill-formed if it "
                                   "does not allow a unique declaration to "
                                   "be chosen from those found in the "
                                   "namespaces",
                                   "namespace.qual");
    entity_index e = found.entity;
    return named_member_type(n, t, e, "basic.lookup.qual", type, cv);
}

/*
 * The entity e a qualified type-name found (t its identifier): a class,
 * enumeration or typedef-name, accessible ([class.access]/1), as a type and
 * its cv-qualifiers; anything else is not supported yet (cited by citation).
 */
analysis_result semantic_analysis::named_member_type(SyntaxRef n, token_index t,
                                                     entity_index e,
                                                     const char *citation,
                                                     type_index &type,
                                                     uint32_t &cv)
{
    /* [temp.res]/3, [dcl.type.simple]/2: the name of a
       typename-specifier or type-name names a type; a variable, function
       or enumerator found is ill-formed (in a substitution, a deduction
       failure: [temp.deduct]/11.3) */
    if (e && (entity_table[e].kind == entity_kind::variable ||
              entity_table[e].kind == entity_kind::function ||
              entity_table[e].kind == entity_kind::enumerator))
        return ill_formed_at_token(t, "the qualified name does not name a type",
                                   citation);
    if (!e || (entity_table[e].kind != entity_kind::class_type &&
               entity_table[e].kind != entity_kind::enumeration &&
               entity_table[e].kind != entity_kind::typedef_name))
        return not_supported(n, citation);
    if (entity_table[e].enclosing &&
        entity_table[entity_table[e].enclosing].kind ==
            entity_kind::class_type &&
        !accessible(e))
        return ill_formed_at_token(t,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    /* [ext:gnu_attributes] a qualified typedef-name's aligned */
    if (entity_table[e].kind == entity_kind::typedef_name &&
        entity_table[e].alignment)
        return not_supported(n, "ext.gnu");
    used(t, e);
    type = entity_table[e].type.unqualified();
    if (entity_table[e].kind == entity_kind::typedef_name)
        cv = entity_table[e].type.cv();
    return analysis_result::well_formed;
}

/*
 * A name the grammar requires to be a class's: a type-name, or
 * nested-name-specifier type-name, found by unqualified or qualified lookup
 * that considers types only ([basic.lookup.elab]/1's and [class.derived]/2's
 * "non-type names are ignored").  [dcl.typedef]/1: a typedef-name "is thus
 * a synonym for another type": one that names a class is the class.
 */
analysis_result semantic_analysis::named_class(SyntaxElement name, SyntaxRef at,
                                               entity_index &c, token_index &id)
{
    c = 0;
    lookup_result found;
    if (name.is_token()) {
        if (tokens[name.token()].kind != token_kind::identifier)
            return not_supported(at, "basic.lookup.qual");
        id = name.token();
        found = lookup_unqualified(tokens.identifier(id), lookup_filter::types);
    } else {
        if (name.size() != 2 ||
            !name[0].is(syntax_kind::nested_name_specifier) ||
            !name[1].is_identifier())
            /* a simple-template-id, a decltype-specifier */
            return not_supported(at, "basic.lookup.qual");
        nominated_scope scope;
        VTRY(r, nested_name_specifier(name[0], scope));
        id = name[1].token();
        found = lookup_qualified(scope.entity, tokens.identifier(id),
                                 lookup_filter::types);
    }
    if (found.ambiguous)
        return ill_formed_at_token(id,
                                   "a name found in two different namespaces "
                                   "whose declarations do not declare the "
                                   "same entity is ill-formed",
                                   "namespace.udir");
    entity_index e = found.entity;
    if (!e)
        return not_supported(at, "basic.lookup.qual");
    entity_index named = e;
    if (entity_table[e].kind == entity_kind::typedef_name) {
        const struct type &t = type_table[entity_table[e].type.unqualified()];
        if (t.kind != type_kind::class_type)
            return not_supported(at, "basic.lookup.qual");
        named = t.entity;
    }
    if (entity_table[named].kind != entity_kind::class_type)
        return not_supported(at, "basic.lookup.qual");
    if (entity_table[e].enclosing &&
        entity_table[entity_table[e].enclosing].kind ==
            entity_kind::class_type &&
        !accessible(e))
        return ill_formed_at_token(id,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    used(id, e);
    c = named;
    return analysis_result::well_formed;
}

} // namespace cplusplus
