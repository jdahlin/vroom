/* Mapping input files and reading archives into the list of pending members. */

#include "ld-internal.h"

/* ---------------- loading ---------------- */

VPRIV uint8_t *ld_file_map(struct ld_context *ctx, const char *path, size_t *size)
{
	int fd = openat(ctx->options.directory_fd, path, O_RDONLY);
	if (fd < 0) {
		ld_error_report(ctx, "cannot open %s: %s", path, strerror(errno));
		return NULL;
	}
	struct stat st;
	fstat(fd, &st);
	*size = st.st_size;
	void *p = st.st_size ? mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : (void *)"";
	close(fd);
	if (p == MAP_FAILED) {
		ld_error_report(ctx, "cannot mmap %s", path);
		return NULL;
	}
	return p;
}


VPRIV void ld_archive_add(struct ld_context *ctx, const char *path, const uint8_t *b, size_t n)
{
	bool thin = !memcmp(b, "!<thin>\n", 8);
	const char *dir = "";
	const char *slash = strrchr(path, '/');
	if (thin && slash)
		dir = ld_script_text_duplicate(path, slash + 1);
	size_t off = 8;
	const char *longn = NULL;
	size_t longlen = 0;
	while (off + 60 <= n) {
		const char *h = (const char *)b + off;
		char sz[11];
		memcpy(sz, h + 48, 10);
		sz[10] = 0;
		size_t size = strtoull(sz, NULL, 10);
		off += 60;
		if (memcmp(h + 58, "`\n", 2)) {
			ld_error_report(ctx, "%s: invalid archive member header at offset %zu", path, off - 60);
			return;
		}
		char nm[17];
		memcpy(nm, h, 16);
		nm[16] = 0;
		for (int i = 15; i >= 0 && nm[i] == ' '; i--)
			nm[i] = 0;
		const char *name;
		if (!strcmp(nm, "/") || !strcmp(nm, "/SYM64/")) {
			off += size + (size & 1);
			continue;
		}
		if (!strcmp(nm, "//")) {
			if (size > n - off) {
				ld_error_report(ctx, "%s: archive member // is truncated", path);
				return;
			}
			longn = (const char *)b + off;
			longlen = size;
			off += size + (size & 1);
			continue;
		}
		if (nm[0] == '/' && longn) {
			size_t i = strtoull(nm + 1, NULL, 10);
			if (i >= longlen) {
				ld_error_report(ctx, "%s: invalid long name offset %s", path, nm);
				return;
			}
			const char *e = longn + i;
			while (e < longn + longlen && *e != '\n')
				e++;
			if (e > longn + i && e[-1] == '/')
				e--;
			name = ld_script_text_duplicate(longn + i, e);
		} else {
			size_t l = strlen(nm);
			if (l && nm[l - 1] == '/')
				nm[l - 1] = 0;
			name = ld_region_string_duplicate(nm);
		}
		if (thin) {
			char *full = ld_region_alloc(strlen(dir) + strlen(name) + 1);
			strcpy(full, name[0] == '/' ? "" : dir);
			strcat(full, name);
			VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = full}), ld_region_alloc);
		} else {
			if (size > n - off) {
				ld_error_report(ctx, "%s(%s): archive member goes past the end of the file", path, name);
				return;
			}
			char *full = ld_region_alloc(strlen(path) + strlen(name) + 3);
			sprintf(full, "%s(%s)", path, name);
			VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = full, .file_bytes = b + off, .size = size}), ld_region_alloc);
			off += size + (size & 1);
		}
	}
}
