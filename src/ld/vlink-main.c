/* main() of the standalone tools (VLD_MAIN): vlink, vobjcopy, vnm, wasm-ld by argv[0]. */

#ifdef VLD_MAIN
#ifdef VCC_WASM
int wasm_ld_main(int argc, char **argv);
#endif

int main(int argc, char **argv)
{
	const char *b = strrchr(argv[0], '/');
	b = b ? b + 1 : argv[0];
	/* WebAssembly (wasmld.c): as wasm-ld, or for -mwasm64 */
	int is_wasm = strstr(b, "wasm-ld") != NULL;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-mwasm64"))
			is_wasm = 1;
	if (is_wasm) {
#ifdef VCC_WASM
		return wasm_ld_main(argc, argv);
#else
		fprintf(stderr, "vcc: error: this build has no WebAssembly support (make VCC_WASM=1)\n");
		return 1;
#endif
	}
	if (strstr(b, "objcopy"))
		return objcopy_main(argc, argv, AT_FDCWD, stderr);
	if (strstr(b, "nm") && !strstr(b, "link"))
		return nm_main(argc, argv, AT_FDCWD, stdout, stderr);
	/* VLD_REPEAT=N: link N times in one process (for profilers) */
	const char *rep = getenv("VLD_REPEAT");
	for (int i = rep ? atoi(rep) : 1; i > 1; i--)
		if (ld_main(argc, argv, AT_FDCWD, stderr))
			return 1;
	ld_keep_mappings = true;
	return ld_main(argc, argv, AT_FDCWD, stderr);
}
#endif
