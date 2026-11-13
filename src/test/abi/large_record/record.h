/* Odd size checks the final bytes; two records exercise distinct stack offsets. */
struct large_record { unsigned char bytes[32771]; };
struct large_record transform(long, struct large_record, double, struct large_record, long);
