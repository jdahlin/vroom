/* [dcl.typedef]/2: "the optional attribute-specifier-seq following the
   identifier appertains to that typedef-name"; attributes that change no
   code (libc++'s _LIBCPP_NODEBUG, [[__gnu__::__nodebug__]]) */
using Int [[__gnu__::__nodebug__]] = int;
using Long [[maybe_unused, gnu::unused]] = long;
using Ptr [[gnu::deprecated("old")]] = const char *;
int main()
{
    Int i = 2;
    Long l = 40;
    Ptr p = "ab";
    return (int)(i + l + p[1]) - 42 - 'b';
}
