/* A call of an abbreviated function template with a function parameter pack
   ([dcl.fct]/22): its instantiation binds the invented pack, which has no
   name to bind yet. */
int count(auto &&...rest) { return sizeof...(rest); }
int main()
{
    return count(1, 2, 3);
}
