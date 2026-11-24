// [expr.new]/2, [dcl.type.auto.deduct]/2: new auto(E) allocates an object of the deduced type.
int main() {
    auto *p = new auto(41);
    ++*p;
    int x = 3;
    auto *q = new auto(&x);
    int r = **q + *p;
    delete q;
    delete p;
    return r == 45 ? 0 : 1;
}
