// [basic.scope.declarative]/4: a function redeclared ill-formed is still the function its first declaration declared; later overloads and uses are analysed
void f(int);
static void f(int);
void f(char *);
void g() { f(1); f("x"); }
