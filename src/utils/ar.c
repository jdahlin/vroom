/*
 * ar inside vccd: the archiver Kbuild runs as $(AR), byte-identical to
 * llvm-ar for what Kbuild does with it, so no llvm-ar is needed:
 *
 *   ar cDPrST A FILE...        every built-in.a (thin, no symbol table)
 *   ar cDPrsT A FILE...        lib.a (thin, with a symbol table)
 *   ar cDPrS --thin A FILE...  vmlinux.a
 *   ar mPi POS A MEMBER...     move MEMBERs before POS (head objects first)
 *   ar t A                     list the members
 *
 * and the MinGW world's ar rcs/rc/qc/crsD on COFF x86-64 objects and import
 * records (libvccrt.a, src/runtime/build.sh; bench/var-cmp.py coff).
 *
 * Operations r, m, t, q, s (p, x, d: regular archives only); modifiers c D U P s S T u(ignored) i b a, and --thin.
 * A regular archive (GNU format: basenames, "name/" or "/OFFSET" into "//"
 * for names of 16 bytes or more, each such name once, the data padded to even
 * with '\n') is only created and updated (r by basename, q, d, s, m) and read
 * (t, p, x).  Its format is llvm-ar's: from the first member of a new one
 * (GNU for ELF; COFF for a COFF object or import record: GNU's layout with a
 * second linker member, see ar_archive_write; BSD for Mach-O; for anything
 * else the host's, BSD on macOS and GNU elsewhere), an existing one keeps its
 * own (one without a symbol table becomes COFF when its first member is).
 * The symbol table has ELF64's global defined symbols and COFF's external
 * ones as llvm reads them (ar_coff_symbols_read); a malformed object fails
 * the request as it fails llvm-ar.  D (the default): dates, uids and gids 0,
 * mode 644; U: the files' own, the symbol tables dated now.
 * A thin archive's members are paths, relative to the archive's directory,
 * with no data:
 *
 *   !<thin>\n
 *   "/" member:   symbol table unless S (and only if some member is an
 *                 object): big-endian count, the offset of
 *                 each symbol's member header, NUL-terminated names, padded
 *                 to even length with NUL (global defined symbols)
 *   "//" member:  "PATH/\n" per member, padded to even length with '\n'
 *   per member:   header "/OFFSET-IN-//", date/uid/gid 0 (D), mode, size
 *
 * The semantics follow llvm-ar's: new members are added in command-line
 * order (a thin archive given as a file is flattened into its members); r
 * replaces an old member of the same path in place; m moves the named
 * members, in archive order, before (i, b) or after (a) POS, where POS is
 * compared with the stored names as given.  Other paths are compared and
 * made relative lexically (".", ".." and "//" resolved against
 * the request's cwd).  The archive is written to a temporary file and
 * renamed; on an error nothing is written.
 *
 * Why: llvm-ar is a ~167 M-instruction process start on macOS (dyld binds
 * libLLVM.dylib), and Kbuild runs it once per directory (~420 times).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "utils/ar.h"
#include "common/archive-reader.h"
#include "common/buffer.h"
#include "port/file.h"
#include "common/scoped-cleanup.h"

/* ---------------- growable buffer ---------------- */

/* ---------------- request state ---------------- */

typedef struct ar_member {
	char *name;		/* as stored: relative to the archive's directory */
	char *absolute_name;		/* absolute, normalized */
	uint32_t mode;
	uint64_t size;
	const unsigned char *data;	/* a regular archive's member: its bytes in the old archive (abs NULL) */
	int64_t mtime;			/* U (not deterministic): the header's date, uid and gid */
	uint64_t uid, gid;
} ar_member;

typedef struct ar_member_list {
	ar_member *members;
	size_t count, capacity;
} ar_member_list;

typedef struct ar_request {
	const char *cwd;
	int outfd, errfd;
	char *directory;		/* the archive's directory, absolute, normalized */
	bool failed;
	bool thin;			/* the archive written is thin */
	bool real_times;		/* U: each member's date, uid, gid and mode; the symbol tables' date now */
	int64_t now;
} ar_request;

static void ar_error_report(ar_request *q, const char *fmt, const char *s, const char *t)
{
	char line[4200];
	int n = snprintf(line, sizeof(line), fmt, s, t);

	q->failed = true;
	if (q->errfd >= 0 && n > 0)
		(void)!write(q->errfd, line, n < (int)sizeof(line) ? n : (int)sizeof(line) - 1);
}

static bool ar_member_list_push(ar_member_list *l, ar_member m)
{
	if (l->count == l->capacity) {
		size_t cap = l->capacity ? l->capacity * 2 : 256;
		ar_member *p = realloc(l->members, cap * sizeof(*p));

		if (!p)
			return false;
		l->members = p;
		l->capacity = cap;
	}
	l->members[l->count++] = m;
	return true;
}

static void ar_member_list_free(ar_member_list *l)
{
	for (size_t i = 0; i < l->count; i++) {
		free(l->members[i].name);
		free(l->members[i].absolute_name);
	}
	free(l->members);
	l->members = NULL;
	l->count = l->capacity = 0;
}

/* ---------------- paths ---------------- */

/* base + path, absolute and lexically normalized ("." and ".." resolved) */
/* scoped: the members of a list, the bytes of a vbuffer, an ar_request's directory */
#define MLIST VCLEANUP(ar_member_list_free)
static void ar_buffer_finish(vbuffer *b)
{
	free(b->p);
	b->p = NULL;
}
#define SBUF VCLEANUP(ar_buffer_finish)

static char *ar_path_normalize(const char *base, const char *path)
{
	size_t bl = path[0] == '/' ? 0 : strlen(base), pl = strlen(path), n = 0;
	char *in = malloc(bl + pl + 2), *out = malloc(bl + pl + 2), *s;

	if (!in || !out) {
		free(in);
		free(out);
		return NULL;
	}
	memcpy(in, base, bl);
	in[bl] = '/';
	memcpy(in + bl + 1, path, pl + 1);
	for (s = in; *s;) {
		char *e;
		size_t l;

		while (*s == '/')
			s++;
		for (e = s; *e && *e != '/'; e++)
			;
		l = e - s;
		if (l == 2 && s[0] == '.' && s[1] == '.') {
			while (n > 0 && out[n - 1] != '/')
				n--;
			if (n > 0)
				n--;
		} else if (l && !(l == 1 && s[0] == '.')) {
			out[n++] = '/';
			memcpy(out + n, s, l);
			n += l;
		}
		s = e;
	}
	if (!n)
		out[n++] = '/';
	out[n] = 0;
	free(in);
	return out;
}

/* the directory part of an absolute normalized path */
static char *ar_path_directory(const char *abs)
{
	const char *s = strrchr(abs, '/');

	return strndup(abs, s == abs ? 1 : (size_t)(s - abs));
}

/* abs relative to dir (both absolute, normalized) */
static char *ar_path_relative(const char *dir, const char *abs)
{
	size_t k = 0, last = 0, up = 0, i;
	const char *rest;
	char *r, *p;

	if (!strcmp(dir, "/"))
		return strdup(abs + 1);
	/* common leading components */
	for (;;) {
		if (dir[k] == 0 && (abs[k] == '/' || abs[k] == 0)) {
			last = k;
			break;
		}
		if (dir[k] != abs[k])
			break;
		if (dir[k] == '/')
			last = k;
		k++;
	}
	for (i = last; dir[i]; i++)
		if (dir[i] == '/')
			up++;
	rest = abs + last + (abs[last] == '/');
	if (!(r = malloc(up * 3 + strlen(rest) + 1)))
		return NULL;
	for (p = r; up--; p += 3)
		memcpy(p, "../", 3);
	strcpy(p, rest);
	return r;
}

/* ---------------- reading ---------------- */

static uint64_t ar_field_number_parse(const unsigned char *s, size_t n, int base)
{
	char b[24];

	if (n >= sizeof(b))
		n = sizeof(b) - 1;
	memcpy(b, s, n);
	b[n] = 0;
	return strtoull(b, NULL, base);
}

