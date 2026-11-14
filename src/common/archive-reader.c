/*
 * The ar archive reader of common/archive-reader.h: magic, member headers,
 * long and BSD names.  It decodes; the callers decide what is an error, so a
 * field is read as the tools always read it (strtoull on the field, which
 * stops at the first space) and never past the end of the archive.
 */
#include "common/archive-reader.h"

#include <stdlib.h>
#include <string.h>

static const char varchive_magic[] = "!<arch>\n", varchive_thin_magic[] = "!<thin>\n";

varchive_format varchive_format_detect(const void *bytes, size_t size)
{
	if (size < VARCHIVE_MAGIC_SIZE)
		return VARCHIVE_FORMAT_NONE;
	if (!memcmp(bytes, varchive_magic, VARCHIVE_MAGIC_SIZE))
		return VARCHIVE_FORMAT_REGULAR;
	if (!memcmp(bytes, varchive_thin_magic, VARCHIVE_MAGIC_SIZE))
		return VARCHIVE_FORMAT_THIN;
	return VARCHIVE_FORMAT_NONE;
}

void varchive_cursor_init(varchive_cursor *cursor, const void *bytes, size_t size)
{
	*cursor = (varchive_cursor){
		.bytes = bytes,
		.size = size,
		.offset = VARCHIVE_MAGIC_SIZE,
		.thin = varchive_format_detect(bytes, size) == VARCHIVE_FORMAT_THIN,
	};
}

/* a decimal field of the header: at most n bytes, as strtoull reads it */
static uint64_t varchive_field_number(const char *field, size_t n)
{
	char text[16];

	memcpy(text, field, n);
	text[n] = 0;
	return strtoull(text, NULL, 10);
}

static bool varchive_name_is_bsd_symbol_table(const char *name, size_t length)
{
	static const char *const names[] = { "__.SYMDEF", "__.SYMDEF SORTED", "__.SYMDEF_64" };

	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (strlen(names[i]) == length && !memcmp(names[i], name, length))
			return true;
	return false;
}

/* a "/OFFSET" name in the cursor's "//" table */
static void varchive_long_name_find(const varchive_cursor *cursor, varchive_member *member)
{
	uint64_t offset = varchive_field_number(member->header + 1, 15);
	const char *names = cursor->long_names;
	size_t end;

	if (!names || offset >= cursor->long_names_size)
		return;
	for (end = offset; end < cursor->long_names_size && names[end] != '\n'; end++)
		;
	member->name_ends_in_slash = end > offset && names[end - 1] == '/';
	member->name = names + offset;
	member->name_length = end - offset - member->name_ends_in_slash;
}

bool varchive_member_next(varchive_cursor *cursor, varchive_member *member)
{
	if (cursor->offset > cursor->size || cursor->size - cursor->offset < VARCHIVE_HEADER_SIZE)
		return false;

	const char *h = (const char *)cursor->bytes + cursor->offset;
	size_t after = cursor->size - cursor->offset - VARCHIVE_HEADER_SIZE;	/* bytes after the header */

	*member = (varchive_member){
		.header = h,
		.header_offset = cursor->offset,
		.kind = VARCHIVE_MEMBER_FILE,
		.name_kind = VARCHIVE_NAME_SHORT,
		.header_valid = h[58] == '`' && h[59] == '\n',
		.stored_size = varchive_field_number(h + 48, 10),
	};
	if (h[0] == '/' && h[1] == '/' && h[2] == ' ')
		member->kind = VARCHIVE_MEMBER_LONG_NAMES;
	else if (h[0] == '/' && (h[1] == ' ' || !memcmp(h, "/SYM64/", 7)))
		member->kind = VARCHIVE_MEMBER_SYMBOL_TABLE;
	else if (h[0] == '/' && h[1] >= '0' && h[1] <= '9')
		member->name_kind = VARCHIVE_NAME_LONG;
	else if (!memcmp(h, "#1/", 3)) {
		member->name_kind = VARCHIVE_NAME_BSD;
		cursor->bsd = true;
	}
	member->has_data = !cursor->thin || member->kind != VARCHIVE_MEMBER_FILE;
	member->data_fits = member->has_data && member->stored_size <= after;
	member->size = member->stored_size;
	if (member->data_fits)
		member->data = (const uint8_t *)h + VARCHIVE_HEADER_SIZE;

	if (member->kind == VARCHIVE_MEMBER_LONG_NAMES) {
		if (member->data_fits) {
			cursor->long_names = (const char *)member->data;
			cursor->long_names_size = member->size;
		}
	} else if (member->kind == VARCHIVE_MEMBER_FILE) {
		switch (member->name_kind) {
		case VARCHIVE_NAME_SHORT: {
			size_t n = 0;

			while (n < 16 && h[n] != '/')
				n++;
			if (n == 16)
				while (n && h[n - 1] == ' ')
					n--;
			member->name = h;
			member->name_length = n;
			break;
		}
		case VARCHIVE_NAME_LONG:
			varchive_long_name_find(cursor, member);
			break;
		case VARCHIVE_NAME_BSD: {
			uint64_t length = varchive_field_number(h + 3, 13);

			if (length > member->stored_size) {
				member->header_valid = false;
				break;
			}
			if (!member->data_fits)
				break;
			member->name = (const char *)member->data;
			member->name_length = strnlen(member->name, length);
			member->data += length;
			member->size -= length;
			if (varchive_name_is_bsd_symbol_table(member->name, member->name_length))
				member->kind = VARCHIVE_MEMBER_SYMBOL_TABLE;
			break;
		}
		}
	}
	return true;
}

void varchive_cursor_advance(varchive_cursor *cursor, const varchive_member *member)
{
	uint64_t next = (uint64_t)member->header_offset + VARCHIVE_HEADER_SIZE;

	if (member->has_data)
		next += member->stored_size + (cursor->bsd ? 0 : member->stored_size & 1);
	cursor->offset = next > cursor->size ? cursor->size + 1 : (size_t)next;
}
