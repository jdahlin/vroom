/* [ext:gnu_attributes] visibility("hidden") on a class's forward and
   friend declarations (libc++'s _LIBCPP_HIDDEN class __do_message). */
class __attribute__((visibility("hidden"))) hidden_impl;

class base {
public:
    int value() const { return 4; }
    friend class __attribute__((visibility("hidden"))) hidden_impl;
};

class __attribute__((visibility("hidden"))) hidden_impl : public base {
public:
    int twice() const { return 2 * value(); }
};

int main()
{
    hidden_impl h;
    if (h.twice() != 8)
        return 1;
    return 0;
}
