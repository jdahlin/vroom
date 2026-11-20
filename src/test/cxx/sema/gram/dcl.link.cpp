// [dcl.link]/1 and /4: the declarations of a linkage-specification's braces
// are the enclosing namespace's.  One of them kept as tokens (a GNU
// extension the parser does not read yet, here _Complex as in glibc's
// <stdlib.h>) leaves the others analysed and their names declared.
extern "C" {
typedef _Complex float cfloat;
struct timeval { long tv_sec; long tv_usec; };
extern int gettimeofday(struct timeval *tv, void *tz);
}
struct itimerval { struct timeval it_interval; struct timeval it_value; };

extern "C" {
typedef _Complex double cdouble;
namespace in_block { struct S { int x; }; }
}
in_block::S s = {1};

// nested blocks: the unread declaration is in the inner one
extern "C" {
extern "C" {
typedef _Complex long double cldouble;
struct inner { int a; };
}
struct outer { struct inner i; };
}
