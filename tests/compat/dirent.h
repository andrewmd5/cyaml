// Windows-compatible dirent.h implementation
// Uses Windows FindFirstFile/FindNextFile API

#ifndef COMPAT_DIRENT_H
#define COMPAT_DIRENT_H

#ifdef _WIN32

#include <windows.h>
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
    snprintf(search_path, sizeof(search_path), "%s\\*", path);

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

    strncpy(dir->ent.d_name, dir->ffd.cFileName, MAX_PATH - 1);
    dir->ent.d_name[MAX_PATH - 1] = '\0';
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
#include <dirent.h>
#endif

#endif // COMPAT_DIRENT_H
