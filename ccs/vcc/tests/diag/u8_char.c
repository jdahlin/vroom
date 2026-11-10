/* diag-flags: -std=c23 */
/* C23 u8 character constants: unsigned char, and u8 still an identifier */
_Static_assert(u8'a' == 97, "");
_Static_assert(_Generic(u8'a', unsigned char: 1, default: 0), "");
int u8 = 3;
int x = u8 + 1;
int y = u8'ab';
