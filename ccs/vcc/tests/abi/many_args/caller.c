#include "args.h"
static double (*volatile indirect)(EACH(PARAM)) = many;
#define ALLARGS ARG(1),ARG(2),ARG(3),ARG(4),ARG(5),ARG(6),ARG(7),ARG(8),ARG(9),ARG(10),ARG(11),ARG(12),ARG(13),ARG(14),ARG(15),ARG(16),ARG(17),ARG(18),ARG(19),ARG(20),ARG(21),ARG(22),ARG(23),ARG(24)
#define NESTED_ARG(n) (long)((n) == 1 ? many(ALLARGS) : (n)), (n)+0.5, (struct pair){(n),(n)+1}
int main(void)
{
    if (many(EACH(ARG)) != 1236) return 1;
    if (indirect(EACH(ARG)) != 1236) return 2;
    if (variadic_many(24, EACH(ARG)) != 1236) return 3;
    struct result r = record_many(EACH(ARG));
    if (r.a != 1236 || r.b != 1237 || r.c != 1238) return 4;
    if (many(EACH(NESTED_ARG)) != 2471) return 5;
    if (wide_many(WIDE(ARG)) != 19157.5) return 6;
    return 0;
}
