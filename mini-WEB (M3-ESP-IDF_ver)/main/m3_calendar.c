#include "m3_calendar.h"
unsigned m3_calendar_days(unsigned year, unsigned month) {
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
unsigned m3_calendar_first(unsigned year, unsigned month) {
    static const unsigned offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (year < 1 || month < 1 || month > 12) return 0;
    if (month < 3) --year;
    return (year + year / 4 - year / 100 + year / 400 + offsets[month - 1] + 1) % 7;
}
void m3_calendar_shift(unsigned *year, unsigned *month, int direction) {
    if (!year || !month || *month < 1 || *month > 12) return;
    if (direction < 0) { if (*month > 1) --*month; else if (*year > 1970) { --*year; *month = 12; } }
    if (direction > 0) { if (*month < 12) ++*month; else if (*year < 2199) { ++*year; *month = 1; } }
}
