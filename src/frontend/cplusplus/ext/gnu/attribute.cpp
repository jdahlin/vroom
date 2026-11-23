/*
 * attribute.cpp: [ext:gnu_attributes] GCC's attribute syntax (GCC manual
 * 6.39, Attribute Syntax), which clang accepts as GCC does:
 *
 *     gnu-attribute-specifier:
 *         __attribute__ ( ( gnu-attribute-list ) )
 *
 *     gnu-attribute-specifier-seq:
 *         gnu-attribute-specifier-seq_opt gnu-attribute-specifier
 *
 *     gnu-attribute-list:
 *         gnu-attribute_opt
 *         gnu-attribute-list , gnu-attribute_opt
 *
 *     gnu-attribute:
 *         identifier
 *         identifier ( gnu-attribute-arguments_opt )
 *
 *     gnu-attribute-arguments:
 *         identifier
 *         identifier , expression-list
 *         expression-list
 *
 * An attribute's identifier may be a keyword, as an attribute-token's
 * ([dcl.attr.grammar]/5).  Where it goes:
 *
 *     attribute-specifier:
 *         gnu-attribute-specifier
 *
 *     decl-specifier:
 *         gnu-attribute-specifier
 *
 *     declarator:
 *         ptr-declarator gnu-attribute-specifier-seq
 *         noptr-declarator parameters-and-qualifiers trailing-return-type gnu-attribute-specifier-seq
 *
 *     named-namespace-definition:
 *         inline_opt namespace attribute-specifier-seq_opt identifier gnu-attribute-specifier-seq { namespace-body }
 *
 *     unnamed-namespace-definition:
 *         inline_opt namespace attribute-specifier-seq_opt gnu-attribute-specifier-seq { namespace-body }
 *
 * "An attribute specifier list may appear as part of a struct, union or
 * enum specifier", "immediately before a declarator", "immediately before
 * the comma, = or semicolon terminating the declaration of an identifier
 * other than a function definition", and among the declaration specifiers.
 * The places the standard has for an attribute-specifier-seq take this
 * one too (at_attribute_specifier); the others are entered from their
 * production: gnu_attributes_after_declarator, decl_specifier_seq,
 * namespace_definition.
 *
 * ("The parameters ... can be: an identifier ... ; an identifier followed
 * by a comma and a non-empty comma-separated list of expressions ... ; a
 * possibly empty comma-separated list of expressions.")  What an attribute
 * means, and which of them it takes, is not grammar.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

bool parser::at_gnu_attribute(unsigned ahead)
{
    /* [ext:microsoft_declspec] __declspec is the same keyword class: clang
       takes it where an attribute-specifier goes */
    return at(extension_keyword::attribute, ahead);
}

node_index parser::gnu_attribute_specifier()
{
    int k = peek().keyword;
    if (k == keyword_of_spelling("__declspec") ||
        k == keyword_of_spelling("_declspec"))
        return microsoft_declspec();
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __attribute__ */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'((' after __attribute__", "ext.gnu");
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    for (;;) {
        if (at_attribute_identifier()) {
            size_t attribute_mark = begin_node();
            token_index attribute_first = position;
            bool availability = at_availability();
            add_token();
            if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                add_token();
                flag_set greater(greater_is_operator, true);
                bool more = !availability &&
                            !at(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
                if (availability) {
                    clang_availability_arguments();
                    if (failed())
                        return 0;
                }
                if (more && at_attribute_identifier() &&
                    (at(preprocessing_op_or_punc::COMMA, 1) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1))) {
                    add_token();
                    more = accept(preprocessing_op_or_punc::COMMA);
                }
                if (more) {
                    expression_list();
                    if (failed())
                        return 0;
                }
                if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                    return expected("')' to end the attribute's parameters",
                                    "ext.gnu");
                finish_node(syntax_kind::gnu_attribute, attribute_mark,
                            attribute_first);
            }
        } else if (!at(preprocessing_op_or_punc::COMMA) &&
                   !at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            return expected("an attribute", "ext.gnu");
        }
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::gnu_attribute_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("'))' to end the attribute-specifier", "ext.gnu");
    return finish_node(syntax_kind::gnu_attribute_specifier, mark, first);
}

/*
 * [ext:microsoft_declspec]
 *
 *     microsoft-declspec:
 *         __declspec ( gnu-attribute-list )
 *     attribute-specifier:
 *         microsoft-declspec
 *     decl-specifier:
 *         microsoft-declspec
 *
 * clang's ParseMicrosoftDeclSpecs: the attributes are separated by white
 * space (commas are skipped); each is an identifier, with an
 * expression-list in parentheses.  The node is a gnu-attribute-specifier
 * whose first token is __declspec, so the analysis of the attributes
 * (gnu_attribute_rules) is shared.
 */
