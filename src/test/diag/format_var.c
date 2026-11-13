/* -Wformat through const variables: insufficient/extra args, invalid specifier */
int printf(const char *, ...);
static const char a[] = "%d %d\n";
static const char *const p = "%y %d";
const char *q = "%d";
char w[] = "%d";
void f(long l){ printf(a, 1); printf(a, 1, 2, 3); printf(p, 1, 2); printf(q, l); printf(w, l); printf("%k %d", l);}
