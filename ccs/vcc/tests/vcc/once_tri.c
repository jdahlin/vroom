/* vcc-flags: -std=c11 */
/* trigraph mode lexes a copy of each file: #pragma once still holds */
#include "once_inc.h"
#include "once_inc.h"

int main(void)
{
	return once_count != 1;
}