node_index parser::microsoft_declspec()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __declspec */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after __declspec", "ext.gnu");
    while (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        if (accept(preprocessing_op_or_punc::COMMA))
            continue;
        if (!at_attribute_identifier())
            return expected("an attribute", "ext.gnu");
        size_t attribute_mark = begin_node();
        token_index attribute_first = position;
        add_token();
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            add_token();
            flag_set greater(greater_is_operator, true);
            if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                expression_list();
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to end the attribute's parameters",
                                "ext.gnu");
            finish_node(syntax_kind::gnu_attribute, attribute_mark,
                        attribute_first);
        }
    }
    add_token(); /* ) */
    return finish_node(syntax_kind::gnu_attribute_specifier, mark, first);
}

/* attribute-specifier ... in the places only GCC's syntax has: 0 when
   there is none */
node_index parser::gnu_attributes_opt()
{
    node_index last = 0;
    while (at_gnu_attribute()) {
        last = gnu_attribute_specifier();
        if (failed())
            return 0;
    }
    return last;
}

VPRIV bool has_gnu_attribute(const syntax_tree &tree, node_index n)
{
    if (tree[n].kind == syntax_kind::gnu_attribute_specifier)
        return true;
    if (tree[n].kind != syntax_kind::attribute_specifier_seq)
        return false;
    for (uint32_t i = 0; i < tree[n].child_count; i++) {
        syntax_child c = tree.children(n)[i];
        if (!c.is_token() &&
            tree[c.node()].kind == syntax_kind::gnu_attribute_specifier)
            return true;
    }
    return false;
}

VPRIV bool only_gnu_attributes(const syntax_tree &tree, node_index n)
{
    if (tree[n].kind == syntax_kind::gnu_attribute_specifier)
        return true;
    if (tree[n].kind != syntax_kind::attribute_specifier_seq)
        return false;
    for (uint32_t i = 0; i < tree[n].child_count; i++) {
        syntax_child c = tree.children(n)[i];
        if (c.is_token() ||
            tree[c.node()].kind != syntax_kind::gnu_attribute_specifier)
            return false;
    }
    return true;
}

/* ---- semantic analysis ---------------------------------------------- */

/* GCC manual 6.33 Declaring Attributes of Functions, 6.34 Specifying
   Attributes of Variables: the attributes analysis has rules for, by
   spelling (each also as __name__) */
struct gnu_attribute_rule {
    const char *name;
    uint32_t flag;
};

