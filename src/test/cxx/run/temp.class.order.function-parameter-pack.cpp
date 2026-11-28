/* [temp.class.order]: partial specializations on a pointer to member function
   with a parameter pack and with one parameter, naming a closure type's
   operator() (Catch's UnaryLambdaTraits, in a mem-initializer). */
template <class A, class B> struct same { static const bool value = false; };
template <class A> struct same<A, A> { static const bool value = true; };
namespace Catch {
struct Result { int v; static Result ok(int x) { return {x}; } };
namespace clara { namespace detail {
template <typename L> struct UnaryLambdaTraits : UnaryLambdaTraits<decltype(&L::operator())> {};
template <typename C, typename R, typename... Args> struct UnaryLambdaTraits<R (C::*)(Args...) const> { static const bool isValid = false; };
template <typename C, typename R, typename A> struct UnaryLambdaTraits<R (C::*)(A) const> {
    static const bool isValid = true;
    using ArgType = A;
    using ReturnType = R;
};
struct NonCopyable {
    NonCopyable() = default;
    NonCopyable(NonCopyable const &) = delete;
    NonCopyable(NonCopyable &&) = delete;
};
struct BoundRef : NonCopyable { virtual ~BoundRef() = default; };
struct FlagBase : BoundRef { virtual Result set(bool) = 0; };
template <typename L> struct FlagLambda : FlagBase {
    L m;
    static_assert(UnaryLambdaTraits<L>::isValid, "x");
    static_assert(same<typename UnaryLambdaTraits<L>::ArgType, bool>::value, "y");
    explicit FlagLambda(L const &l) : m(l) {}
    Result set(bool f) override { return m(f); }
};
template <class D> struct RefImpl {
    BoundRef *m_ref = nullptr;
    int hint = 0;
    explicit RefImpl(BoundRef *ref) : m_ref(ref) {}
    template <class T> RefImpl(T &ref, int h) : hint(h) {}
    template <class L> RefImpl(L const &ref, int h) : hint(h) {}
};
struct Opt : RefImpl<Opt> {
    template <class L> explicit Opt(L const &ref) : RefImpl(new FlagLambda<L>(ref)) {}
    explicit Opt(bool &ref) : RefImpl(static_cast<BoundRef *>(nullptr)) {}
    template <class L> Opt(L const &ref, int h) : RefImpl(ref, h) {}
    template <class T> Opt(T &ref, int h) : RefImpl(ref, h) {}
};
struct Help : Opt {
    Help(bool &show) : Opt([&](bool flag) { show = flag; return Result::ok(7); }) {}
};
}}}
int main()
{
    bool b = false;
    Catch::clara::detail::Help h(b);
    return static_cast<Catch::clara::detail::FlagBase &>(*h.m_ref).set(true).v != 7 || !b;
}
