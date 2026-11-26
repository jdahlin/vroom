/* A dynamically initialized static local of an inline function (a member
   function template defined in its class) has one guard variable */
static int src = 5;
struct K {
    template <class T> int f()
    {
        static int a = src;
        static int b = src + 1;
        return a + b;
    }
    int g()
    {
        static int c = src;
        return c;
    }
};
inline int h()
{
    static int d = src;
    return d;
}

int main() { return K().f<int>() + K().g() + h() + K().f<int>() != 32; }
