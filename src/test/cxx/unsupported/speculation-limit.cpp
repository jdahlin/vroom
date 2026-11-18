/* diag-vpp-flags: -fcxx-speculation-limit=3 */
/* A declaration that needs more tentative parses than the parser's limit
   (here lowered to 3) is refused at the token where the limit was passed,
   instead of the parse running on (an exponential grammar hangs). */
struct R {
    int a, b;
};
bool inside(const R &r, const R &o) { return r.a < o.b && r.b < o.a && (r.a < o.a); }
