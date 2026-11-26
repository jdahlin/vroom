/* [temp.inst]/2: a non-const lvalue reference parameter does not need its
   referenced class complete: stream<unsigned char> must not be
   instantiated (traits<unsigned char> has no definition) */
template <class T> struct traits;
template <> struct traits<char> { typedef int int_type; };

template <class T> struct stream {
    typedef typename traits<T>::int_type int_type;
};

template <class T> struct adapter {
    adapter(stream<T> &) : n(1) {}
    adapter(int) : n(2) {}
    int n;
};

struct tag {};

int which(adapter<unsigned char>) { return 1; }
int which(tag) { return 2; }

int main()
{
    if (which(tag()) != 2)
        return 1;
    if (which(3) != 1)
        return 2;
    return 0;
}
