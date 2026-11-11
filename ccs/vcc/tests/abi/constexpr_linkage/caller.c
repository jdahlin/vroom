/* vcc-flags: -std=c23 */
extern const int shared_scalar;
constexpr int shared_scalar = 11;
constexpr int shared_array[2] = { 13, 17 };
struct record { int x, y; };
constexpr struct record shared_record = { 19, 23 };

extern const int *other_scalar(void);
extern const int *other_array(void);
extern const struct record *other_record(void);

int main(void)
{
	const int *s = other_scalar(), *a = other_array();
	const struct record *r = other_record();
	return s == &shared_scalar || *s != 29 || shared_scalar != 11 ||
	       a == shared_array || a[0] != 31 || a[1] != 37 || shared_array[1] != 17 ||
	       r == &shared_record || r->x != 41 || r->y != 43 || shared_record.y != 23;
}
