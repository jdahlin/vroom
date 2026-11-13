/* Copying each input section into the output image and applying its relocations, in parallel. */

#include "ld-internal.h"


VPRIV void ld_input_section_write(void *arg, size_t j)
{
	struct ld_context *ctx = arg;
	uint32_t si = ctx->output.write_jobs.v[j];
	ld_input_section *is = &ctx->inputs.sections.v[si];
	uint32_t oi = ld_input_section_output_section(ctx, si);
	ld_output_section *os = &ctx->script.sections.v[oi];
	if (os->type == SHT_NOBITS || is->type == SHT_NOBITS)
		return;
	uint8_t *dst = ctx->output.image + os->offset + is->offset;
	switch (is->kind) {
	case LD_SECTION_KIND_NORMAL:
		memcpy(dst, is->data, is->size);
		if (is->eh_frame_cie_piece)	/* an FDE's CIE pointer: from the field to its CIE */
			ld_u32_store(dst + 4, (uint32_t)(ld_input_section_address(ctx, si, 4) - ld_input_section_address(ctx, is->eh_frame_cie_piece - 1, 0)));
		if (is->rela_count && (is->flags & SHF_ALLOC || !ctx->options.relocatable)) {
			if (ctx->inputs.x86)
				x64_ld_input_section_relocate(ctx, si, dst);
			else
				a64_ld_input_section_relocate(ctx, si, dst);
		}
		break;
	case LD_SECTION_KIND_MERGE: {
		ld_merge_section *mg = &ctx->inputs.merge_sections.v[is->auxiliary_index];
		for (int s = 0; s < 32; s++)
			for (uint32_t k = 0; k < mg->unique_pieces[s].n; k++) {
				uint64_t u = mg->unique_pieces[s].v[k];
				ld_merge_input *m = &ctx->inputs.merge_inputs.v[u >> 32];
				uint32_t p = (uint32_t)u;
				memcpy(dst + m->output_offsets[p], ctx->inputs.sections.v[m->input_section].data + m->input_offsets[p], ld_merge_piece_length(ctx, m, p));
			}
		break;
	}
	case LD_SECTION_KIND_THUNK: {
		a64_ld_thunk_section *ts = &ctx->relocations.thunk_sections.v[is->auxiliary_index];
		for (uint32_t k = 0; k < ts->list.n; k++) {
			uint32_t t = ts->list.v[k];
			uint8_t *b = dst + ctx->relocations.thunks.v[t].offset;
			uint64_t s = a64_ld_thunk_destination(ctx, t), p = a64_ld_thunk_address(ctx, t);
			if (!ctx->relocations.thunks.v[t].is_long) {
				ld_u32_store(b, 0x14000000 | (uint32_t)(((s - p) & 0x0ffffffc) >> 2));
				continue;
			}
			int64_t pg = (int64_t)((s & ~0xfffULL) - (p & ~0xfffULL));
			uint64_t imm = (uint64_t)pg >> 12;
			uint32_t adrp = 0x90000010 | (uint32_t)((imm & 3) << 29 | ((imm >> 2) & 0x7ffff) << 5);
			ld_u32_store(b, adrp);
			ld_u32_store(b + 4, 0x91000210 | (uint32_t)(s & 0xfff) << 10);
			ld_u32_store(b + 8, 0xd61f0200);
		}
		break;
	}
	case LD_SECTION_KIND_GOT: {
		elf_rela *dyn = ctx->relocations.rela_dyn_buffer ? ctx->relocations.rela_dyn_buffer + is->first_dynamic_relocation : NULL;
		uint32_t nd = 0;
		for (uint32_t k = 0; k < ctx->output.got_symbols.n; k++) {
			uint32_t g = ctx->output.got_symbols.v[k];
			bool undef = ctx->symbols.globals[g].state == LD_SYMBOL_STATE_UNDEFINED || ctx->symbols.globals[g].demoted;
			uint64_t v = undef ? 0 : ld_global_symbol_address(ctx, g);
			ld_u64_store(dst + 8 * k, v);
			if (ctx->relocations.pic && !undef && !ld_global_symbol_is_absolute(ctx, g)) {
				dyn[nd].r_offset = ld_input_section_address(ctx, si, 8 * k);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		for (uint32_t k = 0; k < ctx->output.got_locals.n; k++) {
			const ld_object *o = ctx->inputs.objects.v[ctx->output.got_locals.v[k].object_index];
			const elf_symbol *y = &o->symbols[ctx->output.got_locals.v[k].symbol_index];
			bool abs = y->st_shndx == SHN_ABS;
			uint64_t v = abs ? y->st_value : ld_input_section_address(ctx, o->first_input_section + y->st_shndx, y->st_value);
			uint64_t at = 8 * ((uint64_t)ctx->output.got_symbols.n + k);

			ld_u64_store(dst + at, v);
			if (ctx->relocations.pic && !abs) {
				dyn[nd].r_offset = ld_input_section_address(ctx, si, at);
				dyn[nd].r_info = R_AARCH64_RELATIVE;
				dyn[nd].r_addend = (int64_t)v;
				nd++;
			}
		}
		break;
	}
	case LD_SECTION_KIND_BUILD_ID:
		ld_u32_store(dst, 4);
		ld_u32_store(dst + 4, (uint32_t)is->size - 16);
		ld_u32_store(dst + 8, 3);
		memcpy(dst + 12, "GNU", 4);
		break;
	default:
		break;
	}
}
