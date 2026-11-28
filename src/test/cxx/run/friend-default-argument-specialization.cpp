/* a friend naming ref<set> chooses no pattern for ref, but the partial
   specialization its default argument needs is chosen (std::bitset) */
template <class T> struct has_word {
    static constexpr bool value = false;
};
template <class T, bool = has_word<T>::value> struct ref;
template <int N> struct set;
template <int N> struct has_word<set<N>> {
    static constexpr bool value = true;
};
template <int N> struct set {
    friend struct ref<set>;
    int word = N;
    ref<set> bit();
};
template <class T> struct ref<T, true> {
    T *s;
    int get()
    {
        return s->word;
    }
};
template <int N> ref<set<N>> set<N>::bit()
{
    return ref<set>{this};
}
int main()
{
    set<7> s;
    return s.bit().get() != 7;
}
