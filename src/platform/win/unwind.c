/*
 * unwind.c: Windows x64 unwind information (.xdata, .pdata) from the .seh_* directives, byte for byte
 * as LLVM MC's Win64EH writes it for x86_64-w64-mingw32 (UNWIND_INFO version 1).
 *
 * The caller (the assembler for .seh_*, or a code generator directly) defines a temporary label where a
 * directive stands and passes it; a frame keeps its labels and unwind codes (CoffState in objimpl.h).
 * At .seh_endproc (or .seh_handlerdata) the frame's UNWIND_INFO goes to the end of its .xdata section:
 *
 *   align 4 | version 1 + handler flags << 3 | prologue size | code count | frame register + offset |
 *   the codes, last first, each (offset in the prologue, op | info << 4, 0-2 more 16-bit slots) |
 *   a 16-bit pad to an even count | the handler's RVA, or 4 zero bytes when there are no codes
 *
 * where every offset is a 1-byte label difference the layout resolves.  coff_finish calls
 * coff_unwind_finish, which writes a RUNTIME_FUNCTION (start, end, UNWIND_INFO: three ADDR32NB) per
 * frame to .pdata, in frame order.  The sections follow the function's: .xdata/.pdata for .text,
 * ".xdata$NAME"/".pdata$NAME" (COMDAT, select any, as MinGW has no associative COMDAT) for a COMDAT
 * ".text$NAME", and a .xdata/.pdata of their own for any other section.  Errors are MC's messages,
 * returned to the caller with nothing changed; epilogue directives are checked and otherwise ignored
 * (version 1 has no epilogue codes).  Chained unwind information and version 2 are not written.
 */
#include "../../obj/objimpl.h"

#define SEH_NO_FRAME ".seh_ directive must appear within an active frame"

static CoffSehFrame *coff_seh_frame(ElfObj *o)
{
	CoffState *c = o->coff;
	return c && c->current >= 0 ? &c->frames[c->current] : NULL;
}

static uint32_t coff_seh_section_of(ElfObj *o, uint32_t label)
{
	return elf_frag(o, elf_symp(o, label)->frag)->sec;
}

const char *coff_seh_check(ElfObj *o) { return coff_seh_frame(o) ? NULL : SEH_NO_FRAME; }
int coff_seh_open(ElfObj *o) { return coff_seh_frame(o) != NULL; }

const char *coff_seh_proc(ElfObj *o, uint32_t function_sym, uint32_t label)
{
	CoffState *c = coff_state(o);
	if (c->current >= 0)
		return "Starting a function before ending the previous one!";
	c->frames = coff_grow(o, c->frames, c->frame_count, &c->frame_capacity, sizeof *c->frames);
	CoffSehFrame *f = &c->frames[c->frame_count];
	memset(f, 0, sizeof *f);
	f->function = function_sym; f->begin = label; f->section = coff_seh_section_of(o, label);
	f->first = c->code_count; f->frame_code = -1;
	c->current = (int32_t)c->frame_count++;
	return NULL;
}

static void coff_seh_code(ElfObj *o, CoffSehFrame *f, int op, uint32_t label, unsigned reg, uint64_t offset)
{
	CoffState *c = o->coff;
	c->codes = coff_grow(o, c->codes, c->code_count, &c->code_capacity, sizeof *c->codes);
	c->codes[c->code_count++] = (CoffSehCode){ (uint8_t)op, (uint8_t)reg, label, offset };
	f->count++;
}

const char *coff_seh_push_reg(ElfObj *o, uint32_t label, unsigned reg)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	coff_seh_code(o, f, SEH_PUSH_NONVOL, label, reg, 0);
	return NULL;
}

const char *coff_seh_alloc(ElfObj *o, uint32_t label, uint64_t size)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (!size) return "stack allocation size must be non-zero";
	if (size & 7) return "stack allocation size is not a multiple of 8";
	coff_seh_code(o, f, size > 128 ? SEH_ALLOC_LARGE : SEH_ALLOC_SMALL, label, 0, size);
	return NULL;
}

const char *coff_seh_set_frame(ElfObj *o, uint32_t label, unsigned reg, uint64_t offset)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (f->frame_code >= 0) return "frame register and offset can be set at most once";
	if (offset & 15) return "offset is not a multiple of 16";
	if (offset > 240) return "frame offset must be less than or equal to 240";
	f->frame_code = (int32_t)f->count;
	coff_seh_code(o, f, SEH_SET_FPREG, label, reg, offset);
	return NULL;
}

const char *coff_seh_save_reg(ElfObj *o, uint32_t label, unsigned reg, uint64_t offset)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (offset & 7) return "register save offset is not 8 byte aligned";
	coff_seh_code(o, f, offset > 512 * 1024 - 8 ? SEH_SAVE_NONVOL_FAR : SEH_SAVE_NONVOL, label, reg, offset);
	return NULL;
}