static const gnu_attribute_rule gnu_attribute_rules[] = {
    /* "noinline: This function attribute prevents a function from being
       considered for inlining."  noipa: "Disable interprocedural
       optimizations between the function with this attribute and its
       callers, as if the body of the function is not available when
       optimizing callers and the callers are unavailable when optimizing
       the body" (no inlining, which is the one vcc does) */
    {"noinline", gnu_noinline},
    {"noipa", gnu_noinline},
    /* "always_inline: Generally, functions are not inlined unless
       optimization is specified.  For functions declared inline, this
       attribute inlines the function independent of any restrictions" */
    {"always_inline", gnu_always_inline},
    /* "gnu_inline: ... the definition is used only for inlining" (in C++
       whether or not it is declared extern; sema.h) */
    {"gnu_inline", gnu_inline_definition},
    /* "noreturn: ... cannot return" */
    {"noreturn", gnu_noreturn},
    /* "weak: The weak attribute causes a declaration of an external symbol
       to be emitted as a weak symbol rather than a global." */
    {"weak", gnu_weak},
    /* "used: This attribute, attached to a function, means that code must
       be emitted for the function even if it appears that the function is
       not referenced." */
    {"used", gnu_used},
    /* "section ("section-name"): Normally, the compiler places the code it
       generates in the text section.  Sometimes, however, you need
       additional sections" (its argument is taken below) */
    {"section", gnu_section},
    /* "packed: The packed attribute specifies that a structure member
       should have the smallest possible alignment—one bit for a bit-field
       and one byte otherwise, unless a larger value is specified with the
       aligned attribute." (ignored elsewhere, clang's warning) */
    {"packed", gnu_packed},
    /* "visibility (visibility_type)": "default" only, the visibility
       vcc's symbols have; clang's type_visibility the same for a type's
       vague linkage (libc++'s namespace std) */
    {"visibility", gnu_default_visibility},
    {"type_visibility", gnu_default_visibility},
    /* clang: "abi_tag ("tag", ...): The abi_tag attribute can be applied to
       a function, variable, class or inline namespace declaration to
       modify the mangled name of the entity" (one tag, on a function or
       variable, here) */
    {"abi_tag", gnu_abi_tag},
    /* clang: "exclude_from_explicit_instantiation: ... a member of a class
       template is not part of an explicit instantiation of that class
       template" */
    {"exclude_from_explicit_instantiation",
     gnu_exclude_from_explicit_instantiation},
    /* clang's using_if_exists: a using-declarator whose name is not
       found declares nothing */
    {"using_if_exists", gnu_using_if_exists},
    /* "constructor", "constructor (priority)", "destructor",
       "destructor (priority)": "The constructor attribute causes the
       function to be called automatically before execution enters main ().
       Similarly, the destructor attribute causes the function to be called
       automatically after main () completes or exit () is called."  "You
       may provide an optional integer priority to control the order in
       which constructor and destructor functions are run.  A constructor
       with a smaller priority number runs before a constructor with a
       larger priority number; the opposite relationship holds for
       destructors." (below) */
    {"constructor", gnu_constructor},
    {"destructor", gnu_destructor},
    /* "cleanup (cleanup_function)": "The cleanup attribute runs a function
       when the variable goes out of scope.  This attribute can only be
       applied to auto function scope variables ... The function must take
       one parameter, a pointer to a type compatible with the variable."
       (below) */
    {"cleanup", gnu_cleanup},
    /* "aligned (alignment)" (below) */
    {"aligned", gnu_aligned},
    /* "mode (mode)": the size of the type of a typedef or variable by the
       name of a machine mode (below) */
    {"mode", gnu_mode},
    /* "vector_size (bytes)": the type of a typedef or variable is a vector
       of its type's elements (gnu_vector_type) */
    {"vector_size", gnu_vector_size},
    /* the attributes that change no code vcc generates: they say what a
       function or variable does (const, pure, nothrow, leaf, malloc,
       returns_nonnull, nonnull, format, format_arg, alloc_size,
       alloc_align), ask for a diagnostic (unused, maybe_unused,
       warn_unused_result, deprecated, sentinel), or name an optimization
       vcc does not do (noclone, cold, hot, flatten, no_icf, no_reorder,
       artificial, no_instrument_function), or ask for an instrumentation vcc does not
       add (no_sanitize("cfi"): libc++'s _LIBCPP_NO_CFI) */
    /* "may_alias: Accesses through pointers to types with this attribute
       are not subject to type-based alias analysis, but are instead
       assumed to be able to alias any other type of objects" (vcc does no
       type-based alias analysis); "nonstring: ... an object or member
       declaration with type array of char, signed char, or unsigned char,
       or pointer to such a type is intended to store character arrays that
       do not necessarily contain a terminating NUL" (a diagnostic's
       hint) */
    /* clang's nodebug (no debug information for a function: vcc's -g
       tables are by line), min_vector_width(N) (the widest vector the
       function uses: an optimization hint) and GCC's target("features")
       (<immintrin.h>'s intrinsics: the features the function's
       instructions need, which -m options of the compilation give) */
    {"nodebug", 0},
    {"min_vector_width", 0},
    {"target", 0},
    {"may_alias", 0},
    {"nonstring", 0},
    {"unused", 0},
    {"maybe_unused", 0},
    {"noclone", 0},
    {"const", 0},
    {"pure", 0},
    {"nothrow", gnu_nothrow},
    {"leaf", 0},
    {"cold", 0},
    {"hot", 0},
    {"malloc", 0},
    {"returns_nonnull", 0},
    {"nonnull", gnu_nonnull},
    {"format", 0},
    {"format_arg", 0},
    {"alloc_size", 0},
    {"alloc_align", 0},
    {"warn_unused_result", 0},
    {"deprecated", 0},
    {"sentinel", 0},
    {"flatten", 0},
    {"no_icf", 0},
    {"no_reorder", 0},
    {"artificial", 0},
    {"no_instrument_function", 0},
    {"no_sanitize", 0},
    /* clang's noescape: a pointer parameter does not escape the call
       (libc++'s _LIBCPP_NOESCAPE), a promise vcc does not use */
    {"noescape", 0},
    /* clang's availability(PLATFORM, introduced = V, ...): a diagnostic
       when the deployment target is older (libc++'s
       _LIBCPP_INTRODUCED_IN_LLVM_N_ATTRIBUTE) */
    {"availability", 0},
    /* clang's swift_attr("..."): an annotation for Swift's importer (the
       SDK's __swift_nonisolated_unsafe) */
    {"swift_attr", 0},
    /* clang's diagnose_if(condition, message, "warning" | "error"): a
       diagnostic at a call where the condition holds (libc++'s
       _LIBCPP_DIAGNOSE_WARNING and _LIBCPP_CHECK_*_MEMORY_ORDER); accepted
       and not evaluated, so a call it would diagnose is accepted */
    {"diagnose_if", 0},
    /* clang's thread-safety attributes (libc++'s _LIBCPP_CAPABILITY,
       _LIBCPP_ACQUIRE_CAPABILITY, ...): arguments are expressions naming
       members or this, or a string; checked only under -Wthread-safety,
       which vcc does not do */
    {"capability", 0},
    {"lockable", 0},
    {"scoped_lockable", 0},
    {"acquire_capability", 0},
    {"acquire_shared_capability", 0},
    {"release_capability", 0},
    {"release_shared_capability", 0},
    {"release_generic_capability", 0},
    {"try_acquire_capability", 0},
    {"try_acquire_shared_capability", 0},
    {"requires_capability", 0},
    {"requires_shared_capability", 0},
    {"no_thread_safety_analysis", 0},
    {"guarded_by", 0},
    {"pt_guarded_by", 0},
    {"guarded_var", 0},
    {"pt_guarded_var", 0},
    {"acquired_before", 0},
    {"acquired_after", 0},
    {"locks_excluded", 0},
    {"assert_capability", 0},
    {"assert_shared_capability", 0},
    {"exclusive_lock_function", 0},
    {"shared_lock_function", 0},
    {"unlock_function", 0},
    {"exclusive_trylock_function", 0},
    {"shared_trylock_function", 0},
    {"exclusive_locks_required", 0},
    {"shared_locks_required", 0},
    {"lock_returned", 0},
    {"assert_exclusive_lock", 0},
    {"assert_shared_lock", 0},
    /* clang's enable_if(condition, message) (below) */
    {"enable_if", gnu_enable_if},
};

