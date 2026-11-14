/*
 * debug-rows.c: the line table of a function (-g): which source line, column and file each
 * instruction record belongs to.
 *
 * codegen_debug_row_add is called by the lowering at each statement it wants a row for and attaches the current
 * record index to the source location; rows for nothing emitted are dropped.  A pass that deletes or
 * moves records must call codegen_debug_rows_remap so the rows follow (the printers and optimisers do).  File
 * numbers are unit-wide and handed out under E->dmu, so the threads agree on them; the main file is
 * number 0, as in clang (MC maps `.file N` of the main file to 0); below DWARF 5 every number is one more,
 * the main file 1.  Users: arm64's and x86-64's
 * direct emitters; VIR does not call it.
 */
#include "codegen-internal.h"

/* the .file number of a presumed file name (1-based; under dmu, shared by the threads) */
static uint32_t codegen_debug_file_find(codegen_context *e, const char *name, uint32_t len)
{
	codegen_unit *E = e->unit;
	uint32_t f = 0;

	const char *mf = E->options->dbg_name;

	if (e->debug_last_file_name && e->debug_last_file_name_length == len && !memcmp(e->debug_last_file_name, name, len))
		return e->debug_last_file_number;
	if (mf && strlen(mf) == len && !memcmp(mf, name, len))
		return 0;	/* the main file is file 0 (as clang; MC maps a .file N of it to 0) */
	pthread_mutex_lock(&E->debug_mutex);
	for (uint32_t i = 0; i < E->debug_file_count && !f; i++)
		if (strlen(E->debug_files[i]) == len && !memcmp(E->debug_files[i], name, len))
			f = i + 1;
	if (!f) {
		if (E->debug_file_count == E->debug_file_capacity) {
			E->debug_file_capacity = E->debug_file_capacity ? 2 * E->debug_file_capacity : 16;
			E->debug_files = realloc(E->debug_files, E->debug_file_capacity * sizeof *E->debug_files);
		}
		E->debug_files[E->debug_file_count] = strndup(name, len);
		f = ++E->debug_file_count;
	}
	pthread_mutex_unlock(&E->debug_mutex);
	e->debug_last_file_name = name, e->debug_last_file_name_length = len, e->debug_last_file_number = f;
	return f;
}

VPRIV bool codegen_debug_location_get(codegen_context *e, uint32_t loc, uint32_t *file, uint32_t *line, uint32_t *col)
{
	const char *name;
	uint32_t len;

	if (!pp_location_describe(e->compile_context->p, loc, &name, &len, line, col) || !*line)
		return false;
	*file = codegen_debug_file_find(e, name, len) + (CODEGEN_DWARF_VERSION(e->compile_context) < 5);	/* (DWARF 4: no file 0) */
	return true;
}

VPRIV void codegen_debug_row_add(codegen_context *e, uint32_t loc)
{
	uint32_t file, line, col;

	if (!e->is_reachable || !codegen_debug_location_get(e, loc, &file, &line, &col))
		return;
	if (e->debug_row_count && e->debug_rows[e->debug_row_count - 1].record_index >= e->record_count)
		e->debug_row_count--;	/* nothing emitted for the previous one */
	if (e->debug_row_count == e->debug_row_capacity) {
		e->debug_row_capacity = e->debug_row_capacity ? 2 * e->debug_row_capacity : 256;
		e->debug_rows = realloc(e->debug_rows, e->debug_row_capacity * sizeof *e->debug_rows);
	}
	e->debug_rows[e->debug_row_count++] = (struct codegen_debug_row){ e->record_count, file, line, col };
}

VPRIV void codegen_debug_rows_remap(codegen_context *e, uint32_t *j, uint32_t k, uint32_t n)
{
	/* compaction: record k (kept or dropped) is followed by new index n; rows at it move there */
	while (*j < e->debug_row_count && e->debug_rows[*j].record_index <= k)
		e->debug_rows[(*j)++].record_index = n;
}
