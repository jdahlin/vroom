/* [conv.ptr]: int* converts to no class pointer: a substitution failure */
template <class T> T &&make();
struct node {
    int v;
};
struct sink {
    void take(node *);
};
template <class P, class = void> struct takes {
    static const int value = 2;
};
template <class P>
struct takes<P, decltype(make<sink>().take(make<P>()))> {
    static const int value = 1;
};
int main() { return !(takes<node *>::value == 1 && takes<int *>::value == 2); }
