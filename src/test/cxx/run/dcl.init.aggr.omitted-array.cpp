// [dcl.init.aggr]/5.2: an omitted array element in an aggregate whose
// elements need construction (e.g. `std::array<T, 4> a{}` where `T` has a
// default constructor).

struct Item {
    int v;
    Item() : v(7) {}
};

struct ArrayWrap {
    Item elems[4];
};

struct Holder {
    int tag;
    ArrayWrap arr{};
};

int main()
{
    Holder h{1};
    if (h.tag != 1)
        return 1;
    for (int i = 0; i < 4; i++)
        if (h.arr.elems[i].v != 7)
            return 2 + i;
    ArrayWrap a{};
    for (int i = 0; i < 4; i++)
        if (a.elems[i].v != 7)
            return 10 + i;
    return 0;
}
