/* [over.match.class.deduct]/1.1: an implicit guide of a constructor whose
   parameter names a member typedef of a template parameter
   (std::lock_guard's mutex_type) */
template <class M> struct G {
    typedef M mutex_type;
    explicit G(mutex_type &m) : r(m) {}
    mutex_type &r;
};
int main()
{
    int m = 3;
    G g(m);
    return g.r - 3;
}
