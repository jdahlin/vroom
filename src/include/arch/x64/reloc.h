/*
 * reloc.h (x86-64): ELF relocation types for x86-64, the ones fixups carry when the object's machine
 * is EM_X86_64.
 *
 * Included by include/obj/obj.h, next to arm64's.  The values are the ELF numbers (the x86-64 psABI);
 * a type added here needs its encoding in arch/x64/obj/fixup.c and, for vlink, a case in the
 * relocation scan and apply (ld/).
 */
#ifndef VCC_X64_RELOC_H
#define VCC_X64_RELOC_H

enum {
	R_X86_64_NONE = 0, R_X86_64_64 = 1, R_X86_64_PC32 = 2, R_X86_64_GOT32 = 3, R_X86_64_PLT32 = 4,
	R_X86_64_GOTPCREL = 9, R_X86_64_32 = 10, R_X86_64_32S = 11, R_X86_64_16 = 12, R_X86_64_PC16 = 13,
	R_X86_64_8 = 14, R_X86_64_PC8 = 15, R_X86_64_DTPMOD64 = 16, R_X86_64_DTPOFF64 = 17,
	R_X86_64_TPOFF64 = 18, R_X86_64_TLSGD = 19, R_X86_64_TLSLD = 20, R_X86_64_DTPOFF32 = 21,
	R_X86_64_GOTTPOFF = 22, R_X86_64_TPOFF32 = 23, R_X86_64_PC64 = 24, R_X86_64_GOTOFF64 = 25,
	R_X86_64_GOTPC32 = 26, R_X86_64_SIZE32 = 32, R_X86_64_SIZE64 = 33, R_X86_64_GOTPCRELX = 41,
	R_X86_64_REX_GOTPCRELX = 42,
};

/* COFF-only fixup kinds (obj.h, "COFF"): no ELF number, never in an ELF object; above arm64's R_VCC_* */
enum {
	R_VCC_COFF_IMGREL32 = 0x10010,	/* sym@IMGREL, .rva: IMAGE_REL_AMD64_ADDR32NB */
	R_VCC_COFF_SECREL32 = 0x10011,	/* sym@SECREL32, .secrel32: IMAGE_REL_AMD64_SECREL */
	R_VCC_COFF_SECTION = 0x10012,	/* .secidx: IMAGE_REL_AMD64_SECTION, 2 bytes */
	R_VCC_COFF_SYMIDX = 0x10013,	/* .symidx: the symbol's index in the symbol table, 4 bytes */
};

#endif
