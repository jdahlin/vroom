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
 *     (variant 2): tpoff = va - tls_thread_pointer, dtpoff = va - the block's start.
 */

#include "../../../ld/ld-internal.h"


/* a [REX_]GOTPCRELX that relaxes, as lld's adjustGotPcExpr (0: no, 1: rip form, 2: immediate form) */
static int x64_ld_got_relaxation_kind(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl, const ld_relocation_target *t)
{
	uint32_t type = (uint32_t)rl->r_info;

	if (!ctx->options.relax || rl->r_addend != -4 || (type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX))
		return 0;
	if (t->is_undefined || t->is_absolute || t->discarded || rl->r_offset < 2)
		return 0;
	(void)o;
	{
		const uint8_t *p = is->data + rl->r_offset;
		uint8_t op = p[-2], modrm = p[-1];

		if (op == 0x8b)
			return 1;
		if (op == 0xff && (modrm == 0x15 || modrm == 0x25))
			return 1;
		if (type == R_X86_64_GOTPCRELX || rl->r_offset < 3)
			return 0;	/* (the immediate form rewrites the REX byte too) */
		return ctx->relocations.pic ? 0 : 2;
	}
}

static bool x64_ld_relocation_needs_got(struct ld_context *ctx, const ld_object *o, const ld_input_section *is, const elf_rela *rl)
{
	uint32_t type = (uint32_t)rl->r_info;
	ld_relocation_target t;

	/* lld keeps .got (empty if need be) once a GD/LD sequence is relaxed */
	if (type == R_X86_64_TLSGD || type == R_X86_64_TLSLD)
		return true;
	if (type != R_X86_64_GOTPCREL && type != R_X86_64_GOTPCRELX && type != R_X86_64_REX_GOTPCRELX)
		return false;
	a64_ld_relocation_target_classify(ctx, o, (uint32_t)(rl->r_info >> 32), &t);
	return !x64_ld_got_relaxation_kind(ctx, o, is, rl, &t);
}

VPRIV void x64_ld_got_find(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	const ld_input_section *is = &ctx->inputs.sections.v[i];

	if (is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE || atomic_load_explicit(&ctx->relocations.got_seen, memory_order_relaxed))
		return;
	for (uint32_t r = 0; r < is->rela_count; r++)
		if (x64_ld_relocation_needs_got(ctx, ctx->inputs.objects.v[is->object], is, &is->rela[r])) {
			atomic_store(&ctx->relocations.got_seen, true);
			return;
		}
}

VPRIV void x64_ld_relocation_scan(void *arg, size_t i)
{
	struct ld_context *ctx = arg;
	ld_input_section *is = &ctx->inputs.sections.v[i];
	if (!is->rela_count || is->kind == LD_SECTION_KIND_NONE || is->object == LD_NONE)
		return;
	uint32_t os = ld_input_section_output_section(ctx, i);
	if (os == LD_OUTPUT_SECTION_DISCARD || os == LD_NONE)
		return;
	ld_object *o = ctx->inputs.objects.v[is->object];
	ld_relocation_target t;

	for (uint32_t r = 0; r < is->rela_count; r++) {
		const elf_rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;

		if (symi >= o->symbol_count) {
			ld_error_report(ctx, "%s: bad symbol index in relocation", o->path);
			continue;
		}
		a64_ld_relocation_target_classify(ctx, o, symi, &t);
		if (t.discarded && (is->flags & SHF_ALLOC))
			ld_error_report(ctx, "%s:(%s+0x%llx): relocation refers to a discarded section", o->path, is->name,
			      (unsigned long long)rl->r_offset);
		switch (type) {
		case R_X86_64_NONE: case R_X86_64_64: case R_X86_64_PC32: case R_X86_64_PLT32: case R_X86_64_32:
		case R_X86_64_32S: case R_X86_64_16: case R_X86_64_PC16: case R_X86_64_8: case R_X86_64_PC8:
		case R_X86_64_PC64: case R_X86_64_TPOFF32: case R_X86_64_TPOFF64: case R_X86_64_DTPOFF32:
		case R_X86_64_DTPOFF64: case R_X86_64_GOTTPOFF: case R_X86_64_TLSGD: case R_X86_64_SIZE32: case R_X86_64_SIZE64:
			if (ctx->options.shared && (type == R_X86_64_GOTTPOFF || type == R_X86_64_TLSGD))
				ld_error_report(ctx, "%s:(%s+0x%llx): TLS relocation %u in a shared object is not supported", o->path,
				      is->name, (unsigned long long)rl->r_offset, type);
			break;
		case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX:
			if (x64_ld_relocation_needs_got(ctx, o, is, rl)) {
				if (symi < o->first_global_index)
					atomic_store_explicit(&o->local_needs_got[symi], 1, memory_order_relaxed);
				else
					atomic_store(&ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].needs_got, 1);
			}
			break;
		default:
			ld_error_report(ctx, "%s:(%s+0x%llx): unsupported relocation type %u", o->path, is->name,
			      (unsigned long long)rl->r_offset, type);
		}
	}
	is->dynamic_relocation_count = 0;
}

