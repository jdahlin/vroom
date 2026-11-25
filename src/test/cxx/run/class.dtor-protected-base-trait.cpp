/* [class.dtor]/7: an implicit destructor calls a base's protected one */
class base {
  protected:
    ~base() {}
};
struct derived : private base {
    derived() {}
};
struct holder {
    derived d;
};
static_assert(__is_constructible(derived), "");
static_assert(__is_constructible(holder), "");
static_assert(!__is_constructible(base), "");
int main() { holder h; (void)h; return 0; }
