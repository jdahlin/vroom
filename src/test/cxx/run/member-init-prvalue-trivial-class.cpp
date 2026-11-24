/* [class.copy.elision]/1.3: a member of a trivially copyable class with a
   user-provided constructor, initialized from a prvalue of its own type. */
struct node {
    int *next;
    int tag;
    node() : next(nullptr), tag(5) {}
};

struct holder {
    node before;
    int other;
    holder(int o) : before(node()), other(o) {}
};

template <class T>
struct tholder {
    node before;
    T other;
    tholder(T o) : before(node()), other(o) {}
};

int main()
{
    holder h(3);
    tholder<int> t(4);
    return (h.before.tag + h.other + t.before.tag + t.other) - 17;
}
