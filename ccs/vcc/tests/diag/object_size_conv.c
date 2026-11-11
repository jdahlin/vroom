/* -Wconstant-conversion: __builtin_object_size's (size_t)-1 into an int does not warn (clang; the kernel's ucopysize.h) */
int f(void *a) { int sz = __builtin_object_size(a, 0); return sz; }
