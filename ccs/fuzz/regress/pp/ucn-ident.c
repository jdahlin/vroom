#define CAT(a,b) a##b
int \U0001F600x = 1; int \u00e9y; int a\u00E9\u0100b; const char *s = "\U0001F600";
#define U \U0001F600
U
#define STR(x) #x
STR(\u00e9)
CAT(\u00e9, z) CAT(x, \u00e9) CAT(\u00e9,\u0100)
CAT(1, \u00e9) CAT(\u00e9, 1)
