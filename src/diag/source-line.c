/* Display width of source text in terminal columns. */
#include "../frontend/pp/pp-internal.h"
#include "common/buffer.h"
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* the display width of code point c: 2 for East Asian wide and emoji, 0 for combining marks */
static uint32_t diag_codepoint_width(uint32_t c)
{
	if ((c >= 0x300 && c <= 0x36f) || (c >= 0x200b && c <= 0x200f) || (c >= 0xfe00 && c <= 0xfe0f))
		return 0;
	if ((c >= 0x1100 && c <= 0x115f) || (c >= 0x2e80 && c <= 0xa4cf && c != 0x303f) || (c >= 0xac00 && c <= 0xd7a3) ||
	    (c >= 0xf900 && c <= 0xfaff) || (c >= 0xfe30 && c <= 0xfe4f) || (c >= 0xff00 && c <= 0xff60) ||
	    (c >= 0xffe0 && c <= 0xffe6) || (c >= 0x1f300 && c <= 0x1f64f) || (c >= 0x1f900 && c <= 0x1f9ff) ||
	    (c >= 0x20000 && c <= 0x3fffd))
		return 2;
	return 1;
}

/* display columns of src[b, e) from col: tabs to the next multiple of 8, UTF-8 by its
   code point's width (a malformed byte is one column) */
VPRIV uint32_t diag_columns_width(const char *s, uint32_t b, uint32_t e, uint32_t col)
{
	for (uint32_t i = b; i < e; i++) {
		unsigned char ch = (unsigned char)s[i];

		if (ch == '\t') {
			col = (col / 8 + 1) * 8;
		} else if (ch < 0xc0) {
			col += (ch & 0xc0) != 0x80;
		} else {
			int n = ch >= 0xf0 ? 3 : ch >= 0xe0 ? 2 : 1;
			uint32_t c = ch & (0x3f >> n);
			int k;

			for (k = 0; k < n && i + 1 + k < e && ((unsigned char)s[i + 1 + k] & 0xc0) == 0x80; k++)
				c = c << 6 | ((unsigned char)s[i + 1 + k] & 0x3f);
			col += k == n ? diag_codepoint_width(c) : 1;
			i += (uint32_t)k;
		}
	}
	return col;
}
