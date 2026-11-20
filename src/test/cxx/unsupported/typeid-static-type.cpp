/* [expr.typeid]/3-4: typeid of a type-id or of an expression that is not a
   glvalue of polymorphic class type is an lvalue of type const
   std::type_info; analysis accepts it, there are no type_info objects to
   lower. */
/* cxx-unsupported: lowering */
namespace std {
class type_info {
public:
    const char *name_;
};
} // namespace std
struct S {
    int x;
};
const std::type_info &f(S s) { return typeid(s); }
const std::type_info &g() { return typeid(int &); }
