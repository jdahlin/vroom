/* @FILE response files as GNU ld and lld read them. */

/* Response files as GNU ld and lld read them: @FILE is replaced by FILE's
 * words (blanks separate them; '...', "..." and \ quote), recursively; an
 * @FILE that cannot be read stays as it is.  kbuild links a module of
 * several objects with `ld -r -o m.o @m.mod`.  The new vector and its words
 * live in one malloc'ed block list, *keep, freed by the caller. */
struct ld_response_memory { struct ld_response_memory *next; };

static void *ld_response_alloc(struct ld_response_memory **keep, size_t n)
{
	struct ld_response_memory *m = malloc(sizeof *m + n);
	if (!m) {
		fputs("vld: out of memory\n", stderr);
		exit(1);
	}
	m->next = *keep;
	*keep = m;
	return m + 1;
}

static bool ld_response_files_expand(int *pargc, char ***pargv, int dirfd, struct ld_response_memory **keep)
{
	bool any = false;
	for (int i = 1; i < *pargc; i++)
		any |= (*pargv)[i][0] == '@';
	if (!any)
		return false;
	/* n == i throughout: out[n] is written only after argv[i] is read, and
	 * after the first expansion out and argv are the same vector */
	int argc = *pargc, n = 0, nexp = 0;
	char **argv = *pargv, **out = ld_response_alloc(keep, (argc + 1) * sizeof *out);
	for (int i = 0; i < argc; i++) {
		const char *a = argv[i];
		char *text = NULL;
		size_t len = 0;
		if (i > 0 && a[0] == '@' && nexp < 64) {
			int fd = openat(dirfd, a + 1, O_RDONLY | O_CLOEXEC);
			struct stat st;
			if (fd >= 0 && !fstat(fd, &st) && st.st_size >= 0) {
				len = (size_t)st.st_size;
				text = ld_response_alloc(keep, len + 1);
				size_t got = 0;
				while (got < len) {
					ssize_t r = read(fd, text + got, len - got);
					if (r <= 0)
						break;
					got += (size_t)r;
				}
				len = got;
				text[len] = 0;
			}
			if (fd >= 0)
				close(fd);
		}
		if (!text) {
			out[n++] = (char *)a;
			continue;
		}
		/* split in place: a word never grows when its quotes are removed */
		char *p = text, *end = text + len;
		int nw = 0;
		char **words = ld_response_alloc(keep, (len / 2 + 2) * sizeof *words);
		while (p < end) {
			while (p < end && isspace((unsigned char)*p))
				p++;
			if (p >= end)
				break;
			char *w = p, *d = p, q = 0;
			while (p < end && (q || !isspace((unsigned char)*p))) {
				char c = *p++;
				if (c == '\\' && p < end)
					*d++ = *p++;
				else if (q && c == q)
					q = 0;
				else if (!q && (c == '\'' || c == '"'))
					q = c;
				else
					*d++ = c;
			}
			if (p < end)
				p++;
			*d = 0;
			words[nw++] = w;
		}
		/* the words replace @FILE; an @ among them is expanded on the next pass */
		int rest = argc - i - 1;
		char **nargv = ld_response_alloc(keep, (n + nw + rest + 1) * sizeof *nargv);
		memcpy(nargv, out, n * sizeof *nargv);
		memcpy(nargv + n, words, nw * sizeof *nargv);
		memcpy(nargv + n + nw, argv + i + 1, rest * sizeof *nargv);
		nargv[n + nw + rest] = NULL;
		argv = out = nargv, argc = n + nw + rest;
		i = n - 1;	/* rescan from the first word */
		nexp++;	/* (a file that names itself stops here) */
	}
	out[n] = NULL;
	*pargc = n;
	*pargv = out;
	return true;
}
