/* Little World - Windows host platform implementation (storage, clock, log).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "lw/lw_platform.h"

static char s_base_dir[MAX_PATH];

static bool host_get_time(struct tm *out)
{
    /* Screenshot regressions compare whole frames, and the ambience tint is a
     * function of the local hour, so a run at 18:00 and a run at 23:00 differ by
     * ~25 levels on every pixel even when nothing changed. LW_FAKE_TIME pins the
     * hour ("HH:MM") for the harness; without it the real clock is used. */
    static int fake_hour = -1, fake_minute = 0, fake_parsed = 0;
    if (!fake_parsed) {
        fake_parsed = 1;
        const char *text = getenv("LW_FAKE_TIME");
        if (text != NULL && text[0] != '\0') {
            int hour = 0, minute = 0;
            if (sscanf(text, "%d:%d", &hour, &minute) == 2 && hour >= 0 && hour < 24 &&
                minute >= 0 && minute < 60) {
                fake_hour = hour;
                fake_minute = minute;
            }
        }
    }
    if (out == NULL) {
        return false;
    }
    if (fake_hour >= 0) {
        struct tm pinned;
        memset(&pinned, 0, sizeof(pinned));
        pinned.tm_year = 126; /* 2026, so lw_ambience_localtime()'s epoch check passes */
        pinned.tm_mon = 0;
        pinned.tm_mday = 1;
        pinned.tm_hour = fake_hour;
        pinned.tm_min = fake_minute;
        *out = pinned;
        return true;
    }
    time_t now = time(NULL);
    if (now <= 0) {
        return false;
    }
    struct tm local;
    if (localtime_s(&local, &now) != 0) {
        return false;
    }
    *out = local;
    return true;
}

static void save_path(char *buffer, size_t size, const char *key)
{
    snprintf(buffer, size, "%s\\%s.bin", s_base_dir, key);
}

static bool host_storage_read(const char *key, void *buffer, size_t *length)
{
    char path[MAX_PATH];
    save_path(path, sizeof(path), key);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    size_t read = fread(buffer, 1, *length, file);
    fclose(file);
    *length = read;
    return read > 0;
}

static bool host_storage_write(const char *key, const void *buffer, size_t length)
{
    char path[MAX_PATH];
    save_path(path, sizeof(path), key);
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    size_t written = fwrite(buffer, 1, length, file);
    fclose(file);
    return written == length;
}

static void host_log(const char *message)
{
    printf("[littleworld] %s\n", message);
    fflush(stdout);
}

static const lw_platform_t s_platform = {
    .get_time = host_get_time,
    .storage_read = host_storage_read,
    .storage_write = host_storage_write,
    .log = host_log,
};

void host_platform_init(void)
{
    const char *override=getenv("LW_DATA_DIR");
    char requested[MAX_PATH];
    if (override && override[0]) {
        if (snprintf(requested,sizeof(requested),"%s",override)>=(int)sizeof(requested)) exit(2);
    } else {
        DWORD n=GetModuleFileNameA(NULL,requested,sizeof(requested));
        if (!n || n>=sizeof(requested)) exit(2);
        char *slash=strrchr(requested,'\\'); if (!slash) exit(2);
        *slash='\0';
        size_t len=strlen(requested);
        if (len+sizeof("\\LittleWorld-data")>sizeof(requested)) exit(2);
        strcat(requested,"\\LittleWorld-data");
    }
    DWORD n=GetFullPathNameA(requested,sizeof(s_base_dir),s_base_dir,NULL);
    if (!n || n>=sizeof(s_base_dir) || !((s_base_dir[0]=='D'||s_base_dir[0]=='d') && s_base_dir[1]==':' && s_base_dir[2]=='\\')) {
        fprintf(stderr,"Simulator data must be stored on D:. Set LW_DATA_DIR to an existing-parent D-drive directory.\n");
        exit(2);
    }
    if (!CreateDirectoryA(s_base_dir,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) {
        fprintf(stderr,"Cannot create isolated simulator data directory.\n"); exit(2);
    }
    lw_platform_set(&s_platform);
}

const char *host_platform_dir(void)
{
    return s_base_dir;
}
