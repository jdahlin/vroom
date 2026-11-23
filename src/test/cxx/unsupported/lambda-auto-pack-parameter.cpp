/* A generic lambda's parameter pack of a generic parameter type placeholder
   ([dcl.fct]/22, [expr.prim.lambda.closure]/3): its instantiation binds the
   invented pack, which has no name to bind yet (as abbreviated-pack-use). */
int main()
{
    auto count = [](auto &&...rest) { return sizeof...(rest); };
    return count(1, 2, 3) - 3;
}
