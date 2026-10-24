/* Little World - Windows host platform implementation (storage, clock, log).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "lw/lw_platform.h"

static char s_base_dir[MAX_PATH];

static bool host_get_time(struct tm *out)
{
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
    const char *appdata = getenv("LOCALAPPDATA");
    if (appdata != NULL && appdata[0] != '\0') {
        snprintf(s_base_dir, sizeof(s_base_dir), "%s\\LittleWorld", appdata);
    } else {
        snprintf(s_base_dir, sizeof(s_base_dir), ".");
    }
    CreateDirectoryA(s_base_dir, NULL);
    lw_platform_set(&s_platform);
}

const char *host_platform_dir(void)
{
    return s_base_dir;
}
