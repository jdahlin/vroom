/* [temp.inst]/11: the virtual functions of a specialization whose member
   bodies wait for the end of the translation unit (one a std::unique_ptr
   of it defined first: SkThreadPool). */
#include <memory>

extern "C" int printf(const char *, ...);

struct F {
    virtual ~F() = default;
    virtual void borrow() {}
    virtual int get() const { return 0; }
};

namespace
{
template <typename L> class Pool final : public F
{
  public:
    explicit Pool(int v) : n(v) {}
    void borrow() override { n++; }
    int get() const override { return static_cast<int>(n); }
    L n = 0;
};
} // namespace

template <class T> T *make(int v) { return new T(v); }

std::unique_ptr<F> pool()
{
    return std::unique_ptr<Pool<int>>(make<Pool<int>>(1));
}

int main()
{
    std::unique_ptr<F> p = pool();
    p->borrow();
    printf("%d\n", p->get());
    return 0;
}
