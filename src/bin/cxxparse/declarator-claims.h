/*
 * declarator-claims.h: cxxparse -declaration-claims (doc/PLAN-cxx-parse.md,
 * V4b): for every name a declaration declares, the type its
 * decl-specifier-seq and declarator give it, worked out from the syntax tree
 * as [dcl.meaning] says and written in an encoding that says how the type
 * is built:
 *
 *     int *const (*a[3])(char, ...);
 *     ::vcc_claim::array_of<::vcc_claim::pointer_to<::vcc_claim::function<
 *         ::vcc_claim::const_<::vcc_claim::pointer_to<int>>(char, ...)>>, (3)>
 *
 * bench/cxx-parse.py defines those alias templates and has clang compile
 * static_assert(__is_same(decltype(a), CLAIM)) after the declaration: clang
 * checks our reading of how *, &, [] and () nest, of the cv-qualifiers and
 * of the combinations of type specifiers, and nobody compares syntax trees.
 *
 * A line per name:
 *
 *     FILE <tab> LINE-LINE <tab> LINE:COLUMN <tab> KIND <tab> NAME <tab> CLAIM
 *
 * LINE-LINE are the lines of the declaration that say the type (all of it
 * but a function-body) and LINE:COLUMN is just after the declaration (a
 * declaration of a declaration-seq), where the static_assert can stand.
 * KIND is how NAME's type is asked for: `object` (decltype(NAME)), `function`
 * (the same, or the overload of that type), `type` (a typedef-name: NAME
 * itself); with `[]` appended when the type is an array of unknown bound,
 * whose bound an initializer or an earlier declaration may have supplied
 * ([dcl.array]/7 and /8).
 *
 * No claim is made for a name whose type the tree does not say: one with a
 * placeholder type to deduce, of an unnamed class or enumeration, a
 * destructor's, one that names a lambda's closure type (decltype([]{})); nor for a declaration with a token a macro made, or one
 * whose type names one of its own parameters (decltype(x), noexcept(x)),
 * which the place of the static_assert does not have in scope.  Nor for a
 * declaration with an attribute of a namespace other than the standard's:
 * [dcl.attr.grammar]/6 leaves its meaning to the implementation, and
 * clang's [[gnu::noreturn]] is part of its function types.
 */

class declarator_claims
{
  public:
    declarator_claims(const syntax_tree &tree, token_stream &tokens,
                      const parser &p, vbuffer *out)
        : tree(tree), tokens(tokens), p(p), out(out)
    {
    }

    /* the claims of the declarations under n */
    void write(node_index n)
    {
        declarations(n, 0, false);
    }

  private:
    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
    vbuffer *out;

    /* what a decl-specifier-seq says besides its type */
    struct specifiers {
        bool typedef_ = false, constexpr_ = false;
        /* auto: [dcl.fct]/2's T, until a trailing-return-type replaces it */
        bool placeholder = false;
    };

    /* the last type constructor applied: the declared type's outermost */
    enum class outermost {
        none,
        pointer,
        reference,
        member_pointer,
        array,
        array_of_unknown_bound,
        function,
    };

    /* what [dcl.meaning] finds in a declarator */
    struct declared {
        bool has_id = false;
        token_index id_first = 0, id_end = 0;
        outermost last = outermost::none;
    };

    /* identifiers: the parameters the declarator declares, and the names in
       the expressions the claim repeats as they are written */
    owned_array<uint32_t> parameter_names, mentioned_names;

    bool is_operator_token(syntax_child c, preprocessing_op_or_punc op) const
    {
        if (!c.is_token())
            return false;
        const token &t = tokens[c.index()];
        return t.kind == token_kind::operator_or_punctuator &&
               preprocessing_op_or_punc_primary(t.op_or_punc) == op;
    }

