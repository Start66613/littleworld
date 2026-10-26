import { test } from "node:test";
import assert from "node:assert/strict";
import {
  validDate,
  civilDay,
  daysBetween,
  localToday,
  nextAnniversary,
} from "../shared/date.ts";
for (const date of ["2024-02-29", "2000-02-29", "1970-01-01", "2200-12-31"])
  test(`accept calendar date ${date}`, () =>
    assert.equal(validDate(date), true));
for (const date of [
  "2023-02-29",
  "2100-02-29",
  "2026-04-31",
  "2026-13-01",
  "1969-12-31",
  "2026-1-1",
  "2026-10-25T00:00:00Z",
  "../2026-01-01",
  "",
])
  test(`reject calendar date ${date}`, () =>
    assert.equal(validDate(date), false));
test("civil-day epoch, leap day, future and negative differences", () => {
  assert.equal(civilDay("1970-01-01"), 0);
  assert.equal(daysBetween("2024-02-28", "2024-03-01"), 2);
  assert.equal(daysBetween("2026-10-26", "2026-10-25"), -1);
  assert.equal(daysBetween("2024-10-25", "2026-10-25"), 730);
  assert.equal(daysBetween("2026-10-25", "2026-10-25"), 0);
});
test("DST transitions count calendar days in America/New_York", () => {
  const before = process.env.TZ;
  try {
    process.env.TZ = "America/New_York";
    assert.equal(daysBetween("2026-03-08", "2026-03-09"), 1);
    assert.equal(daysBetween("2026-11-01", "2026-11-02"), 1);
    assert.equal(localToday(new Date(2026, 2, 9, 0, 1)), "2026-03-09");
  } finally {
    if (before) process.env.TZ = before;
    else delete process.env.TZ;
  }
});
test("Shanghai near midnight uses local civil date", () => {
  const before = process.env.TZ;
  try {
    process.env.TZ = "Asia/Shanghai";
    assert.equal(localToday(new Date("2026-10-25T16:01:00Z")), "2026-10-26");
  } finally {
    if (before) process.env.TZ = before;
    else delete process.env.TZ;
  }
});
test("leap anniversary clamps only to Feb 28, recomputes next year", () => {
  assert.equal(nextAnniversary("2024-02-29", "2026-02-28"), "2026-02-28");
  assert.equal(nextAnniversary("2024-02-29", "2027-03-01"), "2028-02-29");
  assert.equal(nextAnniversary("2024-10-25", "2026-10-26"), "2027-10-25");
});
test("invalid dates never silently normalize", () =>
  assert.throws(() => daysBetween("2026-02-30", "2026-03-01")));
