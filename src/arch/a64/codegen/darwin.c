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
#include "../../../codegen/codegen-internal.h"

static bool a64_darwin_is_identifier_character(char ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
	       ch == '_' || ch == '.' || ch == '$';
}

static bool a64_darwin_text_starts_with(const char *p, const char *e, const char *w)
{
	size_t n = strlen(w);

	return (size_t)(e - p) >= n && !memcmp(p, w, n);
}

/* a label "name:" at the start of some line of the text */
static bool a64_darwin_label_is_defined(const char *t, size_t tn, const char *s, size_t n)
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
static const char *a64_darwin_relocation_translate(tbuf *o, const char *p, const char *e, const char *spec)
{
	const char *s = p, *k;

	while (p < e && *p != ']' && *p != ',' && *p != '\n' && *p != ' ')
		p++;
	for (k = s + 1; k < p && *k != '+' && *k != '-'; k++)
		;
	if (s + 1 < p && s[0] == '.' && s[1] == 'L')
		s++;
	vtext_append(o, s, (size_t)(k - s));
	vtext_string_append(o, spec);
	vtext_append(o, k, (size_t)(p - k));
	return p;
}

/* a .xword/.word of a symbol before the next section directive */
static bool a64_darwin_data_has_relocations(const char *p, const char *end)
{
	while (p < end) {
		const char *nl = memchr(p, '\n', (size_t)(end - p));
		const char *le = nl ? nl : end;

		if (a64_darwin_text_starts_with(p, le, "\t.section") || a64_darwin_text_starts_with(p, le, "\t.data") || a64_darwin_text_starts_with(p, le, "\t.text") ||
		    a64_darwin_text_starts_with(p, le, "\t.bss"))
			return false;
		if ((a64_darwin_text_starts_with(p, le, "\t.xword\t") && !(p[8] >= '0' && p[8] <= '9') && p[8] != '-') ||
		    (a64_darwin_text_starts_with(p, le, "\t.word\t") && !(p[7] >= '0' && p[7] <= '9') && p[7] != '-'))
			return true;
		p = le + 1;
	}
	return false;
}

static void a64_darwin_section_translate(tbuf *o, const char *p, const char *e, const char *end)
{
	/* p: after "\t.section\t" */
	if (a64_darwin_text_starts_with(p, e, ".rodata.str1.")) {
		vtext_string_append(o, "\t.section\t__TEXT,__cstring,cstring_literals\n");
	} else if (a64_darwin_text_starts_with(p, e, ".rodata") || a64_darwin_text_starts_with(p, e, "\".rodata")) {
		/* const data holding addresses is __DATA,__const (no text relocations), as clang */
		vtext_string_append(o, a64_darwin_data_has_relocations(e + 1, end) ? "\t.section\t__DATA,__const\n" : "\t.section\t__TEXT,__const\n");
	} else if (a64_darwin_text_starts_with(p, e, ".tdata")) {
		vtext_string_append(o, "\t.section\t__DATA,__thread_data,thread_local_regular\n");
	} else if (a64_darwin_text_starts_with(p, e, ".tbss")) {
		vtext_string_append(o, "\t.section\t__DATA,__thread_bss,thread_local_zerofill\n");
	} else if (a64_darwin_text_starts_with(p, e, ".tlv")) {
		vtext_string_append(o, "\t.section\t__DATA,__thread_vars,thread_local_variables\n");
	} else if (a64_darwin_text_starts_with(p, e, ".init_array")) {
		vtext_string_append(o, "\t.section\t__DATA,__mod_init_func,mod_init_funcs\n");
	} else if (a64_darwin_text_starts_with(p, e, ".fini_array")) {
		vtext_string_append(o, "\t.section\t__DATA,__mod_term_func,mod_term_funcs\n");
	} else if (*p == '"') {
		/* section("seg,sect") as the attribute gave it */
		const char *q = memchr(p + 1, '"', (size_t)(e - p - 1));

		vtext_string_append(o, "\t.section\t");
		vtext_append(o, p + 1, q ? (size_t)(q - p - 1) : 0);
		vtext_char_append(o, '\n');
	} else {
		vtext_string_append(o, "\t.section\t");
		vtext_append(o, p, (size_t)(e - p));
		vtext_char_append(o, '\n');
	}
}