/* the rule of an attribute's spelling, nullptr if analysis has none */
static const gnu_attribute_rule *gnu_attribute_rule_of(const char *name,
                                                       size_t length)
{
    /* "You may optionally specify attribute names with __ preceding and
       following the name." */
    if (length > 4 && name[0] == '_' && name[1] == '_' &&
        name[length - 1] == '_' && name[length - 2] == '_') {
        name += 2;
        length -= 4;
    }
    for (const gnu_attribute_rule &r : gnu_attribute_rules)
        if (strlen(r.name) == length && !memcmp(r.name, name, length))
            return &r;
    return nullptr;
}

/* [ext:clang_ext_vector_type] ext_vector_type or __ext_vector_type__ */
static bool spelling_is_ext_vector_type(const owned_buffer &spelling)
{
    const char *name = reinterpret_cast<const char *>(spelling.data());
    size_t length = spelling.size();
    if (length > 4 && name[0] == '_' && name[1] == '_') {
        name += 2;
        length -= 4;
    }
    return length == 15 && !memcmp(name, "ext_vector_type", 15);
}

/* [ext:clang_availability] the attribute at the token is clang's
   availability (or __availability__) */
bool parser::at_availability()
{
    if (!tokens.files.behavior.has(extension::clang_availability))
        return false;
    owned_buffer spelling;
    size_t n = tokens.spelling(position, spelling.get());
    const gnu_attribute_rule *rule = gnu_attribute_rule_of(
        reinterpret_cast<const char *>(spelling.data()), n);
    return rule && !strcmp(rule->name, "availability");
}

/*
 * [ext:clang_availability] the arguments after availability's '(': a
 * version is a pp-number that need not be a literal
 * ([lex.ppnumber]/2: 10.12.1 converts to no token, so it is not an
 * expression).
 *
 *     attribute-specifier:
 *         __attribute__ ( ( clang-availability-attribute ) )
 *
 *     clang-availability-attribute:
 *         identifier ( identifier , clang-availability-clause-list )
 *
 * (and in any gnu-attribute-list)
 *
 *     clang-availability-clause-list:
 *         clang-availability-clause
 *         clang-availability-clause-list , clang-availability-clause
 *
 *     clang-availability-clause:
 *         identifier
 *         identifier = pp-number
 *         identifier = string-literal
 */
node_index parser::clang_availability_arguments()
{
    if (!at_identifier())
        return expected("a platform name", "ext.clang");
    add_token();
    while (accept(preprocessing_op_or_punc::COMMA)) {
        if (!at_attribute_identifier())
            return expected("an availability clause", "ext.clang");
        add_token();
        if (!accept(preprocessing_op_or_punc::EQUAL))
            continue;
        if (peek().preprocessing.kind != preprocessing_token_kind::pp_number &&
            peek().literal != literal_kind::string_literal)
            return expected("a version or a string-literal", "ext.clang");
        add_token();
    }
    return 0;
}

/* abi_tag's argument: one ordinary string-literal of identifier
   characters, the same as an earlier one */
