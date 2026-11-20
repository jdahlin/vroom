/* [expr.typeid]/2: typeid of a glvalue of polymorphic class type refers to
   the dynamic type: not analysed. */
namespace std {
class type_info {
public:
    const char *name_;
};
} // namespace std
struct V {
    virtual ~V();
};
const std::type_info &f(V &v) { return typeid(v); }
