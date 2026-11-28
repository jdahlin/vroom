// [temp.res]: a typedef of typename X<V> names the members of X too, so the
// statements below are declarations
namespace n
{
template <class T> struct List {
    struct iterator {
        int v;
    };
    iterator it;
};
} // namespace n
template <class V> class Multi {
  public:
    typedef typename n::List<V>::iterator Member;
    typedef typename n::List<V> Items;
    Items items;
};
typedef Multi<int *> Packets;
struct S {
    Packets packets;
    int get();
};
int S::get()
{
    Packets::Items &list = packets.items;
    list.it.v = 5;
    Packets::Items::iterator it{list.it};
    return it.v;
}
int main()
{
    S s{};
    return s.get() - 5;
}
