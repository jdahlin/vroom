/* __attribute__((musttail)): clang's rules */
/* diag-differs: a note on a declaration is at its name (clang: its start), and the cleanup error at
   the call (clang: the statement) */
int f(int);
long g(long);
void cl(int *);
int v(int, ...);
int two(int, char *);

int not_a_call(int x)
{
    __attribute__((musttail)) return x;
}

int return_type(int x)
{
    __attribute__((musttail)) return g(x);
}

int count(int x)
{
    __attribute__((musttail)) return two(x, 0);
}

int parameter(int x, int *p)
{
    __attribute__((musttail)) return two(x, 0);
}

void not_a_return(void)
{
    __attribute__((musttail)) cl(0);
}

int variadic(int x)
{
    __attribute__((musttail)) return v(x);
}

int cleanup(int x)
{
    __attribute__((cleanup(cl))) int y = 0;
    __attribute__((musttail)) return f(x);
}

int argument(int x)
{
    __attribute__((musttail(1))) return f(x);
}

int fine(int x)
{
    __attribute__((musttail)) return (f(x));
}
