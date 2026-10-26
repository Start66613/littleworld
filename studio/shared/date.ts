// Gregorian civil days, the same algorithm as lw_time.c. No UTC clock arithmetic.
export function validDate(value: string): boolean {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(value)) return false;
  const [y, m, d] = value.split("-").map(Number);
  const leap = y % 4 === 0 && (y % 100 !== 0 || y % 400 === 0);
  return (
    y >= 1970 &&
    y <= 2200 &&
    m >= 1 &&
    m <= 12 &&
    d >= 1 &&
    d <= [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]
  );
}
export function civilDay(value: string): number {
  if (!validDate(value))
    throw new Error("日期必须为 1970–2200 年内有效的 YYYY-MM-DD");
  let [y, m, d] = value.split("-").map(Number);
  y -= m <= 2 ? 1 : 0;
  const era = Math.floor(y / 400),
    yoe = y - era * 400;
  const doy = Math.floor((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5) + d - 1;
  return (
    era * 146097 +
    yoe * 365 +
    Math.floor(yoe / 4) -
    Math.floor(yoe / 100) +
    doy -
    719468
  );
}
export function daysBetween(start: string, end: string): number {
  return civilDay(end) - civilDay(start);
}
export function localToday(now = new Date()): string {
  return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, "0")}-${String(now.getDate()).padStart(2, "0")}`;
}
export function nextAnniversary(start: string, today: string): string {
  if (!validDate(start) || !validDate(today)) throw new Error("无效的日期");
  const monthDay = start.slice(4);
  const inYear = (y: number) => {
    const leap = y % 4 === 0 && (y % 100 !== 0 || y % 400 === 0);
    return monthDay === "-02-29" && !leap ? `${y}-02-28` : `${y}${monthDay}`;
  };
  const year = Number(today.slice(0, 4));
  const candidate = inYear(year);
  return candidate >= today ? candidate : inYear(year + 1);
}
