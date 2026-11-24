// [stmt.label]/1: an attribute-specifier-seq before a label; ones that change
// nothing vcc generates and ones clang does not know are passed over.
int f(int a){ switch(a){ [[likely]] case 1: a++; [[foo]] default: break; } [[unknown]] l: return a; }
