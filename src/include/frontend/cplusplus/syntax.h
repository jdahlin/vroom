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
 * is used; nothing is stored in the tree or allocated.  syntax_ref is the
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
struct syntax_context {
    const syntax_tree &tree;
    token_stream &tokens;
    const parser &p;
};

/* the nonterminals with a view: SYNTAX_VIEW(name), the view's type is
   name_syntax and syntax_ref::name() gives it */
#define SYNTAX_VIEWS(SYNTAX_VIEW)                                              \
    SYNTAX_VIEW(function_definition)                                           \
    SYNTAX_VIEW(function_body)

#define SYNTAX_VIEW_DECLARE(name) struct name##_syntax;
SYNTAX_VIEWS(SYNTAX_VIEW_DECLARE)
#undef SYNTAX_VIEW_DECLARE

class syntax_ref
{
  public:
    syntax_ref() = default;
    syntax_ref(const syntax_context &cx, syntax_child c) : cx(&cx)
    {
        if (!c.is_token())
            n = c.index();
    }
    syntax_ref(const syntax_context &cx, node_index n) : cx(&cx), n(n)
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

#define SYNTAX_VIEW_ACCESSOR(name) name##_syntax name() const;
    SYNTAX_VIEWS(SYNTAX_VIEW_ACCESSOR)
#undef SYNTAX_VIEW_ACCESSOR

  protected:
    const syntax_node &node() const
    {
        return cx->tree[n];
    }
    const syntax_context *cx = nullptr;
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
 * function_body_syntax too.
 */
struct function_body_syntax : syntax_ref {
    using syntax_ref::syntax_ref;
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
struct function_definition_syntax : syntax_ref {
    using syntax_ref::syntax_ref;
    static bool accepts(syntax_kind k)
    {
        return k == syntax_kind::function_definition;
    }
    function_body_syntax body() const
    {
        if (!*this)
            return {};
        syntax_ref last(*cx, (*this)[size() - 1]);
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

#define SYNTAX_VIEW_ACCESSOR(name)                                             \
    inline name##_syntax syntax_ref::name() const                              \
    {                                                                          \
        if (!n || !name##_syntax::accepts(node().kind))                        \
            return {};                                                         \
        return name##_syntax(*cx, n);                                          \
    }
SYNTAX_VIEWS(SYNTAX_VIEW_ACCESSOR)
#undef SYNTAX_VIEW_ACCESSOR

} // namespace cplusplus

#endif
