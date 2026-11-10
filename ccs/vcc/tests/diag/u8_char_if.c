/* diag-flags: -std=c23 */
/* C23 u8 character constants in #if */
#if u8'a' == 97 && u8'\xff' > 0
int ok;
#else
#error bad
#endif
#if u8 + 1
#error u8 is 0
#endif
