// [except.throw] Example 1, part 3 (paragraph 1), docs/cxx-spec/except/except.throw.md
// needs: P6
// status: well-formed (as far as the example says)

class Overflow {
public:
    Overflow(char,double,double);
};

void f(double x) {
    throw Overflow('+',x,3.45e107);
}
