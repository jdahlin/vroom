/*
 * vpp --lint, the naming check: a file-scope function or object defined in a file named on the
 * command line must carry its module's prefix (doc/STYLE.md, "Module prefixes").
 *
 * Runs once at the end of the TU (cc_run), on the symbol table the parse built anyway: no AST is
 * kept for it and nothing runs unless cc_opts.lint is set (only `vpp --lint`, driver/cmdline.c).
 * The module is the file's path after its last "src/": the longest directory in the table below
 * decides.  A file outside the table (bin/, test/, tools/, headers/, arch/wasm, a file not under
 * src/ at all) is not checked: no finding.  Names in headers the file includes are not checked,
 * and neither are declarations without a definition (a prototype of a library function).
 * `main` and names starting with two underscores are exempt.  Each finding is a warning
 * [-Wvcc-lint-naming] whose text names the expected prefix; no fix-it, since the new name is
 * not unambiguous.
 */
#include "ccimpl.h"

typedef struct lint_module {
	const char *dir;	/* after src/, with the trailing slash */
	const char *prefix[3];	/* any of these (NULL ends); "v" is the common/ rule: v + facility + _ */
} lint_module;

/* longest directory first within one parent (codegen/walk/ before codegen/) */
static const lint_module lint_modules[] = {
	{ "common/", { "v" } },
	{ "port/", { "v" } },
	{ "frontend/lex/", { "lex_" } },
	{ "frontend/pp/", { "pp_" } },
	{ "frontend/c/", { "cc_", "ast_" } },
	{ "diag/", { "diag_" } },
	{ "codegen/walk/", { "walk_" } },
	{ "codegen/rec/", { "record_" } },
	{ "codegen/vir/", { "virbuild_" } },
	{ "codegen/", { "codegen_" } },
	{ "vir/", { "vir_" } },
	{ "as/", { "as_" } },
	{ "obj/", { "obj_" } },
	{ "ld/", { "ld_" } },
	{ "driver/", { "driver_" } },
	{ "server/", { "vccd_" } },
	{ "utils/", { "ar_", "shell_", "fixdep_" } },
	{ "arch/a64/", { "a64_" } },
	{ "arch/x64/", { "x64_" } },
	{ "platform/linux/", { "elf_" } },
	{ "platform/darwin/", { "macho_" } },
};

/* the module of a path (name, len), NULL for a file the table does not cover */
static const lint_module *lint_module_of(const char *path, uint32_t len)
{
	const char *rest = NULL;

	for (const char *s = path; s + 4 <= path + len; s++)
		if (!memcmp(s, "src/", 4) && (s == path || s[-1] == '/'))
			rest = s + 4;
	if (!rest)
		return NULL;
	for (size_t i = 0; i < sizeof lint_modules / sizeof *lint_modules; i++) {
		size_t n = strlen(lint_modules[i].dir);

		if ((size_t)(path + len - rest) > n && !memcmp(rest, lint_modules[i].dir, n))
			return &lint_modules[i];
	}
	return NULL;
}

static bool lint_name_ok(const lint_module *m, const char *name, uint32_t len)
{
	if ((len == 4 && !memcmp(name, "main", 4)) || (len >= 2 && name[0] == '_' && name[1] == '_'))
		return true;
	for (int k = 0; k < 3 && m->prefix[k]; k++) {
		size_t n = strlen(m->prefix[k]);

		if (n == 1) {	/* v + facility + _ */
			const char *u = len > 2 ? memchr(name + 1, '_', len - 1) : NULL;

			if (name[0] == 'v' && u && u > name + 1)
				return true;
		} else if (len > n && !memcmp(name, m->prefix[k], n))
			return true;
	}
	return false;
}

VPRIV void cc_lint_naming_report(cc *c)
{
	static const char *const g[] = { "vcc-lint", NULL };

	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		const lint_module *m;
		const char *file, *name;
		uint32_t flen, line, col, nlen, at;
		bool fn = y->kind == S_FUNC, defined = false;

		if ((y->kind != S_VAR && !fn) || y->depth || !y->name || lookup(c, y->name) != i || (y->flags & SF_OVERLOAD))
			continue;
		for (uint32_t s = i; s; s = c->syms[s].next)
			defined |= (c->syms[s].flags & (fn ? SF_DEFINED : (SF_DEFINED | SF_TENTATIVE))) != 0;
		at = y->defloc ? y->defloc : y->loc;
		if (!defined || !pp_location_is_in_main_file(c->p, at))
			continue;
		pp_location_describe(c->p, at, &file, &flen, &line, &col);
		m = lint_module_of(file, flen);
		name = idstr_(y->name);
		nlen = (uint32_t)idlen_(y->name);
		if (!m || lint_name_ok(m, name, nlen))
			continue;
		{
			diag_span r = { at, pp_token_end_find(c->p, at) };
			diag_info d = { "warning", at, &r, 1, NULL, 0, name, nlen };
			char want[64] = "";

			for (int k = 0; k < 3 && m->prefix[k]; k++)
				snprintf(want + strlen(want), sizeof want - strlen(want), "%s%s", k ? "' or '" : "",
					 m->prefix[k][1] ? m->prefix[k] : "v<facility>_");
			if (fn)
				diag_warning_report(c->p, "vcc-lint-naming", true, g, &d, "function '%.*s' should be named '%s...' (module src/%s)",
					 (int)nlen, name, want, m->dir);
			else
				diag_warning_report(c->p, "vcc-lint-naming", true, g, &d, "global variable '%.*s' should be named '%s...' (module src/%s)",
					 (int)nlen, name, want, m->dir);
		}
	}
}
