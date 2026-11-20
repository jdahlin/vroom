// [temp.param]/12, [temp.over.link]: a class template defined with other
// parameter names than the declaration that gave the defaults
template <class T, class A = T*, int N = sizeof(T)> struct S;

template <class U, class B, int M> struct S {
    static int size() { return M; }
    static int ptr() { return sizeof(B); }
    U *u;
};

template <class V, class W = V> struct R;
template <class X, class Y> struct R { static int f() { return sizeof(X) * 10 + sizeof(Y); } };

int main() {
    if (S<char>::size() != 1 || S<long>::size() != 8)
        return 1;
    if (S<char, short>::ptr() != 2 || S<char>::ptr() != sizeof(char *))
        return 2;
    if (R<int>::f() != 44 || R<char, long>::f() != 18)
        return 3;
    return 0;
}
