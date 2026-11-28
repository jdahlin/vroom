/* [temp.inst]/12: a default argument that is ill-formed for a
   specialization's arguments matters only to a call that uses it: one
   that passes it takes the later parameters' defaults */
struct NoDefault {
    explicit NoDefault(int v) : v(v) {}
    int v;
};
template <class C> int run(C c = {}, int k = 5) { return c.v + k; }
int main() { return run(NoDefault(2)) - 7; }
