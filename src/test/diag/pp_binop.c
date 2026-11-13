/* #if: a token after an operand: the caret there, the operand underlined, the macro note */
#define X 1
#if 1 + 2 3
#endif
#if (1) 2
#endif
#if X 2
#endif
#if 1 ? 2 : 3 4
#endif
#if 1 X
#endif
#if 1 == 2 "s"
#endif
