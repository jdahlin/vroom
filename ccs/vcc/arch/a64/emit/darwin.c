/* vcc emitter: -S text for Mach-O (Apple's assembler dialect)
 *
 * The emitter prints one GNU/ELF dialect; the object backend writes Mach-O
 * itself from the items, so only the text output needs this pass.  It is a
 * line rewrite of what the emitter prints (and of top-level asm, which on
 * Darwin is already in this dialect and passes unchanged):
 *   .type/.size/.protected/.note.GNU-stack   dropped
 *   ELF sections                             their __SEG,__sect
 *   .hidden / .weak                          .private_extern / .weak_definition
 *                                            (defined here) or .weak_reference
 *   .xword .word .hword .zero                .quad .long .short .space
 *   :lo12:x+k  :got:x  :got_lo12:x  adrp x   x@PAGEOFF+k  x@GOTPAGE  ...
 *   .Lname                                   Lname (Mach-O's assembler-local)
 */
#include "../../../emit/emitimpl.h"

static bool idch(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
	       ch == '_' || ch == '.' || ch == '$';
}

static bool starts(const char *p, const char *e, const char *w)
{
	size_t n = strlen(w);

	return (size_t)(e - p) >= n && !memcmp(p, w, n);
}

/* a label "name:" at the start of some line of the text */
static bool defined(const char *t, size_t tn, const char *s, size_t n)
{
	for (const char *p = t, *e = t + tn; p < e;) {
		const char *nl = memchr(p, '\n', (size_t)(e - p));
		const char *le = nl ? nl : e;

		if ((size_t)(le - p) == n + 1 && !memcmp(p, s, n) && p[n] == ':')
			return true;
		p = le + 1;
	}
	return false;
}

/* a symbol expression x, x+k or x-k with a relocation specifier: x@SPEC+k */
static const char *reloc(tbuf *o, const char *p, const char *e, const char *spec)
{
	const char *s = p, *k;

	while (p < e && *p != ']' && *p != ',' && *p != '\n' && *p != ' ')
		p++;
	for (k = s + 1; k < p && *k != '+' && *k != '-'; k++)
		;
	if (s + 1 < p && s[0] == '.' && s[1] == 'L')
		s++;
	tb_put(o, s, (size_t)(k - s));
	tb_s(o, spec);
	tb_put(o, k, (size_t)(p - k));
	return p;
}

/* a .xword/.word of a symbol before the next section directive */
static bool relocs(const char *p, const char *end)
{
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *le = nl ? nl : end;

		if (starts(p, le, "\t.section") || starts(p, le, "\t.data") || starts(p, le, "\t.text") ||
		    starts(p, le, "\t.bss"))
			return false;
		if ((starts(p, le, "\t.xword\t") && !(p[8] >= '0' && p[8] <= '9') && p[8] != '-') ||
		    (starts(p, le, "\t.word\t") && !(p[7] >= '0' && p[7] <= '9') && p[7] != '-'))
			return true;
		p = le + 1;
	}
	return false;
}

static void section(tbuf *o, const char *p, const char *e, const char *end)
{
	/* p: after "\t.section\t" */
	if (starts(p, e, ".rodata.str1.")) {
		tb_s(o, "\t.section\t__TEXT,__cstring,cstring_literals\n");
	} else if (starts(p, e, ".rodata") || starts(p, e, "\".rodata")) {
		/* const data holding addresses is __DATA,__const (no text relocations), as clang */
		tb_s(o, relocs(e + 1, end) ? "\t.section\t__DATA,__const\n" : "\t.section\t__TEXT,__const\n");
	} else if (starts(p, e, ".init_array")) {
		tb_s(o, "\t.section\t__DATA,__mod_init_func,mod_init_funcs\n");
	} else if (starts(p, e, ".fini_array")) {
		tb_s(o, "\t.section\t__DATA,__mod_term_func,mod_term_funcs\n");
	} else if (*p == '"') {
		/* section("seg,sect") as the attribute gave it */
		const char *q = memchr(p + 1, '"', (size_t)(e - p - 1));

		tb_s(o, "\t.section\t");
		tb_put(o, p + 1, q ? (size_t)(q - p - 1) : 0);
		tb_c(o, '\n');
	} else {
		tb_s(o, "\t.section\t");
		tb_put(o, p, (size_t)(e - p));
		tb_c(o, '\n');
	}
}

VPRIV void darwin_text(tbuf *o, const char *t, size_t tn)
{
	static const struct { const char *from, *to; } dirs[] = {
		{ "\t.xword\t", "\t.quad\t" }, { "\t.word\t", "\t.long\t" }, { "\t.hword\t", "\t.short\t" },
		{ "\t.zero\t", "\t.space\t" }, { "\t.hidden\t", "\t.private_extern\t" },
	};

	for (const char *p = t, *e = t + tn; p < e;) {
		const char *nl = memchr(p, '\n', (size_t)(e - p));
		const char *le = nl ? nl : e;

		if (starts(p, le, "\t.type\t") || starts(p, le, "\t.size\t") || starts(p, le, "\t.protected\t") ||
		    starts(p, le, "\t.section\t\".note.GNU-stack\""))
			goto next;
		if (starts(p, le, "\t.section\t")) {
			section(o, p + 10, le, e);
			goto next;
		}
		if (starts(p, le, "\t.bss")) {
			tb_s(o, "\t.section\t__DATA,__bss\n");
			goto next;
		}
		if (starts(p, le, "\t.weak\t")) {
			tb_s(o, defined(t, tn, p + 7, (size_t)(le - p - 7)) ? "\t.weak_definition\t" : "\t.weak_reference\t");
			tb_put(o, p + 7, (size_t)(le - p - 7));
			tb_c(o, '\n');
			goto next;
		}
		for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
			if (starts(p, le, dirs[i].from)) {
				tb_s(o, dirs[i].to);
				p += strlen(dirs[i].from);
				break;
			}
		{
			bool adrp = starts(p, le, "\tadrp\t");
			bool ins = p < le && p[0] == '\t' && p + 1 < le && p[1] != '.';

			for (const char *q = p; q < le;) {
				if (ins && *q == ':' && starts(q, le, ":lo12:")) {
					q = reloc(o, q + 6, le, "@PAGEOFF");
				} else if (ins && starts(q, le, ":got_lo12:")) {
					q = reloc(o, q + 10, le, "@GOTPAGEOFF");
				} else if (ins && starts(q, le, ":got:")) {
					q = reloc(o, q + 5, le, "@GOTPAGE");
				} else if (adrp && *q == ',' && q + 2 < le && q[2] != ':') {
					tb_s(o, ", ");
					q += 2;
					q = reloc(o, q, le, "@PAGE");
				} else if (*q == '.' && q + 1 < le && q[1] == 'L' && (q == p || !idch(q[-1]))) {
					q++;	/* .Lx: Mach-O's local labels are Lx */
				} else {
					tb_c(o, *q++);
				}
			}
			tb_c(o, '\n');
		}
next:
		p = le + 1;
	}
}