/*
 * Append the members of the thin archive at abs (vfile_mapping as d) to l: names
 * are made relative to q->directory, mode and size come from the headers.
 * shown is the path the user gave, for messages.
 */
static bool ar_thin_archive_read(ar_request *q, const char *abs, const char *shown, const vfile_mapping *d, ar_member_list *l)
{
	VAUTOFREE char *adir = ar_path_directory(abs);
	varchive_cursor cursor;
	varchive_member member;
	bool ok = adir != NULL;

	if (varchive_format_detect(d->p, d->n) != VARCHIVE_FORMAT_THIN) {
		ar_error_report(q, "ar: error: %s: %s\n", shown, "only thin archives are supported");
		return false;
	}
	varchive_cursor_init(&cursor, d->p, d->n);
	while (ok && varchive_member_next(&cursor, &member)) {
		if (member.kind != VARCHIVE_MEMBER_FILE) {
			/* the symbol table, the names */
		} else if (member.name_kind == VARCHIVE_NAME_LONG && cursor.long_names) {
			ar_member m = { 0 };
			VAUTOFREE char *nm = NULL;

			if (!member.name || !member.name_length || !member.name_ends_in_slash) {
				ar_error_report(q, "ar: error: %s: %s\n", shown, "malformed thin archive");
				ok = false;
				break;
			}
			nm = strndup(member.name, member.name_length);
			m.absolute_name = nm ? ar_path_normalize(adir, nm) : NULL;
			m.name = m.absolute_name ? ar_path_relative(q->directory, m.absolute_name) : NULL;
			m.mode = ar_field_number_parse((const unsigned char *)member.header + 40, 8, 8);
			m.mtime = (int64_t)ar_field_number_parse((const unsigned char *)member.header + 16, 12, 10);
			m.uid = ar_field_number_parse((const unsigned char *)member.header + 28, 6, 10);
			m.gid = ar_field_number_parse((const unsigned char *)member.header + 34, 6, 10);
			m.size = member.stored_size;
			if (!m.name || !ar_member_list_push(l, m)) {
				free(m.name);
				free(m.absolute_name);
				ok = false;
			}
		} else {
			ar_error_report(q, "ar: error: %s: %s\n", shown, "unsupported archive member");
			ok = false;
		}
		varchive_cursor_advance(&cursor, &member);
	}
	return ok;
}

/* append the file given as path (into a thin archive, a thin archive is flattened and
   a regular one refused; a regular archive takes either as a file) */
static bool ar_member_file_add(ar_request *q, const char *path, ar_member_list *l)
{
	VAUTOFREE char *abs = ar_path_normalize(q->cwd, path);
	struct stat st;
	VAUTOUNMAP vfile_mapping d = { 0 };
	bool ok;

	if (!abs)
		return false;
	if (!vfile_map(abs, &d, &st)) {
		ar_error_report(q, "ar: error: %s: %s\n", path, strerror(errno));
		return false;
	}
	if (q->thin && varchive_format_detect(d.p, d.n) == VARCHIVE_FORMAT_THIN) {
		ok = ar_thin_archive_read(q, abs, path, &d, l);
	} else if (q->thin && varchive_format_detect(d.p, d.n) == VARCHIVE_FORMAT_REGULAR) {
		ar_error_report(q, "ar: error: %s: %s\n", path, "a regular archive cannot go into a thin one");
		ok = false;
	} else {
		ar_member m = { ar_path_relative(q->directory, abs), abs, st.st_mode & 07777, (uint64_t)st.st_size, NULL,
				(int64_t)st.st_mtime, (uint64_t)st.st_uid, (uint64_t)st.st_gid };

		ok = m.name && ar_member_list_push(l, m);
		if (ok)
			abs = NULL;	/* (the member owns it now) */
		else
			free(m.name);
	}
	return ok;
}

/* ---------------- symbol table ---------------- */

static uint16_t ar_load_u16_le(const unsigned char *p) { return p[0] | p[1] << 8; }
static uint32_t ar_load_u32_le(const unsigned char *p) { return ar_load_u16_le(p) | (uint32_t)ar_load_u16_le(p + 2) << 16; }
static uint64_t ar_load_u64_le(const unsigned char *p) { return ar_load_u32_le(p) | (uint64_t)ar_load_u32_le(p + 4) << 32; }

/*
 * The archive's symbols, member by member in archive order and in each
 * member's symbol-table order: names[offset, offset + length) (followed by a
 * NUL in names; a COFF short name can hold a NUL of its own).
 */
typedef struct ar_symbol {
	uint32_t offset, length, member;
} ar_symbol;

typedef struct ar_symbol_table {
	vbuffer names;
	ar_symbol *symbols;
	size_t count, capacity;
	bool any_object;		/* a member is an object file: the archive gets a symbol table */
	bool coff_arm64, coff_other;	/* COFF members for ARM64, for another machine */
	bool failed;
} ar_symbol_table;

static void ar_symbol_table_finish(ar_symbol_table *t)
{
	free(t->names.p);
	free(t->symbols);
	memset(t, 0, sizeof(*t));
}

/* one symbol of member: prefix (may be "") and name[0, length) */
static void ar_symbol_add(ar_symbol_table *t, uint32_t member, const char *prefix, const void *name, size_t length)
{
	size_t pl = strlen(prefix);

	if (t->count == t->capacity) {
		size_t cap = t->capacity ? t->capacity * 2 : 1024;
		ar_symbol *p = realloc(t->symbols, cap * sizeof(*p));

		if (!p) {
			t->failed = true;
			return;
		}
		t->symbols = p;
		t->capacity = cap;
	}
	if (t->names.n + pl + length + 1 > UINT32_MAX) {
		t->failed = true;
		return;
	}
	t->symbols[t->count++] = (ar_symbol){ (uint32_t)t->names.n, (uint32_t)(pl + length), member };
	vbuffer_append(&t->names, prefix, pl);
	vbuffer_append(&t->names, name, length);
	vbuffer_append(&t->names, "", 1);
}

/*
 * Global (or weak) defined symbols of an ELF64 little-endian object, in
 * symbol-table order, as llvm's archive writer takes them: no undefined,
 * local, section or file symbols.  Other files have no symbols, and an
 * archive without any object file gets no symbol table (any_object).
 */
static bool ar_elf_symbols_read(const vfile_mapping *d, ar_symbol_table *t, uint32_t member)
{
	const unsigned char *p = d->p;
	uint64_t shoff, shnum, i;
	unsigned shentsize;

	if (d->n < 64 || memcmp(p, "\177ELF", 4) || p[4] != 2 || p[5] != 1)
		return true;
	t->any_object = true;
	shoff = ar_load_u64_le(p + 0x28);
	shentsize = ar_load_u16_le(p + 0x3a);
	shnum = ar_load_u16_le(p + 0x3c);
	if (shentsize < 64 || shoff > d->n)
		return true;
	if (!shnum && shoff + 64 <= d->n)
		shnum = ar_load_u64_le(p + shoff + 0x20);
	if (shnum > (d->n - shoff) / shentsize)
		return true;
	for (i = 0; i < shnum; i++) {
		const unsigned char *sh = p + shoff + i * shentsize, *str;
		uint64_t off, size, ent, n, j, soff, ssize;
		unsigned link;

		if (ar_load_u32_le(sh + 4) != 2)	/* SHT_SYMTAB */
			continue;
		off = ar_load_u64_le(sh + 0x18);
		size = ar_load_u64_le(sh + 0x20);
		link = ar_load_u32_le(sh + 0x28);
		ent = ar_load_u64_le(sh + 0x38);
		if (ent < 24 || off > d->n || size > d->n - off || link >= shnum)
			return false;
		sh = p + shoff + (uint64_t)link * shentsize;
		soff = ar_load_u64_le(sh + 0x18);
		ssize = ar_load_u64_le(sh + 0x20);
		if (soff > d->n || ssize > d->n - soff)
			return false;
		str = p + soff;
		n = size / ent;
		for (j = 1; j < n; j++) {
			const unsigned char *s = p + off + j * ent;
			unsigned bind = s[4] >> 4, type = s[4] & 15;
			uint32_t nm = ar_load_u32_le(s);
			size_t len;

			if (bind == 0 || type == 3 || type == 4 || ar_load_u16_le(s + 6) == 0)
				continue;
			if (nm >= ssize)
				return false;
			len = strnlen((const char *)str + nm, ssize - nm);
			if (nm + len >= ssize)
				return false;
			ar_symbol_add(t, member, "", str + nm, len);
		}
		return true;	/* one symbol table per object */
	}
	return true;
}

