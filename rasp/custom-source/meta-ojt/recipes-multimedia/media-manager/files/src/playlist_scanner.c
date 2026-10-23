/*
 * playlist_scanner.c - Recursive scan for video files
 */
#define LOG_TAG "SCAN"
#include "common.h"
#include "playlist_scanner.h"

#include <string.h>
#include <strings.h>     /* strcasecmp */
#include <stdlib.h>      /* qsort */
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include <glib.h>        /* g_utf8_validate */

#define MAX_DEPTH 8      /* do not go deeper than 8 sub-folders */

static bool is_video_file(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot)
        return false;
    return strcasecmp(dot, ".mp4") == 0 || strcasecmp(dot, ".mkv") == 0;
}

static bool should_skip(const char *name)
{
    if (name[0] == '.')                                   /* ".", "..", hidden, .Trashes */
        return true;
    if (strcasecmp(name, "System Volume Information") == 0) /* Windows */
        return true;
    if (strcasecmp(name, "$RECYCLE.BIN") == 0)              /* Windows */
        return true;
    return false;
}

static void add_track(Playlist_t *pl, const char *path, const char *file_name)
{
    VideoTrack_t *t;
    char *dot;

    if (pl->count >= MAX_TRACKS) {
        LOG_W("Playlist full (%d), skip %s", MAX_TRACKS, path);
        return;
    }
    /* D-Bus strings must be valid UTF-8, otherwise sending fails */
    if (!g_utf8_validate(path, -1, NULL)) {
        LOG_W("Skip file with non UTF-8 name: %s", path);
        return;
    }

    t = &pl->tracks[pl->count];
    snprintf(t->path, sizeof(t->path), "%s", path);
    snprintf(t->title, sizeof(t->title), "%s", file_name);
    dot = strrchr(t->title, '.');
    if (dot)
        *dot = '\0';            /* "movie.mp4" -> "movie" */

    pl->count++;
}

static void scan_dir(Playlist_t *pl, const char *dir_path, int depth)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char path[MAX_PATH_LEN];

    if (depth > MAX_DEPTH)
        return;

    dir = opendir(dir_path);
    if (!dir) {
        LOG_W("Cannot open folder %s", dir_path);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (should_skip(entry->d_name))
            continue;

        if (snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(path))
            continue;           /* path too long */

        if (stat(path, &st) != 0)
            continue;

        if (S_ISDIR(st.st_mode))
            scan_dir(pl, path, depth + 1);          /* go into sub-folder */
        else if (S_ISREG(st.st_mode) && is_video_file(entry->d_name))
            add_track(pl, path, entry->d_name);
    }
    closedir(dir);
}

static int compare_title(const void *a, const void *b)
{
    const VideoTrack_t *ta = (const VideoTrack_t *)a;
    const VideoTrack_t *tb = (const VideoTrack_t *)b;
    return strcasecmp(ta->title, tb->title);
}

int playlist_scan(Playlist_t *pl, const char *dir_path)
{
    int i;

    playlist_clear(pl);
    LOG_I("Scanning %s ...", dir_path);

    scan_dir(pl, dir_path, 0);

    /* readdir() order is random -> sort by title, then give ids 0..n-1 */
    qsort(pl->tracks, pl->count, sizeof(VideoTrack_t), compare_title);
    for (i = 0; i < pl->count; i++) {
        pl->tracks[i].id = i;
        LOG_I("  [%d] %s", i, pl->tracks[i].path);
    }

    LOG_I("Found %d video file(s)", pl->count);
    return pl->count;
}

void playlist_clear(Playlist_t *pl)
{
    pl->count = 0;
}

const VideoTrack_t *playlist_get(const Playlist_t *pl, int id)
{
    if (id < 0 || id >= pl->count)
        return NULL;
    return &pl->tracks[id];
}
