/* [temp.expl.spec]/7: a base that is an explicit specialization has its
   own members (typedefs the primary template lacks); [temp.explicit]/3:
   two attributes after the class-key of an explicit instantiation. */
template <class T> struct ctype;
template <> struct ctype<char> {
    typedef char char_type;
    char_type value() const { return 'a'; }
};
template <class T> struct byname;
template <> struct byname<char> : public ctype<char> {
    char_type twice(char_type c) const { return c; }
};

template <class T> struct box {
    T v;
};
extern template struct __attribute__((__deprecated__))
__attribute__((__visibility__("default"))) box<int>;
template struct box<int>;

int main()
{
    byname<char> b;
    if (b.twice(b.value()) != 'a')
        return 1;
    box<int> x = {3};
    return x.v - 3;
}
