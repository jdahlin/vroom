/*
 * Minimal ELF64 definitions for vld/vobjcopy/vnm (macOS has no <elf.h>).
 * Readers only; the assembler's src/elf.h is separate on purpose.
 */
#ifndef VCC_LDELF_H
#define VCC_LDELF_H
#include <stdint.h>

typedef struct {
	uint8_t e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} elf_file_header;
typedef struct {
	uint32_t sh_name, sh_type;
	uint64_t sh_flags, sh_addr, sh_offset, sh_size;
	uint32_t sh_link, sh_info;
	uint64_t sh_addralign, sh_entsize;
} elf_section_header;
typedef struct {
	uint32_t st_name;
	uint8_t st_info, st_other;
	uint16_t st_shndx;
	uint64_t st_value, st_size;
} elf_symbol;
typedef struct {
	uint64_t r_offset, r_info;
	int64_t r_addend;
} elf_rela;
typedef struct {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} elf_program_header;

enum {
	SHT_NULL = 0, SHT_PROGBITS = 1, SHT_SYMTAB = 2, SHT_STRTAB = 3, SHT_RELA = 4,
	SHT_HASH = 5, SHT_DYNAMIC = 6, SHT_NOTE = 7, SHT_NOBITS = 8, SHT_REL = 9,
	SHT_DYNSYM = 11, SHT_INIT_ARRAY = 14, SHT_FINI_ARRAY = 15,
	SHT_PREINIT_ARRAY = 16, SHT_GROUP = 17, SHT_SYMTAB_SHNDX = 18,
	SHT_LLVM_ADDRSIG = 0x6fff4c03,
};
enum {
	SHF_WRITE = 1, SHF_ALLOC = 2, SHF_EXECINSTR = 4, SHF_MERGE = 0x10,
	SHF_STRINGS = 0x20, SHF_INFO_LINK = 0x40, SHF_GROUP = 0x200, SHF_TLS = 0x400,
	SHF_EXCLUDE = 0x80000000u,
};
enum { STB_LOCAL = 0, STB_GLOBAL = 1, STB_WEAK = 2 };
enum { STT_NOTYPE = 0, STT_OBJECT = 1, STT_FUNC = 2, STT_SECTION = 3, STT_FILE = 4, STT_TLS = 6 };
enum { STV_DEFAULT = 0, STV_INTERNAL = 1, STV_HIDDEN = 2, STV_PROTECTED = 3 };
enum { SHN_UNDEF = 0, SHN_ABS = 0xfff1, SHN_COMMON = 0xfff2, SHN_XINDEX = 0xffff };
enum { PT_LOAD = 1, PT_DYNAMIC = 2, PT_NOTE = 4, PT_PHDR = 6, PT_TLS = 7, PT_GNU_STACK = 0x6474e551, PT_GNU_RELRO = 0x6474e552, PT_GNU_EH_FRAME = 0x6474e550 };
enum { PF_X = 1, PF_W = 2, PF_R = 4 };
enum {
	R_AARCH64_NONE = 0, R_AARCH64_ABS64 = 257, R_AARCH64_ABS32 = 258,
	R_AARCH64_ABS16 = 259, R_AARCH64_PREL64 = 260, R_AARCH64_PREL32 = 261,
	R_AARCH64_PREL16 = 262, R_AARCH64_LD_PREL_LO19 = 273,
	R_AARCH64_ADR_PREL_LO21 = 274, R_AARCH64_ADR_PREL_PG_HI21 = 275,
	R_AARCH64_ADD_ABS_LO12_NC = 277, R_AARCH64_LDST8_ABS_LO12_NC = 278,
	R_AARCH64_JUMP26 = 282, R_AARCH64_CALL26 = 283,
	R_AARCH64_LDST16_ABS_LO12_NC = 284, R_AARCH64_LDST32_ABS_LO12_NC = 285,
	R_AARCH64_LDST64_ABS_LO12_NC = 286, R_AARCH64_LDST128_ABS_LO12_NC = 299,
	R_AARCH64_ADR_GOT_PAGE = 311, R_AARCH64_LD64_GOT_LO12_NC = 312,
	R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21 = 541, R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC = 542,
	R_AARCH64_TLSLE_ADD_TPREL_HI12 = 549, R_AARCH64_TLSLE_ADD_TPREL_LO12 = 550,
	R_AARCH64_TLSLE_ADD_TPREL_LO12_NC = 551, R_AARCH64_TLSDESC_ADR_PAGE21 = 562,
	R_AARCH64_TLSDESC_LD64_LO12 = 563, R_AARCH64_TLSDESC_ADD_LO12 = 564, R_AARCH64_TLSDESC_CALL = 569,
	R_AARCH64_RELATIVE = 1027,
};

#endif