analysis_result semantic_analysis::add_gnu_abi_tag(SyntaxRef at,
                                                   token_index literal,
                                                   gnu_attributes &g)
{
    if (tokens[literal].literal != literal_kind::string_literal)
        return not_supported(at, "ext.gnu");
    owned_buffer tag;
    tokens.spelling(literal, tag.get());
    if (tag.size() < 3 || tag.data()[0] != '"')
        return not_supported(at, "ext.gnu");
    for (size_t i = 1; i + 1 < tag.size(); i++) {
        unsigned char ch = tag.data()[i];
        if (!(ch == '_' || (ch >= '0' && ch <= '9') ||
              ((ch | 32) >= 'a' && (ch | 32) <= 'z')))
            return not_supported(at, "ext.gnu");
    }
    if (g.abi_tag) {
        /* two tags: their order in the name is sorted */
        owned_buffer before;
        tokens.spelling(g.abi_tag, before.get());
        if (before.size() != tag.size() ||
            memcmp(before.data(), tag.data(), tag.size()))
            return not_supported(at, "ext.gnu");
    }
    g.abi_tag = literal;
    g.flags |= gnu_abi_tag;
    return analysis_result::well_formed;
}

/*
 * The attributes of a gnu-attribute-specifier, wherever it is: before a
 * declaration, among its decl-specifiers or after a declarator (GCC
 * applies each to the declaration).  aligned: "The aligned attribute
 * specifies a minimum alignment for the variable or structure field,
 * measured in bytes. When specified, alignment must be an integer constant
 * power of 2.  Specifying no alignment argument implies the ideal
 * alignment for the target." (the target's largest, 16 on aarch64 and
 * x86-64, as clang's).  An attribute analysis has no rule for is
 * unsupported with ext.gnu.
 */
