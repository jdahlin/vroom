/* A generic lambda without a lambda-capture converts to a pointer to
   function ([expr.prim.lambda.closure]/8): its conversion function template
   is not declared. */
int main()
{
    auto g = [](auto x) { return x + 1; };
    int (*gp)(int) = g;
    return gp(1) - 2;
}