/*
 * COFF, as llvm's object library reads it for llvm-ar.  What a member is
 * comes from its first bytes (llvm's identify_magic): a COFF object for the
 * Windows machine types it lists, "\0\0" (machine unknown) included, a
 * bigobj ("\0\0\xff\xff" with bigobj's UUID at 12), or a short import record
 * (the same signature without it).  Files under 4 bytes are nothing.
 */
typedef enum ar_file_type {
	AR_FILE_OTHER,
	AR_FILE_ELF,
	AR_FILE_MACHO,
	AR_FILE_COFF,
	AR_FILE_COFF_IMPORT,
} ar_file_type;

static const unsigned char ar_coff_bigobj_magic[16] = {
	0xc7, 0xa1, 0xba, 0xd1, 0xee, 0xba, 0xa9, 0x4b, 0xaf, 0x20, 0xfa, 0xf6, 0x6a, 0xa4, 0xdc, 0xb8,
};
static const unsigned char ar_coff_cl_gl_magic[16] = {	/* cl.exe's /GL objects: not symbolic */
	0x38, 0xfe, 0xb3, 0x0c, 0xa5, 0xd9, 0xab, 0x4d, 0xac, 0x9b, 0xd6, 0xb6, 0x22, 0x26, 0x53, 0xc2,
};
static const unsigned char ar_coff_resource_magic[16] = {	/* a .res file */
	0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00,
};

static ar_file_type ar_file_type_identify(const unsigned char *p, size_t n)
{
	if (n >= 4 && !memcmp(p, "\177ELF", 4))
		return AR_FILE_ELF;
	if (n < 4)
		return AR_FILE_OTHER;
	if (!memcmp(p, "\xfe\xed\xfa\xce", 4) || !memcmp(p, "\xfe\xed\xfa\xcf", 4) ||
	    !memcmp(p, "\xce\xfa\xed\xfe", 4) || !memcmp(p, "\xcf\xfa\xed\xfe", 4))
		return AR_FILE_MACHO;
	switch (p[0]) {
	case 0x00:
		if (p[1] == 0 && p[2] == 0xff && p[3] == 0xff) {
			if (n < 28)
				return AR_FILE_COFF_IMPORT;
			if (!memcmp(p + 12, ar_coff_bigobj_magic, 16))
				return AR_FILE_COFF;
			if (!memcmp(p + 12, ar_coff_cl_gl_magic, 16))
				return AR_FILE_OTHER;
			return AR_FILE_COFF_IMPORT;
		}
		if (n >= 16 && !memcmp(p, ar_coff_resource_magic, 16))
			return AR_FILE_OTHER;
		return p[1] == 0 ? AR_FILE_COFF : AR_FILE_OTHER;
	case 0x64:	/* x86-64, arm64 */
		return p[1] == 0x86 || p[1] == 0xaa ? AR_FILE_COFF : AR_FILE_OTHER;
	case 0x41:	/* arm64ec */
	case 0x4e:	/* arm64x */
		return p[1] == 0xa6 ? AR_FILE_COFF : AR_FILE_OTHER;
	case 0xf0: case 0x83: case 0x84: case 0x66: case 0x50: case 0x4c: case 0xc4:
		if (p[1] == 0x01)
			return AR_FILE_COFF;
		__attribute__((fallthrough));
	case 0x90: case 0x68:
		return p[1] == 0x02 ? AR_FILE_COFF : AR_FILE_OTHER;
	}
	return AR_FILE_OTHER;
}

/*
 * ARM64EC and ARM64X objects, and an archive with both ARM64 and other COFF
 * members, get llvm-ar's "/<ECSYMBOLS>/" member as well (the symbols of the
 * members that are not ARM64): var does not write it and refuses them.
 */
static bool ar_coff_machine_is_arm64ec(unsigned machine)
{
	return machine == 0xa641 || machine == 0xa64e;	/* ARM64EC, ARM64X */
}

static void ar_coff_machine_note(ar_symbol_table *t, unsigned machine)
{
	if (machine == 0xaa64)
		t->coff_arm64 = true;
	else
		t->coff_other = true;
}

typedef enum ar_read_result {
	AR_READ_OK,
	AR_READ_MALFORMED,	/* llvm-ar fails on it too */
	AR_READ_UNSUPPORTED,	/* llvm-ar would write a member var does not (ARM64EC's "/<ECSYMBOLS>/") */
} ar_read_result;

/*
 * A COFF object's archive symbols (llvm's isArchiveSymbol on COFF's symbol
 * flags): external symbols but the undefined ones (section 0 and value 0;
 * a common symbol has a size as its value and is kept), and weak externals
 * unless their auxiliary record asks for other than an alias search (an
 * auxiliary record past the end of the file reads as 0).  A header or
 * section table past the end, or a long name with no string table, is
 * malformed; a symbol or string table that does not fit, or a string table
 * whose last byte is not NUL, leaves the object without symbols (llvm
 * recovers there).  A regular header with 0xffff sections is an import
 * library to llvm, and has no symbols.
 */
static ar_read_result ar_coff_symbols_read(const unsigned char *p, size_t n, ar_symbol_table *t, uint32_t member)
{
	uint64_t sections, table, symbols, entry, section_table;
	uint64_t strtab, strtab_size, i;
	unsigned machine;
	bool big = false;

	if (n >= 56 && ar_load_u16_le(p) == 0 && ar_load_u16_le(p + 2) == 0xffff && ar_load_u16_le(p + 4) >= 2 &&
	    !memcmp(p + 12, ar_coff_bigobj_magic, 16)) {
		big = true;
		machine = ar_load_u16_le(p + 6);
		sections = ar_load_u32_le(p + 44);
		table = ar_load_u32_le(p + 48);
		symbols = ar_load_u32_le(p + 52);
		section_table = 56;
		entry = 20;
	} else {
		if (n < 20)
			return AR_READ_MALFORMED;
		machine = ar_load_u16_le(p);
		sections = ar_load_u16_le(p + 2);
		table = ar_load_u32_le(p + 8);
		symbols = ar_load_u32_le(p + 12);
		section_table = 20 + (uint64_t)ar_load_u16_le(p + 16);
		entry = 18;
		if (sections == 0xffff) {
			t->any_object = true;
			ar_coff_machine_note(t, machine);
			return AR_READ_OK;
		}
	}
	if (section_table + sections * 40 > n)
		return AR_READ_MALFORMED;
	t->any_object = true;
	ar_coff_machine_note(t, machine);
	if (ar_coff_machine_is_arm64ec(machine))
		return AR_READ_UNSUPPORTED;
	if (!table)
		return AR_READ_OK;
	/* llvm: the string table where the symbols end, in 32 bits; the symbols run
	   from the table to it (one at most when it is before the table) */
	strtab = (uint32_t)(table + (uint32_t)(symbols * entry));
	if (strtab + 4 > n)
		return AR_READ_OK;
	strtab_size = ar_load_u32_le(p + strtab);
	if (strtab + strtab_size > n)
		return AR_READ_OK;
	if (strtab_size < 4)
		strtab_size = 4;
	if (strtab_size > 4 && p[strtab + strtab_size - 1] != 0)
		return AR_READ_OK;
	for (i = table; i != strtab && i + entry <= n;) {
		const unsigned char *s = p + i;
		unsigned storage = s[entry - 2], aux = s[entry - 1];
		uint64_t next = i + entry * (1 + (uint64_t)aux);

		i = next < strtab ? next : strtab;
		int64_t section = big ? (int32_t)ar_load_u32_le(s + 12) : (int16_t)ar_load_u16_le(s + 12);
		uint32_t value = ar_load_u32_le(s + 8);
		bool keep = false;

		if (storage == 2)		/* IMAGE_SYM_CLASS_EXTERNAL */
			keep = section != 0 || value != 0;
		else if (storage == 105) {	/* IMAGE_SYM_CLASS_WEAK_EXTERNAL */
			keep = true;
			if (aux) {
				uint64_t a = (uint64_t)(s - p) + entry;	/* TagIndex, Characteristics */

				keep = a + 8 <= n && ar_load_u32_le(p + a + 4) == 3;	/* IMAGE_WEAK_EXTERN_SEARCH_ALIAS */
			}
		}
		if (!keep)
			continue;
		if (!ar_load_u32_le(s)) {
			uint32_t offset = ar_load_u32_le(s + 4);

			if (strtab_size <= 4 || offset >= strtab_size)
				return AR_READ_MALFORMED;
			ar_symbol_add(t, member, "", p + strtab + offset, strnlen((const char *)p + strtab + offset, strtab_size - offset));
		} else {
			ar_symbol_add(t, member, "", s, s[7] ? 8 : strlen((const char *)s));
		}
	}
	return AR_READ_OK;
}

