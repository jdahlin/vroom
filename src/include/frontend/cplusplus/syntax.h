/*
 * syntax.h: typed views of the syntax tree (parse.h's syntax_tree).
 *
 * The tree stores every node the same way: a kind, and its children in the
 * order of the alternative of the grammar that matched.  A view decodes
 * that order in one place, so that the code reading a nonterminal names
 * its parts after the standard's grammar instead of indexing children:
 *
 *     auto body = syntax(n).function_definition().body();
 *     if (body.deleted()) ...
 *
 * A view is a handle (the tree's context and a node index) built where it
 * is used; nothing is stored in the tree or allocated.  SyntaxRef is the
 * untyped handle, with one accessor per nonterminal that has a view
 * (SYNTAX_VIEW below) which gives that view, or an empty one when the node
 * is of another kind.  An empty view answers every question with "none"
 * (no child, false), so a chain needs no test between its steps.
 */
#ifndef VCC_CPLUSPLUS_SYNTAX_H
#define VCC_CPLUSPLUS_SYNTAX_H

#include "frontend/cplusplus/parse.h"

namespace cplusplus
{

/* what a view reads: the tree, its tokens, and the parser's keyword
   table (parser::keyword_of) */
struct SyntaxContext {
    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
};

/* the nonterminals with a view: SYNTAX_VIEW(name, Type): the view's type
   is Type, the nonterminal's name in CamelCase, and SyntaxRef::name()
   gives it */
#define SYNTAX_VIEWS(SYNTAX_VIEW)                                              \
    SYNTAX_VIEW(declaration_seq, DeclarationSeq)                               \
    SYNTAX_VIEW(declaration, Declaration)                                      \
    SYNTAX_VIEW(function_definition, FunctionDefinition)                       \
    SYNTAX_VIEW(template_declaration, TemplateDeclaration)                     \
    SYNTAX_VIEW(concept_definition, ConceptDefinition)                         \
    SYNTAX_VIEW(alias_declaration, AliasDeclaration)                           \
    SYNTAX_VIEW(simple_declaration, SimpleDeclaration)                         \
    SYNTAX_VIEW(function_body, FunctionBody)

#define SYNTAX_VIEW_DECLARE(name, Type) struct Type;
SYNTAX_VIEWS(SYNTAX_VIEW_DECLARE)
#undef SYNTAX_VIEW_DECLARE

class SyntaxRef
{
  public:
    SyntaxRef() = default;
    SyntaxRef(const SyntaxContext &cx, syntax_child c) : cx(&cx)
    {
        if (!c.is_token())
            n = c.index();
    }
    SyntaxRef(const SyntaxContext &cx, node_index n) : cx(&cx), n(n)
    {
    }

    /* false for an empty view, or a token */
    explicit operator bool() const
    {
        return n != 0;
    }
    /* the node (0 for an empty view): a view goes wherever a node_index
       does, so a function can take a view before its callees do */
    operator node_index() const
    {
        return n;
    }
    node_index index() const
    {
        return n;
    }
    syntax_child child() const
    {
        if (!n)
            return {};
        return syntax_child::node(n);
    }
    syntax_kind kind() const
    {
        return node().kind;
    }
    uint32_t size() const
    {
        if (!n)
            return 0;
        return node().child_count;
    }
    /* the i-th child; {} past the end */
    syntax_child operator[](uint32_t i) const
    {
        if (i >= size())
            return {};
        return cx->tree.children(n)[i];
    }
    bool is(syntax_kind k) const
    {
        return n && node().kind == k;
    }
    /* whether child c is a node of kind k */
    bool is(syntax_child c, syntax_kind k) const
    {
        return !c.is_token() && c.index() && cx->tree[c.index()].kind == k;
    }
    /* whether child c is the keyword k */
    bool is_keyword(syntax_child c, standard_keyword k) const
    {
        return c.is_token() && cx->p.keyword_of(cx->tokens[c.index()]) == k;
    }
    bool is_punctuator(syntax_child c, preprocessing_op_or_punc op) const
    {
        if (!c.is_token())
            return false;
        const token &t = cx->tokens[c.index()];
        return t.kind == token_kind::operator_or_punctuator &&
               preprocessing_op_or_punc_primary(t.op_or_punc) == op;
    }

#define SYNTAX_VIEW_ACCESSOR(name, Type) Type name() const;
    SYNTAX_VIEWS(SYNTAX_VIEW_ACCESSOR)
#undef SYNTAX_VIEW_ACCESSOR

