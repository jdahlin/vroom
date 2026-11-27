/* [over.ics.list]/5: each element of an initializer_list parameter
   converts to its element type by itself: insert({a, b}) of aggregates is
   one value, not a list of two through brace elision. */
namespace std
{
template <class T> class initializer_list {
    const T *first;
    unsigned long count;
};
} // namespace std
struct point {
    int x;
};
struct upper {
    int u;
};
struct event {
    point where;
    upper type;
};
struct set {
    int insert(const event &) { return 1; }
    int insert(event &&) { return 2; }
    int insert(std::initializer_list<event>) { return 3; }
};
int main()
{
    set s;
    event e{{1}, {2}};
    if (s.insert({point{1}, upper{2}}) != 2)
        return 1;
    if (s.insert({point{1}, {3}}) != 2)
        return 2;
    if (s.insert({e, e}) != 3)
        return 3;
    if (s.insert({{{1}, {2}}, {{3}, {4}}}) != 3)
        return 4;
    return 0;
}
