/*
 * Reading ar archives in place: the System V/GNU format with its "//" table
 * of long names, GNU thin archives ("!<thin>": the members are files named by
 * path) and BSD's "#1/LENGTH" names (the name at the start of the data).  The
 * one reader of the ELF linker (ld_archive_add), nm (nm_inputs_collect) and
 * ar (utils/ar.c); ar's writer stays in ar.c.
 *
 * Use: varchive_cursor_init on the mapped file, then varchive_member_next for
 * each header and varchive_cursor_advance past it.  The reader validates
 * nothing on its own account: it says what it found (header_valid,
 * data_fits, name NULL for a long name it cannot find), and each tool keeps
 * its own messages and its own choice of what ends the job.  It allocates
 * nothing and never reads outside [bytes, bytes + size); names point into the
 * archive and are not NUL-terminated.
 */
#ifndef VCC_COMMON_ARCHIVE_READER_H
#define VCC_COMMON_ARCHIVE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { VARCHIVE_MAGIC_SIZE = 8, VARCHIVE_HEADER_SIZE = 60 };

typedef enum varchive_format {
	VARCHIVE_FORMAT_NONE,		/* not an archive (or shorter than its magic) */
	VARCHIVE_FORMAT_REGULAR,	/* "!<arch>\n" */
	VARCHIVE_FORMAT_THIN,		/* "!<thin>\n" */
} varchive_format;

typedef enum varchive_member_kind {
	VARCHIVE_MEMBER_SYMBOL_TABLE,	/* "/", "/SYM64/", BSD's "__.SYMDEF" ones */
	VARCHIVE_MEMBER_LONG_NAMES,	/* "//" */
	VARCHIVE_MEMBER_FILE,		/* a member file */
} varchive_member_kind;

typedef enum varchive_name_kind {
	VARCHIVE_NAME_SHORT,		/* in the header: up to the first '/', else without trailing spaces */
	VARCHIVE_NAME_LONG,		/* "/OFFSET" into "//", up to "/\n" */
	VARCHIVE_NAME_BSD,		/* "#1/LENGTH": the first LENGTH bytes of the data */
} varchive_name_kind;

typedef struct varchive_member {
	const char *header;		/* its 60 bytes */
	size_t header_offset;		/* in the archive */
	varchive_member_kind kind;
	varchive_name_kind name_kind;
	bool header_valid;		/* ends in "`\n"; a BSD name is no longer than the data */
	bool has_data;			/* false for a thin archive's member files */
	bool data_fits;			/* has_data and the size field's bytes are in the archive */
	uint64_t stored_size;		/* the size field: the bytes after the header */
	uint64_t size;			/* of the member's data (BSD: stored_size less the name) */
	const uint8_t *data;		/* when data_fits (BSD: after the name), else NULL */
	const char *name;		/* member files; NULL when it cannot be found */
	size_t name_length;		/* without the terminating '/' */
	bool name_ends_in_slash;	/* a long name ended in '/' before its '\n' */
} varchive_member;

typedef struct varchive_cursor {
	const uint8_t *bytes;
	size_t size;
	size_t offset;			/* of the next header */
	bool thin;
	bool bsd;			/* a "#1/" name was seen: the members are not padded to even sizes (ar's rule) */
	const char *long_names;		/* the "//" member, once read and in the archive */
	size_t long_names_size;
} varchive_cursor;

/* what the first bytes of a file say it is */
varchive_format varchive_format_detect(const void *bytes, size_t size);

/* a cursor at the first header; thin from the magic, which is not checked */
void varchive_cursor_init(varchive_cursor *cursor, const void *bytes, size_t size);

/* the header at the cursor into *member; false when fewer than 60 bytes are left */
bool varchive_member_next(varchive_cursor *cursor, varchive_member *member);

/* the cursor past member (its data, unless a thin archive's member file) */
void varchive_cursor_advance(varchive_cursor *cursor, const varchive_member *member);

#endif
