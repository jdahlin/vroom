/* Mapping input files and reading archives into the list of pending members. */

#include "ld-internal.h"

/* ---------------- loading ---------------- */

VPRIV uint8_t *ld_file_map(struct ld_context *ctx, const char *path, size_t *size)
{
	VAUTOCLOSE int fd = openat(ctx->options.directory_fd, path, O_RDONLY);
	if (fd < 0) {
		ld_error_report(ctx, "cannot open %s: %s", path, strerror(errno));
		return NULL;
	}
	vfile_mapping m;
	struct stat st;
	if (!vfile_map_fd(fd, &m, &st)) {
		ld_error_report(ctx, "cannot mmap %s", path);
		return NULL;
	}
	*size = m.n;
	return m.n ? (uint8_t *)m.p : (uint8_t *)"";	/* an empty file is not a failure */
}


/* the members of the archive at path into the pending list: by path for a thin archive, else as
   "path(member)" with their bytes in the mapping */
VPRIV void ld_archive_add(struct ld_context *ctx, const char *path, const uint8_t *b, size_t n)
{
	varchive_cursor cursor;
	varchive_cursor_init(&cursor, b, n);
	const char *dir = "";
	const char *slash = strrchr(path, '/');
	if (cursor.thin && slash)
		dir = ld_script_text_duplicate(path, slash + 1);
	varchive_member m;
	for (; varchive_member_next(&cursor, &m); varchive_cursor_advance(&cursor, &m)) {
		if (!m.header_valid) {
			ld_error_report(ctx, "%s: invalid archive member header at offset %zu", path, m.header_offset);
			return;
		}
		if (m.kind == VARCHIVE_MEMBER_LONG_NAMES && !m.data_fits) {
			ld_error_report(ctx, "%s: archive member // is truncated", path);
			return;
		}
		if (m.kind != VARCHIVE_MEMBER_FILE)
			continue;
		if (!m.name) {
			int field = 16;
			while (field && m.header[field - 1] == ' ')
				field--;
			ld_error_report(ctx, "%s: invalid long name offset %.*s", path, field, m.header);
			return;
		}
		const char *name = ld_script_text_duplicate(m.name, m.name + m.name_length);
		if (cursor.thin) {
			char *full = ld_region_alloc(strlen(dir) + strlen(name) + 1);
			strcpy(full, name[0] == '/' ? "" : dir);
			strcat(full, name);
			VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = full}), ld_region_alloc);
		} else {
			if (!m.data_fits) {
				ld_error_report(ctx, "%s(%s): archive member goes past the end of the file", path, name);
				return;
			}
			char *full = ld_region_alloc(strlen(path) + strlen(name) + 3);
			sprintf(full, "%s(%s)", path, name);
			VVEC_PUSH(ctx->inputs.pending, ((ld_pending_member){.path = full, .file_bytes = m.data, .size = m.size}), ld_region_alloc);
		}
	}
}
