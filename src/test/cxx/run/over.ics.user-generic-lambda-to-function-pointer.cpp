/* [over.ics.user], [expr.prim.lambda.closure]/8: overload resolution of a
   constructor converts a generic lambda to a pointer to function */
typedef void Cb(int &);
struct F {
    F(Cb post) : cb(post) {}
    Cb *cb;
};
int main()
{
    int x = 0;
    F f([](auto &v) { v = 3; });
    f.cb(x);
    return x - 3;
}
