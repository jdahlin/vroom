/* diag-vpp-flags: --lint */
/* diag-differs: --lint is vcc's own mode; clang has no such check */
/* The file is checked as if it were src/as/lint_naming.c: the module is read from the path. */
#line 1 "src/as/lint_naming.c"
int as_good(int x) { return x; }
static int helper(int x) { return x; }
int counter;
static int as_ok_table[4] = { 1, 2, 3, 4 };
extern int errno_like;
int prototype_only(int);
int __internal_name(int x) { return x; }
int main(void) { return as_good(helper(counter)) + as_ok_table[0]; }
