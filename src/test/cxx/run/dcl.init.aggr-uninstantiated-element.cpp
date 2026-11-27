/* [dcl.init.aggr]: an array element of a class template's specialization not
   yet instantiated, which has constructors, is no subaggregate. */
extern "C" int printf(const char *, ...);

template <class R> struct function {
    function() {}
    template <class F> function(F f) : value(f()) {}
    R value = 0;
};

int one() { return 1; }

int main()
{
    int two = 2;
    function<int> all[] = {one, [two] { return two; }};
    printf("%d %d\n", all[0].value, all[1].value);
    return 0;
}
