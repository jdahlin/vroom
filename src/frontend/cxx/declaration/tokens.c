/* C++ declarations: lookahead, attributes, token scans */
#include "declaration.h"

/* does fn parse from here?  Always rewinds: a tentative parse decides, the real one follows */
VPRIV bool cxx_lookahead(cxx *c, void (*fn)(cxx *))
{
	cxx_mark m;
	jmp_buf jb;
	volatile bool ok = false;

	cxx_mark_set(c, &m);
	c->tentative++;
	c->fail = &jb;
	if (!setjmp(jb)) {
		fn(c);
		ok = true;
	}
	c->tentative--;
	cxx_mark_rewind(c, &m);
	return ok;
}

/* ---- attributes -------------------------------------------------------------------------- */

/* an attribute's tokens [b, e): one that changes a layout? (counted: a class with one is not laid out yet) */
/* alignas(T), alignas(N), aligned(N) at '(': the alignment (0: not known here: a template's pattern, a
   value not constant); the position after its ')' */
VPRIV uint32_t cxx_alignment_argument(cxx *c)
{
	uint32_t v = 0, end;

	{
		uint32_t save = c->pos;

		cxx_skip_balanced(c);
		end = c->pos;
		c->pos = save;
	}
	if (c->template_depth || c->tentative) {
		c->pos = end;
		return 0;
	}
	if (cxx_paren_type_id_follows(c)) {
		uint64_t size;
		uint32_t align;

		cxx_advance(c);
		cxx_type_id_parse(c);
		if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN) && cxx_type_size_known(c, c->type_out, &size, &align))
			v = align;
	} else {
		uint8_t gen = c->gen;

		cxx_advance(c);
		c->unevaluated++, c->gen = 0;
		cxx_conditional_expression_parse(c);
		c->unevaluated--, c->gen = gen;
		if (c->econst && c->evalue > 0 && c->evalue <= (1 << 20) && !(c->evalue & (c->evalue - 1)) && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
			v = (uint32_t)c->evalue;
	}
	c->pos = end;
	return v;
}

VPRIV void cxx_attribute_scan(cxx *c, uint32_t b, uint32_t e)
{
	static const char *const names[] = { "no_unique_address", "__no_unique_address__", "packed", "__packed__",
					     "aligned", "__aligned__", "ms_struct", "__ms_struct__" };

	for (uint32_t i = b; i < e; i++) {
		const cxx_token *t = &c->tok[i];

		if (t->kind != LEX_TOKEN_IDENTIFIER)
			continue;
		for (size_t k = 0; k < sizeof names / sizeof *names; k++)
			if (t->len == strlen(names[k]) && !memcmp(t->s, names[k], t->len)) {
				/* no_unique_address on a member and packed on a class are laid out (types.c); the
				   others are counted: a class with one is not laid out yet */
				if (k < 2)
					c->attr_nua = 1;
				else if (k < 4)
					c->attr_packed = 1;
				else if (k < 6) {
					/* aligned(N) (aligned alone: the largest, 16): its value, else counted as below */
					uint32_t v = 16;

					if (i + 1 < e && c->tok[i + 1].kind == LEX_TOKEN_LEFT_PAREN) {
						uint32_t save = c->pos;

						c->pos = i + 1;
						v = cxx_alignment_argument(c);
						c->pos = save;
					}
					if (v)
						c->attr_align = v > c->attr_align ? v : c->attr_align;
					else
						c->layout_attributes++;
				} else
					c->layout_attributes++;
				break;
			}
	}
}

/* is the keyword among tokens [b, e) */
VPRIV bool cxx_tokens_have_kw(cxx *c, uint32_t b, uint32_t e, int kw)
{
	for (uint32_t i = b; i < e && i < c->ntok; i++)
		if (c->tok[i].kw == kw || (kw == CXX_KW_thread_local && (c->tok[i].kw == CXX_KW___thread || c->tok[i].kw == CXX_KW__Thread_local)))
			return true;
	return false;
}

VPRIV void cxx_extern_member_record(cxx *c);

