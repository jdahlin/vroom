/* [temp.friend]/3: a friend declaration naming a class template
   specialization by class-key and simple-template-id, the specialization
   declared before and defined after the befriending class
   (libc++'s __thread_id befriends hash<__thread_id>) */
namespace N {
template <class T> struct hash;
template <class T> struct Wrap;

class Id {
    int value;
    friend struct hash<Id>;
    friend class Wrap<int>;

  public:
    Id(int v) : value(v) {}
};

template <> struct hash<Id> {
    int operator()(const Id &i) const { return i.value * 3; }
};

template <class T> struct Wrap {
    int get(const Id &i) const { return i.value + 1; }
};
} // namespace N

int main()
{
    N::Id id(7);
    N::hash<N::Id> h;
    N::Wrap<int> w;
    if (h(id) != 21)
        return 1;
    if (w.get(id) != 8)
        return 2;
    return 0;
}
