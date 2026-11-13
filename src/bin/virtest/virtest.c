/*
 * virtest: parse a VIR unit, run the passes the file names (`canon` and the
 * registry's, vir/pass.h), verify after each, print (SPEC-vir.md 16, 17.1).
 *
 *   virtest [--no-verify] [--bless] [--passes A,B,...] FILE
 *   virtest --selftest		the builder API (construction, canon, uses, compact), not a file
 *
 * A test file is a unit with directives in comments at the top
 * (`; PASSES: canon,compact`), a line `; ---`, and the expected printed output.
 * --passes overrides the file's list (ad hoc runs, not for the suite).  After each pass the
 * function is verified (`@f bN.I: after PASS: RULE: message`) and the analyses its `keeps`
 * names are checked against a fresh computation; the first failure stops the function.
 * `; CHANGES: yes` appends `; pass NAME @f: changed, N instructions` (or `unchanged`) for each
 * pass of the registry and function, so that a test can state that nothing changed.
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

#include "vir/vir.h"
#include "vir/layout.h"
#include "vir/pass.h"

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
	vbuffer out = { 0 }, ver = { 0 };
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
	/* The layout is a plan, including the entry guard; a reordered latch
	 * alone would add a jump on every entry into a short loop. */
	{
	 uint32_t chase[4]={0,1,2,3},order[4],position[4],rotation[4],path[4];
	 vir_loop loop={.head=1,.end=2,.parent=VIR_NOBLK,.depth=1};
	 vir_loops lp={.l=&loop,.n=1};
	 vir_layout_order(&f,&lp,chase,false,order,position,rotation);
	 CHECK(order[0]==0 && order[1]==1 && order[2]==2 && order[3]==3);
	 vir_layout_order(&f,&lp,chase,true,order,position,rotation);
	 CHECK(order[0]==0 && order[1]==2 && order[2]==1 && order[3]==3 && position[1]==2);
	 CHECK(rotation[1]==0 && vir_layout_duplicate(&f,&lp,rotation,0,1,2,0,true));
	 CHECK(!vir_layout_duplicate(&f,&lp,rotation,2,1,1,0,true));
	 CHECK(!vir_layout_duplicate(&f,&lp,rotation,1,1,2,0,true));
	 CHECK(!vir_layout_duplicate(&f,&lp,rotation,0,1,2,8,true));
	 CHECK(vir_layout_duplicate(&f,&lp,rotation,0,3,1,0,true));
	 f.blk[1].fl|=VB_ADDR;
	 vir_layout_order(&f,&lp,chase,true,order,position,rotation);
	 CHECK(order[1]==1 && rotation[1]==VIR_NOBLK);
	 f.blk[1].fl&=~VB_ADDR;
	 uint32_t soff[5]={0,1,3,4,4},eidx[4]={VIR_NOBLK,VIR_NOBLK,VIR_NOBLK,VIR_NOBLK},boff[10]={0};
	 vir_alloc_out o={.soff=soff,.eidx=eidx,.boff=boff};
	 vir_layout_chases(&f,&o,chase,path);
	 CHECK(chase[0]==0 && chase[1]==1 && chase[2]==1 && chase[3]==3);
	 uint32_t branch=f.blk[2].first;
	 f.ins[branch].a=2;
	 vir_layout_chases(&f,&o,chase,path);CHECK(chase[2]==2);
	 f.ins[branch].a=1;
	 boff[8]=boff[9]=1;
	 vir_layout_chases(&f,&o,chase,path);CHECK(chase[2]==2);
	 boff[8]=boff[9]=0;
	 vir_aedge edge={.n=1};o.edge=&edge;eidx[3]=0;
	 vir_layout_chases(&f,&o,chase,path);CHECK(chase[2]==2);
	}
	/* ARM's dispatch-latch rule is shared too: one plain operation and br. */
	{
	 vir_ins ins[3]={{.op=VO_ADD},{.op=VO_BR},{.op=VO_RET}};
	 vir_blk blk[3]={{.first=2,.n=1},{.first=0,.n=2},{.first=2,.n=1}};
	 vir_fn g={.ins=ins,.blk=blk,.nins=3,.nblk=3};
	 vir_loops lp={0};uint32_t rotation[3]={VIR_NOBLK,VIR_NOBLK,VIR_NOBLK};
	 CHECK(vir_layout_duplicate(&g,&lp,rotation,0,1,2,0,true));
	 CHECK(!vir_layout_duplicate(&g,&lp,rotation,0,1,2,0,false));
	 CHECK(!vir_layout_duplicate(&g,&lp,rotation,0,1,2,2,true));
	 blk[0].fl=VB_COLD;CHECK(!vir_layout_duplicate(&g,&lp,rotation,0,1,2,0,true));blk[0].fl=0;
	 blk[1].fl=VB_ADDR;CHECK(!vir_layout_duplicate(&g,&lp,rotation,0,1,2,0,true));blk[1].fl=0;
	 ins[0].op=VO_CALL;CHECK(!vir_layout_duplicate(&g,&lp,rotation,0,1,2,0,true));
	}
	/* No-overflow widening is a shared proof, independent of hardware scales. */
	{
	 vir_ins ins[4]={{.op=VO_ICONST,.ty=VT_I32,.b=1},{.op=VO_PARAM,.ty=VT_I32},
	  {.op=VO_SHL,.ty=VT_I32,.fl=VIF_NSW,.a=1,.b=0},{.op=VO_EXT,.ty=VT_I64,.fl=2|VIF_MS,.a=2}};
	 vir_fn g={.ins=ins,.nins=4};unsigned shift=99;
	 CHECK(vir_sext_shift(&g,2,&shift) && shift==1);
	 ins[2].fl=0;CHECK(!vir_sext_shift(&g,2,&shift));ins[2].fl=VIF_NSW;
	 ins[3].fl=2;CHECK(!vir_sext_shift(&g,2,&shift));ins[3].fl=2|VIF_MS;
	 ins[0].b=32;CHECK(!vir_sext_shift(&g,2,&shift));ins[0].b=0;
	 CHECK(vir_sext_shift(&g,2,&shift) && !shift);
	 CHECK(!vir_sext_shift(&g,3,&shift) && !vir_sext_shift(&g,VIR_NOBLK,&shift));
	}
	/* vir_function_compact: an instruction a pass made a nop and the constant only it used are dropped, the rest keeps its
	 * order and is renumbered (operands in a list too), the source location runs follow, the analyses stay valid */
	{
	 static const char cwant[] =
		"func @f %s0 {\n"
		"b0:\n  v0:i64 = param 0.0\n  v1:i64 = add v0, 5\n  v2:i64 = add v1, 5\n  ret v2\n"
		"}\n";
	 uint32_t k5, k9, pp, a1, b1, c1, dropped;
	 vir_pass_context pc = { .budget = VIR_PASS_BUDGET, .checked = true };
	 char perr[200];
	 const vir_pass_info *cp = vir_pass_find("compact", 7);

	 vir_begin(&f, VTG_X86_64_LINUX, sym, 0);
	 f.sig = vir_extra(&f, sg, 5);
	 vir_blk_add(&f, 0);
	 pp = vir_ins_add(&f, VO_PARAM, VT_I64, 0, 0, 0, 0);
	 k5 = vir_const(&f, VO_ICONST, VT_I64, 0, 5, 0);
	 k9 = vir_const(&f, VO_ICONST, VT_I64, 0, 9, 0);
	 vir_loc_add(&f, 10);
	 a1 = vir_ins_add(&f, VO_ADD, VT_I64, 0, pp, k5, 0);
	 vir_loc_add(&f, 20);
	 b1 = vir_ins_add(&f, VO_ADD, VT_I64, 0, a1, k9, 0);
	 vir_loc_add(&f, 30);
	 c1 = vir_ins_add(&f, VO_ADD, VT_I64, 0, b1, k5, 0);
	 l[0] = 1;
	 l[1] = c1;
	 vir_ins_add(&f, VO_RET, VT_VOID, 0, 0, 0, vir_extra(&f, l, 2));
	 CHECK(vir_canon(&f) && f.nconst == 3 && f.nins == 8 && f.nloc == 3);
	 CHECK(vir_function_compact(&f) == 0);	/* nothing to drop */
	 CHECK(f.nins == 8 && f.nconst == 3 && f.nloc == 3);
	 CHECK(vir_dom(&f) && f.valid == (VIR_VALID_CFG | VIR_VALID_DOM));
	 /* a pass rewrites c to use a, and b becomes a nop */
	 f.ins[6].a = 4;
	 memset(&f.ins[5], 0, sizeof(vir_ins));
	 dropped = vir_function_compact(&f);
	 CHECK(dropped == 2 && f.nins == 6 && f.nconst == 2);	/* the nop and the constant 9 */
	 CHECK(f.valid == (VIR_VALID_CFG | VIR_VALID_DOM) && f.blk[0].first == 2 && f.blk[0].n == 4);
	 CHECK(f.ins[1].b == 5 && f.ins[3].a == 2 && f.ins[3].b == 1 && f.ins[4].a == 3 && f.ins[4].b == 1);
	 CHECK(f.extra[f.ins[5].c + 1] == 4);	/* the return list names the renumbered value */
	 CHECK(f.nloc == 2 && f.loc[0].first == 3 && f.loc[0].loc == 10 && f.loc[1].first == 4 && f.loc[1].loc == 30);	/* the run of b went with it */
	 ver.n = 0;
	 CHECK(vir_verify(&f, &u, &ver, 5) == 0);
	 if (ver.n)
		fwrite(ver.p, 1, ver.n, stdout);
	 out.n = 0;
	 vir_print_fn(&out, &u, &f);
	 CHECK(out.n >= sizeof(cwant) - 1 && !memcmp((char *)out.p + out.n - (sizeof(cwant) - 1), cwant, sizeof(cwant) - 1));
	 CHECK(vir_function_compact(&f) == 0);
	 /* the registry's row, with the check of `keeps` */
	 CHECK(cp && cp->run && vir_pass_run(&f, cp, &pc, perr, sizeof perr) == false && !perr[0]);
	 CHECK(vir_pass_find("no-such-pass", 12) == NULL);
	 /* a pass that claims it keeps the dominators and changes them is reported by name */
	 {
	  vir_pass_info lie = { "lie", cp->run, 0, VIR_VALID_CFG | VIR_VALID_DOM, 0 };

	  f.blk[0].post += 1;	/* what a pass that moved a block without invalidating would leave */
	  CHECK(vir_dom(&f) && f.valid == (VIR_VALID_CFG | VIR_VALID_DOM));
	  vir_pass_run(&f, &lie, &pc, perr, sizeof perr);
	  CHECK(perr[0] && strstr(perr, "dom"));
	  f.blk[0].post -= 1;
	 }
	}
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

