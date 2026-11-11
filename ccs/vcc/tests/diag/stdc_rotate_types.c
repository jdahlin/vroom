/* diag-flags: -std=c23 */
unsigned bad_value(int x) { return __builtin_stdc_rotate_left(x, 1); }
unsigned bad_count(unsigned x) { return __builtin_stdc_rotate_right(x, 1.0); }
