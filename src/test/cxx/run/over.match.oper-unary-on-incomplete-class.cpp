/* [over.match.oper]/3.1: "If T1 is a complete class type or a class
   currently being defined, the set of member candidates is the result of
   the qualified lookup of T1::operator@ ...; otherwise, the set of member
   candidates is empty."  &o on an incomplete class finds no candidate, and
   the built-in address-of applies ([expr.unary.op]/3) (Stockfish's tune.h:
   options = &o, OptionsMap only declared). */
class incomplete;
incomplete *saved;
void keep(incomplete &o) { saved = &o; }
bool same(incomplete &a, incomplete *b) { return &a == b; }
int main()
{
    incomplete *p = reinterpret_cast<incomplete *>(&saved);
    keep(*p);
    if (!same(*p, saved))
        return 1;
    return 0;
}
