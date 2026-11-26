/* [basic.lookup.argdep]/4: every overload in an associated namespace is a
   candidate when unqualified lookup found a function template (nlohmann's
   detail::to_json templates calling a user's to_json) */
extern "C" int printf(const char *, ...);
struct json {};
namespace user
{
struct a {};
struct b {};
struct c {};
int put(json &, a) { return 1; }
int put(json &, b) { return 2; }
int put(json &, c) { return 3; }
} // namespace user
namespace library
{
template <class T> int put(json &, const T *) { return 4; }
int call()
{
    json j;
    int n = 0;
    return put(j, user::a{}) * 1000 + put(j, user::b{}) * 100 +
           put(j, user::c{}) * 10 + put(j, &n);
}
} // namespace library
int main()
{
    printf("%d\n", library::call());
    return 0;
}
