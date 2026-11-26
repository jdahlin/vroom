// [basic.start.dynamic]: dynamic initialization of a non-constexpr static
// storage duration variable of class type.
int init_val() { return 41; }

struct S {
    int x;
    constexpr S(int v) : x(v + 1) {}
};

const S global_s = init_val();

int main()
{
    return global_s.x == 42 ? 0 : 1;
}
