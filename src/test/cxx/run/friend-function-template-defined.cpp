// [temp.friend]/2 and [class.friend]/7: a friend function template
// defined inside a class uses the class scope and is found through ADL.
struct Box {
private:
    int value = 7;
    using number = int;
public:
    template <class T> friend int sum(const Box &box, T addend) {
        number n = box.value;
        return n + addend;
    }
};
int compare_sentinel();
int remote_call();
namespace visible { int check(); }
int main() {
    Box box;
    return sum(box, 3) != 10 || sum(box, 5L) != 12 || !compare_sentinel() || !visible::check() || remote_call() != 9;
}

struct Sentinel {
    template <class T> friend bool operator==(Sentinel, const T &value) {
        return value == 17;
    }
};
int compare_sentinel() { return Sentinel{} == 17; }

namespace visible {
struct Item;
template <class T> int value(const Item &, T);
struct Item {
private:
    int n = 21;
    template <class T> friend int value(const Item &i, T add) {
        return i.n + add;
    }
};
int check() { Item item; return value(item, 1) == 22; }
}
namespace remote {
struct Item { template<class T> friend int call(Item, T v) { return v + 1; } };
}
int remote_call() { return call(remote::Item{}, 8); }
