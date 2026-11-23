// x++ / x-- of a class: operator++(int) as a member or not, the old value returned
struct I { int v; I &operator++() { ++v; return *this; } I operator++(int) { I o = *this; ++v; return o; } };
struct J { int v; };
J operator--(J &j, int) { J o = j; --j.v; return o; }
int main() { I i{1}; I a = i++; J j{5}; J b = j--; return a.v == 1 && i.v == 2 && b.v == 5 && j.v == 4 ? 0 : 1; }
