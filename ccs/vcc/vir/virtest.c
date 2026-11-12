/*
 * virtest: parse a VIR unit, run the passes the file names (only `canon`
 * exists), verify, print (SPEC-vir.md 16, 17.1).
 *
 *   virtest [--no-verify] [--bless] FILE
 *   virtest --selftest		the builder API (construction, canon, uses), not a file
 *
 * A test file is a unit with directives in comments at the top
 * (`; PASSES: canon`), a line `; ---`, and the expected printed output.
 * The unit is the text before that line.  Prints the unit (or the errors:
 * `file:line:col: message` for syntax, `@f bN.I: RULE: message` for the
 * verifier) on standard output and exits 0, or 1 on any error.  The printed
 * text is parsed and printed again and must be identical (11.1).
 * `; VERIFY: no` is --no-verify.  `; DOM:` appends each function's dominator tree (idom, preorder, subtree end) as comments.
 * --bless rewrites the part after `; ---` with what was printed.
 * --equiv (interpreter, step V1's second half) is not implemented.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vir.h"

static char *slurp(const char *path, size_t *n)
{
	FILE *fp = fopen(path, "rb");
	char *b;
	long sz;

	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	b = malloc((size_t)sz + 1);
	if (!b || (sz > 0 && fread(b, 1, (size_t)sz, fp) != (size_t)sz)) {
		fclose(fp);
		free(b);
		return NULL;
	}
	fclose(fp);
	b[sz] = 0;
	*n = (size_t)sz;
	return b;
}

/* the start of the line `; ---`, or n when there is none */
static size_t find_sep(const char *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		size_t e = i;

		while (e < n && s[e] != '\n')
			e++;
		if (e - i >= 5 && !memcmp(s + i, "; ---", 5)) {
			size_t k = i + 5;

			while (k < e && (s[k] == ' ' || s[k] == '\r'))
				k++;
			if (k == e)
				return i;
		}
		i = e + 1;
	}
	return n;
}

/* the builder API (vir_begin, vir_const, vir_ins_add, ... vir_canon, vir_uses): a function built in the
 * builder form, with a constant created inside a block, a nop, an unreachable block and an unused constant */
