// Windows-compatible dirent.h implementation
// Uses Windows FindFirstFile/FindNextFile API

#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dirent {
    char d_name[MAX_PATH];
};

typedef struct {
    HANDLE hFind;
    WIN32_FIND_DATAA ffd;
    struct dirent ent;
    int first;
} DIR;

static inline DIR* opendir(const char* path)
{
    DIR* dir = (DIR*)malloc(sizeof(DIR));
    if (!dir)
        return NULL;

    char search_path[MAX_PATH];
    _snprintf_s(search_path, sizeof(search_path), _TRUNCATE, "%s\\*", path);

    dir->hFind = FindFirstFileA(search_path, &dir->ffd);
    if (dir->hFind == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static inline struct dirent* readdir(DIR* dir)
{
    if (!dir)
        return NULL;

    if (dir->first) {
        dir->first = 0;
    } else {
        if (!FindNextFileA(dir->hFind, &dir->ffd))
            return NULL;
    }

    strncpy_s(dir->ent.d_name, MAX_PATH, dir->ffd.cFileName, _TRUNCATE);
    return &dir->ent;
}

static inline int closedir(DIR* dir)
{
    if (!dir)
        return -1;
    FindClose(dir->hFind);
    free(dir);
    return 0;
}

#else
// Unix systems have native dirent.h
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200809L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <dirent.h>
#endif

#endif // COMPAT_DIRENT_H
