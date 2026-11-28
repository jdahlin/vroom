/* [temp.over.link]/6, [temp.inst]: a function template's specialization named
   before the definition, which names the template parameters differently,
   uses the definition's names */
enum E { e0, e1 };
template <E C, int cb> static void unpack(int *k);
using P = void (*)(int *);
template <E C, int T> constexpr P make() { return &unpack<C, T>; }
static constexpr P p = make<e1, 2>();
static P q = &unpack<e0, 3>;
template <int N> int get() { return N; }
template <E Cmd, int Tcb> void unpack(int *k) { *k = Cmd * 100 + get<Tcb>(); }
int main()
{
    int k = 0, l = 0;
    p(&k);
    q(&l);
    return k != 102 || l != 3;
}
