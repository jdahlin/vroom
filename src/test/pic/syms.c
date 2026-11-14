extern int ext; int def = 1; static int st; __attribute__((visibility("hidden"))) int hid; int tent;
extern int extw __attribute__((weak)); __attribute__((weak)) int defw = 2;
extern __attribute__((visibility("hidden"))) int exthid;
int f(void); static int sf(void) { return 1; } int gdef(void) { return 2; }
__attribute__((visibility("protected"))) int prot;
int use(void) { return ext + def + st + hid + tent + extw + defw + exthid + prot + f() + sf() + gdef(); }
void *addrs[] = { &ext, &def, f, gdef, sf, &st };
void *fa(void) { return (void*)f; } void *ga(void) { return (void*)gdef; } void *sa(void) { return (void*)sf; }
const char *s(void) { return "x"; }
