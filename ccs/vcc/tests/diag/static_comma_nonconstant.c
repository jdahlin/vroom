/* diag-flags: -w */
int function(void);
int ordinary;
volatile int vol;
struct S { int n; };
int call_comma = (function(), 1);
int read_comma = (ordinary, 1);
int volatile_comma = (vol, 1);
struct S bad_comma = (function(), (struct S){1});
