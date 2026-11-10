/* the note for an unnamed parameter: where its name would be; the parameter type without top-level qualifiers */
void a(int);
void b(int *);
void c2(int (*)(void));
void d(int []);
void e(const int * const);
void t(void){ int *p=0; long l=0; a(p); b(l); c2(l); d(l); e(l); }
