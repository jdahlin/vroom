/* vcc preprocessor: builtin macros and the __has_* queries */
#include "pp-internal.h"
#include <sys/stat.h>
#include "ppdata.inc"
#define PPDATA_X86_HAS
#include "ppdata-x86.inc"
#define PPDATA_WIN64_HAS
#include "ppdata-win64.inc"

/* ---------------- builtin macros ---------------- */

VPRIV const pp_context *pp_file_context_current(const pp *p)
{
	for (int d = p->context_depth - 1; d >= 0; d--)
		if (p->contexts[d].type == PP_CONTEXT_FILE)
			return &p->contexts[d];
	return NULL;
}

static void pp_token_number_make(pp *p, ptok *t, unsigned long long v)
{
	char *b = varena_alloc(&p->temporary_region, 40);
	int n = pp_unsigned_format(b, v);

	memset(b + n, 0, 16);
	t->s = b;
	t->len = (uint32_t)n;
	t->kind = LEX_TOKEN_NUMBER;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(LEX_TOKEN_FLAG_SPLICED | LEX_TOKEN_FLAG_RAW_STRING | PP_TOKEN_FLAG_NO_EXPAND);
}

static void pp_token_string_make(pp *p, ptok *t, const char *s, size_t n)
{
	char *b = varena_alloc(&p->temporary_region, 2 * n + 24), *w = b;

	*w++ = '"';
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '\\' || s[i] == '"')
			*w++ = '\\';
		*w++ = s[i];
	}
	*w++ = '"';
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	t->kind = LEX_TOKEN_STRING;
	t->id = 0;
	t->ctr = 0;
	t->flags &= ~(LEX_TOKEN_FLAG_SPLICED | LEX_TOKEN_FLAG_RAW_STRING | PP_TOKEN_FLAG_NO_EXPAND);
}

/* put t back: it is read again next */
static void pp_token_unread(pp *p, const ptok *t)
{
	ptok *c = varena_alloc(&p->temporary_region, sizeof(*c));

	*c = *t;
	pp_context_tokens_push(p, c, 1, 0, 0, 0);
}

/*
 * A header name after '(' (has_include) or in #include: "x" or <x> as
 * written, or else from macro-expanded tokens.  Returns false if malformed.
 */
VPRIV bool pp_header_name_read(pp *p, char *buf, size_t cap, size_t *len, bool *angled, ptok *t)
{
	size_t n = 0;
	bool expanded = false;

	if (t->kind == LEX_TOKEN_IDENTIFIER) {
		pp_token_unread(p, t);
		pp_token_expanded_next(p, t);
		expanded = true;
	}
	if (t->kind == LEX_TOKEN_STRING) {
		size_t l;
		const char *s = pp_token_spell(t, buf, &l);

		if (l < 2 || l - 2 >= cap)
			return false;
		memmove(buf, s + 1, l - 2);
		*len = l - 2;
		*angled = false;
		return true;
	}
	if (t->kind != LEX_TOKEN_LESS)
		return false;
	{
		const ptok lt = *t;
		bool contiguous = true;
		const char *prev_end = lt.s + lt.len;

		/* <...> written out is one header-name token: no expansion inside */
		for (;;) {
			if (expanded)
				pp_token_expanded_next(p, t);
			else
				pp_token_raw_next(p, t);
			if (t->kind == LEX_TOKEN_EOF)
				return false;
			if (t->kind == LEX_TOKEN_GREATER)
				break;
			if (n && (t->flags & (LEX_TOKEN_FLAG_SPACE_BEFORE | LEX_TOKEN_FLAG_LINE_START)) && n < cap)
				buf[n++] = ' ';
			if (t->s < prev_end || t->s > prev_end + 256)
				contiguous = false;
			prev_end = t->s + t->len;
			if (n + t->len >= cap)
				return false;
			n = (size_t)(pp_token_spell_to(buf + n, t) - buf);
		}
		/* written in one piece in a file: take the text as is */
		if (contiguous && t->s > lt.s && (size_t)(t->s - lt.s - 1) < cap) {
			n = (size_t)(t->s - lt.s - 1);
			memcpy(buf, lt.s + 1, n);
		}
	}
	*len = n;
	*angled = true;
	return true;
}