analysis_result semantic_analysis::gnu_attribute_specifier(SyntaxRef n,
                                                           gnu_attributes &g)
{
    /* (standard attributes without effect, or [[noreturn]]:
       leading_gnu_attributes) */
    uint32_t standard = 0;
    if (token_index tag = scoped_gnu_abi_tag(n))
        return add_gnu_abi_tag(n, tag, g);
    if (n.is(syntax_kind::attribute_specifier) &&
        attributes_as_gnu(n, standard)) {
        g.flags |= standard;
        return analysis_result::well_formed;
    }
    owned_array<SyntaxElement> attributes;
    owned_array<SyntaxRef> work;
    work.append(n);
    while (!work.empty()) {
        SyntaxRef next = work.back();
        work.resize(work.size() - 1);
        for (uint32_t i = 0; i < next.size(); i++) {
            SyntaxElement c = next[i];
            if (next == n && i == 0 && c.is_token())
                continue; /* __attribute__ */
            if (c.is(syntax_kind::gnu_attribute))
                attributes.append(c);
            else if (c.is(syntax_kind::gnu_attribute_list,
                          syntax_kind::gnu_attribute_specifier))
                work.append(c);
            else if (c.is(syntax_kind::attribute_specifier) &&
                     attributes_as_gnu(c, standard))
                g.flags |= standard;
            else if (!c.is_token())
                return not_supported(n, "ext.gnu");
            else if (tokens[c.token()].kind !=
                     token_kind::operator_or_punctuator)
                /* an attribute without arguments: its token (or
                   __attribute__) */
                attributes.append(c);
        }
    }
    for (SyntaxElement a : attributes) {
        token_index name_token = a.token();
        uint32_t count = a.size();
        if (!a.is_token()) {
            if (!count || !a[0].is_token())
                return not_supported(a.index(), "ext.gnu");
            name_token = a[0].token();
        }
        owned_buffer spelling;
        tokens.spelling(name_token, spelling.get());
        const gnu_attribute_rule *rule = gnu_attribute_rule_of(
            reinterpret_cast<const char *>(spelling.data()), spelling.size());
        SyntaxRef at = n;
        if (!a.is_token())
            at = a.index();
        if (a.is_token() && ((spelling.size() == 13 &&
                              !memcmp(spelling.data(), "__attribute__", 13)) ||
                             (spelling.size() == 11 &&
                              !memcmp(spelling.data(), "__attribute", 11))))
            continue;
        if (!rule && g.ext_vector.allowed && count == 4 &&
            spelling_is_ext_vector_type(spelling)) {
            VTRY(r, clang_ext_vector_count(at, a[2], g));
            continue;
        }
        if (!rule)
            return not_supported(at, "ext.gnu");
        /* clang's enable_if: the function a candidate only where the
           condition holds; on a constructor template's declarator
           (enable_if_allowed: libc++'s _CmpUnspecifiedParam, which allows
           the literal 0 alone) unchecked, so a call it would refuse is
           accepted; elsewhere a constant true condition (libc++'s
           _LIBCPP_PREFERRED_OVERLOAD) makes the function one of its own,
           preferred over one without (translation-unit.cpp,
           overload-resolution.cpp); any other condition is not analysed */
        if (rule->flag == gnu_enable_if) {
            if (enable_if_allowed)
                continue;
            if (count != 6)
                return not_supported(at, "ext.gnu");
            bool constant = false, value = false;
            VTRY(r, bool_constant_expression(at, a[2], constant, value));
            if (!constant || !value)
                return not_supported(at, "ext.gnu");
            g.flags |= gnu_enable_if;
            continue;
        }
        if (rule->flag == gnu_aligned) {
            uint64_t alignment = 16;
            if (count == 4) {
                VTRY(r, gnu_integer_argument(at, a[2], alignment));
                if (!alignment || (alignment & (alignment - 1)) ||
                    alignment > (1u << 28))
                    /* clang: "requested alignment is not a power of 2" */
                    return not_supported(at, "ext.gnu");
            } else if (count > 1 && count != 3) {
                return not_supported(at, "ext.gnu");
            }
            if (alignment > g.aligned)
                g.aligned = alignment;
            continue;
        }
        if (rule->flag == gnu_constructor || rule->flag == gnu_destructor) {
            /* the priority: an integer constant, 0 to 65535 (clang:
               "'constructor' attribute requires integer constant between 0
               and 65535 inclusive") */
            uint64_t priority = 0;
            if (count == 4) {
                VTRY(r, gnu_integer_argument(at, a[2], priority));
                if (priority > 65535)
                    return not_supported(at, "ext.gnu");
            } else if (count > 1 && count != 3) {
                return not_supported(at, "ext.gnu");
            }
            g.flags |= rule->flag;
            g.priority = static_cast<uint32_t>(priority);
            continue;
        }
        if (rule->flag == gnu_cleanup) {
            VTRY(r, gnu_cleanup_function(at, a, count, g));
            continue;
        }
        if (rule->flag == gnu_mode) {
            /* one identifier: the mode */
            if (count != 4 || !a[2].is_token() ||
                tokens[a[2].token()].kind != token_kind::identifier || g.mode)
                return not_supported(at, "ext.gnu");
            g.mode = a[2].token();
            g.flags |= gnu_mode;
            continue;
        }
        if (rule->flag == gnu_vector_size) {
            uint64_t bytes = 0;
            if (count != 4 || g.vector_bytes)
                return not_supported(at, "ext.gnu");
            VTRY(r, gnu_integer_argument(at, a[2], bytes));
            if (!bytes)
                return ill_formed(at, "zero vector size", "ext.gnu");
            g.vector_bytes = bytes;
            g.flags |= gnu_vector_size;
            continue;
        }
        if (rule->flag == gnu_section) {
            /* one ordinary string-literal: "section-name" */
            if (count != 4 || !a[2].is_token() ||
                tokens[a[2].token()].literal != literal_kind::string_literal)
                return not_supported(at, "ext.gnu");
            owned_buffer name;
            tokens.spelling(a[2].token(), name.get());
            if (name.size() < 2 || name.data()[0] != '"' ||
                memchr(name.data(), '\\', name.size()))
                return not_supported(at, "ext.gnu");
            g.section = a[2].token();
            g.flags |= gnu_section;
            continue;
        }
        if (rule->flag == gnu_default_visibility) {
            owned_buffer value;
            if (count == 4 && a[2].is_token())
                tokens.spelling(a[2].token(), value.get());
            if (value.size() == 9 && !memcmp(value.data(), "\"default\"", 9))
                continue;
            /* visibility("hidden") on a function or variable (the
               declaration's kind decides where it is allowed);
               type_visibility's is a class's */
            bool type_visibility = !strcmp(rule->name, "type_visibility");
            if (type_visibility || value.size() != 8 ||
                memcmp(value.data(), "\"hidden\"", 8))
                return not_supported(at, "ext.gnu");
            g.flags |= gnu_hidden_visibility;
            continue;
        }
        if (rule->flag == gnu_abi_tag) {
            if (count != 4 || !a[2].is_token())
                return not_supported(at, "ext.gnu");
            VTRY(r, add_gnu_abi_tag(at, a[2].token(), g));
            continue;
        }
        /* the others take no arguments analysis has rules for (format's
           and nonnull's say nothing to code generation) */
        if (count > 1 && rule->flag && rule->flag != gnu_nonnull)
            return not_supported(at, "ext.gnu");
        g.flags |= rule->flag;
    }
    return analysis_result::well_formed;
}

/* an attribute's argument that is an integer constant expression */
analysis_result semantic_analysis::gnu_integer_argument(SyntaxRef at,
                                                        SyntaxElement argument,
                                                        uint64_t &value)
{
    expression_meaning e;
    VTRY(r, expression(argument, e));
    if (!is_integral(e.type) || e.category != value_category::prvalue)
        return not_supported(at, "ext.gnu");
    bool constant = false;
    VTRY_SET(r, constant_expression(at, e, constant));
    if (!constant)
        return not_supported(at, "ext.gnu");
    value = e.value;
    return analysis_result::well_formed;
}

