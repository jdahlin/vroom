/* a noexcept-specifier in a member class defined outside its class template
   names data members declared before (libc++'s transform_view::__iterator) */
namespace std2 { template <class F, class A> int invoke(F &&f, A &&a) noexcept { return f(a); } }
template <class V, class F> struct tv {
    F *func_;
    struct it;
};
template <class V, class F>
struct tv<V, F>::it {
        tv *parent_ = nullptr;
        V current_ = 0;
        decltype(auto) operator*() const noexcept(noexcept(std2::invoke(*parent_->func_, current_)))
        {
            return std2::invoke(*parent_->func_, current_);
        }
        int g() const { return parent_->func_ != nullptr; }
    };
int sq(int x) { return x * x; }
int main()
{
    tv<int, int(int)> t{sq};
    tv<int, int(int)>::it i{&t, 3};
    return *i != 9;
}
