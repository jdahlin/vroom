/* [expr.typeid]: the address of a typeid result in a constant initializer
   (libc++ std::function's __policy): the evaluator has no value for it. */
namespace std {
class type_info {
public:
    const char *name_;
};
} // namespace std
struct P {
    const std::type_info *t;
};
static constexpr P p = {&typeid(int)};
