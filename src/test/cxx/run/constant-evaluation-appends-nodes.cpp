/* Evaluating a constant expression can analyse more code (the body of a
   constexpr function is analysed when the call needs it), which appends to
   the semantic tree: the evaluator must not hold a reference into it across
   an evaluation (explicit(!Check<A, B>::template implicit<U1, U2>()) of
   libc++'s pair did, and <istream> crashed vcc: a heap use after free).
   Sixty distinct instantiations cross the tree's growth boundaries. */
template <class T, class U> constexpr bool convertible = __is_convertible(T, U);
template <class A, class B> struct Check {
    template <class U1, class U2> static constexpr bool implicit()
    {
        return convertible<U1, A> && convertible<U2, B>;
    }
};
template <class A, class B> struct Pair {
    A first;
    B second;
    template <class U1, class U2>
    explicit(!Check<A, B>::template implicit<U1, U2>()) Pair(const U1 &, const U2 &) : first(), second() {}
};
template <int N> struct Sized { operator int() const { return N; } };
int main()
{
    Pair<int, int> p1{Sized<1>(), Sized<1>()};
    Pair<int, int> p2{Sized<2>(), Sized<2>()};
    Pair<int, int> p3{Sized<3>(), Sized<3>()};
    Pair<int, int> p4{Sized<4>(), Sized<4>()};
    Pair<int, int> p5{Sized<5>(), Sized<5>()};
    Pair<int, int> p6{Sized<6>(), Sized<6>()};
    Pair<int, int> p7{Sized<7>(), Sized<7>()};
    Pair<int, int> p8{Sized<8>(), Sized<8>()};
    Pair<int, int> p9{Sized<9>(), Sized<9>()};
    Pair<int, int> p10{Sized<10>(), Sized<10>()};
    Pair<int, int> p11{Sized<11>(), Sized<11>()};
    Pair<int, int> p12{Sized<12>(), Sized<12>()};
    Pair<int, int> p13{Sized<13>(), Sized<13>()};
    Pair<int, int> p14{Sized<14>(), Sized<14>()};
    Pair<int, int> p15{Sized<15>(), Sized<15>()};
    Pair<int, int> p16{Sized<16>(), Sized<16>()};
    Pair<int, int> p17{Sized<17>(), Sized<17>()};
    Pair<int, int> p18{Sized<18>(), Sized<18>()};
    Pair<int, int> p19{Sized<19>(), Sized<19>()};
    Pair<int, int> p20{Sized<20>(), Sized<20>()};
    Pair<int, int> p21{Sized<21>(), Sized<21>()};
    Pair<int, int> p22{Sized<22>(), Sized<22>()};
    Pair<int, int> p23{Sized<23>(), Sized<23>()};
    Pair<int, int> p24{Sized<24>(), Sized<24>()};
    Pair<int, int> p25{Sized<25>(), Sized<25>()};
    Pair<int, int> p26{Sized<26>(), Sized<26>()};
    Pair<int, int> p27{Sized<27>(), Sized<27>()};
    Pair<int, int> p28{Sized<28>(), Sized<28>()};
    Pair<int, int> p29{Sized<29>(), Sized<29>()};
    Pair<int, int> p30{Sized<30>(), Sized<30>()};
    Pair<int, int> p31{Sized<31>(), Sized<31>()};
    Pair<int, int> p32{Sized<32>(), Sized<32>()};
    Pair<int, int> p33{Sized<33>(), Sized<33>()};
    Pair<int, int> p34{Sized<34>(), Sized<34>()};
    Pair<int, int> p35{Sized<35>(), Sized<35>()};
    Pair<int, int> p36{Sized<36>(), Sized<36>()};
    Pair<int, int> p37{Sized<37>(), Sized<37>()};
    Pair<int, int> p38{Sized<38>(), Sized<38>()};
    Pair<int, int> p39{Sized<39>(), Sized<39>()};
    Pair<int, int> p40{Sized<40>(), Sized<40>()};
    Pair<int, int> p41{Sized<41>(), Sized<41>()};
    Pair<int, int> p42{Sized<42>(), Sized<42>()};
    Pair<int, int> p43{Sized<43>(), Sized<43>()};
    Pair<int, int> p44{Sized<44>(), Sized<44>()};
    Pair<int, int> p45{Sized<45>(), Sized<45>()};
    Pair<int, int> p46{Sized<46>(), Sized<46>()};
    Pair<int, int> p47{Sized<47>(), Sized<47>()};
    Pair<int, int> p48{Sized<48>(), Sized<48>()};
    Pair<int, int> p49{Sized<49>(), Sized<49>()};
    Pair<int, int> p50{Sized<50>(), Sized<50>()};
    Pair<int, int> p51{Sized<51>(), Sized<51>()};
    Pair<int, int> p52{Sized<52>(), Sized<52>()};
    Pair<int, int> p53{Sized<53>(), Sized<53>()};
    Pair<int, int> p54{Sized<54>(), Sized<54>()};
    Pair<int, int> p55{Sized<55>(), Sized<55>()};
    Pair<int, int> p56{Sized<56>(), Sized<56>()};
    Pair<int, int> p57{Sized<57>(), Sized<57>()};
    Pair<int, int> p58{Sized<58>(), Sized<58>()};
    Pair<int, int> p59{Sized<59>(), Sized<59>()};
    return p1.first + p2.second;
}
