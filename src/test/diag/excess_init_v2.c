/* -Wexcess-initializers: a warning, once per list, the elements dropped */
struct T { int y; };
int d[2] = { 1, 2, 3, 4 };
struct T e = { .y = 1, 2 };
union { int a; } u = { 1, 2 };
