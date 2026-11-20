/* [temp.expl.spec]/16, [temp.mem]/1: explicit specializations of a member function template of a class template, with "template <> template <>" and "template <> template <class T>" */
template <class P> struct H {
    template <class T> int h(T) { return 4; }
};
template <> template <> int H<int>::h<int>(int) { return 40; }
template <> template <class T> int H<char>::h(T) { return 50; }
int main()
{
    H<int> hi;
    H<char> hc;
    H<long> hl;
    return hi.h(1) - 40 + hi.h('a') - 4 + hc.h(1) - 50 + hl.h(1) - 4;
}
