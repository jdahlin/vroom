/* [temp.inst]/11: the virtual functions of a specialization whose member
   bodies wait for the end of the translation unit (one a std::unique_ptr
   specialization defined while another class is: SkThreadPool). */

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

struct holder {
    char storage[sizeof(Pool<int>)];
};

template <class T> T *make(int v) { return new T(v); }

int main()
{
    F *p = make<Pool<int>>(1);
    p->borrow();
    printf("%d %d\n", p->get(), sizeof(holder) >= sizeof(void *));
    return 0;
}
