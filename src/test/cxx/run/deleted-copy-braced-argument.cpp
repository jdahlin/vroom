/* [over.best.ics]/4: a braced argument of a class whose implicit copy
   constructor is deleted goes to its other constructor (Highway's
   state_{{n, m}}) */
struct Array {
    long a, b;
};
class Move
{
  public:
    Move(Array s) : v(s.a * 10 + s.b) {}
    Move(Move &&) = default;
    long v;
};
struct Holder {
    Holder(long n) : state{{n, 2}} {}
    Move state;
};
int main() { return Holder(4).state.v != 42; }
