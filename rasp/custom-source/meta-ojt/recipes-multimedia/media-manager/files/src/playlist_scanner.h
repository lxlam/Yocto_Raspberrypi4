/*
 * playlist_scanner.h - Find .mp4 / .mkv files in a folder (and sub-folders)
 */
#ifndef PLAYLIST_SCANNER_H
#define PLAYLIST_SCANNER_H

#define MAX_TRACKS    256
#define MAX_PATH_LEN  512

typedef struct {
    int  id;                    /* 0, 1, 2 ... (index in the list) */
    char title[128];            /* file name without extension     */
    char path[MAX_PATH_LEN];    /* full path, e.g. /media/usb0/a.mp4 */
} VideoTrack_t;

typedef struct {
    VideoTrack_t tracks[MAX_TRACKS];   /* fixed array: simple, no malloc */
    int          count;
} Playlist_t;

/* Clear the list, scan dir_path recursively, sort by title. Returns count. */
int  playlist_scan(Playlist_t *pl, const char *dir_path);

void playlist_clear(Playlist_t *pl);

/* Returns NULL if id is out of range */
const VideoTrack_t *playlist_get(const Playlist_t *pl, int id);

#endif /* PLAYLIST_SCANNER_H */
