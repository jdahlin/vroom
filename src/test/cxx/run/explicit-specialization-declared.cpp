/* [temp.expl.spec]/1: an explicit specialization of a class template
   declared without a definition (libc++'s `template <> struct
   char_traits<char>;`), named while incomplete, declared again, then
   defined and used */
template <class T>
struct traits;
template <>
struct traits<char>;
template <>
struct traits<char>;
template <>
class traits<int>;

traits<char> *pointer = 0;

template <>
struct traits<char> {
    static int size() { return 1; }
};
template <>
class traits<int> {
  public:
    static int size() { return 4; }
};

template <class T>
struct traits {
    static int size() { return 99; }
};

int main()
{
    traits<char> c;
    (void)c;
    if (traits<char>::size() != 1 || traits<int>::size() != 4 ||
        traits<long>::size() != 99 || pointer)
        return 1;
    return 0;
}