/*
 * A short import record (llvm's COFFImportFile): "__imp_NAME", then NAME
 * unless the import is data (type 1).  NAME runs from byte 20 to a NUL or
 * the end of the member (llvm's buffer ends in a NUL); a record shorter than
 * its header has the empty name.
 */
static ar_read_result ar_coff_import_symbols_read(const unsigned char *p, size_t n, ar_symbol_table *t, uint32_t member)
{
	const unsigned char *name = n > 20 ? p + 20 : (const unsigned char *)"";
	size_t length = n > 20 ? strnlen((const char *)name, n - 20) : 0;

	t->any_object = true;
	ar_coff_machine_note(t, n >= 8 ? ar_load_u16_le(p + 6) : 0);
	if (n >= 8 && ar_coff_machine_is_arm64ec(ar_load_u16_le(p + 6)))
		return AR_READ_UNSUPPORTED;
	ar_symbol_add(t, member, "__imp_", name, length);
	if (n < 20 || (ar_load_u16_le(p + 18) & 3) != 1)
		ar_symbol_add(t, member, "", name, length);
	return AR_READ_OK;
}

/* ---------------- writing ---------------- */

/* a member's bytes: the old archive's, or the file mapped */
static bool ar_member_map(const ar_member *m, vfile_mapping *d)
{
	struct stat st;

	if (m->data) {
		d->p = m->data;
		d->n = m->size;
		return true;
	}
	return vfile_map(m->absolute_name, d, &st);
}

static void ar_member_unmap(const ar_member *m, vfile_mapping *d)
{
	if (!m->data)
		vfile_unmap(d);
}

/*
 * The symbols of every member of l, in order (see ar_symbol_table).  A
 * malformed object fails the request, as llvm-ar's writer does.
 */
static bool ar_symbols_collect(ar_request *q, const ar_member_list *l, ar_symbol_table *t)
{
	for (size_t i = 0; i < l->count; i++) {
		vfile_mapping d;
		ar_read_result r = AR_READ_OK;
		const char *why = "malformed ELF symbol table";

		if (!ar_member_map(&l->members[i], &d)) {
			ar_error_report(q, "ar: error: %s: %s\n", l->members[i].name, strerror(errno));
			return false;
		}
		switch (ar_file_type_identify(d.p, d.n)) {
		case AR_FILE_ELF:
			if (!ar_elf_symbols_read(&d, t, (uint32_t)i))
				r = AR_READ_MALFORMED;
			break;
		case AR_FILE_COFF:
			r = ar_coff_symbols_read(d.p, d.n, t, (uint32_t)i);
			why = "malformed COFF object";
			break;
		case AR_FILE_COFF_IMPORT:
			r = ar_coff_import_symbols_read(d.p, d.n, t, (uint32_t)i);
			break;
		default:
			break;
		}
		ar_member_unmap(&l->members[i], &d);
		if (r == AR_READ_UNSUPPORTED)
			why = "ARM64EC and ARM64X objects are not supported";
		if (r != AR_READ_OK) {
			ar_error_report(q, "ar: error: %s: %s\n", l->members[i].name, why);
			return false;
		}
	}
	if (t->coff_arm64 && t->coff_other) {
		ar_error_report(q, "ar: error: %s%s\n", "ARM64 and other COFF members in one archive (ARM64X) are not supported", "");
		return false;
	}
	if (t->failed || t->names.failed) {
		ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
		return false;
	}
	return true;
}

/* llvm's order of names (std::string's: bytes as unsigned, then length) */
static int ar_symbol_compare(const ar_symbol_table *t, uint32_t a, uint32_t b)
{
	const ar_symbol *x = &t->symbols[a], *y = &t->symbols[b];
	uint32_t n = x->length < y->length ? x->length : y->length;
	int c = memcmp(t->names.p + x->offset, t->names.p + y->offset, n);

	return c ? c : (x->length > y->length) - (x->length < y->length);
}

/* sort v[0, n) by name (merge sort; tmp has room for n) */
static void ar_symbols_sort(const ar_symbol_table *t, uint32_t *v, uint32_t *tmp, size_t n)
{
	size_t h = n / 2, i = 0, j = h, k = 0;

	if (n < 2)
		return;
	ar_symbols_sort(t, v, tmp, h);
	ar_symbols_sort(t, v + h, tmp, n - h);
	while (i < h && j < n)
		tmp[k++] = ar_symbol_compare(t, v[j], v[i]) < 0 ? v[j++] : v[i++];
	while (i < h)
		tmp[k++] = v[i++];
	memcpy(v, tmp, k * sizeof(*v));
}

/*
 * COFF's first linker member names each symbol once: the symbols of t whose
 * name an earlier symbol does not have (llvm's map from name to member keeps
 * the first).  keep[i] for each; returns how many, or -1 out of memory.
 */
static long ar_symbols_first_occurrences(const ar_symbol_table *t, bool *keep)
{
	size_t cap = 16, kept = 0;
	uint32_t *slot;

	while (cap < 2 * t->count)
		cap *= 2;
	if (!(slot = malloc(cap * sizeof(*slot))))
		return -1;
	memset(slot, 0xff, cap * sizeof(*slot));
	for (size_t i = 0; i < t->count; i++) {
		const unsigned char *s = (const unsigned char *)t->names.p + t->symbols[i].offset;
		uint64_t h = 1469598103934665603ull;
		size_t k;

		for (uint32_t j = 0; j < t->symbols[i].length; j++)
			h = (h ^ s[j]) * 1099511628211ull;
		for (k = h & (cap - 1); slot[k] != UINT32_MAX; k = (k + 1) & (cap - 1))
			if (!ar_symbol_compare(t, slot[k], (uint32_t)i))
				break;
		keep[i] = slot[k] == UINT32_MAX;
		if (keep[i]) {
			slot[k] = (uint32_t)i;
			kept++;
		}
	}
	free(slot);
	return (long)kept;
}

/*
 * A member header.  Deterministic (llvm-ar's default, D): date, uid and gid
 * 0, mode 644 (the symbol tables' "0").  U: the member's own, uid and gid
 * modulo 10^6 as llvm-ar prints them, and the symbol tables dated now.
 */
static void ar_header_append(vbuffer *b, const char *name, const char *date, const char *uid, const char *gid,
		   const char *mode, uint64_t size)
{
	char h[61];

	snprintf(h, sizeof(h), "%-16s%-12s%-6s%-6s%-8s%-10llu`\n", name, date, uid, gid, mode,
		 (unsigned long long)size);
	vbuffer_append(b, h, 60);
}

