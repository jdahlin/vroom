/*
 * x86-64 relocations for vlink (static executables, as ld.lld -static): the
 * scan (GOT slots, what is supported), relaxations and the application.
 * Part of ld/ld.c (#included there, after arm64's).  lld's decisions:
 *   - PLT32 and PC32 resolve directly (no PLT in a static link);
 *   - [REX_]GOTPCRELX with addend -4 against a defined, non-absolute
 *     symbol relaxes: mov -> lea, call/jmp * -> addr32 call / jmp + nop,
 *     test and the ALU ops (with REX, not PIC) -> their immediate forms;
 *     anything else gets a .got slot;
 *   - initial-exec GOTTPOFF and general-dynamic TLSGD relax to local-exec;
 *     the thread pointer is the TLS block's end rounded to its alignment
 *     (variant 2): tpoff = va - tls_tp, dtpoff = va - the block's start.
 */


/* a [REX_]GOTPCRELX that relaxes, as lld's adjustGotPcExpr (0: no, 1: rip form, 2: immediate form) */
static int x64_relax_kind(const Obj *o, const ISec *is, const Rela *rl, const Target *t)
{
	uint32_t type = (uint32_t)rl->r_info;

	if (!opt.relax || rl->r_addend != -4 || (type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX))
		return 0;
	if (t->undef || t->abs || t->discarded || rl->r_offset < 2)
		return 0;
	(void)o;
	{
		const uint8_t *p = is->data + rl->r_offset;
		uint8_t op = p[-2], modrm = p[-1];

		if (op == 0x8b)
			return 1;
		if (op == 0xff && (modrm == 0x15 || modrm == 0x25))
			return 1;
		if (type == R_X86_64_GOTPCRELX)
			return 0;
		return pic ? 0 : 2;
	}
}

static bool x64_needs_got(const Obj *o, const ISec *is, const Rela *rl)
{
	uint32_t type = (uint32_t)rl->r_info;
	Target t;

	/* lld keeps .got (empty if need be) once a GD/LD sequence is relaxed */
	if (type == R_X86_64_TLSGD || type == R_X86_64_TLSLD)
		return true;
	if (type != R_X86_64_GOTPCREL && type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX)
		return false;
	reloc_class(o, (uint32_t)(rl->r_info >> 32), &t);
	return !x64_relax_kind(o, is, rl, &t);
}

static void x64_find_got(void *arg, size_t i)
{
	(void)arg;
	const ISec *is = &isecs.v[i];

	if (is->kind == K_NONE || is->obj == NONE || atomic_load_explicit(&got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->nrela; r++)
		if (x64_needs_got(objs.v[is->obj], is, &is->rela[r])) {
			atomic_store(&got_seen, true);
			return;
		}
}

static void x64_scan_relocs(void *arg, size_t i)
{
	(void)arg;
	ISec *is = &isecs.v[i];
	if (!is->nrela || is->kind == K_NONE || is->obj == NONE)
		return;
	uint32_t os = isec_osec(i);
	if (os == OS_DISCARD || os == NONE)
		return;
	Obj *o = objs.v[is->obj];
	Target t;
	uint32_t nd = 0;	/* R_X86_64_RELATIVE entries this section needs (-pie, -shared) */

	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;

		if (symi >= o->nsym) {
			error("%s: bad symbol index in relocation", o->path);
			continue;
		}
		reloc_class(o, symi, &t);
		if (t.discarded && (is->flags & SHF_ALLOC))
			error("%s:(%s+0x%llx): relocation refers to a discarded section", o->path, is->name,
			      (unsigned long long)rl->r_offset);
		switch (type) {
		case R_X86_64_NONE: case R_X86_64_64: case R_X86_64_PC32: case R_X86_64_PLT32: case R_X86_64_32:
		case R_X86_64_32S: case R_X86_64_16: case R_X86_64_PC16: case R_X86_64_8: case R_X86_64_PC8:
		case R_X86_64_PC64: case R_X86_64_TPOFF32: case R_X86_64_TPOFF64: case R_X86_64_DTPOFF32:
		case R_X86_64_DTPOFF64: case R_X86_64_GOTTPOFF: case R_X86_64_TLSGD: case R_X86_64_SIZE32: case R_X86_64_SIZE64:
			if (opt.shared && (type == R_X86_64_GOTTPOFF || type == R_X86_64_TLSGD))
				error("%s:(%s+0x%llx): TLS relocation %u in a shared object is not supported", o->path,
				      is->name, (unsigned long long)rl->r_offset, type);
			if (pic && (is->flags & SHF_ALLOC) && !t.abs && !t.undef && !t.discarded) {
				/* an absolute address in position-independent output: RELATIVE at run time (lld) */
				if (type == R_X86_64_64 && opt.shared && t.global && !opt.bsymbolic)
					error("%s:(%s+0x%llx): R_X86_64_64 against a preemptible symbol is not supported yet",
					      o->path, is->name, (unsigned long long)rl->r_offset);
				else if (type == R_X86_64_64)
					nd++;
				else if (type == R_X86_64_32 || type == R_X86_64_32S || type == R_X86_64_16 || type == R_X86_64_8)
					error("%s:(%s+0x%llx): relocation %u cannot be used against a relocatable address; recompile with -fPIC",
					      o->path, is->name, (unsigned long long)rl->r_offset, type);
			}
			break;
		case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX:
			if (x64_needs_got(o, is, rl)) {
				if (symi < o->firstg)
					atomic_store_explicit(&o->lgot[symi], 1, memory_order_relaxed);
				else
					atomic_store(&gsyms[o->gs[symi - o->firstg]].needgot, 1);
			}
			break;
		default:
			error("%s:(%s+0x%llx): unsupported relocation type %u", o->path, is->name,
			      (unsigned long long)rl->r_offset, type);
		}
	}
	is->ndyn = nd;
}

