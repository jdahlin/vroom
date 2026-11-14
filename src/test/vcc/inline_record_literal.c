/* An inlined aggregate return initializes omitted fields on every path,
 * even when the result is assigned back to the initializer's source. */
struct record { long words[4]; int tag; };
static int evaluations;

static inline __attribute__((always_inline))
struct record update(const struct record *old, int flag)
{
	if (flag)
		return (struct record){ .words = {old->words[2], ++evaluations}, .tag = old->tag + 1 };
	return *old;
}

int main(void)
{
	struct record value = {{3, 4, 17, 8}, 9};
	value = update(&value, 0);
	if (value.words[2] != 17 || value.tag != 9 || evaluations) return 1;
	value = update(&value, 1);
	return value.words[0] != 17 || value.words[1] != 1 || value.words[2] ||
	       value.words[3] || value.tag != 10 || evaluations != 1;
}
