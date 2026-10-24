/* Little World - calendar helpers (pure C, host tested).
 *
 * All date maths is proleptic Gregorian and timezone independent: the caller
 * supplies the local calendar date, this module only does arithmetic.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int year;
    int month; /* 1..12 */
    int day;   /* 1..31 */
} lw_date_t;

bool lw_is_leap_year(int year);
int lw_days_in_month(int year, int month);
bool lw_date_valid(lw_date_t date);

/** Days since 1970-01-01 (negative before that). */
int lw_days_from_civil(int year, int month, int day);
void lw_civil_from_days(int days, lw_date_t *out);

/** Whole days from @p a to @p b (b - a). */
int lw_days_between(lw_date_t a, lw_date_t b);

lw_date_t lw_date_add_days(lw_date_t date, int days);

/**
 * Next yearly occurrence of @p start's month/day on or after @p today.
 * 02-29 falls back to 02-28 in non-leap years.
 */
lw_date_t lw_next_anniversary(lw_date_t start, lw_date_t today);

/** Days until the next anniversary (0 when today is the anniversary). */
int lw_days_until_anniversary(lw_date_t start, lw_date_t today);

#ifdef __cplusplus
}
#endif
