/* cxx-run-flags: -frtti */
/* [expr.typeid]/1: typeid(type) names a std::type_info object whose address
   is a constant (libc++'s __policy: static constexpr {..., &typeid(void)});
   the objects of the fundamental types are the run-time library's */
namespace std {
class type_info {
public:
  bool operator==(const type_info &o) const { return this == &o; }
  bool operator!=(const type_info &o) const { return this != &o; }
};
} // namespace std

/* the C++ run-time library's objects, defined here: the test links no library */
extern "C" {
extern const long _ZTIv[2] = {0, 0};
extern const long _ZTIi[2] = {0, 0};
extern const long _ZTIl[2] = {0, 0};
extern const long _ZTIPKc[2] = {0, 0};
}

struct policy {
  const void *invoke;
  bool is_null;
  const std::type_info *type;
};

policy null_policy = {nullptr, true, &typeid(void)};

const std::type_info *of_int() { return &typeid(int); }
const std::type_info *of_pointer() { return &typeid(const char *); }

int main()
{
  if (null_policy.type != &typeid(void))
    return 1;
  if (of_int() != &typeid(int) || *of_int() != typeid(int))
    return 2;
  if (of_int() == &typeid(long))
    return 3;
  if (*of_pointer() != typeid(const char *))
    return 4;
  return 0;
}
