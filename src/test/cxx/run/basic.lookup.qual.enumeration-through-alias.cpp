/* A template-id that names an enumeration as a nested-name-specifier: its
   enumerators, and no member type (a deduction failure: libc++'s
   __try_key_extraction_impl for a std::set of an enumeration). */
template <class T> struct identity {
    typedef T type;
};
template <class T> using identity_t = typename identity<T>::type;
enum class E { A = 3 };

template <class T> int pick(T, typename identity_t<T>::first_type * = nullptr)
{
    return 1;
}
template <class T> int pick(T, ...) { return 2; }

int main()
{
    return pick(E::A) != 2 || static_cast<int>(identity_t<E>::A) != 3;
}
