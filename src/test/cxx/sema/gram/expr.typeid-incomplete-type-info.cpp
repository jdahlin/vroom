// [expr.typeid]/1: typeid with std::type_info declared but incomplete is an
// lvalue of that type (clang accepts it)
namespace std { class type_info; }
void j() { (void)typeid(int); }
const std::type_info &k() { return typeid(double *); }
