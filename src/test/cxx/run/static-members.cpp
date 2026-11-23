// static data members: [class.static.data], [dcl.meaning]/1,
// [expr.prim.id.qual]
struct Counter {
    static int count;
    static int table[];
    static const char *name;
    int id;
    void bump() { count++; id = count; }
    static int get() { return count; }
};
int Counter::count = 10;
int Counter::table[3] = {1, 2, 3};
const char *Counter::name = "counter";

class Hidden {
    static int secret;
  public:
    static int reveal() { return secret; }
};
int Hidden::secret = 42;

int main() {
    Counter a, b;
    a.bump();
    b.bump();
    Counter::count += 1;
    int failures = 0;
    failures += Counter::get() != 13;
    failures += a.id != 11 || b.id != 12;
    failures += Counter::table[2] != 3;
    failures += sizeof(Counter) != sizeof(int);
    failures += Counter::name[0] != 'c';
    failures += Hidden::reveal() != 42;
    int *p = &Counter::count;
    *p = 1;
    failures += Counter::get() != 1;
    return failures;
}
