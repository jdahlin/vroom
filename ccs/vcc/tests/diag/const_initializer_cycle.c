/* Recursive initializers must be rejected without corrupting their ASTs. */
const int self = self;
extern const int second;
const int first = second;
const int second = first;
int values[] = { self, first, second };
const int good = 3;
int folded[] = { good, good + 1 };