  protected:
    const syntax_node &node() const
    {
        return cx->tree[n];
    }
    const SyntaxContext *cx = nullptr;
    node_index n = 0;
};

/*
 * [dcl.fct.def.general]/1:
 *
 *     function-body:
 *         ctor-initializer_opt compound-statement
 *         function-try-block
 *         = default ;
 *         = delete ;
 *
 * with [dcl.fct.def.delete]'s = delete ( string-literal ) ; and, in the
 * first pass over a member-specification, a body kept as its tokens
 * (unparsed).  The parser makes a function-try-block its own node, which
 * stands in a function-definition in place of a function-body; it is a
 * FunctionBody too.
 */
struct FunctionBody : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::function_body ||
               k == syntax_kind::function_try_block;
    }
    bool function_try_block() const
    {
        return is(syntax_kind::function_try_block);
    }
    bool unparsed() const
    {
        return is(syntax_kind::function_body) && size() == 1 &&
               is((*this)[0], syntax_kind::unparsed);
    }
    bool defaulted() const
    {
        return is(syntax_kind::function_body) && size() == 3 &&
               is_punctuator((*this)[0], preprocessing_op_or_punc::EQUAL) &&
               is_keyword((*this)[1], standard_keyword::default_);
    }
    bool deleted() const
    {
        return is(syntax_kind::function_body) && size() >= 3 &&
               is_punctuator((*this)[0], preprocessing_op_or_punc::EQUAL) &&
               is_keyword((*this)[1], standard_keyword::delete_);
    }
    /* ctor-initializer_opt compound-statement: the compound-statement
       ({} for another form) and the ctor-initializer ({} if none) */
    syntax_child compound_statement() const
    {
        if (!is(syntax_kind::function_body))
            return {};
        syntax_child last = (*this)[size() - 1];
        if (!is(last, syntax_kind::compound_statement) || size() > 2 ||
            (size() == 2 && !is((*this)[0], syntax_kind::ctor_initializer)))
            return {};
        return last;
    }
    syntax_child ctor_initializer() const
    {
        if (!compound_statement().bits || size() != 2)
            return {};
        return (*this)[0];
    }
    /* a compound-statement alone: what analysis handles for any function
       (a ctor-initializer is a constructor's) */
    bool plain() const
    {
        return compound_statement().bits && size() == 1;
    }
};

/*
 * [dcl.fct.def.general]/1:
 *
 *     function-definition:
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator
 *             virt-specifier-seq_opt function-body
 *         attribute-specifier-seq_opt decl-specifier-seq_opt declarator
 *             requires-clause function-body
 *
 * ([ext:gnu_attributes] may come first too.)  The function-body is the
 * last child.
 */
struct FunctionDefinition : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::function_definition;
    }
    FunctionBody body() const
    {
        if (!*this)
            return {};
        SyntaxRef last(*cx, (*this)[size() - 1]);
        return last.function_body();
    }
    /* the decl-specifier-seq ({} without one) */
    syntax_child decl_specifier_seq() const
    {
        for (uint32_t i = 0; i < size(); i++)
            if (is((*this)[i], syntax_kind::decl_specifier_seq))
                return (*this)[i];
        return {};
    }
    /* the declarator after the decl-specifier-seq ({} without one; a
       declarator that is only its identifier is that token) */
    syntax_child declarator() const
    {
        for (uint32_t i = 0; i + 1 < size(); i++)
            if (is((*this)[i], syntax_kind::decl_specifier_seq))
                return (*this)[i + 1];
        return {};
    }
    /* decl-specifier-seq declarator function-body and nothing after the
       decl-specifier-seq: no virt-specifier-seq, requires-clause or
       function-try-block (what comes before it is the caller's) */
    bool plain() const
    {
        return size() >= 3 &&
               is((*this)[size() - 3], syntax_kind::decl_specifier_seq) &&
               is((*this)[size() - 1], syntax_kind::function_body);
    }
};

/* the node children of a node, each as a view of type T (tokens are
   skipped): for (auto d : seq.declarations()) */
template <class T> class SyntaxRange
{
  public:
    class iterator
    {
      public:
        iterator(const SyntaxContext *cx, const syntax_child *at,
                 const syntax_child *end)
            : cx(cx), at(at), end(end)
        {
            skip();
        }
        T operator*() const
        {
            return T(*cx, at->index());
        }
        iterator &operator++()
        {
            at++;
            skip();
            return *this;
        }
        bool operator!=(const iterator &o) const
        {
            return at != o.at;
        }

      private:
        void skip()
        {
            while (at != end && (at->is_token() || !at->index()))
                at++;
        }
        const SyntaxContext *cx;
        const syntax_child *at;
        const syntax_child *end;
    };
    SyntaxRange() = default;
    SyntaxRange(const SyntaxContext &cx, const syntax_child *first,
                uint32_t count)
        : cx(&cx), first(first), count(count)
    {
    }
    iterator begin() const
    {
        return iterator(cx, first, first + count);
    }
    iterator end() const
    {
        return iterator(cx, first + count, first + count);
    }

  private:
    const SyntaxContext *cx = nullptr;
    const syntax_child *first = nullptr;
    uint32_t count = 0;
};

/* one callable from several lambdas, for visit:
   d.visit(Overloaded{[&](FunctionDefinition f) {...},
                      [&](Declaration other) {...}}) */
