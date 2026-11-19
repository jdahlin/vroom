// c ? "true" : "false": arrays of different lengths, their pointers (llvm-test-suite Shootout methcall, objinst)
#include <string.h>
const char *f(bool v) { return v ? "true" : "false"; }
int g(bool v) { char a[3] = "ab"; const char *p = v ? a : "xyz"; return (int)strlen(p); }
int main() { return !strcmp(f(true), "true") && !strcmp(f(false), "false") && g(true) == 2 && g(false) == 3 ? 0 : 1; }
