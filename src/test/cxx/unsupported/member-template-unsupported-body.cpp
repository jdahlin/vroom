/* cxx-unsupported: lowering */
/* [temp.inst]/4: a member function template's specialization whose body
   has a construct analysis has no rules for (here [ext:gnu_complex]) is not
   made: its use is refused when lowered, never called with an empty body
   (the specialization of a static operator() template once was). */
_Complex double z;
struct S {
    template <class T> static T operator()(T t)
    {
        _Complex double w = z;
        (void)w;
        return t + 1;
    }
};
int main() { return S()(1) - 2; }