/* does the identifier w appear in tokens [b, e) */
VPRIV bool cxx_word_scan(cxx *c, uint32_t b, uint32_t e, const char *w)
{
	size_t n = strlen(w);

	for (uint32_t i = b; i < e && i < c->ntok; i++)
		if (c->tok[i].kind == LEX_TOKEN_IDENTIFIER && c->tok[i].len == n && !memcmp(c->tok[i].s, w, n))
			return true;
	return false;
}

/* __attribute__((__abi_tag__("x"))) or [[gnu::abi_tag("x")]] in tokens [b, e): the tag (interned; 0: none) */
VPRIV uint32_t cxx_abi_tag_scan(cxx *c, uint32_t b, uint32_t e)
{
	for (uint32_t i = b; i + 2 < e && i + 2 < c->ntok; i++) {
		const cxx_token *t = &c->tok[i];

		if (t->kind == LEX_TOKEN_IDENTIFIER && ((t->len == 7 && !memcmp(t->s, "abi_tag", 7)) || (t->len == 11 && !memcmp(t->s, "__abi_tag__", 11))) &&
		    c->tok[i + 1].kind == LEX_TOKEN_LEFT_PAREN && c->tok[i + 2].kind == LEX_TOKEN_STRING && c->tok[i + 2].len > 2)
			return vintern_copy(c->tok[i + 2].s + 1, c->tok[i + 2].len - 2);
	}
	return 0;
}

/* at a class key: is the class unnamed (struct {, struct __attribute__((packed)) {, struct [[a]] {)? */
VPRIV bool cxx_class_key_unnamed(cxx *c)
{
	uint32_t i = c->pos + 1;

	while (i < c->ntok) {
		int depth = 0;

		if (c->tok[i].kw == CXX_KW___attribute__ || c->tok[i].kw == CXX_KW___attribute || c->tok[i].kw == CXX_KW_alignas)
			i++;
		else if (!(c->tok[i].kind == LEX_TOKEN_LEFT_SQUARE && i + 1 < c->ntok && c->tok[i + 1].kind == LEX_TOKEN_LEFT_SQUARE))
			break;
		if (c->tok[i].kind != LEX_TOKEN_LEFT_PAREN && c->tok[i].kind != LEX_TOKEN_LEFT_SQUARE)
			return false;
		for (; i < c->ntok; i++) {
			int k = c->tok[i].kind;

			if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE)
				depth++;
			else if ((k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE) && !--depth) {
				i++;
				break;
			}
		}
	}
	return i < c->ntok && c->tok[i].kind == LEX_TOKEN_LEFT_BRACE;
}

VPRIV void cxx_attributes_skip(cxx *c)
{
	for (;;) {
		if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE) {
			uint32_t b = c->pos;

			cxx_skip_balanced(c);
			cxx_attribute_scan(c, b, c->pos);
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___attribute__) || cxx_is_kw(c, CXX_KW___attribute) || cxx_is_kw(c, CXX_KW___declspec) ||
		    cxx_is_kw(c, CXX_KW_alignas) || cxx_is_kw(c, CXX_KW__Alignas)) {
			uint32_t b = c->pos;
			bool align = cxx_is_kw(c, CXX_KW_alignas) || cxx_is_kw(c, CXX_KW__Alignas);

			cxx_advance(c);
			if (!cxx_is(c, LEX_TOKEN_LEFT_PAREN))
				cxx_fail(c, "expected '(' after attribute");
			if (align) {
				uint32_t v = cxx_alignment_argument(c);	/* alignas(T), alignas(N) */

				if (v)
					c->attr_align = v > c->attr_align ? v : c->attr_align;
				else
					c->layout_attributes++;
			} else {
				cxx_skip_balanced(c);
				cxx_attribute_scan(c, b, c->pos);
			}
			continue;
		}
		if (cxx_is_kw(c, CXX_KW___extension__) || cxx_is_kw(c, CXX_KW__Nullable) || cxx_is_kw(c, CXX_KW__Nonnull) ||
		    cxx_is_kw(c, CXX_KW__Null_unspecified)) {
			cxx_advance(c);
			continue;
		}
		break;
	}
}

/* ---- names ------------------------------------------------------------------------------- */
