// decltype-specifiers: [dcl.type.decltype]/1 (the entity's type for an
// unparenthesized id-expression or class member access, otherwise by value
// category), in declarations, parameters and return types; the operand is
// unevaluated
int counter = 0;
int bump() { return ++counter; }
int &global_ref() { return counter; }
int &&moved(int &x) { return static_cast<int &&>(x); }

struct Point {
    int x;
    long y;
};

const int limit = 9;
decltype(limit) copy_of_limit = limit;
decltype(bump()) from_call = 5;
decltype(counter) plain = 3;
decltype((counter)) alias = counter;

decltype(plain) twice(decltype(plain) v) { return v * 2; }
auto shifted(int v) -> decltype(plain) { return v + 1; }

int main() {
    int failures = 0;
    int local = 1;
    decltype(local) a = 2;
    decltype((local)) r = local;
    r = 7;
    failures += local != 7;
    decltype(global_ref()) g = global_ref();
    g = 4;
    failures += counter != 4;
    decltype(moved(local)) m = moved(local);
    failures += m != 7;
    /* the operand is unevaluated: bump is not called */
    decltype(bump()) n = 0;
    failures += counter != 4;
    Point p = {1, 2};
    decltype(p.y) y = 1L << 40;
    failures += sizeof(y) != 8 || y != (1L << 40);
    decltype((p.x)) px = p.x;
    px = 11;
    failures += p.x != 11;
    const Point cp = {3, 4};
    decltype(cp.x) mutable_x = 5;
    mutable_x++;
    failures += mutable_x != 6;
    decltype(copy_of_limit) c = 1;
    failures += c + copy_of_limit != 10;
    alias = 20;
    failures += counter != 20;
    failures += twice(a) != 4 || shifted(a) != 3;
    failures += from_call + plain + n != 8;
    decltype(1 + 2L) wide = 1;
    failures += sizeof(wide) != 8;
    return failures;
}
