#include "a.h"
#include "a.h"
#include "b.h"
#include <c.h>
A B C ONCE __INCLUDE_LEVEL__
#ifndef A_H
#define A_H
#define A a_from_header __FILE__ __INCLUDE_LEVEL__
#include "b.h"
#endif
#pragma once
#define B(x) b_##x(B)
B(1)
#pragma GCC system_header
#define C c __LINE__
#include_next <c.h>
int sys;
