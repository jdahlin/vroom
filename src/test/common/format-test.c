/* Unit test of common/format.h against snprintf, and the hex digit values. */
#include <string.h>
#include "check.h"
#include "common/format.h"

int main(void)
{
	char b[32], c[32];
	uint64_t vals[] = { 0, 1, 9, 10, 99, 100, 255, 256, 65535, 4294967295u, 4294967296ull, 0x7fffffffffffffffull,
			    0x8000000000000000ull, UINT64_MAX, 12345678901234567890ull };

	for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
		size_t n = vformat_unsigned(b, vals[i]);

		snprintf(c, sizeof c, "%llu", (unsigned long long)vals[i]);
		CHECK(n == strlen(c) && !memcmp(b, c, n));
		n = vformat_hex(b, vals[i]);
		snprintf(c, sizeof c, "%llx", (unsigned long long)vals[i]);
		CHECK(n == strlen(c) && !memcmp(b, c, n));
	}
	for (int ch = 0; ch < 256; ch++) {
		int want = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
			 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;

		CHECK(vhex_digit_value(ch) == want);
	}
	return check_failures;
}