const char *coff_seh_save_xmm(ElfObj *o, uint32_t label, unsigned reg, uint64_t offset)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (offset & 15) return "offset is not a multiple of 16";
	coff_seh_code(o, f, offset > 1024 * 1024 - 16 ? SEH_SAVE_XMM128_FAR : SEH_SAVE_XMM128, label, reg, offset);
	return NULL;
}

const char *coff_seh_push_frame(ElfObj *o, uint32_t label, int code)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (f->count) return "If present, PushMachFrame must be the first UOP";
	coff_seh_code(o, f, SEH_PUSH_MACHFRAME, label, 0, code ? 1 : 0);
	return NULL;
}

const char *coff_seh_end_prologue(ElfObj *o, uint32_t label)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	f->prolog_end = label;
	return NULL;
}

const char *coff_seh_epilogue(ElfObj *o, int start, uint32_t label)
{
	CoffSehFrame *f = coff_seh_frame(o);
	(void)label;
	if (!f) return SEH_NO_FRAME;
	ElfSym *fn = elf_symp(o, f->function);
	if (start) {
		f->in_epilogue = 1;	/* (also after the error, as MC) */
		if (!f->prolog_end) {
			snprintf(o->coff->message, sizeof o->coff->message,
				 "starting epilogue (.seh_startepilogue) before prologue has ended (.seh_endprologue) in %.*s",
				 (int)(fn->len > 200 ? 200 : fn->len), fn->name);
			return o->coff->message;
		}
		f->in_epilogue = 1;
		return NULL;
	}
	if (!f->in_epilogue) {
		snprintf(o->coff->message, sizeof o->coff->message, "Stray .seh_endepilogue in %.*s",
			 (int)(fn->len > 200 ? 200 : fn->len), fn->name);
		return o->coff->message;
	}
	f->in_epilogue = 0;
	return NULL;
}

const char *coff_seh_handler(ElfObj *o, uint32_t handler_sym, int unwind, int except)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	if (!unwind && !except) return "Don't know what kind of handler this is!";
	f->handler = handler_sym;
	f->handles_unwind |= (uint8_t)!!unwind;
	f->handles_except |= (uint8_t)!!except;
	return NULL;
}

/* the .xdata (pdata = 0) or .pdata section for a function in section text */
static uint32_t coff_seh_table_section(ElfObj *o, uint32_t text, int pdata)
{
	const char *base = pdata ? ".pdata" : ".xdata";
	uint32_t flags = COFF_SCN_INIT_DATA | COFF_SCN_READ;
	ElfSec *t = elf_sec(o, text);
	if (text == elf_find_section(o, ".text", 5, 0))
		return coff_section(o, base, 6, flags, 0, 0);
	if (t->flags & COFF_SCN_LNK_COMDAT) {
		const char *dollar = memchr(t->name, '$', t->len);
		uint32_t k = dollar ? (uint32_t)(t->len - (uint32_t)(dollar + 1 - t->name)) : 0;
		char *n = rpush(&o->arena, 8 + k, 1);
		memcpy(n, base, 6); n[6] = '$';
		if (k) memcpy(n + 7, dollar + 1, k);
		return coff_section(o, n, 7 + k, flags | COFF_SCN_LNK_COMDAT, 0, COFF_SELECT_ANY);
	}
	return elf_section_lo(o, base, 6, SHT_PROGBITS, flags, 0, 0, text, 0);	/* one per section, as MC's unique ID */
}

