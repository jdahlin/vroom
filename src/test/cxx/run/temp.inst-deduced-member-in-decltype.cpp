/* [basic.def.odr]/4: a member's body instantiated in the member
   declarations for its deduced return type, named by a decltype, is not
   the unevaluated operand: the templates it calls are defined. */
template <class T> T identity(T x) { return x + 0; }
template <class T> struct holder {
    static auto make(T v) { return identity(v) + 1; }
    using result = decltype(make(T()));
    static result get(T v) { return make(v); }
};
int main()
{
    return holder<int>::get(4) - 5;
}
