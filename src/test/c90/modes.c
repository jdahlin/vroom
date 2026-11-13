#include "modes.h"
extern int strcmp(const char *, const char *);
struct tag { char x; };
static int if_condition(void) {
    if (sizeof(struct tag { int x; })) (void)0;
    return sizeof(struct tag) == 1;
}
static int if_body(void) {
    if (1) (void)sizeof(struct tag { int x; });
    return sizeof(struct tag) == 1;
}
static int while_condition(void) {
    while (sizeof(struct tag { int x; }) && 0) (void)0;
    return sizeof(struct tag) == 1;
}
static int while_body(void) {
    while (0) (void)sizeof(struct tag { int x; });
    return sizeof(struct tag) == 1;
}
static int do_condition(void) {
    do (void)0; while (sizeof(struct tag { int x; }) && 0);
    return sizeof(struct tag) == 1;
}
static int do_body(void) {
    do (void)sizeof(struct tag { int x; }); while (0);
    return sizeof(struct tag) == 1;
}
static int for_condition(void) {
    for ((void)sizeof(struct tag { int x; }); 0;) (void)0;
    return sizeof(struct tag) == 1;
}
static int for_body(void) {
    for (;0;) (void)sizeof(struct tag { int x; });
    return sizeof(struct tag) == 1;
}
static int switch_condition(void) {
    switch (sizeof(struct tag { int x; })) (void)0;
    return sizeof(struct tag) == 1;
}
static int explicit_block(void) {
    if (1) { (void)sizeof(struct tag { int x; }); }
    return sizeof(struct tag) == 1;
}
int main(void) {
    if (strcmp(joined, DIGRAPHS ? "12" : "1 %:%: 2")) return 1;
    if (strcmp(brackets, "<: :> <% %> %:") || strcmp(literal, brackets)) return 2;
    if (strcmp(spliced, "<:")) return 3;
    if (if_condition()!=C99 || if_body()!=C99) return 4;
    if (while_condition()!=C99 || while_body()!=C99) return 5;
    if (do_condition()!=C99 || do_body()!=C99) return 6;
    if (for_condition()!=C99 || for_body()!=C99) return 7;
    if (switch_condition()!=C99 || !explicit_block()) return 8;
    return 0;
}