/* the passes of a run, in order: canon, or a pass of the registry */
#define MAX_STEPS 32
typedef struct step {
	const vir_pass_info *pass;	/* NULL: canon */
} step;

/* the names in s[0..n) (separated by commas, spaces or tabs) appended to steps; false (and a message in err) for an unknown one */
static bool add_steps(step *steps, uint32_t *ns, const char *s, size_t n, const char *path, vbuffer *err)
{
	for (size_t i = 0; i < n;) {
		size_t e;

		while (i < n && (s[i] == ',' || s[i] == ' ' || s[i] == '\t' || s[i] == '\r'))
			i++;
		for (e = i; e < n && s[e] != ',' && s[e] != ' ' && s[e] != '\t' && s[e] != '\r'; e++)
			;
		if (e == i)
			break;
		if (*ns >= MAX_STEPS) {
			vbuffer_format_append(err, "%s: more than %d passes\n", path, MAX_STEPS);
			return false;
		}
		if (e - i == 5 && !memcmp(s + i, "canon", 5)) {
			steps[(*ns)++].pass = NULL;
		} else {
			const vir_pass_info *p = vir_pass_find(s + i, e - i);
			uint32_t np;
			const vir_pass_info *const *all = vir_pass_list(&np);

			if (!p) {
				vbuffer_format_append(err, "%s: unknown pass `%.*s` (passes: canon", path, (int)(e - i), s + i);
				for (uint32_t k = 0; k < np; k++)
					vbuffer_format_append(err, " %s", all[k]->name);
				vbuffer_string_append(err, ")\n");
				return false;
			}
			steps[(*ns)++].pass = p;
		}
		i = e;
	}
	return true;
}

