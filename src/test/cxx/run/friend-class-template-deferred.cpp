// [temp.friend]/1: a friend declaration naming a class template
// specialization does not instantiate the specialization before it is
// needed, and a friend class template declaration is accepted.
template <class T> class Peer;

template <class T> class Box {
    int secret = 19;
    friend class Peer<T>;
    template <class U> friend class Helper;
public:
    int get() const { return secret; }
};

template <class T> class Peer {
    Box<T> b;
public:
    int read() const { return b.secret; }
};

int main() {
    Peer<int> p;
    return p.read() != 19;
}
