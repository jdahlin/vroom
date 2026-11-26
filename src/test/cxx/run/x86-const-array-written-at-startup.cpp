/* a const array whose elements are not constant is written at start-up: not
   in read-only memory (its const is its elements') */
static int next = 3;
template <class T> int f() { alignas(16) static const T lanes[4] = {static_cast<T>(next), 2}; return lanes[0] + lanes[1]; }
int main() { return f<signed char>() != 5; }
