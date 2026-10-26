#pragma once
unsigned m4_calendar_days(unsigned year, unsigned month);
unsigned m4_calendar_first(unsigned year, unsigned month); /* Sunday = 0. */
void m4_calendar_shift(unsigned *year, unsigned *month, int direction);
