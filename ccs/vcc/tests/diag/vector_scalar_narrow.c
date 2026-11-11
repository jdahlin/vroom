typedef int vi __attribute__((vector_size(16)));
typedef unsigned vu __attribute__((vector_size(16)));
vi bad_unsigned(vi v, unsigned x) { return v+x; }
vu bad_wide(vu v, unsigned long long x) { return v+x; }
