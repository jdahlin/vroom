/* diag-flags: -Wall */
struct P { int a, b; };
void use(struct P);
struct P r1(void) { struct P p; return p; }
int r2(void) { struct P p; return p.a; }
void r3(void) { struct P p; use(p); }
struct P r4(void) { struct P p; p.a = 1; return p; }
struct P r5(void) { struct P p; struct P q = p; return q; }
void r6(void) { struct P p, *pp = &p; use(*pp); }
union U { int i; float f; }; union U r7(void) { union U u; return u; }
struct P r8(int c) { struct P p; if (c) p = (struct P){1, 2}; return p; }
