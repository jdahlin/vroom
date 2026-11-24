// [temp.inst]/2: a specialization named by a pointer or a reference
// parameter while its argument J is being defined needs no complete class:
// P<int, J> would have a member of the incomplete type J.
template <class A, class B> struct P {
    A first;
    B second;
};
struct J {
    P<int, J> *link;
    static int read(const P<int, J> &p);
    int value;
};
int J::read(const P<int, J> &p) { return p.first + p.second.value; }
int main()
{
    J j;
    j.link = 0;
    j.value = 5;
    P<int, J> p;
    p.first = 1;
    p.second = j;
    if (J::read(p) != 6)
        return 1;
    return 0;
}
