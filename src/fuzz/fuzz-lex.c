/*
 * fuzz-lex: vlex() on arbitrary bytes.
 *
 * Oracles, besides ASan/UBSan:
 *   - the token array is well formed: kinds in range, tokens in order and
 *     non-empty, inside the input, one T_EOF at the end;
 *   - tokens cover the input: what lies between two tokens is only
 *     whitespace, NUL, line splices and comments;
 *   - the NEON path agrees with the scalar reference (lexref.c: lex.c with
 *     VLEX_SCALAR) on every token's offset, length, kind and flags;
 *   - the result does not depend on where the buffer starts (alignment);
 *   - vlex_spell() of a token is no longer than the token, and the same
 *     for clean tokens; interned identifiers spell like their token.
 *
 * The source buffer is allocated with exactly the 128 zero bytes the lexer
 * may read past the end, so ASan reports any read further out.
 */
#include <stdbool.h>
#include <string.h>

#include "../common/intern.h"
#include "../frontend/lex/lex.h"
#include "fuzz.h"

uint32_t vlex_ref(const char *src, size_t size, vtok *out, void *scratch);
extern _Thread_local int vlex_no_intern_ref;

#define MAXIN (1u << 20)
#define INTERN_MAX 600000	/* leave the rest of the 2^20-slot table alone */

/* a copy of src at offset off in a buffer with exactly 128 zero bytes after it */
static char *place(const uint8_t *d, size_t n, size_t off, char **base)
{
	char *b = malloc(off + n + 128);

	memset(b, 0x55, off);
	memcpy(b + off, d, n);
	memset(b + off + n, 0, 128);
	*base = b;
	return b + off;
}

static size_t splice_len(const char *s, size_t p)
{
	size_t q = p + 1;

	if (s[p] != '\\')
		return 0;
	while (s[q] == ' ' || s[q] == '\t' || s[q] == '\v' || s[q] == '\f')
		q++;
	if (s[q] != '\n' && s[q] != '\r')
		return 0;
	return q + 1 + ((s[q + 1] == '\n' || s[q + 1] == '\r') && s[q + 1] != s[q]) - p;
}

static size_t skip_splices(const char *s, size_t p)
{
	size_t l;

	while ((l = splice_len(s, p)))
		p += l;
	return p;
}

/* s[from, to) is only whitespace, NUL, splices and complete comments */
static bool gap_ok(const char *s, size_t n, size_t from, size_t to, size_t *where)
{
	size_t p = from, q, l;

	while (p < to) {
		char c = s[p];

		*where = p;
		if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\n' || c == '\r' || c == 0) {
			p++;
			continue;
		}
		if ((l = splice_len(s, p))) {
			p += l;
			continue;
		}
		if (c != '/')
			return false;
		q = skip_splices(s, p + 1);
		if (s[q] == '*') {
			for (q++;; q++) {
				if (q >= n)
					return false;	/* unterminated comments are tokens */
				if (s[q] == '*') {
					size_t r = skip_splices(s, q + 1);

					if (s[r] == '/') {
						p = r + 1;
						break;
					}
				}
			}
		} else if (s[q] == '/') {
			for (q++; q < n;) {
				if ((l = splice_len(s, q)))
					q += l;
				else if (s[q] == '\n' || s[q] == '\r')
					break;
				else
					q++;
			}
			p = q;
		} else {
			return false;
		}
	}
	return p == to;
}

static void check_array(const char *s, size_t n, const vtok *t, uint32_t nt)
{
	size_t end = 0, where = 0;

	FUZZ_CHECK(nt <= n, "%u tokens for %zu bytes", nt, n);
	FUZZ_CHECK(t[nt].kind == T_EOF && t[nt].off == n && t[nt].len == 0,
		   "bad EOF token: kind %u off %u len %u", t[nt].kind, t[nt].off, t[nt].len);
	for (uint32_t i = 0; i < nt; i++) {
		FUZZ_CHECK(t[i].kind != T_EOF && t[i].kind < T_NKINDS, "token %u: kind %u", i, t[i].kind);
		FUZZ_CHECK(t[i].len > 0, "token %u: empty", i);
		FUZZ_CHECK(t[i].off >= end, "token %u at %u overlaps the previous one (ends %zu)", i, t[i].off, end);
		FUZZ_CHECK((size_t)t[i].off + t[i].len <= n, "token %u: %u+%u past the end %zu", i, t[i].off, t[i].len, n);
		FUZZ_CHECK(gap_ok(s, n, end, t[i].off, &where), "token %u: stray byte 0x%02x at %zu before it",
			   i, (uint8_t)s[where], where);
		end = t[i].off + t[i].len;
	}
	FUZZ_CHECK(gap_ok(s, n, end, n, &where), "stray byte 0x%02x at %zu after the last token",
		   (uint8_t)s[where], where);
}

