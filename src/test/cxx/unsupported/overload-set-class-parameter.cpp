// [over.over]/1, [over.ics.user]: an overload set as the argument of a class parameter (a converting constructor takes a function pointer): not analysed
struct F { F(int (*)(int)) {} };
int g(int x) { return x; }
double g(double x) { return x; }
int take(F) { return 0; }
int main() { return take(g); }
