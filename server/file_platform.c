/**
 * @file file_platform.c
 * @brief Secure-enough local file access without a check-then-open race.
 *
 * This server serves a *trusted* generated directory. Opening children by
 * descriptor on POSIX prevents a concurrent symlink substitution from
 * causing a file outside that directory to be read; Windows checks the
 * attributes of the already opened handle rather than a prior pathname.
 * No filesystem operation is performed on the URL text before HTTP validates
 * that the filename is one flat ASCII path component.
 */
#ifndef _WIN32
    #define _POSIX_C_SOURCE 200809L
#endif

#include "file_platform.h"

#include <limits.h>
#include <string.h>

#ifdef _WIN32
    #include <stdio.h>
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

int file_platform_open_root(file_platform_root_t* root, const char* path)
{
    if (root == NULL || path == NULL || *path == '\0')
        return -1;

#ifdef _WIN32
    {
        DWORD attributes = GetFileAttributesA(path);
        size_t length = strlen(path);

        if (length + 2u >= SERVER_FILESYSTEM_PATH_LIMIT ||
            attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return -1;
        root->path = path;
    }
#else
    {
        int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            return -1;
        root->descriptor = fd;
    }
#endif
    return 0;
}

void file_platform_close_root(file_platform_root_t* root)
{
    if (root == NULL)
        return;
#ifndef _WIN32
    if (root->descriptor >= 0)
    {
        (void)close(root->descriptor);
        root->descriptor = -1;
    }
#else
    root->path = NULL;
#endif
}

file_platform_result_e file_platform_open(const file_platform_root_t* root,
                                          const char* name,
                                          file_platform_handle_t* file,
                                          uint64_t* length)
{
    if (root == NULL || name == NULL || file == NULL || length == NULL)
        return FILE_PLATFORM_ERROR;

#ifdef _WIN32
    {
        char path[SERVER_FILESYSTEM_PATH_LIMIT];
        BY_HANDLE_FILE_INFORMATION details;
        LARGE_INTEGER size;
        HANDLE handle;
        int count = snprintf(path, sizeof(path), "%s\\%s", root->path, name);

        if (count < 0 || (size_t)count >= sizeof(path))
            return FILE_PLATFORM_ERROR;

        /* OPEN_REPARSE_POINT prevents the last component from being followed.
         * BACKUP_SEMANTICS lets us inspect directories and reject them too.
         */
        handle = CreateFileA(path,
                             GENERIC_READ,
                             FILE_SHARE_READ,
                             NULL,
                             OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                             NULL);
        if (handle == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                   error == ERROR_ACCESS_DENIED ? FILE_PLATFORM_NOT_FOUND : FILE_PLATFORM_ERROR;
        }

        if (!GetFileInformationByHandle(handle, &details) ||
            !GetFileSizeEx(handle, &size))
        {
            (void)CloseHandle(handle);
            return FILE_PLATFORM_ERROR;
        }
        if ((details.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0 ||
            GetFileType(handle) != FILE_TYPE_DISK || size.QuadPart < 0)
        {
            (void)CloseHandle(handle);
            return FILE_PLATFORM_NOT_FOUND;
        }
        *length = (uint64_t)size.QuadPart;
        *file = handle;
    }
#else
    {
        int fd = openat(root->descriptor, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        struct stat details;

        if (fd < 0)
        {
            int error = errno;
            return error == ENOENT || error == ELOOP || error == EACCES ||
                   error == ENOTDIR ? FILE_PLATFORM_NOT_FOUND : FILE_PLATFORM_ERROR;
        }
        if (fstat(fd, &details) != 0)
        {
            (void)close(fd);
            return FILE_PLATFORM_ERROR;
        }
        if (!S_ISREG(details.st_mode) || details.st_size < 0)
        {
            (void)close(fd);
            return FILE_PLATFORM_NOT_FOUND;
        }
        *length = (uint64_t)details.st_size;
        *file = fd;
    }
#endif
    return FILE_PLATFORM_OK;
}

int file_platform_read(file_platform_handle_t file, void* buffer, size_t capacity)
{
#ifdef _WIN32
    DWORD bytes_read = 0;
    if (capacity > UINT_MAX || !ReadFile(file, buffer, (DWORD)capacity, &bytes_read, NULL))
        return -1;
    return (int)bytes_read;
#else
    ssize_t count;
    do
    {
        count = read(file, buffer, capacity);
    } while (count < 0 && errno == EINTR);
    return count < 0 ? -1 : (int)count;
#endif
}

int file_platform_close(file_platform_handle_t file)
{
#ifdef _WIN32
    return CloseHandle(file) ? 0 : -1;
#else
    return close(file) == 0 ? 0 : -1;
#endif
}
