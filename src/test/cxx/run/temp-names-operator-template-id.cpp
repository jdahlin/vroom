/* [temp.names]: an operator-function-id template-id */
struct S { template <class T> int operator()(T) const { return sizeof(T); } template <class T> int operator<(T) const { return 7; } };
int main() { S s; return s.operator()<char>('c') - 1 + s.operator< <int>(1) - 7; }
