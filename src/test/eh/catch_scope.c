/* vcc-only: try/catch (doc/PLAN-vir-eh.md E3).  clang's C has no try, so vcc's test hook stands in for what
   the C++ front end builds with ast_builder_try: __attribute__((vcc_catch(exception, type, &&label, ...)))
   on a variable makes its scope a try block; an unwinding exception matching `type` (a _ZTI object, 0:
   any) continues at the label with the exception pointer in `exception`.  The handler is written as C++
   lowers it: __cxa_begin_catch, a cleanup variable that calls __cxa_end_catch on every exit.
   catch_scope.cpp (built by clang++) throws and checks. */
void cxx_throw(int k);
void *__cxa_begin_catch(void *);
void __cxa_end_catch(void);
extern char _ZTIi, _ZTIl;
int s_order[32], s_n;

static void note(int *p)
{
    s_order[s_n++] = *p;
}

static void end_catch(char *p)
{
    s_order[s_n++] = 90;
    __cxa_end_catch();
}

/* cxx_throw(k): 1 throws an int (7), 2 a long (8), 3 a const char *, other: nothing */

int basic(int k)
{
    void *ex;
    int result = 0;

    {
        char scope __attribute__((vcc_catch(ex, _ZTIi, &&h_int, 0, &&h_all)));
        int a __attribute__((cleanup(note))) = 1;

        cxx_throw(k);
        result = 100;
    }
    goto end;
h_int : {
    char g __attribute__((cleanup(end_catch)));
    int *p = (int *)__cxa_begin_catch(ex);

    result = 10 + *p;
}
    goto end;
h_all : {
    char g __attribute__((cleanup(end_catch)));

    __cxa_begin_catch(ex);
    result = 20;
}
end:
    return result;
}

/* only int is caught: a long goes to the caller's handler through this frame's cleanup */
int only_int(int k)
{
    void *ex;
    int result = 0;
    int outer __attribute__((cleanup(note))) = 2;

    {
        char scope __attribute__((vcc_catch(ex, _ZTIi, &&h_int)));
        int a __attribute__((cleanup(note))) = 3;

        cxx_throw(k);
        result = 100;
    }
    goto end;
h_int : {
    char g __attribute__((cleanup(end_catch)));
    int *p = (int *)__cxa_begin_catch(ex);

    result = 10 + *p;
}
end:
    return result;
}

/* an inner try that does not match goes to the outer one (a long), cleanups run in between */
int nested(int k)
{
    void *ex_outer, *ex_inner;
    int result = 0;

    {
        char outer __attribute__((vcc_catch(ex_outer, _ZTIl, &&h_long)));
        int a __attribute__((cleanup(note))) = 4;

        {
            char inner __attribute__((vcc_catch(ex_inner, _ZTIi, &&h_int)));
            int b __attribute__((cleanup(note))) = 5;

            cxx_throw(k);
            result = 100;
        }
        goto end;
    h_int : {
        char g __attribute__((cleanup(end_catch)));
        int *p = (int *)__cxa_begin_catch(ex_inner);

        result = 10 + *p;
    }
    }
    goto end;
h_long : {
    char g __attribute__((cleanup(end_catch)));
    long *p = (long *)__cxa_begin_catch(ex_outer);

    result = 20 + (int)*p;
}
end:
    return result;
}

/* a handler that throws: its __cxa_end_catch runs on the unwind, the outer try catches the new exception */
int rethrow(int k)
{
    void *ex_outer, *ex_inner;
    int result = 0;

    {
        char outer __attribute__((vcc_catch(ex_outer, _ZTIl, &&h_long)));

        {
            char inner __attribute__((vcc_catch(ex_inner, _ZTIi, &&h_int)));

            cxx_throw(k);
            result = 100;
        }
        goto end;
    h_int : {
        char g __attribute__((cleanup(end_catch)));
        int *p = (int *)__cxa_begin_catch(ex_inner);

        result = 10 + *p;
        cxx_throw(2);
    }
    }
    goto end;
h_long : {
    char g __attribute__((cleanup(end_catch)));
    long *p = (long *)__cxa_begin_catch(ex_outer);

    result = result + 1000 + (int)*p;
}
end:
    return result;
}
