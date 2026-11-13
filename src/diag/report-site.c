/* The VCC_TRACK_DIAG debug aid: where in vcc each diagnostic was reported. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* " {vcc file.c:123 function}": where vcc reported it (VCC_TRACK_DIAG; symbolized by atos) */
VPRIV void track_str(vbuffer *b, const void *pc)
{
	Dl_info di;
	char cmd[1200], out[512];
	FILE *f = NULL;
	const char *l, *r;

	vbuffer_string_append(b, " {vcc ");
	if (!pc || !dladdr(pc, &di) || strlen(di.dli_fname) > 1024 || strchr(di.dli_fname, '\'')) {
		vbuffer_string_append(b, "?}");
		return;
	}
	snprintf(cmd, sizeof(cmd), "atos -o '%s' -l %p %p 2>/dev/null", di.dli_fname, di.dli_fbase, (const char *)pc - 1);
	/* "function (in vpp) (expr.c:123)" with debug info, else "function (in vpp) + 6304" */
	if ((f = popen(cmd, "r")) && fgets(out, sizeof(out), f) && (l = strstr(out, " (in "))) {
		vbuffer_append(b, out, (size_t)(l - out));
		if ((l = strrchr(out, '(')) && (r = strchr(l, ')')) && memchr(l, ':', (size_t)(r - l))) {
			vbuffer_string_append(b, " ");
			vbuffer_append(b, l + 1, (size_t)(r - l - 1));
		} else if ((l = strstr(out, ") + "))) {
			vbuffer_string_append(b, "+");
			vbuffer_append(b, l + 4, strcspn(l + 4, "\n"));
		}
	} else {
		const char *base = strrchr(di.dli_fname, '/');

		vbuffer_format_append(b, "%s+%#lx", base ? base + 1 : di.dli_fname, (unsigned long)((const char *)pc - (const char *)di.dli_fbase));
	}
	if (f)
		pclose(f);
	vbuffer_string_append(b, "}");
}
