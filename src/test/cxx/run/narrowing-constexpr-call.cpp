/* [dcl.init.list]/7: a constexpr function's result that fits is no
   narrowing (Highway's TF{LimitsMax<TI>()}) */
template <class T> constexpr T Value() { return T(1000); }
template <class TF, class TI> TF convert() { return TF{Value<TI>()}; }

int main()
{
    return convert<double, int>() != 1000 || convert<float, short>() != 1000 ||
           convert<short, long>() != 1000;
}
