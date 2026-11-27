/* [namespace.udecl]/11: a function declared after a using-declaration of its
   name is not one the using-declaration introduced. */
extern "C" int printf(const char *, ...);
int pick(long) { return 1; }
namespace lib { using ::pick; }
static int pick(int) { return 2; }
static int pick(char) { return 3; }
int main(){ printf("%d %d %d %d\n", pick(1), pick('a'), pick(1L), lib::pick(1)); }
