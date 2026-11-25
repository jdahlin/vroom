/* [dcl.fct.default]/6: "Except for member functions of class templates,
   the default arguments in a member function definition that appears
   outside of the class definition are added to the set of default
   arguments provided by the member function declaration in the class
   definition".  Stockfish's Position::do_move(Move, StateInfo&,
   const TranspositionTable* tt = nullptr), an overload of another
   do_move, and a static member named in a default argument. */
struct position {
    static constexpr int base = 100;
    int moves = 0;
    int do_move(int m, int &st, const int *tt);
    int do_move(int m, int &st, bool check, const int *tt);
    int see(int m, int threshold = 0) const;
    int scaled(int v, int by);
};
inline int position::do_move(int m, int &st, bool check, const int *tt)
{
    moves++;
    st += m;
    if (check)
        st += 1;
    if (tt)
        st += *tt;
    return st;
}
inline int position::do_move(int m, int &st, const int *tt = nullptr)
{
    return do_move(m, st, true, tt);
}
int position::see(int m, int threshold) const { return m - threshold; }
/* both trailing parameters given defaults here, one named from the class */
int position::scaled(int v, int by = base) { return v * by; }
int main()
{
    position p;
    int st = 0;
    if (p.do_move(2, st) != 3)
        return 1;
    int tt = 10;
    if (p.do_move(2, st, &tt) != 16 || p.moves != 2)
        return 2;
    if (p.see(5) != 5 || p.see(5, 2) != 3)
        return 3;
    if (p.scaled(3) != 300 || p.scaled(3, 2) != 6)
        return 4;
    return 0;
}
