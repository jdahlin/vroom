// [dcl.fct.def.delete]/2: a deleted move constructor participates in
// overload resolution, and is diagnosed when selected.
struct Move {
    Move();
    Move(const Move &);
    Move(Move &&) = delete;
};
Move make() { return Move(); }
Move bad_return() { Move x; return x; }
void bad_direct() { Move x; Move y(static_cast<Move &&>(x)); }
void copy() { Move x; Move y(x); }
