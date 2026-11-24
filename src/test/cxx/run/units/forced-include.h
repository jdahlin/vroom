/* the file src/test/cxx/run/forced-include-names.cpp reads with -include */
namespace forced {
template <class T> struct identity {
    using type = T;
};
template <class T> using identity_t = typename identity<T>::type;
} // namespace forced
