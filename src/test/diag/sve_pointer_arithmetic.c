/* diag-differs: vcc diagnoses pointer arithmetic at the operator. */
void f(__SVFloat32_t *p) {
    (void)(p + 1);
    (void)(p - p);
    p++;
    ++p;
    (void)&p[1];
}
