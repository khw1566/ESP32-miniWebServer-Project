#pragma once
#include <stdbool.h>
/* Bounded expression parser. Trigonometric arguments follow the selected mode. */
bool m3_calculator_eval(const char *expression, bool degrees, double answer, double *result);