VPRIV void x64_ld_input_section_relocate(struct ld_context *ctx, uint32_t si, uint8_t *buf)
{
	ld_input_section *is = &ctx->inputs.sections.v[si];
	ld_object *o = ctx->inputs.objects.v[is->object];
	uint64_t secva = ld_input_section_address(ctx, si, 0);
	ld_relocation_target t;

	for (uint32_t r = 0; r < is->rela_count; r++) {
		const elf_rela *rl = &is->rela[r];
		uint32_t type = (uint32_t)rl->r_info, symi = rl->r_info >> 32;
		uint8_t *loc = buf + rl->r_offset;
		uint64_t P = secva + rl->r_offset;
		int64_t v;

		if (rl->r_offset >= is->size + (type == R_X86_64_NONE)) {
			ld_error_report(ctx, "%s:(%s): relocation offset out of range", o->path, is->name);
			continue;
		}
		ld_relocation_target_final_find(ctx, o, symi, rl->r_addend, &t);
		switch (type) {
		case R_X86_64_NONE:
			break;
		case R_X86_64_64:
			ld_u64_store(loc, t.address);
			break;
		case R_X86_64_PC64:
			ld_u64_store(loc, t.address - P);
			break;
		case R_X86_64_32:
			if ((uint64_t)t.address > UINT32_MAX)
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", (int64_t)t.address);
			ld_u32_store(loc, (uint32_t)t.address);
			break;
		case R_X86_64_32S:
			if (!ld_value_fits_signed((int64_t)t.address, 32))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", (int64_t)t.address);
			ld_u32_store(loc, (uint32_t)t.address);
			break;
		case R_X86_64_16: case R_X86_64_PC16: {
			uint16_t x = (uint16_t)(type == R_X86_64_16 ? t.address : t.address - P);

			memcpy(loc, &x, 2);
			break;
		}
		case R_X86_64_8: case R_X86_64_PC8:
			*loc = (uint8_t)(type == R_X86_64_8 ? t.address : t.address - P);
			break;
		case R_X86_64_PC32: case R_X86_64_PLT32:
			v = (int64_t)(t.address - P);
			if (!ld_value_fits_signed(v, 32))
				a64_ld_relocation_error_report(ctx, o, is, rl, "out of range", v);
			ld_u32_store(loc, (uint32_t)v);
			break;
		case R_X86_64_SIZE32: case R_X86_64_SIZE64: {
			uint64_t sz = symi < o->first_global_index ? o->symbols[symi].st_size : ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].size;

			if (type == R_X86_64_SIZE32)
				ld_u32_store(loc, (uint32_t)(sz + (uint64_t)rl->r_addend));
			else
				ld_u64_store(loc, sz + (uint64_t)rl->r_addend);
			break;
		}
		case R_X86_64_GOTPCREL: case R_X86_64_GOTPCRELX: case R_X86_64_REX_GOTPCRELX: {
			int k = x64_ld_got_relaxation_kind(ctx, o, is, rl, &t);

			if (k == 1) {
				uint8_t op = loc[-2], modrm = loc[-1];

				v = (int64_t)(t.address - P);
				if (op == 0x8b) {
					loc[-2] = 0x8d;		/* mov -> lea */
					ld_u32_store(loc, (uint32_t)v);
				} else if (modrm == 0x15) {
					loc[-2] = 0x67, loc[-1] = 0xe8;	/* addr32 call */
					ld_u32_store(loc, (uint32_t)v);
				} else {
					loc[-2] = 0xe9, loc[3] = 0x90;	/* jmp, nop */
					ld_u32_store(loc - 1, (uint32_t)(v + 1));
				}
			} else if (k == 2) {
				/* test/binop sym@GOTPCREL(%rip), %reg -> $sym (absolute: the -4 compensated) */
				uint8_t op = loc[-2], modrm = loc[-1], rex = loc[-3];
				uint64_t a = t.address + 4;

				if (op == 0x85) {
					loc[-1] = (uint8_t)(0xc0 | (modrm & 0x38) >> 3);
					loc[-2] = 0xf7;
				} else {
					loc[-1] = (uint8_t)(0xc0 | (modrm & 0x38) >> 3 | (op & 0x3c));
					loc[-2] = 0x81;
				}
				loc[-3] = (uint8_t)((rex & ~0x4) | (rex & 0x4) >> 2);
				ld_u32_store(loc, (uint32_t)a);
			} else {
				uint64_t slot = ld_input_section_address(ctx, ctx->output.got_section, 8 * (uint64_t)(symi < o->first_global_index ? o->local_got_index[symi] :
										  ctx->symbols.globals[o->global_slots[symi - o->first_global_index]].got_index));

				v = (int64_t)(slot + (uint64_t)rl->r_addend - P);
				ld_u32_store(loc, (uint32_t)v);
			}
			break;
		}
		case R_X86_64_TPOFF32: case R_X86_64_TPOFF64:
			v = (int64_t)(t.address - ctx->layout.tls_thread_pointer);
			if (type == R_X86_64_TPOFF64)
				ld_u64_store(loc, (uint64_t)v);
			else
				ld_u32_store(loc, (uint32_t)v);
			break;
		case R_X86_64_DTPOFF32: case R_X86_64_DTPOFF64:
			v = (int64_t)(t.address - ctx->layout.tls_start);
			if (type == R_X86_64_DTPOFF64)
				ld_u64_store(loc, (uint64_t)v);
			else
				ld_u32_store(loc, (uint32_t)v);
			break;
		case R_X86_64_GOTTPOFF: {
			/* initial-exec -> local-exec (lld's relaxTlsIeToLe) */
			uint8_t *in = loc - 3, reg;

			if (rl->r_offset < 3) {
				a64_ld_relocation_error_report(ctx, o, is, rl, "GOTTPOFF at the start of a section", 0);
				break;
			}
			reg = (uint8_t)((loc[-1] >> 3) & 7);
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
				a64_ld_relocation_error_report(ctx, o, is, rl, "R_X86_64_GOTTPOFF must be used in MOVQ or ADDQ instructions only", 0);
			ld_u32_store(loc, (uint32_t)(t.address - ctx->layout.tls_thread_pointer + 4));
			break;
		}
		case R_X86_64_TLSGD: {
			/* general dynamic -> local-exec: mov %fs:0, %rax; lea x@tpoff(%rax), %rax (lld's relaxTlsGdToLe) */
			static const uint8_t in[16] = { 0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0, 0x48, 0x8d, 0x80, 0, 0, 0, 0 };

			if (rl->r_offset < 4 || rl->r_offset + 12 > is->size) {
				a64_ld_relocation_error_report(ctx, o, is, rl, "malformed TLSGD sequence", 0);
				break;
			}
			memcpy(loc - 4, in, sizeof in);
			ld_u32_store(loc + 8, (uint32_t)(t.address - ctx->layout.tls_thread_pointer + 4));
			r++;	/* (the call to __tls_get_addr is gone) */
			break;
		}
		default:
			break;
		}
	}
}