/* "(" ... ")" as one spelling, for the feature tests */
static bool pp_feature_argument_read(pp *p, char *buf, size_t cap, uint32_t loc)
{
	size_t n = 0;
	int depth = 0;
	ptok t;

	pp_token_raw_next(p, &t);
	if (t.kind != LEX_TOKEN_LEFT_PAREN) {
		PP_ERROR(p, loc, "missing '(' after feature-test macro");
		return false;
	}
	for (;;) {
		pp_token_raw_next(p, &t);
		if (t.kind == LEX_TOKEN_EOF)
			return false;
		if (t.kind == LEX_TOKEN_LEFT_PAREN)
			depth++;
		if (t.kind == LEX_TOKEN_RIGHT_PAREN && !depth--)
			break;
		if (n + t.len + 1 >= cap)
			return false;
		n = (size_t)(pp_token_spell_to(buf + n, &t) - buf);
	}
	buf[n] = 0;
	return true;
}

static int pp_feature_query_answer(pp *p, int b, const char *name, uint32_t loc)
{
	static const char *const q[] = {
		[PP_BUILTIN_HAS_ATTRIBUTE] = "__has_attribute", [PP_BUILTIN_HAS_BUILTIN] = "__has_builtin",
		[PP_BUILTIN_HAS_FEATURE] = "__has_feature", [PP_BUILTIN_HAS_EXTENSION] = "__has_extension",
		[PP_BUILTIN_HAS_C_ATTRIBUTE] = "__has_c_attribute", [PP_BUILTIN_HAS_CPP_ATTRIBUTE] = "__has_cpp_attribute",
		[PP_BUILTIN_HAS_DECLSPEC_ATTRIBUTE] = "__has_declspec_attribute", [PP_BUILTIN_HAS_WARNING] = "__has_warning",
		[PP_BUILTIN_HAS_CONSTEXPR_BUILTIN] = "__has_constexpr_builtin",
		[PP_BUILTIN_IS_TARGET_ARCHITECTURE] = "__is_target_arch", [PP_BUILTIN_IS_TARGET_OS] = "__is_target_os",
		[PP_BUILTIN_IS_TARGET_VENDOR] = "__is_target_vendor",
		[PP_BUILTIN_IS_TARGET_ENVIRONMENT] = "__is_target_environment",
		[PP_BUILTIN_IS_TARGET_VARIANT_OS] = "__is_target_variant_os",
		[PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT] = "__is_target_variant_environment",
		[PP_BUILTIN_BUILDING_MODULE] = "__building_module",
	};

	size_t lo = 0, hi = sizeof(has_tab) / sizeof(has_tab[0]);

	if (b == PP_BUILTIN_BUILDING_MODULE)
		return 0;	/* no modules */
	if (PP_IS_X86_64(p->options->target)) {	/* its own table (gen-x86data.sh, gen-win64data.sh) */
		bool win = p->options->target == PP_WIN64;
		size_t xl = 0, xh = win ? sizeof(has_tab_win64) / sizeof(has_tab_win64[0]) : sizeof(has_tab_x86) / sizeof(has_tab_x86[0]);

		while (xl < xh) {
			size_t mid = (xl + xh) / 2;
			const char *mq = win ? has_tab_win64[mid].q : has_tab_x86[mid].q, *mn = win ? has_tab_win64[mid].name : has_tab_x86[mid].name;
			int c = strcmp(mq, q[b]);

			if (!c)
				c = strcmp(mn, name);
			if (!c)
				return win ? has_tab_win64[mid].v : has_tab_x86[mid].v;
			if (c < 0)
				xl = mid + 1;
			else
				xh = mid;
		}
		lo = hi;	/* not in the shared table either */
	}
	/* the table holds the nonzero answers, sorted by (query, name) */
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int c = strcmp(has_tab[mid].q, q[b]);

		if (!c)
			c = strcmp(has_tab[mid].name, name);
		if (!c)
			return has_tab[mid].v[p->options->target == PP_WASM32 ? PP_WASM64 : p->options->target];	/* wasm32 asks as wasm64 */
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	/* clang: the __is_target_* macros answer 1 too (AvailabilityInternal.h asks) */
	if (b == PP_BUILTIN_HAS_BUILTIN && !strncmp(name, "__is_target_", 12))
		for (int k = PP_BUILTIN_IS_TARGET_ARCHITECTURE; k <= PP_BUILTIN_IS_TARGET_VARIANT_ENVIRONMENT; k++)
			if (!strcmp(name, q[k]))
				return 1;
	/* the table is what the kernel and SDK headers asked clang; the front end knows more */
	if (b == PP_BUILTIN_HAS_BUILTIN && pp_builtin_known && pp_builtin_known(name))
		return 1;
	return 0;
}

