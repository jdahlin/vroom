/* arm64 thunk pass: calls out of branch range get thunks, as lld does; part of ld/ld.c (#included there) */

/* ---------------- thunk pass ---------------- */

/* call relocations of an ISD in thunk-pass order, with the destination
 * resolved to (plain input section, offset) where the layout cannot change
 * which section it is; anything else (script symbols, merge sections,
 * undefined, absolute, discarded) takes reloc_target() every pass */
typedef struct Call { uint32_t si, r, dsec, th; uint64_t doff; } Call;
static void collect_calls(ISD *d)
{
	uint32_t n = 0;
	for (uint32_t s = 0; s < d->secs.n; s++) {
		ISec *is = &isecs.v[d->secs.v[s]];
		if (is->obj == NONE)
			continue;
		for (uint32_t r = 0; r < is->nrela; r++) {
			uint32_t type = (uint32_t)is->rela[r].r_info;
			n += type == R_AARCH64_CALL26 || type == R_AARCH64_JUMP26;
		}
	}
	d->calls = alloc((size_t)n * sizeof(Call) + 1);
	d->ncalls = 0;
	for (uint32_t s = 0; s < d->secs.n; s++) {
		uint32_t si = d->secs.v[s];
		ISec *is = &isecs.v[si];
		if (is->obj == NONE)
			continue;
		Obj *o = objs.v[is->obj];
		for (uint32_t r = 0; r < is->nrela; r++) {
			const Rela *rl = &is->rela[r];
			uint32_t type = (uint32_t)rl->r_info;
			if (type != R_AARCH64_CALL26 && type != R_AARCH64_JUMP26)
				continue;
			Call *c = &d->calls[d->ncalls++];
			c->si = si;
			c->r = r;
			c->dsec = NONE;
			c->th = NONE;		/* tr_get(si, r), kept in step with tr_put */
			uint32_t symi = rl->r_info >> 32;
			uint32_t sec = NONE;
			uint64_t val = 0;
			if (symi >= o->firstg) {
				G *g = &gsyms[o->gs[symi - o->firstg]];
				if (g->state == GS_OBJ && !g->demoted && g->sec != NONE) {
					sec = g->sec;
					val = g->val;
				}
			} else {
				const Sym *y = &o->sym[symi];
				if (y->st_shndx != SHN_UNDEF && y->st_shndx < 0xff00) {
					sec = o->isec0 + y->st_shndx;
					val = (y->st_info & 15) == STT_SECTION ? 0 : y->st_value;
				}
			}
			if (sec == NONE)
				continue;
			ISec *ds = &isecs.v[sec];
			if (ds->kind != K_NORMAL || ds->osec == NONE || ds->osec == OS_DISCARD)
				continue;
			c->dsec = sec;
			c->doff = val + (uint64_t)rl->r_addend;
		}
	}
}