static int selftest(void)
{
	static const char want[] =
		"func @f %s0 {\n"
		"b0:\n  v0:i64 = param 0.0\n  v1:i64 = add v0, 9 nsw\n  v2:i64 = add v1, 5\n  v3:i32 = icmp.slt v2, 7\n"
		"  brif v3, b2, b1\n"
		"b1:\n  ret v2\n"
		"b2:\n  ret v0\n"
		"}\n";
	int bad = 0;
	vir_unit u;
	vir_fn f;
	vbuf out = { 0 }, ver = { 0 };
	uint32_t sg[5] = { VSIG_HDR(1, 0), VG_WORD0(VG_REG, 1, VC_INT, 0, 0, 0, 0), 0, VG_WORD0(VG_REG, 1, VC_INT, 0, 0, 0, 0), 0 };
	uint32_t sym, p, c5, c9, c7, a, b, cond, l[2];
	uint8_t cnt[64];

#define CHECK(c) do { if (!(c)) { printf("selftest: line %d: %s\n", __LINE__, #c); bad++; } } while (0)
	vir_unit_init(&u);
	u.version = 1;
	u.target = VTG_X86_64_LINUX;
	sym = vir_unit_sym(&u, "f", 1);
	u.sym[sym].kind = VU_FUNC;
	vir_unit_sigw(&u, sg, 5);
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	CHECK(!vir_failed());
	f.sig = vir_extra(&f, sg, 5);
	CHECK(vir_ins_add(&f, VO_NOP, VT_VOID, 0, 0, 0, 0) == 0 && vir_failed());	/* no block is open */
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	f.sig = vir_extra(&f, sg, 5);
	CHECK(!vir_failed() && f.sig == 1);
	vir_blk_add(&f, 0);
	p = vir_ins_add(&f, VO_PARAM, VT_I64, 0, 0, 0, 0);
	c5 = vir_const(&f, VO_ICONST, VT_I64, 0, 5, 0);
	CHECK(vir_const(&f, VO_ICONST, VT_I64, 0, 5, 0) == c5);	/* hash-consed (5.3) */
	CHECK(vir_const(&f, VO_ICONST, VT_I32, 0, 5, 0) != c5);	/* the type is part of the key */
	vir_ins_add(&f, VO_NOP, VT_VOID, 0, 0, 0, 0);
	c9 = vir_const(&f, VO_ICONST, VT_I64, 0, 9, 0);
	a = vir_ins_add(&f, VO_ADD, VT_I64, VIF_NSW, p, c9, 0);
	c7 = vir_const(&f, VO_ICONST, VT_I64, 0, 7, 0);
	b = vir_ins_add(&f, VO_ADD, VT_I64, 0, a, c5, 0);
	cond = vir_ins_add(&f, VO_ICMP, VT_I32, 2, b, c7, 0);
	vir_ins_add(&f, VO_BRIF, VT_VOID, 0, cond, 3, 2);
	vir_blk_add(&f, 0);	/* b1: nothing reaches it */
	{
		uint32_t k = vir_const(&f, VO_ICONST, VT_I64, 0, 1234, 0);

		l[0] = 1;
		l[1] = k;
		vir_ins_add(&f, VO_RET, VT_VOID, 0, 0, 0, vir_extra(&f, l, 2));
	}
	vir_blk_add(&f, 0);	/* b2 */
	l[1] = b;
	vir_ins_add(&f, VO_RET, VT_VOID, 0, 0, 0, vir_extra(&f, l, 2));
	vir_blk_add(&f, 0);	/* b3 */
	l[1] = p;
	vir_ins_add(&f, VO_RET, VT_VOID, 0, 0, 0, vir_extra(&f, l, 2));
	CHECK(!vir_failed());
	CHECK(f.nconst == 0 && vir_verify(&f, &u, &ver, 5) > 0);	/* the builder form is not the layout of 2.3 */
	ver.n = 0;
	CHECK(vir_canon(&f));
	CHECK(f.nblk == 3 && f.nconst == 4);	/* 9, 5, 7 by first use; 5:i32 and 1234 are gone */
	CHECK(vir_verify(&f, &u, &ver, 5) == 0);
	if (ver.n)
		fwrite(ver.p, 1, ver.n, stdout);
	vir_print_fn(&out, &u, &f);
	CHECK(out.n == sizeof(want) - 1 && !memcmp(out.p, want, out.n));
	if (out.n != sizeof(want) - 1 || memcmp(out.p, want, out.n))
		printf("selftest: printed\n%.*s", (int)out.n, out.p);
	CHECK(f.ins[1].b == 9 && f.ins[2].b == 5 && f.ins[3].b == 7);
	CHECK(vir_dominates(&f, 0, 1) && vir_dominates(&f, 0, 2) && !vir_dominates(&f, 1, 2) && vir_dominates(&f, 1, 1));
	vir_uses(&f, cnt);
	CHECK(cnt[4] == 2 && cnt[5] == 1 && cnt[6] == 2 && cnt[7] == 1 && cnt[1] == 1 && cnt[2] == 1 && cnt[3] == 1);
	/* Target-independent demand facts: a return's width must propagate through
	 * a loop parameter and its back edge. Constants/shift masks have no ABI. */
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	f.sig=vir_extra(&f,sg,5);
	vir_blk_add(&f,0);
	p=vir_ins_add(&f,VO_PARAM,VT_I64,0,0,0,0);
	a=vir_ins_add(&f,VO_TRUNC,VT_I32,0,p,0,0);
	b=vir_ins_add(&f,VO_EXT,VT_I64,2|VIF_MS,a,0,0);
	l[0]=b;
	vir_ins_add(&f,VO_BR,VT_VOID,0,1,0,vir_extra(&f,l,1));
	vir_blk_add(&f,0);
	p=vir_ins_add(&f,VO_BPARAM,VT_I64,0,0,0,0);
	a=vir_ins_add(&f,VO_TRUNC,VT_I32,0,p,0,0);
	c5=vir_const(&f,VO_ICONST,VT_I32,0,15,0);
	c9=vir_const(&f,VO_ICONST,VT_I32,0,4,0);
	b=vir_ins_add(&f,VO_AND,VT_I32,0,a,c5,0);
	b=vir_ins_add(&f,VO_SHL,VT_I32,0,b,c9,0);
	cond=vir_ins_add(&f,VO_ICMP,VT_I32,0,b,c5,0);
	vir_ins_add(&f,VO_BRIF,VT_VOID,0,cond,2,3);
	vir_blk_add(&f,0);
	l[0]=p;
	vir_ins_add(&f,VO_BR,VT_VOID,0,1,0,vir_extra(&f,l,1));
	vir_blk_add(&f,0);
	l[0]=1;l[1]=p;
	vir_ins_add(&f,VO_RET,VT_VOID,0,0,0,vir_extra(&f,l,2));
	CHECK(vir_canon(&f) && f.nins<sizeof cnt);
	ver.n=0;CHECK(vir_verify(&f,&u,&ver,5)==0);
	vir_low32(&f,false,cnt);
	for(uint32_t i=f.nconst;i<f.nins;i++) {
		if(f.ins[i].op==VO_BPARAM || f.ins[i].op==VO_EXT)CHECK(!cnt[i]);
		if(f.ins[i].op==VO_SHL)CHECK(vir_mask32(&f,i,8)==0xf0 && vir_mask32(&f,i,0)==UINT32_MAX);
	}
	vir_low32(&f,true,cnt);
	for(uint32_t i=f.nconst;i<f.nins;i++)
		if(f.ins[i].op==VO_BPARAM || f.ins[i].op==VO_EXT)CHECK(cnt[i]);
	/* the limit of the constant table: the 2^23+1st constant fails, and the next function starts clean */
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	for (uint32_t i = 0; i <= VIR_MAX_CONST; i++)
		if (!vir_const(&f, VO_ICONST, VT_I64, 0, i, 0))
			break;
	CHECK(vir_failed());
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	CHECK(!vir_failed() && vir_const(&f, VO_ICONST, VT_I64, 0, 0, 0) == 1);
	/* a block takes 65 535 parameters; one more fails instead of wrapping the 16-bit count */
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	vir_blk_add(&f, 0);
	for (uint32_t i = 0; i < VIR_MAX_BPARAM; i++)
		vir_ins_add(&f, VO_BPARAM, VT_I64, 0, 0, 0, 0);
	CHECK(!vir_failed() && f.blk[0].nparam == VIR_MAX_BPARAM);
	CHECK(vir_ins_add(&f, VO_BPARAM, VT_I64, 0, 0, 0, 0) == 0 && vir_failed() && f.blk[0].nparam == VIR_MAX_BPARAM);
	vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	CHECK(!vir_failed());
#undef CHECK
	vir_unit_free(&u);
	free(out.p);
	free(ver.p);
	if (!bad)
		printf("selftest: ok\n");
	return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
	bool verify = true, bless = false, canon = false, dom = false;
	const char *path = NULL;
	char *src;
	size_t n, sep;
	vir_unit u = { 0 }, u2;
	vir_perr err;
	vbuf out = { 0 }, out2 = { 0 }, ver = { 0 };	/* the unit printed, again, and what is reported instead */
	uint32_t nbad = 0;
	int rc = 1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--no-verify"))
			verify = false;
		else if (!strcmp(argv[i], "--bless"))
			bless = true;
		else if (!strcmp(argv[i], "--selftest"))
			return selftest();
		else if (!strcmp(argv[i], "--equiv")) {
			fprintf(stderr, "virtest: --equiv needs the interpreter (virrun), which is not implemented yet\n");
			return 2;
		} else if (argv[i][0] == '-' && argv[i][1]) {
			fprintf(stderr, "usage: virtest [--no-verify] [--bless] FILE\n");
			return 2;
		} else if (path) {
			fprintf(stderr, "usage: virtest [--no-verify] [--bless] FILE\n");
			return 2;
		} else {
			path = argv[i];
		}
	}
	if (!path) {
		fprintf(stderr, "usage: virtest [--no-verify] [--bless] FILE\n");
		return 2;
	}
	src = slurp(path, &n);
	if (!src) {
		fprintf(stderr, "virtest: cannot read %s\n", path);
		return 2;
	}
	sep = find_sep(src, n);
	/* directives: the comment lines at the top */
	for (size_t i = 0; i < sep;) {
		size_t e = i;
		const char *l;

		while (e < sep && src[e] != '\n')
			e++;
		l = src + i;
		if (e - i > 0 && l[0] != ';' && l[0] != '\r')
			break;
		if (e - i >= 10 && !memcmp(l, "; PASSES:", 9)) {
			char *v = strndup(l + 9, e - i - 9), *c = v;
			char *tk;

			while (*c == ' ')
				c++;
			while ((tk = strsep(&c, ", \r")) != NULL) {
				if (!*tk)
					continue;
				if (!strcmp(tk, "canon")) {
					canon = true;
				} else {
					vbuf_f(&ver, "%s: unknown pass `%s` (only `canon` exists until the passes are written)\n", path, tk);
					free(v);
					goto done;
				}
			}
			free(v);
		} else if (e - i >= 12 && !memcmp(l, "; VERIFY: no", 12)) {
			verify = false;	/* as --no-verify (a unit that is not valid on purpose) */
		} else if (e - i >= 6 && !memcmp(l, "; DOM:", 6)) {
			dom = true;	/* append the dominator tree of each function as comments */
		} else if (!strncmp(l, "; EQUIV:", 8) || !strncmp(l, "; EXPECT-UB:", 12)) {
			vbuf_f(&ver, "%s: %.*s needs the interpreter (virrun), which is not implemented yet\n", path,
			       (int)(e - i > 12 ? 8 : e - i), l + 2);
			goto done;
		}
		i = e + 1;
	}
	vir_unit_init(&u);
	if (!vir_parse(&u, src, sep, &err)) {
		vbuf_f(&ver, "%s:%u:%u: %s\n", path, err.line, err.col, err.msg);
		goto done;
	}
	for (uint32_t i = 0; i < u.nfn; i++) {
		vir_fn f;

		vir_fn_load(&f, &u, i);
		if (canon) {
			if (!vir_canon(&f)) {
				vbuf_f(&ver, "%s: @%s: the function cannot be put in canonical form\n", path, vir_fname(&u, &f));
				nbad++;
				continue;
			}
			vir_fn_store(&u, i, &f);
		}
		if (verify)
			nbad += vir_verify(&f, &u, &ver, 20);
	}
	if (nbad)
		goto done;
	vir_print_unit(&out, &u);
	/* print, parse, print */
	vir_unit_init(&u2);
	if (!vir_parse(&u2, out.p, out.n, &err)) {
		vbuf_f(&ver, "%s: the printed text does not parse: %u:%u: %s\n", path, err.line, err.col, err.msg);
		vbuf_put(&ver, out.p, out.n);
		vir_unit_free(&u2);
		goto done;
	}
	vir_print_unit(&out2, &u2);
	vir_unit_free(&u2);
	if (out.n != out2.n || memcmp(out.p, out2.p, out.n)) {
		vbuf_f(&ver, "%s: print-parse-print is not identical\n--- first\n", path);
		vbuf_put(&ver, out.p, out.n);
		vbuf_s(&ver, "--- second\n");
		vbuf_put(&ver, out2.p, out2.n);
		goto done;
	}
	rc = 0;
	if (dom)
		for (uint32_t i = 0; i < u.nfn; i++) {
			vir_fn f;

			vir_fn_load(&f, &u, i);
			if (!vir_dom(&f)) {
				vbuf_f(&out, "; dom @%s: no graph\n", vir_fname(&u, &f));
				continue;
			}
			for (uint32_t b = 0; b < f.nblk; b++)
				if (f.blk[b].idom == VIR_NOBLK)
					vbuf_f(&out, "; dom @%s b%u unreachable\n", vir_fname(&u, &f), b);
				else
					vbuf_f(&out, "; dom @%s b%u idom b%u pre %u post %u\n", vir_fname(&u, &f), b, f.blk[b].idom,
					       f.blk[b].pre, f.blk[b].post);
		}
done:
	{
		const char *text = rc == 0 ? out.p : ver.p ? ver.p : "";
		size_t tn = rc == 0 ? out.n : ver.n;

		fwrite(text, 1, tn, stdout);
		if (bless) {
			FILE *fp = fopen(path, "wb");

			if (!fp) {
				fprintf(stderr, "virtest: cannot write %s\n", path);
				rc = 2;
			} else {
				fwrite(src, 1, sep, fp);
				if (sep && src[sep - 1] != '\n')
					fputc('\n', fp);
				fputs("; ---\n", fp);
				fwrite(text, 1, tn, fp);
				fclose(fp);
			}
		}
	}
	vir_unit_free(&u);
	free(out.p);
	free(out2.p);
	free(ver.p);
	free(src);
	return rc;
}
