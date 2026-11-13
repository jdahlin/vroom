/* vcc front end: target("...") attribute parsing for x86-64 */
#include "ccimpl.h"

/* Preserve effective function features; parse arch before feature overrides. */
VPRIV void target_attr(cc *c, attrs *a, uint32_t str, uint32_t loc)
{
	if (!str || c->o->target != PP_LINUX_X86_64) return;
	const char *text = c->strs[str - 1].s;
	x86_features features = c->o->x86_features_set ? c->o->x86_raw : cc_x86_features(c, 0), disabled = c->o->x86_disabled;
	char *copy = strdup(text), *save = NULL, *part;
	bool valid = true;
	for (part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
		if (!strncmp(part, "arch=", 5) && !x86_cpu_features(part + 5, &features)) {
			pp_dinfo d = { .what = "warning", .loc = loc };
			pp_warnx(c->p, "ignored-attributes", true, NULL, &d,
					"unsupported '%s' in the 'target' attribute string; 'target' attribute ignored", part);
			valid = false;
		}
	}
	for (unsigned i = 0; i < X86_FEATURE_WORDS; i++)
		features.bits[i] = (features.bits[i] & ~c->o->x86_override_off.bits[i]) | c->o->x86_override_on.bits[i];
	free(copy);
	copy = strdup(text); save = NULL;
	for (part = strtok_r(copy, ",", &save); part; part = strtok_r(NULL, ",", &save)) {
		if (!strncmp(part, "arch=", 5) || !strcmp(part, "default")) continue;
		if (!strcmp(part, "general-regs-only")) {
			const unsigned bits[] = {X86_FEATURE_X87, X86_FEATURE_MMX, X86_FEATURE_SSE};
			for (unsigned k = 0; k < sizeof(bits)/sizeof(*bits); k++) {
				x86_feature_apply(&features, bits[k], false);
				disabled.bits[bits[k]/64] |= UINT64_C(1) << (bits[k]%64);
			}
			continue;
		}
		bool enable = strncmp(part, "no-", 3) != 0;
		const char *name = enable ? part : part + 3;
		if (!strcmp(name, "sse4")) name = enable ? "sse4.2" : "sse4.1";
		int bit = x86_feature_find(name);
		if (bit < 0 || !x86_feature_has(x86_attr_valid, (unsigned)bit)) {
			pp_dinfo d = { .what = "warning", .loc = loc };
			pp_warnx(c->p, "ignored-attributes", true, NULL, &d,
					"unsupported '%s' in the 'target' attribute string; 'target' attribute ignored", part);
			valid = false;
		} else {
			x86_feature_apply(&features, (unsigned)bit, enable);
			if (!enable) disabled.bits[bit / 64] |= UINT64_C(1) << (bit % 64);
			else disabled.bits[bit / 64] &= ~(UINT64_C(1) << (bit % 64));
		}
	}
	free(copy);
	if (valid) { x86_features_finish(&features, disabled); a->x86_features = features; a->x86_target = 1; }
}
