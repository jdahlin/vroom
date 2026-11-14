/* Glob matching for input section patterns. */

#include "ld-internal.h"

/* ---------------- glob matching ---------------- */

VPRIV bool ld_glob_match(const char *p, const char *s)
{
	const char *star_p = NULL, *star_s = NULL;
	while (*s) {
		if (*p == '*') {
			star_p = ++p;
			star_s = s;
			continue;
		}
		if (*p == '[') {
			const char *q = p + 1;
			bool neg = false, ok = false;
			if (*q == '^' || *q == '!') { neg = true; q++; }
			const char *start = q;
			while (*q && (*q != ']' || q == start)) {
				if (q[1] == '-' && q[2] && q[2] != ']') {
					if ((unsigned char)*s >= (unsigned char)q[0] && (unsigned char)*s <= (unsigned char)q[2])
						ok = true;
					q += 3;
				} else {
					if (*q == *s)
						ok = true;
					q++;
				}
			}
			if (*q == ']' && ok != neg) {
				p = q + 1;
				s++;
				continue;
			}
		} else if (*p == '?' || (*p && *p == *s)) {
			if (*p == '\\' && p[1]) { }
			p++;
			s++;
			continue;
		}
		if (!star_p)
			return false;
		p = star_p;
		s = ++star_s;
	}
	while (*p == '*')
		p++;
	return !*p;
}

VPRIV bool ld_pattern_match(const ld_pattern *p, const char *name)
{
	for (uint32_t i = 0; i < p->glob_count; i++)
		if (ld_glob_match(p->globs[i], name))
			return true;
	return false;
}