/*
 * cleanup (GCC manual 6.34.1, Common Variable Attributes): "The cleanup
 * attribute runs a function when the variable goes out of scope.  This
 * attribute can only be applied to auto function scope variables; it may
 * not be applied to parameters or variables with static storage duration.
 * The function must take one parameter, a pointer to a type compatible
 * with the variable.  The return value of the function (if any) is
 * ignored."  The argument is the unqualified name of one function that is
 * not overloaded (clang: "'cleanup' argument not a function" otherwise);
 * where the variable is automatic and what the function takes is checked
 * where the declaration is.
 */
analysis_result semantic_analysis::gnu_cleanup_function(SyntaxRef at,
                                                        SyntaxElement a,
                                                        uint32_t count,
                                                        gnu_attributes &g)
{
    if (count != 4 || !a[2].is_token() ||
        tokens[a[2].token()].kind != token_kind::identifier || g.cleanup)
        return not_supported(at, "ext.gnu");
    lookup_result found =
        lookup_unqualified(tokens.identifier(a[2].token()), lookup_filter::any);
    if (!found.entity || found.unknown || found.ambiguous ||
        entity_table[found.entity].kind != entity_kind::function ||
        (entity_table[found.entity].enclosing &&
         entity_table[entity_table[found.entity].enclosing].kind ==
             entity_kind::class_type))
        /* (a member function needs an object: not GCC's cleanup) */
        return not_supported(at, "ext.gnu");
    owned_array<entity_index> set;
    overload_set(found.entity, set);
    if (set.size() != 1)
        return not_supported(at, "ext.gnu");
    g.cleanup = found.entity;
    g.flags |= gnu_cleanup;
    return analysis_result::well_formed;
}

/* a cleanup function takes one parameter, a pointer ("a pointer to a type
   compatible with the variable": the conversion is C's) */
bool semantic_analysis::gnu_cleanup_takes_pointer(entity_index function) const
{
    type_index f = entity_table[function].type.unqualified();
    return type_table[f].kind == type_kind::function &&
           type_table[f].parameter_count == 1 &&
           type_table[type_table.parameters(f)[0].unqualified()].kind ==
               type_kind::pointer;
}

/*
 * mode (GCC manual 6.39.1, Common Variable Attributes): "This attribute
 * specifies the data type for the declaration—whichever type corresponds
 * to the mode mode.  This in effect lets you request an integer or
 * floating-point type according to its width."  The integer modes QI, HI,
 * SI, DI and TI (8 to 128 bits) of an integer type, which keep its
 * signedness (<sys/types.h>'s int8_t), and byte, word and pointer; the floating-point and
 * complex modes of a floating-point or complex type: SF and DF (float and
 * double), XF (long double, x87), TF (binary128: long double where that
 * is binary128, else __float128), and the complex modes SC, DC, XC and TC
 * of them; with or without the underscores around the name.
 */