int (*pp_builtin_known)(const char *name);

/* -Wc2y-extensions (-pedantic): __COUNTER__; out of line, pp_builtin_macro_expand() is hot */
static __attribute__((noinline, cold)) void pp_counter_extension_warn(pp *p, uint32_t loc)
{
	static const char *const g[] = { "pedantic", NULL };
	diag_info d = { "warning", loc, NULL, 0, NULL, 0, "__COUNTER__", 11 };

	if (diag_warning_report(p, "c2y-extensions", false, g, &d, "'__COUNTER__' is a C2y extension"))
		PP_HEADER_CACHE_REGION_REJECT(p);
}

VPRIV int pp_builtin_macro_expand(pp *p, const pp_macro *m, ptok *t)
{
	uint8_t nf = t->flags & (LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE);
	const pp_context *fc = pp_file_context_current(p);
	char buf[4096];

	switch (m->builtin) {
	case PP_BUILTIN_LINE:
		pp_token_number_make(p, t, pp_location_line(p, t->loc));
		break;
	case PP_BUILTIN_FILE: {
		uint32_t l, loc = t->loc;
		const char *s;

		if (loc < p->files[fc->file_index].location_base || loc > p->files[fc->file_index].location_base + p->files[fc->file_index].lex_file->size)
			loc = p->files[fc->file_index].location_base + fc->lexed_tokens[fc->index ? fc->index - 1 : 0].off;
		s = pp_location_presumed_name(p, fc->file_index, loc, &l);
		pp_token_string_make(p, t, s, l);
		break;
	}
	case PP_BUILTIN_FILE_NAME: {
		const pp_file_entry *e = &p->files[fc->file_index];
		const char *s = e->name + e->name_length;

		while (s > e->name && s[-1] != '/')
			s--;
		pp_token_string_make(p, t, s, e->name_length - (size_t)(s - e->name));
		break;
	}
	case PP_BUILTIN_BASE_FILE:
		PP_HEADER_CACHE_REGION_REJECT(p);
		pp_token_string_make(p, t, p->base_file_name, p->base_file_name_length);
		break;
	case PP_BUILTIN_COUNTER:
		if (unlikely(p->options->nwargs) && pp_warning_is_possible(p, PP_WARNING_C2Y))
			pp_counter_extension_warn(p, t->loc);	/* (the kernel's __UNIQUE_ID: keep the common case one test) */
		if (p->recording.on) {
			if (p->in_directive)
				PP_HEADER_CACHE_REGION_REJECT(p);	/* the value decides something */
			pp_token_number_make(p, t, p->counter_value);
			t->ctr = pp_header_cache_counter_provenance_new(p, p->counter_value, 0, t->len, 0);
			p->counter_value++;
		} else {
			pp_token_number_make(p, t, p->counter_value++);
		}
		break;
	case PP_BUILTIN_FLT_EVAL_METHOD:
		/* clang: a builtin macro, 0 on every target vcc has (SSE on x86-64; -ffp-eval-method and
		   #pragma clang fp eval_method are not supported).  float.h's FLT_EVAL_METHOD names it. */
		pp_token_number_make(p, t, 0);
		break;
	case PP_BUILTIN_INCLUDE_LEVEL:
		for (int i = 0; i < p->recording.level_count; i++)
			p->recording.levels[i].uses_level = 1;
		pp_token_number_make(p, t, p->file_context_count > 1 ? (unsigned)p->file_context_count - 1 : 0);
		break;
	case PP_BUILTIN_DATE: case PP_BUILTIN_TIME: case PP_BUILTIN_TIMESTAMP: {
		time_t now = p->options->has_epoch ? (time_t)p->options->epoch : time(NULL);

		PP_HEADER_CACHE_REGION_REJECT(p);
		struct tm tm;
		struct stat st;

		/* clang: __TIMESTAMP__ is the current file's mtime */
		if (m->builtin == PP_BUILTIN_TIMESTAMP && !p->options->has_epoch && fc) {
			if (stat(p->files[fc->file_index].lex_file->path, &st) != 0) {
				pp_token_string_make(p, t, "??? ??? ?? ??:??:?? ????", 24);
				break;
			}
			now = st.st_mtime;
		}

		if (p->options->has_epoch)	/* reproducible builds: UTC, as clang */
			gmtime_r(&now, &tm);
		else
			localtime_r(&now, &tm);
		strftime(buf, sizeof(buf), m->builtin == PP_BUILTIN_DATE ? "%b %e %Y" :
			 m->builtin == PP_BUILTIN_TIME ? "%H:%M:%S" : "%a %b %e %H:%M:%S %Y", &tm);
		pp_token_string_make(p, t, buf, strlen(buf));
		break;
	}
	case PP_BUILTIN_PRAGMA: {
		ptok s, r;
		size_t l;
		const char *sp;
		char *d;
		size_t dn = 0;

		if (p->argument_preexpansion)
			return 1;	/* runs when the result is rescanned */
		pp_token_expanded_next(p, &r);
		pp_token_expanded_next(p, &s);
		if (r.kind != LEX_TOKEN_LEFT_PAREN || !pp_kind_is_string_literal(s.kind)) {
			PP_ERROR(p, t->loc, "_Pragma takes a parenthesized string literal");
			return 1;
		}
		if (s.ctr)
			PP_HEADER_CACHE_REGION_REJECT(p);
		pp_token_expanded_next(p, &r);
		if (r.kind != LEX_TOKEN_RIGHT_PAREN)
			PP_ERROR(p, t->loc, "_Pragma takes a parenthesized string literal");
		{
			char tmp[s.len + 1];

			sp = pp_token_spell(&s, tmp, &l);
			while (l && *sp != '"') {	/* encoding prefix */
				sp++;
				l--;
			}
			d = varena_alloc(&p->temporary_region, l + 16);
			memcpy(d, "pragma ", 7);
			dn = 7;
			for (size_t i = 1; i + 1 < l; i++) {
				if (sp[i] == '\\' && (sp[i + 1] == '\\' || sp[i + 1] == '"'))
					i++;
				d[dn++] = sp[i];
			}
		}
		if (pp_pragma_run(p, d, dn, t->loc, t, 0)) {
			t->flags = LEX_TOKEN_FLAG_LINE_START;
			return 1;
		}
		return 0;
	}
	case PP_BUILTIN_HAS_INCLUDE: case PP_BUILTIN_HAS_INCLUDE_NEXT: {
		ptok r;
		size_t n;
		bool angled;
		int32_t sd;
		const char *path;

		pp_token_raw_next(p, &r);
		if (r.kind != LEX_TOKEN_LEFT_PAREN) {
			PP_ERROR(p, t->loc, "missing '(' after __has_include");
			return 1;
		}
		pp_token_raw_next(p, &r);
		if (!pp_header_name_read(p, buf, sizeof(buf), &n, &angled, &r)) {
			PP_ERROR(p, t->loc, "invalid __has_include argument");
			return 1;
		}
		pp_token_raw_next(p, &r);
		if (r.kind != LEX_TOKEN_RIGHT_PAREN)
			PP_ERROR(p, t->loc, "missing ')' after __has_include");
		{
			const lex_file *hf = pp_include_find(p, fc, buf, n, angled, m->builtin == PP_BUILTIN_HAS_INCLUDE_NEXT, &sd, &path, NULL);

			if (hf && p->options->deps)
				pp_dependency_add(p, path, pp_include_system_kind(p, sd));	/* a dependency, as clang's */
			pp_token_number_make(p, t, hf != NULL);
		}
		break;
	}
	case PP_BUILTIN_HAS_EMBED:
		if (!p->in_directive) {
			PP_ERROR(p, t->loc, "'__has_embed' must be used within a preprocessing directive");
			return 1;
		}
		pp_token_number_make(p, t, pp_embed_has_query(p, fc, t->loc));
		break;
	case PP_BUILTIN_IS_IDENTIFIER:
		if (!pp_feature_argument_read(p, buf, sizeof(buf), t->loc))
			return 1;
		pp_token_number_make(p, t, 1);
		break;
	default:
		if (!pp_feature_argument_read(p, buf, sizeof(buf), t->loc)) {
			PP_ERROR(p, t->loc, "malformed feature test");
			return 1;
		}
		pp_token_number_make(p, t, (unsigned long long)pp_feature_query_answer(p, m->builtin, buf, t->loc));
		break;
	}
	t->flags = (t->flags & ~(LEX_TOKEN_FLAG_LINE_START | LEX_TOKEN_FLAG_SPACE_BEFORE)) | nf;
	return 1;
}