/* the lines of a verifier report, each with `after PASS: ` inserted after its location (`@f bN.I: `) */
static void report_after(vbuffer *dst, const char *pass, const vbuffer *rep)
{
	for (size_t i = 0; i < rep->n;) {
		size_t e = i, c;

		while (e < rep->n && ((const char *)rep->p)[e] != '\n')
			e++;
		/* the location ends at the first ": " */
		for (c = i; c + 1 < e && !(((const char *)rep->p)[c] == ':' && ((const char *)rep->p)[c + 1] == ' '); c++)
			;
		if (c + 1 < e) {
			vbuffer_append(dst, rep->p, c + 2 - i);
			vbuffer_format_append(dst, "after %s: ", pass);
			vbuffer_append(dst, (const char *)rep->p + c + 2, e - c - 2);
		} else {
			vbuffer_append(dst, (const char *)rep->p + i, e - i);
		}
		vbuffer_string_append(dst, "\n");
		i = e + 1;
	}
}

int main(int argc, char **argv)
{
	bool verify = true, bless = false, dom = false, changes = false;
	const char *path = NULL, *cli_passes = NULL;
	step steps[MAX_STEPS];
	uint32_t nsteps = 0;
	vbuffer chg = { 0 };	/* the `; pass ...` lines of CHANGES: yes */
	char *src;
	size_t n, sep;
	vir_unit u = { 0 }, u2;
	vir_perr err;
	vbuffer out = { 0 }, out2 = { 0 }, ver = { 0 };	/* the unit printed, again, and what is reported instead */
	uint32_t nbad = 0;
	int rc = 1;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--no-verify"))
			verify = false;
		else if (!strcmp(argv[i], "--bless"))
			bless = true;
		else if (!strcmp(argv[i], "--selftest"))
			return selftest();
		else if (!strcmp(argv[i], "--passes") && i + 1 < argc)
			cli_passes = argv[++i];
		else if (!strcmp(argv[i], "--equiv")) {
			fprintf(stderr, "virtest: --equiv needs the interpreter (virrun), which is not implemented yet\n");
			return 2;
		} else if (argv[i][0] == '-' && argv[i][1]) {
			fprintf(stderr, "usage: virtest [--no-verify] [--bless] [--passes A,B,...] FILE\n");
			return 2;
		} else if (path) {
			fprintf(stderr, "usage: virtest [--no-verify] [--bless] [--passes A,B,...] FILE\n");
			return 2;
		} else {
			path = argv[i];
		}
	}
	if (!path) {
		fprintf(stderr, "usage: virtest [--no-verify] [--bless] [--passes A,B,...] FILE\n");
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
			if (!add_steps(steps, &nsteps, l + 9, e - i - 9, path, &ver))
				goto done;
		} else if (e - i >= 11 && !memcmp(l, "; CHANGES:", 10)) {
			changes = strstr(l, "yes") && strstr(l, "yes") < l + (e - i);	/* a report of what each pass did, as comments */
		} else if (e - i >= 12 && !memcmp(l, "; VERIFY: no", 12)) {
			verify = false;	/* as --no-verify (a unit that is not valid on purpose) */
		} else if (e - i >= 6 && !memcmp(l, "; DOM:", 6)) {
			dom = true;	/* append the dominator tree of each function as comments */
		} else if (!strncmp(l, "; EQUIV:", 8) || !strncmp(l, "; EXPECT-UB:", 12)) {
			vbuffer_format_append(&ver, "%s: %.*s needs the interpreter (virrun), which is not implemented yet\n", path,
			       (int)(e - i > 12 ? 8 : e - i), l + 2);
			goto done;
		}
		i = e + 1;
	}
	if (cli_passes) {	/* overrides the file's list */
		nsteps = 0;
		if (!add_steps(steps, &nsteps, cli_passes, strlen(cli_passes), "--passes", &ver))
			goto done;
	}
	vir_unit_init(&u);
	if (!vir_parse(&u, src, sep, &err)) {
		vbuffer_format_append(&ver, "%s:%u:%u: %s\n", path, err.line, err.col, err.msg);
		goto done;
	}
	for (uint32_t i = 0; i < u.nfn; i++) {
		vir_fn f;
		bool known = false;	/* f has been verified as it is now */
		uint32_t before = nbad;

		vir_fn_load(&f, &u, i);
		for (uint32_t k = 0; k < nsteps; k++) {
			const vir_pass_info *p = steps[k].pass;

			if (!p) {
				if (!vir_canon(&f)) {
					vbuffer_format_append(&ver, "%s: @%s: the function cannot be put in canonical form\n", path, vir_fname(&u, &f));
					nbad++;
					break;
				}
				vir_fn_store(&u, i, &f);
				known = false;
			} else {
				vir_pass_context ctx = { .unit = &u, .budget = VIR_PASS_BUDGET, .checked = true };
				char msg[200];
				bool changed;
				vbuffer rep = { 0 };
				uint32_t nv = 0;

				if (verify && !known) {	/* a pass gets a verified function */
					uint32_t nb = vir_verify(&f, &u, &ver, 20);

					nbad += nb;
					if (nb)
						break;
					known = true;
				}
				changed = vir_pass_run(&f, p, &ctx, msg, sizeof msg);
				vir_fn_store(&u, i, &f);
				if (changes)
					vbuffer_format_append(&chg, changed ? "; pass %s @%s: changed, %llu instructions\n" : "; pass %s @%s: unchanged\n",
						   p->name, vir_fname(&u, &f), (unsigned long long)ctx.changes);
				if (msg[0]) {
					vbuffer_format_append(&ver, "@%s: after %s: %s\n", vir_fname(&u, &f), p->name, msg);
					nbad++;
					free(rep.p);
					break;
				}
				if (verify) {
					nv = vir_verify(&f, &u, &rep, 20);
					nbad += nv;
					known = true;
					if (nv)
						report_after(&ver, p->name, &rep);
				}
				free(rep.p);
				if (verify && nv)
					break;
			}
			if (verify && !p) {	/* after canon */
				uint32_t nb = vir_verify(&f, &u, &ver, 20);

				nbad += nb;
				known = true;
				if (nb)
					break;
			}
		}
		if (nbad == before && verify && !known)	/* no step ran (or only a failed one): the unit as it is */
			nbad += vir_verify(&f, &u, &ver, 20);
	}
	if (nbad)
		goto done;
	vir_print_unit(&out, &u);
	/* print, parse, print */
	vir_unit_init(&u2);
	if (!vir_parse(&u2, out.p, out.n, &err)) {
		vbuffer_format_append(&ver, "%s: the printed text does not parse: %u:%u: %s\n", path, err.line, err.col, err.msg);
		vbuffer_append(&ver, out.p, out.n);
		vir_unit_free(&u2);
		goto done;
	}
	vir_print_unit(&out2, &u2);
	vir_unit_free(&u2);
	if (out.n != out2.n || memcmp(out.p, out2.p, out.n)) {
		vbuffer_format_append(&ver, "%s: print-parse-print is not identical\n--- first\n", path);
		vbuffer_append(&ver, out.p, out.n);
		vbuffer_string_append(&ver, "--- second\n");
		vbuffer_append(&ver, out2.p, out2.n);
		goto done;
	}
	rc = 0;
	if (chg.n)
		vbuffer_append(&out, chg.p, chg.n);
	if (dom)
		for (uint32_t i = 0; i < u.nfn; i++) {
			vir_fn f;

			vir_fn_load(&f, &u, i);
			if (!vir_dom(&f)) {
				vbuffer_format_append(&out, "; dom @%s: no graph\n", vir_fname(&u, &f));
				continue;
			}
			for (uint32_t b = 0; b < f.nblk; b++)
				if (f.blk[b].idom == VIR_NOBLK)
					vbuffer_format_append(&out, "; dom @%s b%u unreachable\n", vir_fname(&u, &f), b);
				else
					vbuffer_format_append(&out, "; dom @%s b%u idom b%u pre %u post %u\n", vir_fname(&u, &f), b, f.blk[b].idom,
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
	free(chg.p);
	free(src);
	return rc;
}
