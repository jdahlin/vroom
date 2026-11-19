/* [ext:gnu_complex]: the declaration of an object of complex type is
   analysed (its layout is known), an expression of the type is not: the
   arithmetic, the conversions and the operands of a return have no rules
   yet, and the reason names the extension. */
_Complex double z;
_Complex double copy() { return z; }