VPRIV void a64_darwin_text_translate(tbuf *o, const char *t, size_t tn)
{
	static const struct { const char *from, *to; } dirs[] = {
		{ "\t.xword\t", "\t.quad\t" }, { "\t.word\t", "\t.long\t" }, { "\t.hword\t", "\t.short\t" },
		{ "\t.zero\t", "\t.space\t" }, { "\t.hidden\t", "\t.private_extern\t" },
	};

	for (const char *p = t, *e = t + tn; p < e;) {
		const char *nl = memchr(p, '\n', (size_t)(e - p));
		const char *le = nl ? nl : e;

		if (a64_darwin_text_starts_with(p, le, "\t.type\t") || a64_darwin_text_starts_with(p, le, "\t.size\t") || a64_darwin_text_starts_with(p, le, "\t.protected\t") ||
		    a64_darwin_text_starts_with(p, le, "\t.section\t\".note.GNU-stack\""))
			goto next;
		if (a64_darwin_text_starts_with(p, le, "\t.section\t")) {
			a64_darwin_section_translate(o, p + 10, le, e);
			goto next;
		}
		if (a64_darwin_text_starts_with(p, le, "\t.bss")) {
			vtext_string_append(o, "\t.section\t__DATA,__bss\n");
			goto next;
		}
		if (a64_darwin_text_starts_with(p, le, "\t.weak\t")) {
			vtext_string_append(o, a64_darwin_label_is_defined(t, tn, p + 7, (size_t)(le - p - 7)) ? "\t.weak_definition\t" : "\t.weak_reference\t");
			vtext_append(o, p + 7, (size_t)(le - p - 7));
			vtext_char_append(o, '\n');
			goto next;
		}
		for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
			if (a64_darwin_text_starts_with(p, le, dirs[i].from)) {
				vtext_string_append(o, dirs[i].to);
				p += strlen(dirs[i].from);
				break;
			}
		{
			bool adrp = a64_darwin_text_starts_with(p, le, "\tadrp\t");
			bool ins = p < le && p[0] == '\t' && p + 1 < le && p[1] != '.';

			for (const char *q = p; q < le;) {
				if (ins && *q == ':' && a64_darwin_text_starts_with(q, le, ":lo12:")) {
					q = a64_darwin_relocation_translate(o, q + 6, le, "@PAGEOFF");
				} else if (ins && a64_darwin_text_starts_with(q, le, ":vcc_tlvp_lo12:")) {
					q = a64_darwin_relocation_translate(o, q + 15, le, "@TLVPPAGEOFF");
				} else if (ins && a64_darwin_text_starts_with(q, le, ":vcc_tlvp:")) {
					q = a64_darwin_relocation_translate(o, q + 10, le, "@TLVPPAGE");
				} else if (ins && a64_darwin_text_starts_with(q, le, ":got_lo12:")) {
					q = a64_darwin_relocation_translate(o, q + 10, le, "@GOTPAGEOFF");
				} else if (ins && a64_darwin_text_starts_with(q, le, ":got:")) {
					q = a64_darwin_relocation_translate(o, q + 5, le, "@GOTPAGE");
				} else if (adrp && *q == ',' && q + 2 < le && q[2] != ':') {
					vtext_string_append(o, ", ");
					q += 2;
					q = a64_darwin_relocation_translate(o, q, le, "@PAGE");
				} else if (*q == '.' && q + 1 < le && q[1] == 'L' && (q == p || !a64_darwin_is_identifier_character(q[-1]))) {
					q++;	/* .Lx: Mach-O's local labels are Lx */
				} else {
					vtext_char_append(o, *q++);
				}
			}
			vtext_char_append(o, '\n');
		}
next:
		p = le + 1;
	}
}
