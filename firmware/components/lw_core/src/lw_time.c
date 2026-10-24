/* SPDX-License-Identifier: MIT */
#include "lw/lw_time.h"

bool lw_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int lw_days_in_month(int year, int month)
{
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && lw_is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

bool lw_date_valid(lw_date_t date)
{
    if (date.year < 1970 || date.year > 2200) {
        return false;
    }
    if (date.month < 1 || date.month > 12) {
        return false;
    }
    return date.day >= 1 && date.day <= lw_days_in_month(date.year, date.month);
}

/* Howard Hinnant's days_from_civil / civil_from_days, public domain algorithm. */
int lw_days_from_civil(int year, int month, int day)
{
    int y = year - (month <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468;
}

void lw_civil_from_days(int days, lw_date_t *out)
{
    int z = days + 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    out->year = y + (m <= 2);
    out->month = (int)m;
    out->day = (int)d;
}

int lw_days_between(lw_date_t a, lw_date_t b)
{
    return lw_days_from_civil(b.year, b.month, b.day) - lw_days_from_civil(a.year, a.month, a.day);
}

lw_date_t lw_date_add_days(lw_date_t date, int days)
{
    lw_date_t result;
    lw_civil_from_days(lw_days_from_civil(date.year, date.month, date.day) + days, &result);
    return result;
}

lw_date_t lw_next_anniversary(lw_date_t start, lw_date_t today)
{
    lw_date_t candidate;
    candidate.year = today.year;
    candidate.month = start.month;
    candidate.day = start.day;
    if (!lw_date_valid(candidate)) {
        /* 02-29 in a non leap year: celebrate on 02-28. */
        candidate.day = lw_days_in_month(candidate.year, candidate.month);
    }
    if (lw_days_between(today, candidate) < 0) {
        candidate.year += 1;
        if (!lw_date_valid(candidate)) {
            candidate.day = lw_days_in_month(candidate.year, candidate.month);
        }
    }
    return candidate;
}

int lw_days_until_anniversary(lw_date_t start, lw_date_t today)
{
    return lw_days_between(today, lw_next_anniversary(start, today));
}
