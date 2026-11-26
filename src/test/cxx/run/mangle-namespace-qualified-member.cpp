/* [abi:expressions]: a member of a dependent specialization named through a
   namespace (sr <namespace> <class><args> E <name>), the namespace as
   written, not its inline namespace */

namespace traits {
inline namespace v1 {
template <class T> struct is_small {
    static constexpr bool value = sizeof(T) == 1;
};
} // namespace v1
} // namespace traits

template <bool B, class T = void> struct enable_if {
};
template <class T> struct enable_if<true, T> {
    using type = T;
};

template <class T> struct serializer {
    template <class U = T,
              typename enable_if<(traits::is_small<U>::value &&
                                  traits::is_small<U>::value),
                                 int>::type = 0>
    static int pick(const T &v)
    {
        return v + 1;
    }

    template <class U = T,
              typename enable_if<!(traits::is_small<U>::value &&
                                   traits::is_small<U>::value),
                                 int>::type = 0>
    static int pick(const T &v)
    {
        return static_cast<int>(v) + 2;
    }
};

int main()
{
    return serializer<char>::pick('a') + serializer<long>::pick(2) == 'a' + 5
               ? 0
               : 1;
}
