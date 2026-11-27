/* An inlined callee's endless loop decides nothing about the condition its
   call is in: the if keeps its then branch. */
#include <stdio.h>
#include <stdbool.h>
volatile int g = 3;
static inline bool a(int k) { do { if (k > g) return false; k++; } while (k < 3); return true; }
static inline bool b(int k) { do { if (k > g) return false; k++; } while (1); }
__attribute__((noinline)) int top(int w, int k) {
    if (w == 1 ? !a(k) : !b(k)) { printf("then "); return 10; }
    return 20;
}
int main(void) { volatile int one = 1, ten = 10, z = 0; printf("%d %d %d\n", top(one, ten), top(one, one), top(z, ten)); return 0; }
