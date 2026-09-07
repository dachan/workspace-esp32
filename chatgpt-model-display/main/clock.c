#include "clock.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_timer.h"

static int64_t s_unix;
static int s_tz_min;
static int64_t s_sync_us;
static bool s_have;
static bool s_dirty;
static int s_shown_minute = -1;

static const char *s_wday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *s_mon[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

bool clock_apply_line(const char *line)
{
    long long unix = 0;
    int tz_min = 0;
    if (sscanf(line, "TIME %lld %d", &unix, &tz_min) != 2) {
        return false;
    }
    if (unix <= 0) {
        return false;
    }
    s_unix = unix;
    s_tz_min = tz_min;
    s_sync_us = esp_timer_get_time();
    s_have = true;
    s_dirty = true;
    return true;
}

static bool local_tm(struct tm *out)
{
    if (!s_have || out == NULL) {
        return false;
    }
    int64_t elapsed_s = (esp_timer_get_time() - s_sync_us) / 1000000LL;
    int64_t local = s_unix + elapsed_s + (int64_t)s_tz_min * 60;
    if (local < 0) {
        local = 0;
    }
    time_t t = (time_t)local;
    if (gmtime_r(&t, out) == NULL) {
        return false;
    }
    return true;
}

static int minute_of_day(const struct tm *tm)
{
    return tm->tm_hour * 60 + tm->tm_min;
}

bool clock_format(char *buf, size_t n)
{
    struct tm tm;
    if (!local_tm(&tm) || buf == NULL || n == 0) {
        if (buf && n) {
            buf[0] = '\0';
        }
        return false;
    }
    s_shown_minute = minute_of_day(&tm);
    s_dirty = false;
    const char *ampm = tm.tm_hour >= 12 ? "PM" : "AM";
    int hour12 = tm.tm_hour % 12;
    if (hour12 == 0) {
        hour12 = 12;
    }
    snprintf(buf, n, "%d:%02d %s", hour12, tm.tm_min, ampm);
    return true;
}

bool clock_format_date(char *buf, size_t n)
{
    struct tm tm;
    if (!local_tm(&tm) || buf == NULL || n == 0) {
        if (buf && n) {
            buf[0] = '\0';
        }
        return false;
    }
    snprintf(buf, n, "%s %s %d", s_wday[tm.tm_wday], s_mon[tm.tm_mon], tm.tm_mday);
    return true;
}

bool clock_needs_paint(void)
{
    struct tm tm;
    if (!local_tm(&tm)) {
        return false;
    }
    return s_dirty || minute_of_day(&tm) != s_shown_minute;
}
