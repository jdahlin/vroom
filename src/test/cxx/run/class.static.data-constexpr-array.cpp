/* [class.static.data]/4, [dcl.constexpr]/10: a constexpr static data
   member of array type, initialized in its class: const (its elements,
   [basic.type.qualifier]/3), usable in constant expressions and read at
   run time with a variable index (Stockfish's HalfKAv2_hm tables,
   enumerators as elements). */
enum square { a1, b1, c1, square_nb };
struct tables {
    enum { none = 0, pawn = 1 * square_nb, king = 2 * square_nb };
    static constexpr unsigned index[2][3] = {{none, pawn, king},
                                             {king, pawn, none}};
    static constexpr int orient[square_nb] = {c1, b1, a1};
    static constexpr const char *name = "tables";
};
static_assert(tables::index[1][0] == 6, "a constant expression");
static_assert(tables::orient[0] == c1, "an enumerator element");
int pick(int c, int i) { return static_cast<int>(tables::index[c][i]); }
int main()
{
    volatile int c = 1, i = 2;
    if (pick(c, i) != 0 || pick(0, i) != 6)
        return 1;
    if (tables::orient[i] != a1)
        return 2;
    const unsigned *row = tables::index[c];
    if (row[1] != 3)
        return 3;
    if (tables::name[0] != 't')
        return 4;
    return 0;
}