static bool create_thunks(int pass)
{
	bool changed = false;
	Target t;
	for (uint32_t c = 0; c < topcmds.n; c++) {
		if (topcmds.v[c].kind != C_OSEC)
			continue;
		uint32_t oi = topcmds.v[c].idx;
		OSec *os = &osecs.v[oi];
		if (!os->live || (os->flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR))
			continue;
		for (uint32_t k = 0; k < os->cmds.n; k++) {
			if (os->cmds.v[k].kind != C_ISD)
				continue;
			uint32_t di = os->cmds.v[k].idx;
			ISD *d = &isds.v[di];
			if (pass == 0 && d->secs.n) {
				ISec *last = &isecs.v[d->secs.v[d->secs.n - 1]];
				d->thunk_off = last->off + last->size;
			}
			if (pass == 0)
				collect_calls(d);
			uint32_t cur = NONE;
			uint64_t base = 0;
			for (uint32_t ci = 0; ci < d->ncalls; ci++) {
				Call *c = &d->calls[ci];
				uint32_t si = c->si, r = c->r;
				ISec *is = &isecs.v[si];
				Obj *o = objs.v[is->obj];
				if (si != cur) {
					cur = si;
					base = isec_va(si, 0);
				}
				const Rela *rl = &is->rela[r];
				uint64_t src = base + rl->r_offset;
				{
					uint32_t th = c->th;
					if (th != NONE) {
						if (in_branch_range(src, thunk_va(th)))
							continue;
					}
					if (c->dsec != NONE) {
						/* same value as reloc_target(): a plain input section */
						const ISec *ds = &isecs.v[c->dsec];
						t.va = osecs.v[ds->osec].va + ds->off + c->doff;
					} else {
						reloc_target(o, rl->r_info >> 32, rl->r_addend, &t);
						if (t.undef)
							continue;
					}
					if (in_branch_range(src, t.va))
						continue;
					/* find a compatible thunk in range */
					uint32_t dg = NONE, dsec = NONE;
					uint64_t dval = 0;
					uint32_t symi = rl->r_info >> 32;
					if (symi >= o->firstg) {
						dg = o->gs[symi - o->firstg];
						G *gs = &gsyms[dg];
						if (gs->state == GS_OBJ && gs->sec != NONE) {
							dsec = gs->sec;
							dval = gs->val;
						}
					} else {
						const Sym *y = &o->sym[symi];
						dsec = o->isec0 + y->st_shndx;
						dval = y->st_value;
					}
					uint32_t found = NONE;
					for (uint32_t x = 0; x < thunks.n; x++) {
						Thunk *tx = &thunks.v[x];
						bool same = dsec != NONE ? (tx->sec == dsec && tx->val == dval)
									 : (tx->g == dg && tx->sec == NONE);
						if (same && tx->addend == rl->r_addend &&
						    in_branch_range(src, thunk_va(x))) {
							found = x;
							break;
						}
					}
					if (found == NONE) {
						if (d->thunksec == NONE) {
							uint32_t ts = new_isec(".text.thunk", K_THUNK, SHT_PROGBITS,
									       SHF_ALLOC | SHF_EXECINSTR, 4);
							d = &isds.v[di];
							VVEC_PUSH(thunksecs, ((ThunkSec){0}), alloc);
							isecs.v[ts].aux = thunksecs.n - 1;
							isecs.v[ts].osec = oi;
							isecs.v[ts].isd = di;
							isecs.v[ts].off = d->thunk_off;
							d->thunksec = ts;
						}
						Thunk tn = {.g = dsec == NONE ? dg : NONE, .sec = dsec, .val = dval,
							    .addend = rl->r_addend, .tsec = d->thunksec};
						if (dg != NONE)
							tn.g = dsec == NONE ? dg : NONE;
						const char *dn = dg != NONE ? gname(dg) : o->str + o->sym[symi].st_name;
						char *nm = alloc(strlen(dn) + 20);
						sprintf(nm, "__AArch64ADRPThunk_%s", dn);
						tn.name = nm;
						ThunkSec *tsx = &thunksecs.v[isecs.v[d->thunksec].aux];
						tn.off = 0;
						VVEC_PUSH(thunks, tn, alloc);
						VVEC_PUSH(tsx->list, thunks.n - 1, alloc);
						found = thunks.n - 1;
						changed = true;
					}
					tr_put(si, r, found);
					c->th = found;
				}
			}
			/* assign thunk offsets in this ISD's thunk section */
			if (d->thunksec != NONE) {
				ISec *ts = &isecs.v[d->thunksec];
				ThunkSec *tsx = &thunksecs.v[ts->aux];
				uint64_t off = 0;
				for (uint32_t x = 0; x < tsx->list.n; x++) {
					Thunk *tx = &thunks.v[tsx->list.v[x]];
					tx->off = off;
					/* getMayUseShortThunk(): b if the destination is within
					   +-128 MiB, decided against the current layout */
					if (!tx->lng) {
						int64_t dd = (int64_t)(thunk_dest(tsx->list.v[x]) - thunk_va(tsx->list.v[x]));
						tx->lng = !fits_int(dd, 28);
					}
					tx->size = tx->lng ? 12 : 4;
					off += tx->size;
				}
				if (off != ts->size)
					changed = true;
				ts->size = off;
			}
		}
	}
	/* merge newly created thunk sections into their ISD lists */
	for (uint32_t i = 0; i < isds.n; i++) {
		ISD *d = &isds.v[i];
		if (d->thunksec == NONE)
			continue;
		bool present = false;
		for (uint32_t s = d->secs.n; s-- > 0;)
			if (d->secs.v[s] == d->thunksec) {
				present = true;
				break;
			}
		if (present)
			continue;
		/* before trailing sections that start at or after the thunk offset */
		uint32_t pos = d->secs.n;
		while (pos > 0 && isecs.v[d->secs.v[pos - 1]].off >= d->thunk_off)
			pos--;
		VVEC_PUSH(d->secs, 0, alloc);
		memmove(&d->secs.v[pos + 1], &d->secs.v[pos], (d->secs.n - 1 - pos) * sizeof(uint32_t));
		d->secs.v[pos] = d->thunksec;
	}
	return changed;
}
