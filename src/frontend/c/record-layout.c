/* vcc front end: record layout (clang's ItaniumRecordLayoutBuilder, AArch64) */
#include "cc-internal.h"

/* ---------------- record layout (clang ItaniumRecordLayoutBuilder, AArch64) ---------------- */

static uint64_t cc_layout_align_to(uint64_t v, uint64_t a) { return a ? (v + a - 1) / a * a : v; }

typedef struct cc_record_layout {
	bool is_union, packed, ms_struct;
	uint64_t last_unit;	/* Microsoft bit-field storage-unit size, or zero */
	uint64_t max_field_align;	/* bits; #pragma pack */
	uint64_t data_size, size;	/* bits */
	uint64_t unfilled;		/* bits unfilled in the last bit-field unit */
	uint64_t align, unpacked_align;	/* bits */
	uint64_t padded_field_size;
} cc_record_layout;

static void cc_layout_update(cc_record_layout *l, uint64_t a, uint64_t ua)
{
	if (a > l->align)
		l->align = a;
	if (ua > l->unpacked_align)
		l->unpacked_align = ua;
}

/* field attrs: aligned (bytes), packed */
static void cc_layout_bitfield_place(cc *c, cc_record_layout *l, field *f, uint32_t fattr_align, bool fpacked)
{
	bool packed = l->packed || fpacked;
	uint64_t fsize = f->width;
	uint64_t unit = cc_type_size(c, f->ty) * 8;
	uint64_t falign = (uint64_t)cc_type_alignment(c, f->ty) * 8;
	uint64_t off, ufalign, explicit_align = (uint64_t)fattr_align * 8;
	uint64_t ufoff;
	uint64_t mfa = l->max_field_align;

	if (l->ms_struct) {
		falign = unit;
		if (l->last_unit != unit || l->unfilled < fsize) {
			if (!l->last_unit && !fsize)
				falign = 1; /* zero-width after a non-bitfield has no effect */
			l->unfilled = 0;
			l->last_unit = 0;
		}
	}
	off = l->is_union ? 0 : l->data_size - l->unfilled;
	/* AArch64: useBitFieldTypeAlignment and useZeroLengthBitfieldAlignment */
	ufalign = falign;
	if (packed && fsize != 0)
		falign = 1;	/* bits: a packed bit-field starts anywhere */
	if (explicit_align) {
		falign = falign > explicit_align ? falign : explicit_align;
		ufalign = ufalign > explicit_align ? ufalign : explicit_align;
	}
	if (mfa && fsize) {
		ufalign = ufalign < mfa ? ufalign : mfa;
		if (packed)
			falign = ufalign;
		else
			falign = falign < mfa ? falign : mfa;
	}
	if (l->ms_struct && l->is_union)
		falign = ufalign = 1;
	ufoff = off;
	if (l->ms_struct) {
		if (!fsize || fsize > l->unfilled) {
			off = cc_layout_align_to(off, falign);
			l->unfilled = 0;
		}
	} else {
		bool allow_pad = mfa == 0;

		bool exp = explicit_align && (mfa == 0 || explicit_align <= mfa);	/* useExplicitBitFieldAlignment */

		if (fsize == 0 || (allow_pad && (off & (falign - 1)) + fsize > unit))
			off = cc_layout_align_to(off, falign);
		else if (exp)
			off = cc_layout_align_to(off, explicit_align);
		if (fsize == 0 || (allow_pad && (ufoff & (ufalign - 1)) + fsize > unit))
			ufoff = cc_layout_align_to(ufoff, ufalign);
		else if (exp)
			ufoff = cc_layout_align_to(ufoff, explicit_align);
	}
	f->off = off;
	f->align = (uint32_t)((falign + 7) / 8);
	if (l->is_union) {
		uint64_t rs = l->ms_struct ? (fsize ? unit : 8) : cc_layout_align_to(fsize, 8);

		if (rs > l->data_size)
			l->data_size = rs;
	} else if (l->ms_struct && fsize) {
		if (!l->unfilled)
			l->data_size = off + unit, l->unfilled = unit;
		l->unfilled -= fsize;
		l->last_unit = unit;
	} else {
		uint64_t ns = off + fsize;

		l->last_unit = 0;

		l->data_size = cc_layout_align_to(ns, 8);
		l->unfilled = l->data_size - ns;
	}
	if (l->data_size > l->size)
		l->size = l->data_size;
	if (!l->ms_struct && c->o->target != PP_LINUX_AARCH64 && !f->name)
		falign = ufalign = 8;	/* Apple, wasm (no useZeroLengthBitfieldAlignment): unnamed bit-fields leave the record's alignment */
	cc_layout_update(l, falign, ufalign);
}

static void cc_layout_field_place(cc *c, cc_record_layout *l, field *f, uint32_t fattr_align, bool fpacked)
{
	uint64_t off, fsize, falign, ualign, palign = 8;
	bool packed;

	if (f->bitfield) {
		cc_layout_bitfield_place(c, l, f, fattr_align, fpacked);
		return;
	}
	off = l->is_union ? 0 : l->data_size;
	l->unfilled = 0;
	l->last_unit = 0;
	falign = (uint64_t)cc_type_alignment(c, f->ty) * 8;
	fsize = cc_type_is_array(c, f->ty) && (AST_TYPE(c, f->ty)->flags & TF_INCOMPLETE) ? 0 : cc_type_size(c, f->ty) * 8;
	packed = l->packed || fpacked;
	ualign = falign;
	if (fattr_align) {
		uint64_t m = (uint64_t)fattr_align * 8;

		palign = palign > m ? palign : m;
		ualign = ualign > m ? ualign : m;
	}
	if (l->max_field_align) {
		palign = palign < l->max_field_align ? palign : l->max_field_align;
		ualign = ualign < l->max_field_align ? ualign : l->max_field_align;
	}
	falign = packed ? palign : ualign;
	off = cc_layout_align_to(off, falign);
	f->off = off;
	f->align = (uint32_t)((falign + 7) / 8);
	if (l->is_union) {
		if (fsize > l->data_size)
			l->data_size = fsize;
	} else {
		l->data_size = off + fsize;
	}
	if (off + fsize > l->padded_field_size)
		l->padded_field_size = off + fsize;
	if (l->data_size > l->size)
		l->size = l->data_size;
	cc_layout_update(l, falign, ualign);
}

/* fa[i]/fp[i]: per-field aligned() and packed attributes */
CC_PRIVATE void cc_layout_record(cc *c, tag *r, const uint32_t *fa, const uint8_t *fp)
{
	cc_record_layout l = { .is_union = (r->flags & RF_UNION) != 0, .packed = (r->flags & RF_PACKED) != 0,
		     .ms_struct = (r->flags & RF_MS_STRUCT) != 0, .align = 8, .unpacked_align = 8 };

	if (c->npack && c->pack[c->npack - 1])
		l.max_field_align = (uint64_t)c->pack[c->npack - 1] * 8;
	for (uint32_t i = 0; i < r->nfield; i++)
		cc_layout_field_place(c, &l, &r->f[i], fa ? fa[i] : 0, fp ? fp[i] : 0);
	r->ualign = (uint32_t)(l.align / 8);
	if (r->maxalign)
		cc_layout_update(&l, (uint64_t)r->maxalign * 8, (uint64_t)r->maxalign * 8);
	if (l.padded_field_size > l.size)
		l.size = l.padded_field_size;
	l.size = cc_layout_align_to(l.size, l.align);
	r->size = l.size / 8;
	r->align = (uint32_t)(l.align / 8);
	c->ty[r->ty].size = r->size;
	c->ty[r->ty].align = r->align;
}
