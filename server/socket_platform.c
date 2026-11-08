/**
 * @file socket_platform.c
 * @brief Minimal POSIX/Winsock adaptation for blocking local HTTP service.
 *
 * Use blocking I/O on purpose: accept() sleeps while the server is idle, and
 * recv()/send() sleep until socket operations can make progress. The kernel
 * handles scheduling. There are no periodic wakeups, worker threads, or
 * CPU-intensive loops when the server has no clients.
 */
#ifndef _WIN32
    /* Expose POSIX socket and timeval APIs on strict C99 and newer language modes. */
    #define _POSIX_C_SOURCE 200809L
#endif

#include "socket_platform.h"

#ifdef _WIN32
    #include <windows.h>
#else
    #include <errno.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <time.h>
    #include <netinet/in.h>
    #include <unistd.h>
#endif

/* A small loopback-only preview server does not need a large accept queue. */
#define SOCKET_PLATFORM_LISTEN_BACKLOG 8

/* Winsock expresses socket timeouts in milliseconds, not seconds. */
#define SOCKET_PLATFORM_MILLISECONDS_PER_SECOND 1000u


int socket_platform_initialize(void)
{
#ifdef _WIN32
    WSADATA data;

    return WSAStartup(MAKEWORD(2, 2), &data) == 0 ? 0 : -1;
#else
    return 0;
#endif
}

void socket_platform_shutdown(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int socket_platform_last_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    int err = errno;
    return err;
#endif
}

int socket_platform_is_interrupted(int error_code)
{
#ifdef _WIN32
    return error_code == WSAEINTR;
#else
    return error_code == EINTR;
#endif
}

int socket_platform_is_timeout(int error_code)
{
#ifdef _WIN32
    return error_code == WSAETIMEDOUT || error_code == WSAEWOULDBLOCK;
#else
    return error_code == EAGAIN || error_code == EWOULDBLOCK || error_code == ETIMEDOUT;
#endif
}

/**
 * @brief Restore an error saved before cleaning up an unsuccessful operation.
 *
 * closesocket()/close() may change the last-error value. Reinstating the
 * original code makes diagnostics from the caller identify the real failure.
 */
static void restore_socket_error(int error_code)
{
#ifdef _WIN32
    WSASetLastError(error_code);
#else
    errno = error_code;
#endif
}

int socket_platform_monotonic_ms(uint64_t* milliseconds)
{
    if (milliseconds == NULL)
        return -1;
#ifdef _WIN32
    *milliseconds = (uint64_t)GetTickCount64();
#else
    {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return -1;
        *milliseconds = (uint64_t)now.tv_sec * 1000u +
                        (uint64_t)now.tv_nsec / 1000000u;
    }
#endif
    return 0;
}

void socket_platform_close(socket_platform_socket_t socket)
{
    if (socket == SOCKET_PLATFORM_INVALID)
        return;

#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

socket_platform_socket_t socket_platform_listen(uint16_t port)
{
    socket_platform_socket_t listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in address = {0};

    if (listener == SOCKET_PLATFORM_INVALID)
        return SOCKET_PLATFORM_INVALID;

    /*
     * A normal POSIX restart should not wait for old TCP connections to expire.
     * On Windows, reserve the port exclusively instead of using SO_REUSEADDR,
     * which has different (and potentially surprising) semantics on Winsock.
     */
#ifdef _WIN32
    {
        BOOL exclusive = TRUE;

        if (setsockopt(listener,
                       SOL_SOCKET,
                       SO_EXCLUSIVEADDRUSE,
                       (const char*)&exclusive,
                       (int)sizeof(exclusive)) != 0)
        {
            int error_code = socket_platform_last_error();
            socket_platform_close(listener);
            restore_socket_error(error_code);
            return SOCKET_PLATFORM_INVALID;
        }
    }
#else
    {
        int reuse = 1;

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
        {
            int error_code = socket_platform_last_error();
            socket_platform_close(listener);
            restore_socket_error(error_code);
            return SOCKET_PLATFORM_INVALID;
        }
    }
#endif

    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port        = htons(port);

    if (bind(listener, (const struct sockaddr*)&address, sizeof(address)) != 0 ||
        listen(listener, SOCKET_PLATFORM_LISTEN_BACKLOG) != 0)
    {
        int error_code = socket_platform_last_error();
        socket_platform_close(listener);
        restore_socket_error(error_code);
        return SOCKET_PLATFORM_INVALID;
    }

    return listener;
}

socket_platform_socket_t socket_platform_accept(socket_platform_socket_t listener)
{
    return accept(listener, NULL, NULL);
}

/**
 * @brief Configure one timeout without allowing zero (which means "forever").
 *
 * POSIX SO_*TIMEO uses timeval while Winsock expects milliseconds as DWORD.
 * The unsigned millisecond interface is intentionally narrow: a preview
 * connection has only short waits, never days-long values.
 */
static int set_timeout(socket_platform_socket_t client, int option, unsigned int ms)
{
    if (ms == 0u)
        return -1;
#ifdef _WIN32
    {
        DWORD timeout = (DWORD)ms;
        return setsockopt(client, SOL_SOCKET, option,
                          (const char*)&timeout, (int)sizeof(timeout));
    }
#else
    {
        struct timeval timeout;
        timeout.tv_sec = (time_t)(ms / SOCKET_PLATFORM_MILLISECONDS_PER_SECOND);
        timeout.tv_usec = (suseconds_t)((ms % SOCKET_PLATFORM_MILLISECONDS_PER_SECOND) * 1000u);
        return setsockopt(client, SOL_SOCKET, option, &timeout, (socklen_t)sizeof(timeout));
    }
#endif
}

int socket_platform_set_receive_timeout(socket_platform_socket_t client,
                                        unsigned int milliseconds)
{
    return set_timeout(client, SO_RCVTIMEO, milliseconds);
}

int socket_platform_set_timeouts(socket_platform_socket_t client,
                                 unsigned int receive_ms,
                                 unsigned int send_ms)
{
    if (socket_platform_set_receive_timeout(client, receive_ms) != 0)
        return -1;
    return set_timeout(client, SO_SNDTIMEO, send_ms);
}

int socket_platform_receive(socket_platform_socket_t client, void* buffer, int count)
{
#ifdef _WIN32
    return recv(client, (char*)buffer, count, 0);
#else
    return (int)recv(client, buffer, (size_t)count, 0);
#endif
}

int socket_platform_send(socket_platform_socket_t client, const void* buffer, int count)
{
#ifdef _WIN32
    return send(client, (const char*)buffer, count, 0);
#else
    return (int)send(client, buffer, (size_t)count, 0);
#endif
}