static void coff_seh_byte(ElfObj *o, uint32_t frag, unsigned v) { *elf_grow(o, frag, 1) = (uint8_t)v; }
static void coff_seh_u16(ElfObj *o, uint32_t frag, unsigned v) { uint8_t *p = elf_grow(o, frag, 2); p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* label - begin in one byte, resolved at layout */
static void coff_seh_offset(ElfObj *o, uint32_t frag, uint32_t label, uint32_t begin)
{
	uint64_t at = elf_frag(o, frag)->size;
	uint8_t *p = elf_grow(o, frag, 1);
	*p = 0;
	elf_fixup(o, frag, at, p, R_X86_64_8, (ElfVal){ label, begin, 0 });
}

static void coff_seh_rva(ElfObj *o, uint32_t frag, uint32_t sym)
{
	uint64_t at = elf_frag(o, frag)->size;
	uint8_t *p = elf_grow(o, frag, 4);
	memset(p, 0, 4);
	elf_fixup(o, frag, at, p, R_VCC_COFF_IMGREL32, (ElfVal){ sym, 0, 0 });
}

static unsigned coff_seh_slots(const CoffSehCode *k)
{
	switch (k->op) {
	case SEH_SAVE_NONVOL: case SEH_SAVE_XMM128: return 2;
	case SEH_SAVE_NONVOL_FAR: case SEH_SAVE_XMM128_FAR: return 3;
	case SEH_ALLOC_LARGE: return k->offset > 512 * 1024 - 8 ? 3 : 2;
	default: return 1;
	}
}

/* MC's EmitUnwindInfo: once per frame */
static void coff_seh_info(ElfObj *o, CoffSehFrame *f)
{
	if (f->info) return;
	uint32_t frag = coff_fragment_latest(o, coff_seh_table_section(o, f->section, 0));
	CoffSehCode *codes = o->coff->codes + f->first;
	elf_data(o, frag);
	elf_align(o, frag, 2, 0, 0, 0);
	f->info = coff_label_here(o, frag);
	unsigned flags = 1, count = 0;
	if (f->handles_unwind) flags |= 2 << 3;
	if (f->handles_except) flags |= 1 << 3;
	for (uint32_t i = 0; i < f->count; i++) count += coff_seh_slots(&codes[i]);
	coff_seh_byte(o, frag, flags);
	if (f->prolog_end) coff_seh_offset(o, frag, f->prolog_end, f->begin);
	else coff_seh_byte(o, frag, 0);
	coff_seh_byte(o, frag, count);
	coff_seh_byte(o, frag, f->frame_code >= 0 ? (codes[f->frame_code].reg & 15) | (codes[f->frame_code].offset & 0xf0) : 0);
	for (uint32_t i = f->count; i-- > 0;) {
		const CoffSehCode *k = &codes[i];
		unsigned b = k->op & 15;
		uint64_t off = k->offset;
		switch (k->op) {
		case SEH_PUSH_NONVOL:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b | (k->reg & 15) << 4);
			break;
		case SEH_ALLOC_LARGE:
			coff_seh_offset(o, frag, k->label, f->begin);
			if (off > 512 * 1024 - 8) {
				coff_seh_byte(o, frag, b | 0x10);
				coff_seh_u16(o, frag, (unsigned)(off & 0xfff8));
				coff_seh_u16(o, frag, (unsigned)(off >> 16));
			} else {
				coff_seh_byte(o, frag, b);
				coff_seh_u16(o, frag, (unsigned)(off >> 3));
			}
			break;
		case SEH_ALLOC_SMALL:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b | (unsigned)(((off - 8) >> 3) & 15) << 4);
			break;
		case SEH_SET_FPREG:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b);
			break;
		case SEH_SAVE_NONVOL: case SEH_SAVE_XMM128:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b | (k->reg & 15) << 4);
			coff_seh_u16(o, frag, (unsigned)((off >> 3) >> (k->op == SEH_SAVE_XMM128)));
			break;
		case SEH_SAVE_NONVOL_FAR: case SEH_SAVE_XMM128_FAR:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b | (k->reg & 15) << 4);
			coff_seh_u16(o, frag, (unsigned)(off & (k->op == SEH_SAVE_XMM128_FAR ? 0xfff0 : 0xfff8)));
			coff_seh_u16(o, frag, (unsigned)(off >> 16));
			break;
		case SEH_PUSH_MACHFRAME:
			coff_seh_offset(o, frag, k->label, f->begin);
			coff_seh_byte(o, frag, b | (off == 1 ? 0x10 : 0));
			break;
		}
	}
	if (count & 1) coff_seh_u16(o, frag, 0);
	if (f->handles_unwind || f->handles_except) coff_seh_rva(o, frag, f->handler);
	else if (!count) memset(elf_grow(o, frag, 4), 0, 4);
}

const char *coff_seh_handler_data(ElfObj *o, uint32_t *section)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	coff_seh_info(o, f);
	*section = elf_frag(o, elf_symp(o, f->info)->frag)->sec;
	return NULL;
}

const char *coff_seh_end(ElfObj *o, uint32_t label, uint32_t *section)
{
	CoffSehFrame *f = coff_seh_frame(o);
	if (!f) return SEH_NO_FRAME;
	f->end = label;
	o->coff->current = -1;
	coff_seh_info(o, f);
	*section = f->section;
	return NULL;
}

VPRIV void coff_unwind_finish(ElfObj *o)
{
	CoffState *c = o->coff;
	if (c->current >= 0) { elf_error(o, "Unfinished frame!"); return; }
	for (uint32_t i = 0; i < c->frame_count; i++) {
		CoffSehFrame *f = &c->frames[i];
		if (coff_seh_section_of(o, f->end) != f->section) {
			elf_error(o, "Cannot represent this expression (.seh_endproc in another section than .seh_proc)");
			return;
		}
		uint32_t frag = coff_fragment_latest(o, coff_seh_table_section(o, f->section, 1));
		elf_data(o, frag);
		elf_align(o, frag, 2, 0, 0, 0);
		coff_seh_rva(o, frag, f->begin);
		coff_seh_rva(o, frag, f->end);	/* begin@IMGREL + (end - begin): the same relocation */
		coff_seh_rva(o, frag, f->info);
	}
}
