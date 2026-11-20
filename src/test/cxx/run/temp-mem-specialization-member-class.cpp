/* [temp.expl.spec]/1, [temp.mem.class]: an explicit specialization of a member class template, "template <> struct S::In<int>" */
struct S {
    template <class T> struct In { int v() { return 3; } };
};
template <> struct S::In<int> { int v() { return 30; } };
int main()
{
    return S::In<int>().v() - 30 + S::In<char>().v() - 3;
}
