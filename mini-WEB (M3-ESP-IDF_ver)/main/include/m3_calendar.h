#pragma once
unsigned m3_calendar_days(unsigned year, unsigned month);
unsigned m3_calendar_first(unsigned year, unsigned month); /* Sunday = 0. */
void m3_calendar_shift(unsigned *year, unsigned *month, int direction);