static void check_same(const char *what, const vtok *a, uint32_t na, const vtok *b, uint32_t nb)
{
	FUZZ_CHECK(na == nb, "%s: %u vs %u tokens", what, na, nb);
	for (uint32_t i = 0; i <= na; i++)
		FUZZ_CHECK(a[i].off == b[i].off && a[i].len == b[i].len && a[i].kind == b[i].kind &&
			   a[i].flags == b[i].flags,
			   "%s: token %u: off %u/%u len %u/%u kind %s/%s flags %u/%u", what, i, a[i].off, b[i].off,
			   a[i].len, b[i].len, vtok_name(a[i].kind), vtok_name(b[i].kind), a[i].flags, b[i].flags);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	(void)argc, (void)argv;
	vintern_init();
	vlex_no_intern_ref = 1;
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	char *b0, *b1, *s, *s1, *sp;
	vtok *t0, *t1, *t2;
	void *scr;
	uint32_t n0, n1, n2, before;
	size_t off;
	bool intern;

	if (n > MAXIN)
		return 0;
	s = place(d, n, 0, &b0);
	t0 = malloc((n + 2) * sizeof(vtok));
	t1 = malloc((n + 2) * sizeof(vtok));
	t2 = malloc((n + 2) * sizeof(vtok));
	scr = malloc(vlex_scratch_size(n));
	sp = malloc(n + 1);

	/* the real thing, interning while the table has room */
	before = vintern_count();
	intern = before < INTERN_MAX;
	vlex_no_intern = !intern;
	n0 = vlex(s, n, t0, scr);
	check_array(s, n, t0, n0);
	for (uint32_t i = 0; i < n0; i++) {
		size_t l = vlex_spell(s, &t0[i], sp);

		FUZZ_CHECK(l <= t0[i].len && (l > 0 || t0[i].kind == T_UNKNOWN),
			   "token %u: spelling %zu bytes, raw %u", i, l, t0[i].len);
		FUZZ_CHECK((t0[i].flags & TF_DIRTY) || (l == t0[i].len && !memcmp(sp, s + t0[i].off, l)),
			   "token %u: clean token spelled differently", i);
		if (t0[i].kind == T_IDENT && intern) {
			FUZZ_CHECK(t0[i].id != 0, "identifier %u not interned", i);
			FUZZ_CHECK(vintern_ents[t0[i].id].len == l && !memcmp(vintern_ents[t0[i].id].s, sp, l),
				   "identifier %u: id %u spells '%.*s', token '%.*s'", i, t0[i].id,
				   (int)vintern_ents[t0[i].id].len, vintern_ents[t0[i].id].s, (int)l, sp);
		} else if (t0[i].kind != T_IDENT) {
			FUZZ_CHECK(t0[i].id == 0, "non-identifier %u has id %u", i, t0[i].id);
		}
	}

	/* the scalar reference */
	n1 = vlex_ref(s, n, t1, scr);
	check_same("neon vs scalar", t0, n0, t1, n1);

	/* another alignment: the 64-byte blocks fall elsewhere */
	off = 1 + (n * 7 + (n ? d[0] : 0)) % 63;
	s1 = place(d, n, off, &b1);
	vlex_no_intern = 1;
	n2 = vlex(s1, n, t2, scr);
	vlex_no_intern = 0;
	check_same("offset 0 vs shifted", t0, n0, t2, n2);

	/* interned identifiers point into the source: keep it if any were added */
	if (vintern_count() == before)
		free(b0);
	free(b1);
	free(t0);
	free(t1);
	free(t2);
	free(scr);
	free(sp);
	return 0;
}
