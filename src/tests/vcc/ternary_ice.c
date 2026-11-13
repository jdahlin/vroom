/* a ?: whose condition the front end folds (the kernel's _IOC_TYPECHECK): the other arm, an undefined extern, is not referenced */
extern unsigned int bad;
unsigned f(unsigned cmd) { cmd = (cmd == (2U | (((sizeof(int) == sizeof(int[1]) && sizeof(int) < 16384) ? sizeof(int) : bad) << 16))) ? 5 : 6; return cmd; }
int main(void) { return f(2U | (4u << 16)) != 5; }
