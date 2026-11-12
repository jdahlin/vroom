/* VIR parser/verifier fuzzer: verified units must survive a stable round trip. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../vcc/vir/vir.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	vir_unit u, v;
	vir_perr err;
	vbuf diagnostics = {0}, first = {0}, second = {0};
	uint32_t errors = 0;

	if (size > 65536) return 0;
	vir_unit_init(&u);
	if (!vir_parse(&u, (const char *)data, size, &err)) goto done;
	for (uint32_t i = 0; i < u.nfn; i++) {
		vir_fn f;
		vir_fn_load(&f, &u, i);
		errors += vir_verify(&f, &u, &diagnostics, 20);
	}
	if (errors) goto done;
	vir_print_unit(&first, &u);
	vir_unit_init(&v);
	if (!vir_parse(&v, first.p, first.n, &err)) abort();
	vir_print_unit(&second, &v);
	vir_unit_free(&v);
	if (first.n != second.n || (first.n && memcmp(first.p, second.p, first.n))) abort();
done:
	vir_unit_free(&u);
	free(diagnostics.p);
	free(first.p);
	free(second.p);
	return 0;
}
