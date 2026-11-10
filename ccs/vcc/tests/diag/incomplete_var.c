/* diag-flags: -std=gnu17 */
int m5(a) struct s { int q; } *a; { struct s z = *a; return z.q + sizeof(struct s); }
int n5(struct t { int r; } *p) { struct t z = *p; return z.r; }
struct q;
struct q gb[2];
extern struct q eb[];
void f5(void){ struct q z; }
void g5(void){ int a[]; union u w; struct q b[3]; }
void h5(void){ void *v[2]; int m[2][3]; }

int f6(struct q *p) { return p->x; }
int g6(void) { struct q z; return z.x; }
