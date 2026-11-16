// double d = c; int i = c; of a class with only a conversion function template: its instances (json's
// number_float_t d{j}); the second one does not go through the first's instance forever
struct C { template <class T> operator T() const { return T(2.5); } };
int main() { C c; double d = c; int i = c; return d == 2.5 && i == 2 ? 0 : 1; }
