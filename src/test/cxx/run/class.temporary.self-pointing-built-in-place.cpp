// [class.temporary], [class.copy.elision]/1: a trivially copyable class that
// points into itself is built where it is used: temporaries, range-for,
// returns (Stockfish MoveList).
struct list {
    int items[64], *last;
    list(int n) : last(items) { while (n--) *last++ = n; }
    const int *begin() const { return items; }
    const int *end() const { return last; }
    int size() const { return int(last - items); }
};
list make() { return list(3); }
int main() {
    int bad = 0, n = 0;
    for (const auto &m : list(3)) n += m + 1;
    if (n != 6) bad |= 1;
    if (list(3).size() != 3) bad |= 2;
    list a = list(3);
    if (a.size() != 3) bad |= 4;
    const list &r = list(3);
    if (r.size() != 3) bad |= 8;
    if (make().size() != 3) bad |= 16;
    list b = make();
    if (b.size() != 3) bad |= 32;
    return bad;
}