    /* whether node n has a lambda-expression in it: each one is a closure
       type of its own ([expr.prim.lambda.closure]/1), so the claim's
       decltype([]{}) would name another type */
    bool has_lambda(node_index n) const
    {
        const syntax_node &node = tree[n];
        if (node.kind == syntax_kind::lambda_expression)
            return true;
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(n)[i];
            if (!c.is_token() && has_lambda(c.index()))
                return true;
        }
        return false;
    }

    bool is_kind(syntax_child c, syntax_kind kind) const
    {
        return !c.is_token() && tree[c.index()].kind == kind;
    }

    bool is_attribute(syntax_child c) const
    {
        return is_kind(c, syntax_kind::attribute_specifier) ||
               is_kind(c, syntax_kind::attribute_specifier_seq) ||
               is_kind(c, syntax_kind::alignment_specifier);
    }

    /* type = before type after */
    static void wrap(owned_buffer &type, const char *before, const char *after)
    {
        owned_buffer made;
        vbuffer_string_append(made.get(), before);
        vbuffer_append(made.get(), type.data(), type.size());
        vbuffer_string_append(made.get(), after);
        type.clear();
        vbuffer_append(type.get(), made.data(), made.size());
    }

    /* the tokens [first, end) as written, and the identifiers among them */
    void verbatim(token_index first, token_index end, vbuffer *to)
    {
        for (token_index t = first; t < end; t++) {
            if (t > first)
                vbuffer_append(to, " ", 1);
            tokens.spelling(t, to);
            if (tokens[t].kind == token_kind::identifier)
                mentioned_names.append(tokens.identifier(t));
        }
    }

    /*
     * [dcl.meaning]/4: "First, the decl-specifier-seq determines a type."
     * The type of a decl-specifier-seq, type-specifier-seq or
     * defining-type-specifier-seq.  [dcl.type.simple]/3: "When multiple
     * simple-type-specifiers are allowed, they can be freely intermixed with
     * other decl-specifiers in any order": Table 14's row is found by
     * counting them, and the type is written as the table names it (the
     * example of [dcl.meaning]/4: "the type specifiers int unsigned determine
     * the type unsigned int").
     */
    bool specified_type(node_index seq, owned_buffer &type, specifiers &s)
    {
        const syntax_node &node = tree[seq];
        unsigned const_ = 0, volatile_ = 0, signed_ = 0, unsigned_ = 0;
        unsigned short_ = 0, long_ = 0, char_ = 0, int_ = 0, double_ = 0;
        bool named = false;
        type.clear();
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(seq)[i];
            if (is_attribute(c))
                continue;
            if (c.is_token()) {
                const token &t = tokens[c.index()];
                if (t.kind == token_kind::identifier) {
                    /* a type-name */
                    tokens.spelling(c.index(), type.get());
                    named = true;
                    continue;
                }
                switch (p.keyword_of(t)) {
                case standard_keyword::const_:
                    const_++;
                    break;
                case standard_keyword::volatile_:
                    volatile_++;
                    break;
                case standard_keyword::signed_:
                    signed_++;
                    break;
                case standard_keyword::unsigned_:
                    unsigned_++;
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
                case standard_keyword::char8_t_:
                case standard_keyword::char16_t_:
                case standard_keyword::char32_t_:
                case standard_keyword::wchar_t_:
                case standard_keyword::bool_:
                case standard_keyword::float_:
                case standard_keyword::void_:
                    tokens.spelling(c.index(), type.get());
                    named = true;
                    break;
                case standard_keyword::auto_:
                    s.placeholder = true;
                    break;
                case standard_keyword::typedef_:
                    s.typedef_ = true;
                    break;
                case standard_keyword::constexpr_:
                    s.constexpr_ = true;
                    break;
                /* [dcl.meaning]/2: the specifiers that apply to the
                   declarator-id, not to its type */
                case standard_keyword::static_:
                case standard_keyword::thread_local_:
                case standard_keyword::extern_:
                case standard_keyword::mutable_:
                case standard_keyword::inline_:
                case standard_keyword::consteval_:
                case standard_keyword::constinit_:
                case standard_keyword::register_:
                    break;
                default:
                    return false;
                }
                continue;
            }
            const syntax_node &child = tree[c.index()];
            switch (child.kind) {
            case syntax_kind::decltype_specifier:
                if (has_lambda(c.index()))
                    return false;
                verbatim(child.first_token, child.end_token, type.get());
                named = true;
                break;
            case syntax_kind::simple_type_specifier:
            case syntax_kind::elaborated_type_specifier:
            case syntax_kind::elaborated_enum_specifier:
                verbatim(child.first_token, child.end_token, type.get());
                named = true;
                break;
            case syntax_kind::class_specifier:
            case syntax_kind::enum_specifier:
                /* [class.name]/1, [dcl.enum]/1: the specifier defines a type
                   and its name, which is how the claim says the type; an
                   unnamed one has no way to be said again */
                if (!name_of_defined_type(c.index(), type))
                    return false;
                named = true;
                break;
            default:
                /* a placeholder-type-specifier, or what a later step
                   parses */
                return false;
            }
        }
        if (s.placeholder) {
            type.clear();
            vbuffer_string_append(type.get(), "auto");
        } else if (!named) {
            const char *sign = "";
            if (unsigned_)
                sign = "unsigned ";
            if (char_) {
                if (signed_)
                    sign = "signed ";
                vbuffer_format_append(type.get(), "%schar", sign);
            } else if (double_) {
                if (long_)
                    vbuffer_string_append(type.get(), "long double");
                else
                    vbuffer_string_append(type.get(), "double");
            } else if (short_) {
                vbuffer_format_append(type.get(), "%sshort", sign);
            } else if (long_ == 2) {
                vbuffer_format_append(type.get(), "%slong long", sign);
            } else if (long_ == 1) {
                vbuffer_format_append(type.get(), "%slong", sign);
            } else if (int_ || signed_ || unsigned_) {
                vbuffer_format_append(type.get(), "%sint", sign);
            } else {
                return false; /* no type specifier */
            }
        }
        if (const_)
            wrap(type, "::vcc_claim::const_<", ">");
        if (volatile_)
            wrap(type, "::vcc_claim::volatile_<", ">");
        return true;
    }

    /* the name a class-specifier or enum-specifier gives its type: the
       identifier of its head, with its nested-name-specifier if it has one */
    bool name_of_defined_type(node_index specifier, owned_buffer &type)
    {
        node_index head = specifier;
        const syntax_node &s = tree[specifier];
        if (s.child_count &&
            is_kind(tree.children(specifier)[0], syntax_kind::enum_head))
            head = tree.children(specifier)[0].index();
        const syntax_node &h = tree[head];
        for (uint32_t i = 0; i < h.child_count; i++) {
            syntax_child c = tree.children(head)[i];
            if (c.is_token() &&
                tokens[c.index()].kind == token_kind::identifier) {
                /* after a nested-name-specifier the name is not the next
                   token alone: the two are written together */
                token_index first = c.index();
                if (i && is_kind(tree.children(head)[i - 1],
                                 syntax_kind::nested_name_specifier))
                    first =
                        tree[tree.children(head)[i - 1].index()].first_token;
                verbatim(first, c.index() + 1, type.get());
                return true;
            }
            if (is_kind(c, syntax_kind::enum_head_name)) {
                const syntax_node &name = tree[c.index()];
                verbatim(name.first_token, name.end_token, type.get());
                return true;
            }
            if (is_operator_token(c, preprocessing_op_or_punc::LEFT_BRACE) ||
                is_kind(c, syntax_kind::unparsed) ||
                is_kind(c, syntax_kind::enum_base))
                break;
        }
        return false;
    }

    /*
     * [dcl.ptr]/1: "the type of the identifier of D is
     * derived-declarator-type-list cv-qualifier-seq pointer to T";
     * [dcl.ref]/1: "... reference to T"; [dcl.mptr]/1: "... cv-qualifier-seq
     * pointer to member of class nested-name-specifier of type T".
     */
    bool apply_ptr_operator(node_index op, owned_buffer &type, declared &d)
    {
        const syntax_node &node = tree[op];
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(op)[i];
            if (is_attribute(c))
                continue;
            if (is_kind(c, syntax_kind::nested_name_specifier)) {
                /* the class is the specifier without its last :: */
                const syntax_node &name = tree[c.index()];
                wrap(type, "::vcc_claim::pointer_to_member_of<", ", ");
                verbatim(name.first_token, name.end_token - 1, type.get());
                vbuffer_string_append(type.get(), ">");
                d.last = outermost::member_pointer;
                i++; /* its * */
            } else if (is_kind(c, syntax_kind::cv_qualifier_seq)) {
                const syntax_node &seq = tree[c.index()];
                for (uint32_t k = 0; k < seq.child_count; k++)
                    if (!apply_cv_qualifier(tree.children(c.index())[k], type))
                        return false;
            } else if (is_operator_token(c, preprocessing_op_or_punc::STAR)) {
                wrap(type, "::vcc_claim::pointer_to<", ">");
                d.last = outermost::pointer;
            } else if (is_operator_token(c,
                                         preprocessing_op_or_punc::AMPERSAND)) {
                wrap(type, "::vcc_claim::lvalue_reference_to<", ">");
                d.last = outermost::reference;
            } else if (is_operator_token(
                           c, preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) {
                wrap(type, "::vcc_claim::rvalue_reference_to<", ">");
                d.last = outermost::reference;
            } else if (!apply_cv_qualifier(c, type)) {
                return false;
            }
        }
        return true;
    }

    bool apply_cv_qualifier(syntax_child c, owned_buffer &type)
    {
        if (!c.is_token())
            return false;
        switch (p.keyword_of(tokens[c.index()])) {
        case standard_keyword::const_:
            wrap(type, "::vcc_claim::const_<", ">");
            return true;
        case standard_keyword::volatile_:
            wrap(type, "::vcc_claim::volatile_<", ">");
            return true;
        default:
            return false;
        }
    }

    /*
     * [dcl.fct]/1: "the type of the declarator-id in D is
     * derived-declarator-type-list noexcept_opt function of
     * parameter-type-list cv-qualifier-seq_opt ref-qualifier_opt returning
     * T".  The claim writes the function type as a type-id of the
     * parameters' own types, R(P1, P2, ...) cv ref noexcept: [dcl.fct]/5's
     * adjustments ("any parameter of type array of T or of function type T
     * is adjusted to be pointer to T", top-level cv-qualifiers deleted) are
     * then clang's to make on both sides.  noexcept is part of the type from
     * C++17 (P0012R1).
     */
    bool apply_function(node_index parameters, owned_buffer &type, declared &d)
    {
        const syntax_node &node = tree[parameters];
        owned_buffer list, qualifiers;
        bool closed = false;
        unsigned count = 0;
        owned_array<node_index> work;
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(parameters)[i];
            if (is_attribute(c))
                continue;
            if (!closed) {
                if (is_operator_token(
                        c, preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                    closed = true;
                } else if (is_operator_token(
                               c, preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                    continue;
                } else if (!parameter_types(c, list, count)) {
                    return false;
                }
                continue;
            }
            if (is_kind(c, syntax_kind::cv_qualifier_seq)) {
                const syntax_node &seq = tree[c.index()];
                vbuffer_append(qualifiers.get(), " ", 1);
                verbatim(seq.first_token, seq.end_token, qualifiers.get());
            } else if (is_kind(c, syntax_kind::noexcept_specifier)) {
                const syntax_node &spec = tree[c.index()];
                if (tokens.files.behavior.edition >=
                    standard_edition::cplusplus_17) {
                    vbuffer_append(qualifiers.get(), " ", 1);
                    verbatim(spec.first_token, spec.end_token,
                             qualifiers.get());
                }
            } else if (c.is_token()) {
                /* const, volatile, & or && */
                vbuffer_append(qualifiers.get(), " ", 1);
                tokens.spelling(c.index(), qualifiers.get());
            } else {
                return false;
            }
        }
        wrap(type, "::vcc_claim::function<", "(");
        vbuffer_append(type.get(), list.data(), list.size());
        vbuffer_string_append(type.get(), ")");
        vbuffer_append(type.get(), qualifiers.data(), qualifiers.size());
        vbuffer_string_append(type.get(), ">");
        d.last = outermost::function;
        return true;
    }

    /* a child of parameters-and-qualifiers between its parentheses, or of a
       parameter-declaration-clause or -list: the types of its parameters,
       and the ellipsis, added to list */
    bool parameter_types(syntax_child c, owned_buffer &list, unsigned &count)
    {
        if (is_operator_token(c, preprocessing_op_or_punc::COMMA))
            return true;
        if (is_operator_token(c, preprocessing_op_or_punc::ELLIPSIS)) {
            if (count)
                vbuffer_string_append(list.get(), ", ");
            vbuffer_string_append(list.get(), "...");
            count++;
            return true;
        }
        if (is_kind(c, syntax_kind::parameter_declaration_clause) ||
            is_kind(c, syntax_kind::parameter_declaration_list)) {
            const syntax_node &node = tree[c.index()];
            for (uint32_t i = 0; i < node.child_count; i++)
                if (!parameter_types(tree.children(c.index())[i], list, count))
                    return false;
            return true;
        }
        if (!is_kind(c, syntax_kind::parameter_declaration))
            return false;
        /* attribute-specifier-seq_opt decl-specifier-seq declarator_opt,
           then perhaps = initializer-clause */
        const syntax_node &node = tree[c.index()];
        owned_buffer type;
        specifiers s;
        declared d;
        uint32_t i = 0;
        while (i < node.child_count &&
               is_attribute(tree.children(c.index())[i]))
            i++;
        if (i == node.child_count ||
            !is_kind(tree.children(c.index())[i],
                     syntax_kind::decl_specifier_seq) ||
            !specified_type(tree.children(c.index())[i].index(), type, s))
            return false;
        i++;
        if (i < node.child_count &&
            !is_operator_token(tree.children(c.index())[i],
                               preprocessing_op_or_punc::EQUAL) &&
            !declared_type(tree.children(c.index())[i], type, s, d))
            return false;
        /* a placeholder in a parameter's type makes the function a template
           ([dcl.fct]/18, an abbreviated function template) */
        if (s.placeholder)
            return false;
        if (d.has_id && d.id_end == d.id_first + 1)
            parameter_names.append(tokens.identifier(d.id_first));
        if (count)
            vbuffer_string_append(list.get(), ", ");
        vbuffer_append(list.get(), type.data(), type.size());
        count++;
        return true;
    }

    /*
     * [dcl.array]/1: "the type of the declarator-id in D is
     * derived-declarator-type-list array of N T"; /2: "... array of unknown
     * bound of T".  The bound is written as the tokens of its
     * constant-expression.
     */
    void apply_array(const syntax_child *bound, owned_buffer &type, declared &d)
    {
        if (!bound) {
            wrap(type, "::vcc_claim::array_of_unknown_bound<", ">");
            d.last = outermost::array_of_unknown_bound;
            return;
        }
        wrap(type, "::vcc_claim::array_of<", ", (");
        if (bound->is_token()) {
            verbatim(bound->index(), bound->index() + 1, type.get());
        } else {
            const syntax_node &e = tree[bound->index()];
            verbatim(e.first_token, e.end_token, type.get());
        }
        vbuffer_string_append(type.get(), ")>");
        d.last = outermost::array;
    }

    /*
     * [dcl.meaning]/3: "Thus, a declaration of a particular identifier has
     * the form T D" and /5: "In a declaration T D where D is an unadorned
     * identifier the type of this identifier is T."  type is T on entry and
     * the type of D's declarator-id on return; each form of D is the
     * subclause's "In a declaration T D where D has the form ...": the type
     * D gives its identifier is the type D1 gives it in a declaration whose
     * T has the form's constructor applied.
     *
     * In noptr-declarator's left-recursive productions the last suffix is
     * the outermost D, so the suffixes apply from the last to the first:
     * in int a[2][3], D is D1[3] with D1 = a[2], and a is an array of 2
     * arrays of 3 int.
     */
    bool declared_type(syntax_child declarator, owned_buffer &type,
                       specifiers &s, declared &d)
    {
        if (declarator.is_token()) {
            /* declarator-id: an identifier */
            if (tokens[declarator.index()].kind != token_kind::identifier)
                return false;
            d.has_id = true;
            d.id_first = declarator.index();
            d.id_end = declarator.index() + 1;
            return true;
        }
        node_index n = declarator.index();
        const syntax_node &node = tree[n];
        const syntax_child *children = tree.children(n);
        switch (node.kind) {
        case syntax_kind::qualified_id:
            d.has_id = true;
            d.id_first = node.first_token;
            d.id_end = node.end_token;
            return true;
        case syntax_kind::ptr_declarator:
        case syntax_kind::ptr_abstract_declarator:
            /* ptr-operator ptr-declarator */
            if (!node.child_count ||
                !is_kind(children[0], syntax_kind::ptr_operator) ||
                !apply_ptr_operator(children[0].index(), type, d))
                return false;
            if (node.child_count == 1)
                return true;
            return declared_type(children[1], type, s, d);
        case syntax_kind::parameters_and_qualifiers:
            /* an abstract declarator that is only its parameters */
            return apply_function(n, type, d);
        case syntax_kind::declarator:
        case syntax_kind::abstract_declarator: {
            /* [dcl.fct]/2: noptr-declarator parameters-and-qualifiers
               trailing-return-type: "T shall be the single type-specifier
               auto", and the function returns "the type specified by the
               trailing-return-type" */
            if (node.child_count != 2 ||
                !is_kind(children[1], syntax_kind::trailing_return_type) ||
                !s.placeholder)
                return false;
            const syntax_node &trailing = tree[children[1].index()];
            if (trailing.child_count != 2 ||
                !type_of_type_id(tree.children(children[1].index())[1].index(),
                                 type))
                return false;
            s.placeholder = false;
            return declared_type(children[0], type, s, d);
        }
        case syntax_kind::noptr_declarator:
        case syntax_kind::noptr_abstract_declarator:
            break;
        default:
            /* ~ type-name, or what a later step parses */
            return false;
        }
        /* the head: ( ptr-declarator ) or the declarator-id, or none in an
           abstract declarator; then the suffixes */
        uint32_t i = 0;
        bool has_head = false;
        syntax_child head = {};
        if (node.child_count >= 3 &&
            is_operator_token(children[0],
                              preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            if (!is_operator_token(children[2],
                                   preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return false;
            has_head = true;
            head = children[1];
            i = 3;
        } else if (node.child_count &&
                   ((children[0].is_token() &&
                     tokens[children[0].index()].kind ==
                         token_kind::identifier) ||
                    is_kind(children[0], syntax_kind::qualified_id) ||
                    is_kind(children[0], syntax_kind::unqualified_id))) {
            has_head = true;
            head = children[0];
            i = 1;
        }
        struct suffix {
            bool function;
            /* a function's parameters-and-qualifiers; an array's bound in
               the node's children, or none */
            uint32_t child;
            bool has_bound;
        };
        owned_array<suffix> suffixes;
        while (i < node.child_count) {
            if (is_attribute(children[i])) {
                i++;
            } else if (is_kind(children[i],
                               syntax_kind::parameters_and_qualifiers)) {
                suffixes.append({true, i, false});
                i++;
            } else if (is_operator_token(
                           children[i],
                           preprocessing_op_or_punc::LEFT_BRACKET)) {
                bool has_bound =
                    i + 1 < node.child_count &&
                    !is_operator_token(children[i + 1],
                                       preprocessing_op_or_punc::RIGHT_BRACKET);
                suffixes.append({false, i + 1, has_bound});
                i += 2;
                if (has_bound)
                    i++;
            } else {
                return false;
            }
        }
        for (size_t k = suffixes.size(); k-- > 0;) {
            const suffix &x = suffixes[k];
            if (x.function) {
                if (!apply_function(children[x.child].index(), type, d))
                    return false;
            } else if (x.has_bound) {
                apply_array(&children[x.child], type, d);
            } else {
                apply_array(nullptr, type, d);
            }
        }
        if (!has_head)
            return true;
        return declared_type(head, type, s, d);
    }

    /* [dcl.name]/1: a type-id is "syntactically a declaration for a variable
       or function of that type that omits the name of the entity": its type
       is found as a declaration's is */
    bool type_of_type_id(node_index type_id, owned_buffer &type)
    {
        const syntax_node &node = tree[type_id];
        if (node.kind != syntax_kind::type_id &&
            node.kind != syntax_kind::defining_type_id)
            return false;
        if (!node.child_count || tree.children(type_id)[0].is_token())
            return false;
        specifiers s;
        declared d;
        if (!specified_type(tree.children(type_id)[0].index(), type, s))
            return false;
        if (node.child_count > 1 &&
            !declared_type(tree.children(type_id)[1], type, s, d))
            return false;
        return !s.placeholder;
    }

    /* FILE, the lines of the declaration that say the type, and where a
       static_assert can stand: after anchor, the declaration of a
       declaration-seq that the declaration is or is in */
    void write_place(node_index anchor, node_index declaration)
    {
        const syntax_node &d = tree[declaration];
        token_index last = d.end_token - 1;
        for (uint32_t i = 0; i < d.child_count; i++) {
            syntax_child c = tree.children(declaration)[i];
            if (is_kind(c, syntax_kind::function_body) &&
                tree[c.index()].first_token > d.first_token)
                last = tree[c.index()].first_token - 1;
        }
        const syntax_node &a = tree[anchor];
        source_position end = tokens.position(a.end_token - 1);
        uint32_t extent = tokens[a.end_token - 1].preprocessing.extent;
        vbuffer_format_append(out, "%s\t%u-%u\t%u:%u\t", end.file_name,
                              tokens.position(d.first_token).line,
                              tokens.position(last).line, end.line,
                              end.column + extent);
    }

    /* whether the declaration has an attribute-scoped-token or an
       attribute-using-prefix: an attribute that is not the standard's */
    bool has_vendor_attribute(node_index declaration) const
    {
        owned_array<node_index> work;
        work.append(declaration);
        while (!work.empty()) {
            node_index n = work.back();
            work.resize(work.size() - 1);
            const syntax_node &s = tree[n];
            if (s.kind == syntax_kind::attribute_scoped_token ||
                s.kind == syntax_kind::attribute_using_prefix)
                return true;
            for (uint32_t i = 0; i < s.child_count; i++)
                if (!tree.children(n)[i].is_token())
                    work.append(tree.children(n)[i].index());
        }
        return false;
    }

    bool names_a_parameter() const
    {
        for (uint32_t name : mentioned_names)
            for (uint32_t parameter : parameter_names)
                if (name == parameter)
                    return true;
        return false;
    }

    /* the claim for one declarator of a declaration whose decl-specifier-seq
       is seq */
    void claim(node_index anchor, node_index declaration, node_index seq,
               syntax_child declarator)
    {
        owned_buffer type;
        specifiers s;
        declared d;
        parameter_names.resize(0);
        mentioned_names.resize(0);
        if (!specified_type(seq, type, s) ||
            !declared_type(declarator, type, s, d) || !d.has_id ||
            s.placeholder || names_a_parameter())
            return;
        /* [dcl.constexpr]/10: "A constexpr specifier used in an object
           declaration declares the object as const." */
        if (s.constexpr_ && d.last != outermost::function)
            wrap(type, "::vcc_claim::const_<", ">");
        write_place(anchor, declaration);
        if (s.typedef_)
            vbuffer_string_append(out, "type");
        else if (d.last == outermost::function)
            vbuffer_string_append(out, "function");
        else
            vbuffer_string_append(out, "object");
        if (d.last == outermost::array_of_unknown_bound)
            vbuffer_string_append(out, "[]");
        vbuffer_append(out, "\t", 1);
        for (token_index t = d.id_first; t < d.id_end; t++)
            tokens.spelling(t, out);
        vbuffer_append(out, "\t", 1);
        vbuffer_append(out, type.data(), type.size());
        vbuffer_append(out, "\n", 1);
    }

    /* simple-declaration and function-definition: the decl-specifier-seq,
       then the declarators ([dcl.decl]/3: "Each init-declarator in a
       declaration is analyzed separately as if it was in a declaration by
       itself") */
    void declarators(node_index anchor, node_index declaration)
    {
        const syntax_node &node = tree[declaration];
        const syntax_child *children = tree.children(declaration);
        uint32_t i = 0;
        while (i < node.child_count && is_attribute(children[i]))
            i++;
        if (i == node.child_count ||
            !is_kind(children[i], syntax_kind::decl_specifier_seq))
            return;
        node_index seq = children[i].index();
        for (i++; i < node.child_count; i++) {
            syntax_child c = children[i];
            if (is_kind(c, syntax_kind::function_body) ||
                is_operator_token(c, preprocessing_op_or_punc::SEMICOLON))
                break;
            if (is_kind(c, syntax_kind::init_declarator_list)) {
                const syntax_node &list = tree[c.index()];
                for (uint32_t k = 0; k < list.child_count; k++) {
                    syntax_child item = tree.children(c.index())[k];
                    if (is_operator_token(item,
                                          preprocessing_op_or_punc::COMMA))
                        continue;
                    claim(anchor, declaration, seq, declarator_of(item));
                }
            } else {
                claim(anchor, declaration, seq, declarator_of(c));
            }
        }
    }

    /* init-declarator: declarator initializer_opt; the declarator alone
       passes through */
    syntax_child declarator_of(syntax_child item) const
    {
        if (is_kind(item, syntax_kind::init_declarator) &&
            tree[item.index()].child_count)
            return tree.children(item.index())[0];
        return item;
    }

    /* [dcl.typedef]/2: using identifier = defining-type-id: "the identifier
       following the using keyword becomes a typedef-name" for the type the
       defining-type-id denotes */
    void alias(node_index anchor, node_index declaration)
    {
        const syntax_node &node = tree[declaration];
        const syntax_child *children = tree.children(declaration);
        if (node.child_count < 4 || !children[1].is_token())
            return;
        owned_buffer type;
        parameter_names.resize(0);
        mentioned_names.resize(0);
        for (uint32_t i = 2; i < node.child_count; i++) {
            if (!is_kind(children[i], syntax_kind::defining_type_id))
                continue;
            const syntax_node &id = tree[children[i].index()];
            specifiers s;
            declared d;
            if (!id.child_count ||
                tree.children(children[i].index())[0].is_token() ||
                !specified_type(tree.children(children[i].index())[0].index(),
                                type, s))
                return;
            if (id.child_count > 1 &&
                !declared_type(tree.children(children[i].index())[1], type, s,
                               d))
                return;
            if (s.placeholder || names_a_parameter())
                return;
            write_place(anchor, declaration);
            vbuffer_string_append(out, "type");
            if (d.last == outermost::array_of_unknown_bound)
                vbuffer_string_append(out, "[]");
            vbuffer_append(out, "\t", 1);
            tokens.spelling(children[1].index(), out);
            vbuffer_append(out, "\t", 1);
            vbuffer_append(out, type.data(), type.size());
            vbuffer_append(out, "\n", 1);
            return;
        }
    }

    /* the declarations under n; anchor is the declaration of a
       declaration-seq they are in */
    void declarations(node_index n, node_index anchor, bool in_sequence)
    {
        const syntax_node &s = tree[n];
        if (in_sequence) {
            anchor = n;
            for (token_index t = s.first_token; t < s.end_token; t++)
                if (tokens[t].preprocessing.expansion)
                    return;
        }
        if (anchor) {
            switch (s.kind) {
            case syntax_kind::simple_declaration:
            case syntax_kind::function_definition:
                if (!has_vendor_attribute(n))
                    declarators(anchor, n);
                return;
            case syntax_kind::alias_declaration:
                if (!has_vendor_attribute(n))
                    alias(anchor, n);
                return;
            default:
                break;
            }
        }
        for (uint32_t i = 0; i < s.child_count; i++) {
            syntax_child c = tree.children(n)[i];
            if (!c.is_token())
                declarations(c.index(), anchor,
                             s.kind == syntax_kind::declaration_seq);
        }
    }
};
