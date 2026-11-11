/* vcc-flags: -std=c23 */
constexpr int shared_scalar = 29;
constexpr int shared_array[2] = { 31, 37 };
struct record { int x, y; };
constexpr struct record shared_record = { 41, 43 };

const int *other_scalar(void) { return &shared_scalar; }
const int *other_array(void) { return shared_array; }
const struct record *other_record(void) { return &shared_record; }
