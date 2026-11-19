/* [temp.over.link]/7 and [dcl.inline]/5: a function template declared
   inline, then defined without inline, is one template (libc++'s
   std::copy); another template-head of the name is an overload */
namespace ns {
template <class I, class O>
inline O copy(I first, I last, O out);

template <class I, class O>
O copy(I first, I last, O out) {
    while (first != last)
        *out++ = *first++;
    return out;
}

template <class I, class S, class O>
O copy(I first, S n, O out, int bias) {
    for (S i = 0; i < n; i++)
        *out++ = first[i] + bias;
    return out;
}
} // namespace ns

int main() {
    int a[3] = {1, 2, 3}, b[3] = {0, 0, 0};
    int *e = ns::copy(a, a + 3, b);
    if (e != b + 3 || b[0] != 1 || b[2] != 3)
        return 1;
    ns::copy(a, 3L, b, 10);
    return b[0] == 11 && b[2] == 13 ? 0 : 2;
}
