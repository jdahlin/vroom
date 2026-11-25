/* [class.prop]/2, [dcl.attr.nouniqueaddr], [impl.clang]: a class whose only
   non-static data members are [[no_unique_address]] members of empty class
   type is empty for __is_empty (clang's CXXRecordDecl::isEmpty), and as a
   base it takes no bytes.  libc++'s __map_value_compare<K, V, less<K>>
   (_LIBCPP_COMPRESSED_ELEMENT): __as_transparent static_asserts
   is_empty<_Comparator> for std::map<int, int>::operator[] (Stockfish's
   numa.h). */
struct none {};
struct holder {
    [[no_unique_address]] none comp;
};
template <class T> struct compressed {
    [[no_unique_address]] T comp;
    [[no_unique_address]] none padding;
};
static_assert(__is_empty(holder), "one empty member");
static_assert(__is_empty(compressed<none>), "a template's");
static_assert(!__is_empty(compressed<int>), "an int member");
struct derived : holder {
    int value;
};
static_assert(sizeof(derived) == sizeof(int), "an empty base");
int read(const derived &d) { return d.value; }
int main()
{
    derived d;
    d.value = 4;
    if (read(d) != 4)
        return 1;
    if (sizeof(holder) != 1)
        return 2;
    return 0;
}
