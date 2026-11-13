/*
 * debug-rows.c: the line table of a function (-g): which source line, column and file each
 * instruction record belongs to.
 *
 * dbg_row is called by the lowering at each statement it wants a row for and attaches the current
 * record index to the source location; rows for nothing emitted are dropped.  A pass that deletes or
 * moves records must call dbg_remap so the rows follow (the printers and optimisers do).  File
 * numbers are unit-wide and handed out under E->dmu, so the threads agree on them; the main file is
 * number 0, as in clang (MC maps `.file N` of the main file to 0).  Users: arm64's and x86-64's
 * direct emitters, and VIR's lowering driver (codegen/vir/lowering-driver.c) at each block and
 * each run of f->loc (x86-64 through x64_dbgrow).
 */
#include "emitimpl.h"

/* the .file number of a presumed file name (1-based; under dmu, shared by the threads) */
static uint32_t dbg_file(ecx *e, const char *name, uint32_t len)
{
	emitter *E = e->E;
	uint32_t f = 0;

	const char *mf = E->o->dbg_name;

	if (e->dlname && e->dlnlen == len && !memcmp(e->dlname, name, len))
		return e->dlfile;
	if (mf && strlen(mf) == len && !memcmp(mf, name, len))
		return 0;	/* the main file is file 0 (as clang; MC maps a .file N of it to 0) */
	pthread_mutex_lock(&E->dmu);
	for (uint32_t i = 0; i < E->ndfile && !f; i++)
		if (strlen(E->dfile[i]) == len && !memcmp(E->dfile[i], name, len))
			f = i + 1;
	if (!f) {
		if (E->ndfile == E->dfcap) {
			E->dfcap = E->dfcap ? 2 * E->dfcap : 16;
			E->dfile = realloc(E->dfile, E->dfcap * sizeof *E->dfile);
		}
		E->dfile[E->ndfile] = strndup(name, len);
		f = ++E->ndfile;
	}
	pthread_mutex_unlock(&E->dmu);
	e->dlname = name, e->dlnlen = len, e->dlfile = f;
	return f;
}

VPRIV bool dbg_where(ecx *e, uint32_t loc, uint32_t *file, uint32_t *line, uint32_t *col)
{
	const char *name;
	uint32_t len;

	if (!pp_location_describe(e->c->p, loc, &name, &len, line, col) || !*line)
		return false;
	*file = dbg_file(e, name, len);
	return true;
}

VPRIV void dbg_row(ecx *e, uint32_t loc)
{
	uint32_t file, line, col;

	if (!e->reach || !dbg_where(e, loc, &file, &line, &col))
		return;
	if (e->ndl && e->dl[e->ndl - 1].k >= e->nin)
		e->ndl--;	/* nothing emitted for the previous one */
	if (e->ndl == e->dlcap) {
		e->dlcap = e->dlcap ? 2 * e->dlcap : 256;
		e->dl = realloc(e->dl, e->dlcap * sizeof *e->dl);
	}
	e->dl[e->ndl++] = (struct dbgrow){ e->nin, file, line, col };
}

VPRIV void dbg_remap(ecx *e, uint32_t *j, uint32_t k, uint32_t n)
{
	/* compaction: record k (kept or dropped) is followed by new index n; rows at it move there */
	while (*j < e->ndl && e->dl[*j].k <= k)
		e->dl[(*j)++].k = n;
}
