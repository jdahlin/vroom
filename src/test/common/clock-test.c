/* Unit test of port/clock.h: the clock does not go back and moves with sleeping. */
#include <unistd.h>
#include "check.h"
#include "port/clock.h"

int main(void)
{
	double a = vclock_seconds(), b;

	usleep(20000);
	b = vclock_seconds();
	CHECK(b >= a + 0.015 && b < a + 5);
	CHECK(vclock_seconds() >= b);
	return check_failures;
}
