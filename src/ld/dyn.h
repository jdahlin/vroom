#ifndef VCC_LD_DYN_H
#define VCC_LD_DYN_H
#define NRESERVED 18
typedef struct { uint32_t dso, name, hash; uint16_t other; } NeedVer;
/* Per-link x86-64 executable dynamic sections and import bookkeeping. */
struct ld_dynamic_executable {
	bool dynexe, deflayout;
	uint64_t image_base;
	uint32_t def_nph;
	uint32_t interp_isec, plt_isec, gotplt_isec, relaplt_isec, verneed_isec, copy_isec;
	VVEC(uint32_t) plts, xdsyms; /* PLT order and .dynsym order */
	uint32_t xsymndx, xgh_nb, xgh_mw;
	VVEC(char) xstr; /* .dynstr bytes */
	uint32_t *xname, *xhash;
	uint16_t *xver;
	VVEC(NeedVer) needvers;
	VVEC(uint32_t) needed;
	uint32_t nverneed, nxdyn;
	uint8_t *xexport;
	uint32_t reserved_g[NRESERVED], dl_prev; /* linker symbols and layout predecessor */
};
#endif
