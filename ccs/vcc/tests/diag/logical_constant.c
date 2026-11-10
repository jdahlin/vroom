/* D5.1: -Wconstant-logical-operand: '&&' or '||' with a constant right operand other than
   0 and 1 (likely '&' or '|'), the fix-its to change the operator or drop the constant;
   none for enumerators, a bool left operand or an operator from a macro */
enum { FLAG = 4 };
#define AND(a, b) ((a) && (b))
#define MASK 8
int mode;
_Bool on;
int masked(void) { return mode && 4; }
int either(void) { return mode || 0x10; }
int named(void) { return mode && MASK; }
int fine(void) { return (mode && 1) + (mode || 0) + (mode && FLAG) + (on && 4) + AND(mode, 4); }
