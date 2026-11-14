typedef unsigned vu __attribute__((vector_size(16)));
typedef unsigned char vuc __attribute__((vector_size(4)));
typedef unsigned long long vull __attribute__((vector_size(16)));
static vu add(vu v, int x) { return v+x; }
static vu or_left(int x, vu v) { return x|v; }
static vuc add_small(vuc v, signed char x) { return v+x; }
static vull add_large(vull v, long long x) { return v+x; }
int main(void) {
    vu v={0,1,2,3};
    vu a=add(v,-1), b=or_left(-2147483647-1,v);
    for(int i=0;i<4;i++) {
        if(a[i]!=(unsigned)i-1) return 1;
        if(b[i]!=(0x80000000u|(unsigned)i)) return 2;
    }
    v += 3 != 4;
    if(v[0]!=1 || v[3]!=4) return 3;
    vuc c=add_small((vuc){0,1,127,255},-1);
    if(c[0]!=255 || c[1]!=0 || c[2]!=126 || c[3]!=254) return 4;
    vull d=add_large((vull){0,1},-1);
    if(d[0]!=~0ULL || d[1]!=0) return 5;
    return 0;
}
