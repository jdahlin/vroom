/* [temp.names]/3: after a member name '<' is a less-than unless a
   template-argument-list and a call or '::' follow; a chain of comparisons
   of members (Skia's SkRegion::quickContains) must not parse each '<' as a
   template-argument-list speculatively: that was exponential */
struct R {
    int a, b, c, d;
};
static bool inside(const R &r, const R &o)
{
    return r.a < r.b && r.c < r.d && o.a < r.b && o.c < r.d && r.a < o.b &&
           r.c < o.d && o.a < o.b && o.c < o.d && o.b >= r.b && o.d >= r.d &&
           r.a <= o.a && r.c <= o.c;
}
int main()
{
    R r = {1, 5, 2, 6}, o = {0, 9, 0, 9};
    return !inside(r, o) + inside(o, r) * 2;
}
