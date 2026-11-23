/* [dcl.link]: a linkage-specification's braces are no scope, and a
   declaration in them that is not parsed yet (_Float16) does not hide the
   others: struct timeval_like, defined after it, is complete outside (glibc's
   <bits/timex.h> has a member `struct timeval time;`).  The one error is
   the declaration kept as tokens. */
extern "C" {
typedef _Float16 half_t;
struct timeval_like {
    long sec;
    long usec;
};
}
struct holder {
    struct timeval_like time;
};
long f(holder &h)
{
    return h.time.sec + h.time.usec;
}