template <class... F> struct Overloaded : F... {
    using F::operator()...;
};
template <class... F> Overloaded(F...) -> Overloaded<F...>;

/* a member function of self as a callable for visit */
template <class T, class R, class A>
auto bind_member(T *self, R (T::*member)(A))
{
    return [self, member](A a) { return (self->*member)(a); };
}

/*
 * [temp.pre]/1:
 *
 *     template-declaration:
 *         template-head declaration
 *         template-head concept-definition
 *
 * The declaration (or concept-definition) is the last child.
 */
struct TemplateDeclaration : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::template_declaration;
    }
    /* the declaration after the template-head ({} if there is none) */
    Declaration declaration() const;
};

/* [temp.concept]/1: concept-definition: concept concept-name
   attribute-specifier-seq_opt = constraint-expression ; */
struct ConceptDefinition : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::concept_definition;
    }
};

/* [dcl.pre]/1: alias-declaration: using identifier
   attribute-specifier-seq_opt = defining-type-id ; */
struct AliasDeclaration : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::alias_declaration;
    }
};

/*
 * [dcl.pre]/1:
 *
 *     simple-declaration:
 *         decl-specifier-seq init-declarator-list_opt ;
 *         attribute-specifier-seq decl-specifier-seq init-declarator-list ;
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt
 *             [ identifier-list ] initializer ;
 */
struct SimpleDeclaration : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::simple_declaration;
    }
    /* the init-declarator-list ({} without one; one declarator that is
       not in a list is not one) */
    syntax_child init_declarator_list() const
    {
        for (uint32_t i = 0; i < size(); i++)
            if (is((*this)[i], syntax_kind::init_declarator_list))
                return (*this)[i];
        return {};
    }
};

/*
 * [dcl.pre]/1:
 *
 *     declaration-seq:
 *         declaration declaration-seq_opt
 */
struct DeclarationSeq : SyntaxRef {
    using SyntaxRef::SyntaxRef;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::declaration_seq;
    }
    SyntaxRange<Declaration> declarations() const;
};

/* the alternatives of a declaration that have a view, for visit */
#define DECLARATION_ALTERNATIVES(ALTERNATIVE)                                  \
    ALTERNATIVE(function_definition, FunctionDefinition)                       \
    ALTERNATIVE(template_declaration, TemplateDeclaration)                     \
    ALTERNATIVE(concept_definition, ConceptDefinition)                         \
    ALTERNATIVE(alias_declaration, AliasDeclaration)                           \
    ALTERNATIVE(simple_declaration, SimpleDeclaration)

/*
 * [dcl.pre]/1: a declaration is one of its alternatives
 * (block-declaration, function-definition, template-declaration, ...).
 * visit calls f with the view of the alternative it is, for the
 * alternatives that have one (DECLARATION_ALTERNATIVES), and with the
 * Declaration itself for the others.  Each alternative's view converts to
 * a Declaration, so a case taking a Declaration is the visitor's default:
 * it gets the alternatives the visitor has no case of their own for.  A
 * visitor without a default needs a case for every alternative, or it
 * does not compile.
 */
struct Declaration : SyntaxRef {
    using SyntaxRef::SyntaxRef;
#define DECLARATION_FROM(name, Type)                                           \
    Declaration(Type alternative) : SyntaxRef(alternative)                     \
    {                                                                          \
    }
    DECLARATION_ALTERNATIVES(DECLARATION_FROM)
#undef DECLARATION_FROM
    Declaration() = default;
    static bool accepts(syntax_kind)
    {
        return true;
    }
    /* the same with member functions of self, one per alternative:
       d.visit(this, &analysis::function_definition, &analysis::other) */
    template <class T, class... M>
    decltype(auto) visit(T *self, M... members) const
    {
        return visit(Overloaded{bind_member(self, members)...});
    }
    template <class F> decltype(auto) visit(F &&f) const
    {
        switch (kind()) {
#define DECLARATION_CASE(name, Type)                                           \
    case syntax_kind::name:                                                    \
        return f(Type(*cx, n));
            DECLARATION_ALTERNATIVES(DECLARATION_CASE)
#undef DECLARATION_CASE
        default:
            return f(*this);
        }
    }
};

inline Declaration TemplateDeclaration::declaration() const
{
    if (!*this)
        return {};
    return Declaration(*cx, (*this)[size() - 1]);
}

inline SyntaxRange<Declaration> DeclarationSeq::declarations() const
{
    if (!*this)
        return {};
    return SyntaxRange<Declaration>(*cx, cx->tree.children(n), size());
}

#define SYNTAX_VIEW_ACCESSOR(name, Type)                                       \
    inline Type SyntaxRef::name() const                                        \
    {                                                                          \
        if (!n || !Type::accepts(node().kind))                                 \
            return {};                                                         \
        return Type(*cx, n);                                                   \
    }
SYNTAX_VIEWS(SYNTAX_VIEW_ACCESSOR)
#undef SYNTAX_VIEW_ACCESSOR

} // namespace cplusplus

#endif
