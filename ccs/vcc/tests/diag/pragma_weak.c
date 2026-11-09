/* #pragma weak: malformed, never declared, an alias of a static declared later */
#pragma weak
#pragma weak 3
#pragma weak a b
#pragma weak a =
#pragma weak never
#pragma weak al = tgt
static int tgt(void) { return 1; }
#pragma weak late
static int late;
int use(void) { return tgt() + late; }
