// A member type of a template's dependent member alias, named through a
// specialization outside any template: no typename, a type ([temp.dep]/1).
template <typename T> struct Iter {
    struct Item {
        int v;
    };
};

template <typename T> struct List {
    using CRIter = Iter<T>;
};

struct Outer {
    struct Raw {
        using Stack = List<Raw>;
    };
    class It;
};

class Outer::It
{
  public:
    It(Raw::Stack::CRIter::Item item, int r) : f(item.v + r) {}
    int f;
};

static int get(List<int>::CRIter::Item item, int r) { return item.v + r; }

int main()
{
    Outer::It i({1}, 2);
    return i.f + get({3}, 4) - 10;
}