analysis_result semantic_analysis::gnu_mode_type(SyntaxRef at, token_index mode,
                                                 qualified_type &type)
{
    owned_buffer spelling;
    tokens.spelling(mode, spelling.get());
    const char *name = reinterpret_cast<const char *>(spelling.data());
    size_t length = spelling.size();
    if (length > 4 && name[0] == '_' && name[1] == '_' &&
        name[length - 1] == '_' && name[length - 2] == '_') {
        name += 2;
        length -= 4;
    }
    /* byte, word and pointer name an integer mode by the target's widths
       (word and pointer: those of a pointer; unwind_word too) */
    const char *spelled = name;
    if (length == 4 && !memcmp(name, "byte", 4))
        spelled = "QI";
    else if ((length == 4 && !memcmp(name, "word", 4)) ||
             (length == 7 && !memcmp(name, "pointer", 7)) ||
             (length == 11 && !memcmp(name, "unwind_word", 11))) {
        spelled = "SI";
        if (pointer_size() == 8)
            spelled = "DI";
    } else if (length != 2) {
        return not_supported(at, "ext.gnu");
    }
    name = spelled;
    fundamental_type integer = fundamental_of(type);
    if (type_table[type.unqualified()].kind == type_kind::fundamental &&
        fundamental_type_is_integral(integer) &&
        integer != fundamental_type::bool_ && name[1] == 'I') {
        const implementation_defined_behavior &b = tokens.files.behavior;
        bool negative = fundamental_type_is_signed(b, integer);
        fundamental_type t = fundamental_type::none;
        switch (name[0]) {
        case 'Q':
            t = fundamental_type::unsigned_char;
            if (negative)
                t = fundamental_type::signed_char;
            break;
        case 'H':
            t = fundamental_type::unsigned_short_int;
            if (negative)
                t = fundamental_type::short_int;
            break;
        case 'S':
            t = fundamental_type::unsigned_int;
            if (negative)
                t = fundamental_type::int_;
            break;
        case 'D':
            /* 64 bits: long where it has them, as clang's int64_t */
            if (b.long_width == 64) {
                t = fundamental_type::unsigned_long_int;
                if (negative)
                    t = fundamental_type::long_int;
            } else {
                t = fundamental_type::unsigned_long_long_int;
                if (negative)
                    t = fundamental_type::long_long_int;
            }
            break;
        case 'T':
            t = fundamental_type::unsigned_int128;
            if (negative)
                t = fundamental_type::int128;
            break;
        default:
            return not_supported(at, "ext.gnu");
        }
        type = qualified_type::make(type_table.fundamental(t), type.cv());
        return analysis_result::well_formed;
    }
    const type_kind kind = type_table[type.unqualified()].kind;
    qualified_type base = type;
    if (kind == type_kind::complex)
        base = type_table[type.unqualified()].operand;
    if (!fundamental_type_is_floating(fundamental_of(base)) &&
        fundamental_of(base) != fundamental_type::float128)
        return not_supported(at, "ext.gnu");
    /* the second letter C says complex, F real */
    bool complex = name[1] == 'C';
    if (!complex && name[1] != 'F')
        return not_supported(at, "ext.gnu");
    if (complex != (kind == type_kind::complex))
        return not_supported(at, "ext.gnu");
    fundamental_type t = fundamental_type::none;
    switch (name[0]) {
    case 'S':
        t = fundamental_type::float_;
        break;
    case 'D':
        t = fundamental_type::double_;
        break;
    case 'X':
        if (tokens.files.behavior.long_double_fraction_bits != 63)
            return not_supported(at, "ext.gnu");
        t = fundamental_type::long_double;
        break;
    case 'T':
        t = fundamental_type::float128;
        if (tokens.files.behavior.long_double_fraction_bits == 112)
            t = fundamental_type::long_double;
        break;
    default:
        return not_supported(at, "ext.gnu");
    }
    type_index made = type_table.fundamental(t);
    if (complex)
        made = type_table.complex_of(qualified_type::make(made, cv_none));
    type = qualified_type::make(made, type.cv());
    return analysis_result::well_formed;
}

/* ... of a declarator, and among (or before) a decl-specifier-seq */
analysis_result
semantic_analysis::gnu_attribute_specifier(SyntaxRef n, declarator_meaning &m)
{
    gnu_attributes g;
    g.aligned = m.aligned;
    g.flags = m.gnu_flags;
    g.section = m.section;
    g.abi_tag = m.abi_tag;
    g.mode = m.mode;
    g.vector_bytes = m.vector_bytes;
    g.ext_vector = m.ext_vector;
    g.ext_vector.allowed =
        m.ext_vector.allowed &&
        tokens.files.behavior.has(extension::clang_ext_vector_type);
    g.priority = m.gnu_priority;
    g.cleanup = m.gnu_cleanup;
    analysis_result r = gnu_attribute_specifier(n, g);
    m.gnu_priority = g.priority;
    m.gnu_cleanup = g.cleanup;
    m.mode = g.mode;
    m.vector_bytes = g.vector_bytes;
    m.ext_vector = g.ext_vector;
    m.aligned = g.aligned;
    m.gnu_flags = g.flags;
    m.section = g.section;
    m.abi_tag = g.abi_tag;
    return r;
}

analysis_result semantic_analysis::gnu_attribute_specifier(SyntaxRef n,
                                                           specified &s)
{
    gnu_attributes g;
    g.aligned = s.aligned;
    g.flags = s.gnu_flags;
    g.section = s.section;
    g.abi_tag = s.abi_tag;
    g.priority = s.gnu_priority;
    g.cleanup = s.gnu_cleanup;
    g.ext_vector = s.ext_vector;
    g.ext_vector.allowed =
        s.ext_vector.allowed &&
        tokens.files.behavior.has(extension::clang_ext_vector_type);
    analysis_result r = gnu_attribute_specifier(n, g);
    s.ext_vector = g.ext_vector;
    s.gnu_priority = g.priority;
    s.gnu_cleanup = g.cleanup;
    if (g.mode || g.vector_bytes)
        /* the mode of a decl-specifier's attribute: not analysed */
        return not_supported(n, "ext.gnu");
    s.aligned = g.aligned;
    s.gnu_flags = g.flags;
    s.section = g.section;
    s.abi_tag = g.abi_tag;
    return r;
}

} // namespace cplusplus
