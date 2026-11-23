/* [dcl.init.aggr]/5.2: an empty base is an element; with designators it is
   value-initialized.  Positional clauses mixed with designated ones in a
   class with an empty base are refused (not_supported): they once dropped
   the clause of the next base (F f{{}, {4}, .y = 9} left f.b at 0). */
extern "C" int printf(const char *, ...);
struct B { int b; };
struct E {};
struct F : E, B { int x; int y; };
struct G : B { int x, y; };
G h{{4}, .y = 9};
F f{{}, {4}, 5, 9};
F d{.x = 3, .y = 7};
int main()
{
    printf("%d %d %d\n", h.b, h.x, h.y);
    printf("%d %d %d\n", f.b, f.x, f.y);
    printf("%d %d %d\n", d.b, d.x, d.y);
    return 0;
}
