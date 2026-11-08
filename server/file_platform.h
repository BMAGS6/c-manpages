/**
 * @file file_platform.h
 * @brief Portable read-only access to generated preview documents.
 *
 * Files are opened once and subsequently read through that same handle. On
 * POSIX, openat() with O_NOFOLLOW and fstat() prevent a check/open symlink race.
 * On Windows, CreateFileA() opens the file with OPEN_REPARSE_POINT and the
 * obtained handle is checked before any bytes are sent. No code is executed.
 *
 * The root is user-selected and trusted; children must be safe flat names.
 * A user with write access to the root can still change its contents, so the
 * server is never appropriate for publishing an untrusted file collection.
 */
#ifndef C_MANPAGES_SERVER_FILE_PLATFORM_H1
#define C_MANPAGES_SERVER_FILE_PLATFORM_H1 1

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE file_platform_handle_t;
    #define FILE_PLATFORM_INVALID INVALID_HANDLE_VALUE
#else
    typedef int file_platform_handle_t;
    #define FILE_PLATFORM_INVALID (-1)
#endif

/** Maximum path length for the trusted document root and its files. */
#define SERVER_FILESYSTEM_PATH_LIMIT 4096u

/**
 * @brief Open result, separating absent/unsafe files from true I/O failures.
 *
 * An unsafe file or symlink is intentionally reported as NOT_FOUND so that
 * HTTP clients learn nothing about the content of forbidden filesystem nodes.
 */
typedef enum file_platform_result
{
    FILE_PLATFORM_OK = 0,
    FILE_PLATFORM_NOT_FOUND,
    FILE_PLATFORM_ERROR
} file_platform_result_e;

/**
 * @brief Retained document-root state.
 *
 * Windows retains the root path for child CreateFileA() calls. POSIX retains
 * an open directory descriptor for race-resistant openat() operations.
 */
typedef struct file_platform_root
{
#ifdef _WIN32
    const char* path;
#else
    int descriptor;
#endif
} file_platform_root_t;

/**
 * @brief Validate and open the document root before opening the TCP listener.
 * @param root Receives directory state on success.
 * @param path Nonempty, trusted directory path from the command line.
 * @return 0 on success; -1 with a diagnostic error code on failure.
 */
int file_platform_open_root(file_platform_root_t* root, const char* path);

/** @brief Release the root descriptor/resources at shutdown. */
void file_platform_close_root(file_platform_root_t* root);

/**
 * @brief Open and validate a regular, non-symlink file inside the root.
 * @param name Already validated flat filename, never a path.
 * @param file Receives an opened file handle on success.
 * @param length Receives exact size to advertise as HTTP Content-Length.
 * @return Detailed open result, with outputs valid only on success.
 */
file_platform_result_e file_platform_open(const file_platform_root_t* root,
                                          const char* name,
                                          file_platform_handle_t* file,
                                          uint64_t* length);

/**
 * @brief Read at most capacity bytes from a previously opened file.
 * @return Positive count, zero at EOF, or -1 for a read error.
 * @note On Windows capacity must fit in a DWORD (the server uses 8 KiB).
 */
int file_platform_read(file_platform_handle_t file, void* buffer, size_t capacity);

/** @brief Close a file handle; return -1 if cleanup itself reports an error. */
int file_platform_close(file_platform_handle_t file);

#endif /* C_MANPAGES_SERVER_FILE_PLATFORM_H1 */