static void x64_relocate_isec(uint32_t si, uint8_t *buf)
{
	ISec *is = &isecs.v[si];
	Obj *o = objs.v[is->obj];
	uint64_t secva = isec_va(si, 0);
	Target t;
	uint32_t nd = 0;

	for (uint32_t r = 0; r < is->nrela; r++) {
		const Rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		uint8_t *loc = buf + rl->r_offset;
		uint64_t P = secva + rl->r_offset;
		int64_t v;

		if (rl->r_offset >= is->size + (type == R_X86_64_NONE)) {
			error("%s:(%s): relocation offset out of range", o->path, is->name);
			continue;
		}
		reloc_target_final(o, symi, rl->r_addend, &t);
		switch (type) {
		case R_X86_64_NONE:
			break;
		case R_X86_64_64:
			if (pic && (is->flags & SHF_ALLOC) && !t.abs && !t.undef && !t.discarded && nd < is->ndyn && reladyn_buf) {
				Rela *dyn = reladyn_buf + is->dyn0 + nd++;

				dyn->r_offset = P;
				dyn->r_info = 8;	/* R_X86_64_RELATIVE */
				dyn->r_addend = (int64_t)t.va;
			}
			wr64(loc, t.va);
			break;
		case R_X86_64_PC64:
			wr64(loc, t.va - P);
			break;
		case R_X86_64_32:
			if ((uint64_t)t.va > UINT32_MAX)
				reloc_error(o, is, rl, "out of range", (int64_t)t.va);
			wr32(loc, (uint32_t)t.va);
			break;
		case R_X86_64_32S:
			if (!fits_int((int64_t)t.va, 32))
				reloc_error(o, is, rl, "out of range", (int64_t)t.va);
			wr32(loc, (uint32_t)t.va);
			break;
		case R_X86_64_16: case R_X86_64_PC16: {
			uint16_t x = (uint16_t)(type == R_X86_64_16 ? t.va : t.va - P);

			memcpy(loc, &x, 2);
			break;
		}
		case R_X86_64_8: case R_X86_64_PC8:
			*loc = (uint8_t)(type == R_X86_64_8 ? t.va : t.va - P);
			break;
		case R_X86_64_PC32: case R_X86_64_PLT32:
			v = (int64_t)(t.va - P);
			if (!fits_int(v, 32))
				reloc_error(o, is, rl, "out of range", v);
			wr32(loc, (uint32_t)v);
			break;
		case R_X86_64_SIZE32: case R_X86_64_SIZE64: {
			uint64_t sz = symi < o->firstg ? o->sym[symi].st_size : gsyms[o->gs[symi - o->firstg]].size;

			if (type == R_X86_64_SIZE32)
				wr32(loc, (uint32_t)(sz + (uint64_t)rl->r_addend));
			else
				wr64(loc, sz + (uint64_t)rl->r_addend);
			break;
		}
		case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX: {
			int k = x64_relax_kind(o, is, rl, &t);

			if (k == 1) {
				uint8_t op = loc[-2], modrm = loc[-1];

				v = (int64_t)(t.va - P);
				if (op == 0x8b) {
					loc[-2] = 0x8d;		/* mov -> lea */
					wr32(loc, (uint32_t)v);
				} else if (modrm == 0x15) {
					loc[-2] = 0x67, loc[-1] = 0xe8;	/* addr32 call */
					wr32(loc, (uint32_t)v);
				} else {
					loc[-2] = 0xe9, loc[3] = 0x90;	/* jmp, nop */
					wr32(loc - 1, (uint32_t)(v + 1));
				}
			} else if (k == 2) {
				/* test/binop sym@GOTPCREL(%rip), %reg -> $sym (absolute: the -4 compensated) */
				uint8_t op = loc[-2], modrm = loc[-1], rex = loc[-3];
				uint64_t a = t.va + 4;

				if (op == 0x85) {
					loc[-1] = (uint8_t)(0xc0 | (modrm & 0x38) >> 3);
					loc[-2] = 0xf7;
				} else {
					loc[-1] = (uint8_t)(0xc0 | (modrm & 0x38) >> 3 | (op & 0x3c));
					loc[-2] = 0x81;
				}
				loc[-3] = (uint8_t)((rex & ~0x4) | (rex & 0x4) >> 2);
				wr32(loc, (uint32_t)a);
			} else {
				uint64_t slot = isec_va(got_isec, 8 * (uint64_t)(symi < o->firstg ? o->lgotidx[symi] :
										  gsyms[o->gs[symi - o->firstg]].gotidx));

				v = (int64_t)(slot + (uint64_t)rl->r_addend - P);
				wr32(loc, (uint32_t)v);
			}
			break;
		}
		case R_X86_64_TPOFF32: case R_X86_64_TPOFF64:
			v = (int64_t)(t.va - tls_tp);
			if (type == R_X86_64_TPOFF64)
				wr64(loc, (uint64_t)v);
			else
				wr32(loc, (uint32_t)v);
			break;
		case R_X86_64_DTPOFF32: case R_X86_64_DTPOFF64:
			v = (int64_t)(t.va - tls_start);
			if (type == R_X86_64_DTPOFF64)
				wr64(loc, (uint64_t)v);
			else
				wr32(loc, (uint32_t)v);
			break;
		case R_X86_64_GOTTPOFF: {
			/* initial-exec -> local-exec (lld's relaxTlsIeToLe) */
			uint8_t *in = loc - 3, reg = (uint8_t)((loc[-1] >> 3) & 7);

			if (rl->r_offset < 3) {
				reloc_error(o, is, rl, "GOTTPOFF at the start of a section", 0);
				break;
			}
			if (in[0] == 0x48 && in[1] == 0x03 && in[2] == 0x25)
				in[0] = 0x48, in[1] = 0x81, in[2] = 0xc4;
			else if (in[0] == 0x4c && in[1] == 0x03 && in[2] == 0x25)
				in[0] = 0x49, in[1] = 0x81, in[2] = 0xc4;
			else if (in[0] == 0x4c && in[1] == 0x03)
				in[0] = 0x4d, in[1] = 0x8d, loc[-1] = (uint8_t)(0x80 | reg << 3 | reg);
			else if (in[0] == 0x48 && in[1] == 0x03)
				in[0] = 0x48, in[1] = 0x8d, loc[-1] = (uint8_t)(0x80 | reg << 3 | reg);
			else if (in[0] == 0x4c && in[1] == 0x8b)
				in[0] = 0x49, in[1] = 0xc7, loc[-1] = (uint8_t)(0xc0 | reg);
			else if (in[0] == 0x48 && in[1] == 0x8b)
				in[0] = 0x48, in[1] = 0xc7, loc[-1] = (uint8_t)(0xc0 | reg);
			else
				reloc_error(o, is, rl, "R_X86_64_GOTTPOFF must be used in MOVQ or ADDQ instructions only", 0);
			wr32(loc, (uint32_t)(t.va - tls_tp + 4));
			break;
		}
		case R_X86_64_TLSGD: {
			/* general dynamic -> local-exec: mov %fs:0, %rax; lea x@tpoff(%rax), %rax (lld's relaxTlsGdToLe) */
			static const uint8_t in[16] = { 0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0, 0x48, 0x8d, 0x80, 0, 0, 0, 0 };

			if (rl->r_offset < 4 || rl->r_offset + 12 > is->size) {
				reloc_error(o, is, rl, "malformed TLSGD sequence", 0);
				break;
			}
			memcpy(loc - 4, in, sizeof in);
			wr32(loc + 8, (uint32_t)(t.va - tls_tp + 4));
			r++;	/* (the call to __tls_get_addr is gone) */
			break;
		}
		default:
			break;
		}
	}
}
