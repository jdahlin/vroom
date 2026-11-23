/* [class.pre]/1 and [dcl.type.elab]: a class-head's
   __attribute__((__visibility__("default"))) (libc++'s
   _LIBCPP_EXPORTED_FROM_ABI) changes nothing vcc generates */
namespace std {
class __attribute__((__visibility__("default"))) exception_ptr;
class __attribute__((__visibility__("default"))) exception {
public:
    int code;
    int get() const { return code; }
};
struct __attribute__((visibility("default"))) derived : exception {
    int extra;
};
class __attribute__((__visibility__("default"))) exception_ptr {
public:
    void *p;
};
} // namespace std
int main()
{
    std::derived d;
    d.code = 40;
    d.extra = 2;
    std::exception_ptr e;
    e.p = nullptr;
    return d.get() + d.extra - 42 + (e.p != nullptr);
}
