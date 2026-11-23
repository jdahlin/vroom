/* [temp.names]: typename type requirement naming a member through a template-id, "> ::" (atomic_waitable_traits.h:47) */
template <class T> struct decay_it { using type = T; };
template <class T> using decay_t = typename decay_it<T>::type;
template <class T> struct traits { using value_type = T; };
template <class T>
concept waitable = requires(const T t) {
    typename traits<decay_t<T> >::value_type;
};
int main() { return waitable<int> ? 0 : 1; }