static void ar_member_header_append(const ar_request *q, vbuffer *b, const char *name, const ar_member *m, uint64_t size)
{
	char date[24], uid[24], gid[24], mode[24];

	if (!q->real_times) {
		ar_header_append(b, name, "0", "0", "0", "644", size);
		return;
	}
	snprintf(date, sizeof(date), "%lld", (long long)m->mtime);
	snprintf(uid, sizeof(uid), "%llu", (unsigned long long)(m->uid % 1000000));
	snprintf(gid, sizeof(gid), "%llu", (unsigned long long)(m->gid % 1000000));
	snprintf(mode, sizeof(mode), "%o", (unsigned)(m->mode & 07777));
	ar_header_append(b, name, date, uid, gid, mode, size);
}

static void ar_symbol_table_header_append(const ar_request *q, vbuffer *b, const char *name, uint64_t size)
{
	char date[24] = "0";

	if (q->real_times)
		snprintf(date, sizeof(date), "%lld", (long long)q->now);
	ar_header_append(b, name, date, "0", "0", "0", size);
}

static void ar_buffer_append_u32_be(vbuffer *b, uint32_t v)
{
	unsigned char c[4] = { v >> 24, v >> 16, v >> 8, v };

	vbuffer_append(b, c, 4);
}

static const char *ar_path_base(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

static void ar_buffer_append_u32_le(vbuffer *b, uint32_t v)
{
	unsigned char c[4] = { v, v >> 8, v >> 16, v >> 24 };

	vbuffer_append(b, c, 4);
}

static const char ar_zero_padding[8];

/*
 * llvm-ar's Darwin (BSD) format, what it writes on macOS when an archive's
 * first member is not an object (or is Mach-O): each member "#1/N" with its
 * name (NUL-padded) at
 * the start of the data, N such that the data starts 8-aligned, the member
 * padded with '\n' to end 8-aligned; "__.SYMDEF" (unless S): the byte size
 * of the (name offset, member offset) pairs, the pairs, the string table's
 * size and the strings, zero-padded; all little-endian.
 */
static bool ar_bsd_symbol_table_build(ar_request *q, vbuffer *out, const ar_member_list *l, bool symtab)
{
	VCLEANUP(ar_symbol_table_finish) ar_symbol_table syms = { .names = VBUFFER_STICKY_INIT };
	VAUTOFREE uint64_t *mpos = calloc(l->count + 1, sizeof *mpos);
	uint64_t pos, symsize = 0, total;
	vfile_mapping d;

	if (!mpos)
		return false;
	if (symtab && !ar_symbols_collect(q, l, &syms))
		return false;
	total = syms.count;
	/* "__.SYMDEF_64" (8-byte fields) once a member starts at or past the threshold, as the GNU table */
	const char *thr_s = getenv("SYM64_THRESHOLD");
	uint64_t thr = thr_s ? strtoull(thr_s, NULL, 0) : (uint64_t)1 << 32, w = 4;

	for (int pass = 0; pass < 2; pass++) {
		if (symtab) {
			symsize = 12 + w + 2 * w * total + w + syms.names.n;
			symsize += (8 - (8 + 60 + symsize) % 8) % 8;
		}
		pos = 8 + (symtab ? 60 + symsize : 0);
		for (size_t i = 0; i < l->count; i++) {
			size_t nl = strlen(ar_path_base(l->members[i].name)), npad = (8 - (pos + 60 + nl) % 8) % 8;
			uint64_t body = nl + npad + l->members[i].size;

			mpos[i] = pos;
			pos += 60 + body + (8 - (pos + 60 + body) % 8) % 8;
		}
		if (w == 8 || !symtab || !l->count || mpos[l->count - 1] < thr)
			break;
		w = 8;
	}
	vbuffer_append(out, "!<arch>\n", 8);
	if (symtab) {
		uint64_t start = out->n;

		ar_symbol_table_header_append(q, out, "#1/12", symsize);
		vbuffer_append(out, w == 8 ? "__.SYMDEF_64" : "__.SYMDEF\0\0\0", 12);
#define LEW(v) do { ar_buffer_append_u32_le(out, (uint32_t)(v)); if (w == 8) ar_buffer_append_u32_le(out, (uint32_t)((uint64_t)(v) >> 32)); } while (0)
		LEW(2 * w * total);
		for (size_t k = 0; k < syms.count; k++) {
			LEW(syms.symbols[k].offset);
			LEW(mpos[syms.symbols[k].member]);
		}
		LEW(syms.names.n);
#undef LEW
		vbuffer_append(out, syms.names.p, syms.names.n);
		vbuffer_append(out, ar_zero_padding, 60 + symsize - (out->n - start));
	}
	for (size_t i = 0; i < l->count; i++) {
		const char *bn = ar_path_base(l->members[i].name);
		size_t nl = strlen(bn), npad = (8 - (mpos[i] + 60 + nl) % 8) % 8;
		uint64_t body = nl + npad + l->members[i].size, tail = (8 - (mpos[i] + 60 + body) % 8) % 8;
		char nm[32];

		snprintf(nm, sizeof nm, "#1/%zu", nl + npad);
		ar_member_header_append(q, out, nm, &l->members[i], body + tail);
		vbuffer_append(out, bn, nl);
		vbuffer_append(out, ar_zero_padding, npad);
		if (!ar_member_map(&l->members[i], &d)) {
			ar_error_report(q, "ar: error: %s: %s\n", l->members[i].name, strerror(errno));
			return false;
		}
		vbuffer_append(out, d.p, l->members[i].size < d.n ? l->members[i].size : d.n);
		ar_member_unmap(&l->members[i], &d);
		vbuffer_append(out, "\n\n\n\n\n\n\n\n", tail);
	}
	return !out->failed;
}

static void ar_request_finish(ar_request *q)
{
	free(q->directory);
	q->directory = NULL;
}

/*
 * The archive formats llvm-ar writes: GNU (ELF members, and on Linux
 * anything that is not an object), COFF (the first member a COFF object or
 * import record: GNU's layout with two symbol tables) and BSD (Darwin's, for
 * Mach-O and on macOS for anything that is not an object).
 *
 * COFF's first linker member is GNU's table with each name once; the second
 * (all little-endian): the member count, each member's header offset, the
 * symbol count, per symbol in name order its member's 1-based index (16
 * bits), the names in that order; both padded to even with NUL.  When the
 * symbol table would need 64-bit offsets, llvm-ar falls back to GNU's
 * "/SYM64/" (every symbol, one table).
 */
typedef enum ar_kind {
	AR_KIND_GNU,
	AR_KIND_COFF,
	AR_KIND_BSD,
} ar_kind;

/* llvm-ar's format for an archive whose first member is p[0, n) (its getKindFromMember) */
static ar_kind ar_kind_of_member(const unsigned char *p, size_t n)
{
	switch (ar_file_type_identify(p, n)) {
	case AR_FILE_ELF:
		return AR_KIND_GNU;
	case AR_FILE_COFF:
		return AR_KIND_COFF;
	case AR_FILE_COFF_IMPORT:
		if (n >= 20)	/* a record shorter than its header is no COFF to llvm-ar here */
			return AR_KIND_COFF;
		break;
	case AR_FILE_MACHO:
		return AR_KIND_BSD;
	default:
		break;
	}
#ifdef __APPLE__
	return AR_KIND_BSD;	/* the host's: Darwin */
#else
	return AR_KIND_GNU;
#endif
}

/*
 * "//" into names (emptied first): a thin archive's paths, "PATH/\n" each; a
 * regular one's names of 16 bytes or more, each name once (members of the
 * same name share it, as llvm-ar's), "NAME/\n", in a COFF archive with
 * symbol tables "NAME\0"; padded to even with '\n'.  offset[i]: member i's.
 */
static bool ar_long_names_build(const ar_member_list *l, bool thin, bool coff, vbuffer *names, uint64_t *offset)
{
	size_t slots = 16;
	VAUTOFREE size_t *slot = NULL;

	names->n = 0;
	while (slots < 2 * l->count)
		slots *= 2;
	if (!(slot = malloc(slots * sizeof(*slot))))
		return false;
	memset(slot, 0xff, slots * sizeof(*slot));
	for (size_t i = 0; i < l->count; i++) {
		const char *nm = thin ? l->members[i].name : ar_path_base(l->members[i].name);

		if (!thin && strlen(nm) < 16)
			continue;	/* regular: "name/" in the header */
		if (!thin) {
			uint64_t h = 1469598103934665603ull;
			size_t k;

			for (const unsigned char *c = (const unsigned char *)nm; *c; c++)
				h = (h ^ *c) * 1099511628211ull;
			for (k = h & (slots - 1); slot[k] != SIZE_MAX; k = (k + 1) & (slots - 1))
				if (!strcmp(ar_path_base(l->members[slot[k]].name), nm))
					break;
			if (slot[k] != SIZE_MAX) {
				offset[i] = offset[slot[k]];
				continue;
			}
			slot[k] = i;
		}
		offset[i] = names->n;
		vbuffer_string_append(names, nm);
		if (coff && !thin)
			vbuffer_append(names, "", 1);
		else
			vbuffer_append(names, "/\n", 2);
	}
	if (names->n & 1)
		vbuffer_append(names, "\n", 1);
	return !names->failed;
}

static bool ar_archive_write(ar_request *q, const char *archive_abs, const char *shown, const ar_member_list *l, bool symtab, bool thin,
			  ar_kind kind)
{
	SBUF vbuffer out = VBUFFER_STICKY_INIT, names = VBUFFER_STICKY_INIT;
	VCLEANUP(ar_symbol_table_finish) ar_symbol_table syms = { .names = VBUFFER_STICKY_INIT };
	VAUTOFREE bool *keep = NULL;
	VAUTOFREE uint32_t *sorted = NULL;
	VAUTOFREE uint64_t *name_offset = NULL;
	VAUTOFREE char *tmp = NULL;
	uint64_t total = 0, total_bytes = 0, unique = 0, unique_bytes = 0;
	uint64_t pos, symsize = 0, symsize2 = 0;
	size_t i;
	int fd;
	bool ok = false;

	if (kind == AR_KIND_BSD) {
		if (!ar_bsd_symbol_table_build(q, &out, l, symtab))
			return false;
		goto write;
	}
	if (!(name_offset = calloc(l->count + 1, sizeof(*name_offset)))) {
		ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
		return false;
	}
	if (symtab && l->count) {
		if (!ar_symbols_collect(q, l, &syms))
			return false;
		total = syms.count;
		total_bytes = syms.names.n;
	}
	/* llvm-ar: a symbol table when a member is an object, and in a COFF
	   archive always (with no member left, two empty linker members) */
	if (l->count > 0xfffe)
		kind = AR_KIND_GNU;	/* llvm-ar: the second linker member's 16-bit indices */
	symtab = symtab && (syms.any_object || kind == AR_KIND_COFF);
	if (!symtab || kind != AR_KIND_COFF) {
		kind = AR_KIND_GNU;
	} else {
		long kept;

		if (!(keep = calloc(total + 1, sizeof(*keep))) || !(sorted = calloc(2 * total + 1, sizeof(*sorted))) ||
		    (kept = ar_symbols_first_occurrences(&syms, keep)) < 0) {
			ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
			return false;
		}
		for (i = 0; i < total; i++)
			if (keep[i]) {
				sorted[unique++] = (uint32_t)i;
				unique_bytes += syms.symbols[i].length + 1;
			}
		if (unique != (uint64_t)kept)
			return false;
		ar_symbols_sort(&syms, sorted, sorted + unique, unique);
	}

	vbuffer_append(&out, thin ? "!<thin>\n" : "!<arch>\n", 8);
	if (l->count || symtab) {
		VAUTOFREE uint64_t *mpos = calloc(l->count + 1, sizeof *mpos);

		if (!mpos)
			return false;
		/* llvm-ar: "/SYM64/" (8-byte count and offsets) once a member starts at or past the
		   threshold, 4 GB (SYM64_THRESHOLD in the server's environment, as llvm-ar's, for tests);
		   a COFF archive then becomes GNU's (one table, every symbol, GNU's long names) */
		const char *thr_s = getenv("SYM64_THRESHOLD");
		uint64_t thr = thr_s ? strtoull(thr_s, NULL, 0) : (uint64_t)1 << 32;
		bool sym64 = false;

		for (int pass = 0; pass < 2; pass++) {
			uint64_t w = sym64 ? 8 : 4;

			if (!ar_long_names_build(l, thin, kind == AR_KIND_COFF, &names, name_offset)) {
				ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
				return false;
			}
			if (symtab && kind == AR_KIND_COFF) {
				symsize = 4 + 4 * unique + unique_bytes;
				symsize += symsize & 1;
				symsize2 = 4 + 4 * (uint64_t)l->count + 4 + 2 * unique + unique_bytes;
				symsize2 += symsize2 & 1;
			} else if (symtab) {
				/* llvm-ar: no names at all get three NULs (older Solaris tools want a table) */
				symsize = w + w * total + (total_bytes ? total_bytes : 3);
				symsize += symsize & 1;
			}
			pos = 8 + (symtab ? 60 + symsize : 0) + (symtab && kind == AR_KIND_COFF ? 60 + symsize2 : 0) +
			      (names.n || thin ? 60 + names.n : 0);
			for (i = 0; i < l->count; i++) {
				mpos[i] = pos;
				pos += 60 + (thin ? 0 : l->members[i].size + (l->members[i].size & 1));
			}
			if (sym64 || !symtab || !l->count || mpos[l->count - 1] < thr)
				break;
			sym64 = true;
			kind = AR_KIND_GNU;
		}
		if (symtab) {
			uint64_t start = out.n;
			bool coff = kind == AR_KIND_COFF;

			ar_symbol_table_header_append(q, &out, sym64 ? "/SYM64/" : "/", symsize);
			if (sym64)
				ar_buffer_append_u32_be(&out, 0);
			ar_buffer_append_u32_be(&out, (uint32_t)(coff ? unique : total));
			for (size_t k = 0; k < syms.count; k++) {
				if (coff && !keep[k])
					continue;
				if (sym64)
					ar_buffer_append_u32_be(&out, (uint32_t)(mpos[syms.symbols[k].member] >> 32));
				ar_buffer_append_u32_be(&out, (uint32_t)mpos[syms.symbols[k].member]);
			}
			for (size_t k = 0; k < syms.count; k++)
				if (!coff || keep[k])
					vbuffer_append(&out, syms.names.p + syms.symbols[k].offset, syms.symbols[k].length + 1);
			vbuffer_append(&out, ar_zero_padding, 60 + symsize - (out.n - start));
			if (coff) {
				start = out.n;
				ar_symbol_table_header_append(q, &out, "/", symsize2);
				ar_buffer_append_u32_le(&out, (uint32_t)l->count);
				for (i = 0; i < l->count; i++)
					ar_buffer_append_u32_le(&out, (uint32_t)mpos[i]);
				ar_buffer_append_u32_le(&out, (uint32_t)unique);
				for (uint64_t k = 0; k < unique; k++) {
					uint32_t m = syms.symbols[sorted[k]].member + 1;
					unsigned char c[2] = { (unsigned char)m, (unsigned char)(m >> 8) };

					vbuffer_append(&out, c, 2);
				}
				for (uint64_t k = 0; k < unique; k++)
					vbuffer_append(&out, syms.names.p + syms.symbols[sorted[k]].offset, syms.symbols[sorted[k]].length + 1);
				vbuffer_append(&out, ar_zero_padding, 60 + symsize2 - (out.n - start));
			}
		}
		if (names.n || thin) {
			ar_header_append(&out, "//", "", "", "", "", names.n);
			vbuffer_append(&out, names.p, names.n);
		}
		for (i = 0; i < l->count; i++) {
			const char *bn = thin ? l->members[i].name : ar_path_base(l->members[i].name);
			char nm[24];

			if (thin || strlen(bn) >= 16) {
				snprintf(nm, sizeof(nm), "/%llu", (unsigned long long)name_offset[i]);
			} else {
				snprintf(nm, sizeof(nm), "%s/", bn);
			}
			ar_member_header_append(q, &out, nm, &l->members[i], l->members[i].size);
			if (!thin) {
				vfile_mapping d;

				if (!ar_member_map(&l->members[i], &d)) {
					ar_error_report(q, "ar: error: %s: %s\n", l->members[i].name, strerror(errno));
					return false;
				}
				vbuffer_append(&out, d.p, l->members[i].size < d.n ? l->members[i].size : d.n);
				ar_member_unmap(&l->members[i], &d);
				if (l->members[i].size & 1)
					vbuffer_append(&out, "\n", 1);
			}
		}
	}
write:
	if (out.failed || names.failed || syms.failed) {
		ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
		return false;
	}

	if (!(tmp = malloc(strlen(archive_abs) + 20)))
		return false;
	sprintf(tmp, "%s.tmp-ar-XXXXXX", archive_abs);
	if ((fd = mkstemp(tmp)) < 0) {
		ar_error_report(q, "ar: error: %s: %s\n", shown, strerror(errno));
		return false;
	}
	fchmod(fd, 0644);
	{
		const char *p = out.p;
		size_t n = out.n;

		ok = true;
		while (n && ok) {
			ssize_t w = write(fd, p, n);

			if (w < 0 && errno == EINTR)
				continue;
			ok = w > 0;
			p += w > 0 ? w : 0;
			n -= w > 0 ? w : 0;
		}
	}
	if (close(fd))
		ok = false;
	if (!ok || rename(tmp, archive_abs)) {
		ar_error_report(q, "ar: error: %s: %s\n", shown, strerror(errno));
		unlink(tmp);
		ok = false;
	}
	return ok;
}

/* ---------------- reading a regular archive: t, p, x ---------------- */

static bool ar_fd_write_all(int fd, const void *p, size_t n)
{
	while (n) {
		ssize_t w = write(fd, p, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return false;
		p = (const char *)p + w;
		n -= (size_t)w;
	}
	return true;
}

/* what a regular archive's first members say about its format (llvm's Archive) */
typedef struct ar_old_format {
	bool bsd;		/* "#1/" names */
	bool symbol_table;	/* the first member is a symbol table */
	bool coff;		/* the first two are "/": COFF's two linker members */
} ar_old_format;

/* the members of the regular archive d, in order: names as stored, data in d,
   date, uid, gid and mode from their headers */
static bool ar_regular_archive_read(ar_request *q, const vfile_mapping *d, const char *shown, ar_member_list *l, ar_old_format *format)
{
	varchive_cursor cursor;
	varchive_member member;
	unsigned index = 0;
	bool first_linker_member = false;

	varchive_cursor_init(&cursor, d->p, d->n);
	for (; varchive_member_next(&cursor, &member); varchive_cursor_advance(&cursor, &member), index++) {
		const unsigned char *h = (const unsigned char *)member.header;
		bool linker_member = member.kind == VARCHIVE_MEMBER_SYMBOL_TABLE && h[0] == '/' && h[1] == ' ';
		char name[4096];

		if (!member.header_valid || !member.data_fits)
			goto bad;
		if (cursor.bsd)
			format->bsd = true;
		if (index == 0) {
			format->symbol_table = member.kind == VARCHIVE_MEMBER_SYMBOL_TABLE;
			first_linker_member = linker_member;
		} else if (index == 1 && first_linker_member && linker_member) {
			format->coff = true;
		}
		if (member.kind != VARCHIVE_MEMBER_FILE)
			continue;
		if (!member.name)
			goto bad;
		snprintf(name, sizeof name, "%.*s", (int)member.name_length, member.name);
		ar_member m = { strdup(name), NULL, (uint32_t)ar_field_number_parse(h + 40, 8, 8), member.size, member.data,
				(int64_t)ar_field_number_parse(h + 16, 12, 10), ar_field_number_parse(h + 28, 6, 10),
				ar_field_number_parse(h + 34, 6, 10) };

		if (!m.name || !ar_member_list_push(l, m)) {
			free(m.name);
			ar_error_report(q, "ar: error: %s%s\n", "out of memory", "");
			return false;
		}
	}
	if (cursor.offset < d->n)	/* fewer than a header's bytes left */
		goto bad;
	return true;
bad:
	ar_error_report(q, "ar: error: %s: %s\n", shown, "truncated or malformed archive");
	return false;
}

/* t lists the names, p writes the data, x extracts into cwd (mode 644, as llvm-ar's D);
   named members only when given, each one that is not found an error */
static int ar_regular_archive_operate(ar_request *q, const vfile_mapping *d, const char *shown, char op, char **files, int nfiles)
{
	MLIST ar_member_list l = { 0 };
	VAUTOFREE bool *found = calloc((size_t)nfiles + 1, 1);
	SBUF vbuffer out = VBUFFER_STICKY_INIT;
	int rc = 0;

	ar_old_format format = { 0 };

	if (!found || !ar_regular_archive_read(q, d, shown, &l, &format))
		return 1;
	for (size_t k = 0; k < l.count; k++) {
		const char *name = l.members[k].name;
		bool want = !nfiles;

		for (int i = 0; i < nfiles; i++)
			if (!found[i] && !strcmp(files[i], name)) {
				found[i] = want = true;
				break;
			}
		if (!want)
			continue;
		if (op == 't') {
			vbuffer_string_append(&out, name);
			vbuffer_append(&out, "\n", 1);
		} else if (op == 'p') {
			vbuffer_append(&out, l.members[k].data, l.members[k].size);
		} else if (strchr(name, '/') || !strcmp(name, "..") || !strcmp(name, ".")) {
			ar_error_report(q, "ar: error: %s: %s\n", name, "unsafe member name");
			rc = 1;
		} else {
			char path[8192];

			snprintf(path, sizeof path, "%s/%s", q->cwd, name);
			VAUTOCLOSE int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (fd < 0 || !ar_fd_write_all(fd, l.members[k].data, l.members[k].size)) {
				ar_error_report(q, "ar: error: %s: %s\n", name, strerror(errno));
				rc = 1;
			}
		}
	}
	if (out.n && !ar_fd_write_all(q->outfd, out.p, out.n))
		rc = 1;
	for (int i = 0; i < nfiles; i++)
		if (!found[i]) {
			ar_error_report(q, "ar: error: '%s'%s was not found\n", files[i], "");
			rc = 1;
		}
	return rc || out.failed;
}

/* ---------------- operations ---------------- */

int ar_run(const char *cwd, int argc, char **argv, int outfd, int errfd)
{
	VCLEANUP(ar_request_finish) ar_request q = { .cwd = cwd, .outfd = outfd, .errfd = errfd };
	const char *ops = NULL, *relpos = NULL, *archive = NULL;
	bool thin = false, symtab = true, create = false, before = false, after = false, fullpath = false, sflag = false;
	char op = 0;
	VAUTOFREE char *archive_abs = NULL;
	VAUTOFREE char **files = NULL;
	int nfiles = 0, i, rc = 1;
	MLIST ar_member_list old = { 0 }, res = { 0 }, moved = { 0 };
	struct stat st;
	VAUTOUNMAP vfile_mapping d = { 0 };
	bool have_old = false, regular_old = false;
	ar_old_format format = { 0 };
	ar_kind kind = AR_KIND_GNU;

	if (!(files = calloc(argc, sizeof(*files))))
		return 1;
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (!strcmp(a, "--thin")) {
			thin = true;
		} else if (ops) {
			if ((before || after) && !relpos)
				relpos = a;
			else if (!archive)
				archive = a;
			else
				files[nfiles++] = argv[i];
		} else {
			ops = a[0] == '-' ? a + 1 : a;
			for (const char *c = ops; *c; c++) {
				switch (*c) {
				case 'r': case 'm': case 't': case 'p': case 'x': case 'd': case 'q':
					if (op && op != *c) {
						ar_error_report(&q, "ar: error: %s%s\n", "only one operation may be specified", "");
						return rc;
					}
					op = *c;
					break;
				case 'c': create = true; break;
				case 'P': fullpath = true; break;
				case 'D': q.real_times = false; break;
				case 'U': q.real_times = true; break;
				case 'u': break;
				case 's': symtab = true, sflag = true; break;
				case 'S': symtab = false; break;
				case 'T': thin = true; break;
				case 'i': case 'b': before = true; break;
				case 'a': after = true; break;
				default:
					ar_error_report(&q, "ar: error: unsupported modifier '%c' in '%s'\n", (char[]){ *c, 0 }, ops);
					return rc;
				}
			}
		}
	}
	if (q.real_times)
		q.now = (int64_t)time(NULL);
	if (!op && sflag)
		op = 'r';	/* "ar s A": only the symbol table rewritten (r with no files) */
	if (!op || !archive) {
		ar_error_report(&q, "ar: error: %s%s\n", "expected an operation and an archive", "");
		return rc;
	}
	if (!(archive_abs = ar_path_normalize(cwd, archive)) || !(q.directory = ar_path_directory(archive_abs)))
		return rc;

	if (vfile_map(archive_abs, &d, &st)) {
		have_old = true;
		if (varchive_format_detect(d.p, d.n) == VARCHIVE_FORMAT_REGULAR) {
			if (op == 't' || op == 'p' || op == 'x') {
				return ar_regular_archive_operate(&q, &d, archive, op, files, nfiles);
			}
			if (!ar_regular_archive_read(&q, &d, archive, &old, &format))
				return rc;
			regular_old = true;
		}
		if (!regular_old) {
			if (!ar_thin_archive_read(&q, archive_abs, archive, &d, &old))
				return rc;
			thin = true;
		}
	} else if (errno != ENOENT || (op != 'r' && op != 'q')) {
		ar_error_report(&q, "ar: error: %s: %s\n", archive, strerror(errno));
		return rc;
	}

	if (op == 't') {
		/* the archive argument's directory, then the stored name */
		const char *s = strrchr(archive, '/');
		size_t dl = s ? (size_t)(s - archive) : 0;
		SBUF vbuffer b = VBUFFER_STICKY_INIT;

		for (size_t k = 0; k < old.count; k++) {
			if (s) {
				vbuffer_append(&b, archive, dl ? dl : 1);
				if (dl)
					vbuffer_append(&b, "/", 1);
			}
			vbuffer_string_append(&b, old.members[k].name);
			vbuffer_append(&b, "\n", 1);
		}
		rc = 0;
		for (size_t off = 0; off < b.n;) {
			ssize_t w = write(outfd, b.p + off, b.n - off);

			if (w < 0 && errno == EINTR)
				continue;
			if (w <= 0) {
				rc = 1;
				break;
			}
			off += w;
		}
		if (b.failed)
			rc = 1;
		return rc;
	}
	if (op == 'p' || op == 'x') {
		ar_error_report(&q, "ar: error: %s: %s\n", archive, "p and x only for regular archives (a thin one's members are the files)");
		return rc;
	}
	if (regular_old && thin) {
		ar_error_report(&q, "ar: error: %s: %s\n", archive, "a regular archive cannot be made thin");
		return rc;
	}
	if (op == 'd' && thin) {
		ar_error_report(&q, "ar: error: %s%s\n", "d is only supported for regular archives", "");
		return rc;
	}
	if (!thin && op == 'm' && !have_old) {
		ar_error_report(&q, "ar: error: %s: %s\n", archive, strerror(ENOENT));
		return rc;
	}
	(void)create;
	q.thin = thin;

	{
		/* llvm-ar's computeNewArchiveMembers for r and m */
		VAUTOFREE bool *used = calloc(nfiles + 1, sizeof(bool));
		long insert = -1;

		if (!used)
			return rc;
		for (size_t k = 0; k < old.count; k++) {
			ar_member *m = &old.members[k];
			int match = -1;

			if (relpos && !strcmp(m->name, relpos))
				insert = (long)res.count + (after ? 1 : 0);
			for (i = 0; i < nfiles && match < 0; i++) {
				char *fa;

				if (used[i])
					continue;
				if (op == 'q')
					break;	/* q appends, replacing nothing */
				if (!m->absolute_name) {	/* regular: the stored basename, never a full path (llvm-ar) */
					if (!fullpath && !strcmp(ar_path_base(files[i]), m->name))
						match = i;
					continue;
				}
				fa = ar_path_normalize(cwd, files[i]);
				if (fa && !strcmp(fa, m->absolute_name))
					match = i;
				free(fa);
			}
			if (match < 0) {
				ar_member_list_push(&res, *m);
			} else if (op == 'd') {
				used[match] = true;
				free(m->name);
				free(m->absolute_name);
			} else if (op == 'm') {
				ar_member_list_push(&moved, *m);
				used[match] = true;
			} else {
				/* r: the new file takes the old member's place */
				ar_member_list nw = { 0 };

				used[match] = true;
				if (!ar_member_file_add(&q, files[match], &nw)) {
					ar_member_list_free(&nw);
					return rc;
				}
				for (size_t j = 0; j < nw.count; j++)
					ar_member_list_push(&res, nw.members[j]);
				free(nw.members);
				free(m->name);
				free(m->absolute_name);
			}
			m->name = m->absolute_name = NULL;
		}
		if (op == 'm') {
			if (relpos && insert < 0) {
				ar_error_report(&q, "ar: error: %s%s\n", "insertion point not found", "");
				return rc;
			}
			for (i = 0; i < nfiles; i++)
				if (!used[i]) {
					ar_error_report(&q, "ar: error: %s: %s\n", files[i], "no such member in the archive");
					return rc;
				}
			if (insert < 0)
				insert = res.count;
			for (size_t j = 0; j < moved.count; j++)
				ar_member_list_push(&res, (ar_member){ 0 });
			memmove(res.members + insert + moved.count, res.members + insert, (res.count - moved.count - insert) * sizeof(ar_member));
			memcpy(res.members + insert, moved.members, moved.count * sizeof(ar_member));
			free(moved.members);
			moved.members = NULL;
			moved.count = 0;
		} else if (op != 'd') {
			for (i = 0; i < nfiles; i++)
				if (!used[i] && !ar_member_file_add(&q, files[i], &res)) {
					return rc;
				}
		}
	}
	if (!thin && !regular_old && res.count) {
		/* a new regular archive: llvm-ar's format from its first member (GNU for ELF,
		   COFF for COFF, BSD for Mach-O; for anything else the host's: BSD on macOS) */
		vfile_mapping fm;

		if (!ar_member_map(&res.members[0], &fm)) {
			ar_error_report(&q, "ar: error: %s: %s\n", res.members[0].name, strerror(errno));
			return rc;
		}
		kind = ar_kind_of_member(fm.p, fm.n);
		ar_member_unmap(&res.members[0], &fm);
	} else if (regular_old) {
		/* llvm-ar keeps the old archive's format; one with no symbol table could
		   have been COFF, and is when its first member now is */
		kind = format.bsd ? AR_KIND_BSD : format.coff ? AR_KIND_COFF : AR_KIND_GNU;
		if (kind == AR_KIND_GNU && !format.symbol_table && res.count) {
			vfile_mapping fm;

			if (!ar_member_map(&res.members[0], &fm)) {
				ar_error_report(&q, "ar: error: %s: %s\n", res.members[0].name, strerror(errno));
				return rc;
			}
			if (ar_kind_of_member(fm.p, fm.n) == AR_KIND_COFF)
				kind = AR_KIND_COFF;
			ar_member_unmap(&res.members[0], &fm);
		}
	}
	if (!q.failed && ar_archive_write(&q, archive_abs, archive, &res, symtab, thin, kind))
		rc = 0;
	return rc;
}
