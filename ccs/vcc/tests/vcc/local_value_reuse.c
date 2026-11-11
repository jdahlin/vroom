/* Block-local value reuse: correctness constraints for a future CSE pass. */
static volatile unsigned observed;
static unsigned global_value;
static unsigned update(void) { global_value++; return 7; }
unsigned lvr_shift(unsigned a, unsigned b) { return (a << b) * (a << b); }
unsigned long long lvr_shift64(unsigned long long a, unsigned b) { return (a >> b)*(a >> b); }
unsigned lvr_shift_decl(unsigned a, unsigned b) { unsigned x=a >> b; return x+(a >> b); }
unsigned lvr_shift_kill(unsigned a, unsigned b) { unsigned x=a >> b; b++; return x+(a >> b); }
unsigned lvr_shift_different(unsigned a, unsigned b, unsigned c) { return (a << b) - (a << c); }
unsigned lvr_shift_order(unsigned a, unsigned b) { return (a << b)-(b << a); }
unsigned lvr_shift_volatile(unsigned a) { return (a << observed)*(a << observed); }
int lvr_shift_signed(int a, unsigned b) { int x=a >> b; return x+(a >> b); }
unsigned lvr_square(unsigned a, unsigned b) { return (a + b) * (a + b); }
unsigned lvr_nested(unsigned a, unsigned b, unsigned c) { return ((a+b)^(c+3U))*((a+b)^(c+3U)); }
unsigned lvr_distinct(unsigned a, unsigned b) { return (a-b)*(b-a); }
unsigned lvr_atomic(_Atomic unsigned *p) { return (__c11_atomic_load(p,5)+1)*(__c11_atomic_load(p,5)+1); }
unsigned lvr_reuse(unsigned a, unsigned b) { unsigned x = a + b; return x * (a + b); }
unsigned lvr_commute(unsigned a, unsigned b) { return (a + b) ^ (b + a); }
unsigned lvr_cmp_order(unsigned a, unsigned b) { return (a<b)-(b<a); }
unsigned lvr_cmp_repeat(unsigned a, unsigned b) { return (a<b)^(a<b); }
unsigned lvr_if_label(unsigned a, unsigned b, int enter) { if (enter) goto label; unsigned x=a+b; if (x==(a+b)) { label: return a+b; } return 0; }
unsigned lvr_loop(unsigned a, unsigned b) { unsigned x=a+b; while (x==(a+b)) { a++; if (a>3) break; } return a; }
unsigned lvr_if(unsigned a, unsigned b) { unsigned x=a+b; if (x==(a+b)) return 1; return 0; }
unsigned lvr_if_body_write(unsigned a, unsigned b) { unsigned x=a+b; if (x==(a+b)) { a++; return a+b; } return 0; }
unsigned lvr_if_prior_write(unsigned a, unsigned b) { unsigned x=a+b; a++; if (x==(a+b)) return 1; return 0; }
unsigned lvr_if_order(unsigned a, unsigned b) { unsigned x=a+b; if (x<(a+b)+1U) return 1; return 0; }
unsigned lvr_if_volatile(unsigned a, unsigned b) { unsigned x=a+b; if (x==(a+b)+observed) return 1; return 0; }
unsigned lvr_target_assign(unsigned a, unsigned b) { unsigned x=a+b; unsigned z; z=x*(a+b); return z; }
unsigned lvr_target_cached(unsigned a, unsigned b) { unsigned x=a+b; x=x*(a+b); return x; }
unsigned lvr_target_input(unsigned a, unsigned b) { unsigned x=a+b; a=x*(a+b); return a; }
unsigned lvr_target_dep_prior(unsigned a, unsigned b) { unsigned x=a+b; a^=1U; a=x*(a+b); return a; }
unsigned lvr_target_cached_prior(unsigned a, unsigned b) { unsigned x=a+b; x^=7U; unsigned z; z=x*(a+b); return z; }
unsigned lvr_target_compound(unsigned a, unsigned b) { unsigned x=a+b; unsigned z=2; z+=x*(a+b); return z; }
unsigned lvr_target_volatile(unsigned a, unsigned b) { unsigned x=a+b; volatile unsigned z; z=x*(a+b); return z; }
unsigned lvr_target_hidden(unsigned a, unsigned b) { unsigned x=a+b; unsigned z; z=(a=3,x*(a+b)); return z; }
unsigned lvr_cast_float(unsigned a, unsigned b) { unsigned x=a+b; unsigned y=(unsigned)(float)a; return x*y+(a+b); }
unsigned lvr_decl(unsigned a, unsigned b) { unsigned x=a+b; unsigned z=x*(a+b); return z; }
unsigned lvr_decl_chain(unsigned a, unsigned b) { unsigned x=a+b; unsigned y=a^b; unsigned z=x*y+(a+b); return z; }
unsigned lvr_decl_write(unsigned a, unsigned b) { unsigned x=a+b; unsigned z=(x++)*(a+b); return z; }
unsigned lvr_intervening(unsigned a, unsigned b) { unsigned x = a+b; unsigned y = a^b; return x*y+(a+b); }
unsigned lvr_intervening_assign(unsigned a, unsigned b) { unsigned x = a+b; unsigned y = (a=3); return x*y+(a+b); }
unsigned lvr_intervening_call(unsigned a, unsigned b) { unsigned x = a+b; unsigned y = update(); return x*y+(a+b); }
unsigned lvr_independent(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z=z^7U; return x*z+(a+b); }
unsigned lvr_independent_compound(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z^=7U; return x*z+(a+b); }
unsigned lvr_independent_inc(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z++; return x*z+(a+b); }
unsigned lvr_assignment_hidden_write(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z=(a=3); return x*z+(a+b); }
unsigned lvr_assignment_call(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z=update(); return x*z+(a+b); }
unsigned lvr_assignment_volatile(unsigned a, unsigned b, unsigned z) { unsigned x=a+b; z=observed; return x*z+(a+b); }
unsigned lvr_dep_compound(unsigned a, unsigned b) { unsigned x=a+b; a^=1U; return x*a+(a+b); }
unsigned lvr_value_inc(unsigned a, unsigned b) { unsigned x=a+b; x++; return x*x+(a+b); }
unsigned lvr_dep_written(unsigned a, unsigned b) { unsigned x=a+b; a=a^1U; return x*a+(a+b); }
unsigned lvr_value_written(unsigned a, unsigned b) { unsigned x=a+b; x=x^7U; return x*x+(a+b); }
unsigned lvr_intervening_write(unsigned a, unsigned b) { unsigned x = a+b; a++; unsigned y = a^b; return x*y+(a+b); }
unsigned lvr_intervening_volatile(unsigned a, unsigned b) { unsigned x = a+b; unsigned y = observed; return x*y+(a+b); }
unsigned lvr_written(unsigned a, unsigned b) { unsigned x = a+b; return (x++)*(a+b); }
unsigned lvr_volatile_local(unsigned a, unsigned b) { volatile unsigned x = a+b; return x*(a+b); }
unsigned lvr_kill(unsigned a, unsigned b) { unsigned x = a + b; a++; return x + (a + b); }
unsigned lvr_call(void) { unsigned x = global_value + 3; unsigned y = update(); return x + y + (global_value + 3); }
unsigned lvr_volatile(void) { return (observed + 1) * (observed + 1); }
unsigned lvr_alias(unsigned *p, unsigned *q) { unsigned x = *p + 2; *q = 7; return x + (*p + 2); }
unsigned lvr_join(unsigned a, unsigned b, int c) { unsigned x = a + b; if (c) a++; return x + a + b; }
int main(void)
{
 unsigned v = 3, w = 5;
 if (lvr_shift(3, 2) != 144 || lvr_shift64(1ULL << 40, 20) != (1ULL << 40) || lvr_shift_decl(20, 2) != 10 || lvr_shift_kill(20, 2) != 7 ||
     lvr_shift_different(3, 2, 1) != 6 || lvr_shift_order(3, 2) != (12U-16U) || lvr_shift_signed(-20, 2) != -10 ||
     lvr_square(2, 3) != 25 || lvr_reuse(2, 3) != 25 || lvr_commute(3, 4) ||
     lvr_nested(2, 3, 4) != 4 || lvr_distinct(5, 2) != (3U * (0U-3U)) ||
     lvr_cmp_order(1, 2) != 1 || lvr_cmp_order(2, 1) != ~0U || lvr_cmp_repeat(1, 2) ||
     lvr_if_label(2, 3, 0) != 5 || lvr_if_label(2, 3, 1) != 5 || lvr_loop(2, 3) != 3 ||
     lvr_if(2, 3) != 1 || lvr_if_body_write(2, 3) != 6 || lvr_if_prior_write(2, 3) != 0 ||
     lvr_if_order(2, 3) != 1 || lvr_target_assign(2, 3) != 25 || lvr_target_cached(2, 3) != 25 || lvr_target_input(2, 3) != 25 ||
     lvr_target_dep_prior(2, 3) != 30 || lvr_target_cached_prior(2, 3) != 10 ||
     lvr_target_compound(2, 3) != 27 || lvr_target_volatile(2, 3) != 25 || lvr_target_hidden(2, 3) != 30 ||
     lvr_cast_float(2, 3) != 15 || lvr_decl(2, 3) != 25 || lvr_decl_chain(2, 3) != 10 || lvr_decl_write(2, 3) != 25 ||
     lvr_independent(2, 3, 3) != 25 || lvr_independent_compound(2, 3, 3) != 25 ||
     lvr_independent_inc(2, 3, 3) != 25 || lvr_assignment_hidden_write(2, 3, 0) != 21 || lvr_dep_compound(2, 3) != 21 || lvr_value_inc(2, 3) != 41 || lvr_dep_written(2, 3) != 21 || lvr_value_written(2, 3) != 9 ||
     lvr_intervening(2, 3) != 10 || lvr_intervening_assign(2, 3) != 21 || lvr_intervening_write(2, 3) != 6 ||
     lvr_written(2, 3) != 25 || lvr_volatile_local(2, 3) != 25 ||
     lvr_kill(2, 3) != 11 || lvr_join(2, 3, 0) != 10 || lvr_join(2, 3, 1) != 11) return 1;
 global_value = 4;
 if (lvr_call() != 22 || global_value != 5) return 2;
 _Atomic unsigned atom = 3;
 if (lvr_atomic(&atom) != 16) return 5;
 if (lvr_intervening_call(2, 3) != 40 || global_value != 6 ||
     lvr_assignment_call(2, 3, 0) != 40 || global_value != 7) return 6;
 observed = 3;
 if (lvr_shift_volatile(2) != 256 || lvr_if_volatile(2, 3) != 0 || lvr_volatile() != 16 || lvr_intervening_volatile(2, 3) != 20 || lvr_assignment_volatile(2, 3, 0) != 20) return 3;
 if (lvr_alias(&v, &v) != 14 || v != 7 || lvr_alias(&v, &w) != 18 || w != 7) return 4;
 return 0;
}
