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

int socket_platform_was_interrupted(void)
{
#ifdef _WIN32
    return socket_platform_last_error() == WSAEINTR;
#else
    return socket_platform_last_error() == EINTR;
#endif
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
            socket_platform_close(listener);
            return SOCKET_PLATFORM_INVALID;
        }
    }
#else
    {
        int reuse = 1;

        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
        {
            socket_platform_close(listener);
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
        socket_platform_close(listener);
        return SOCKET_PLATFORM_INVALID;
    }

    return listener;
}

socket_platform_socket_t socket_platform_accept(socket_platform_socket_t listener)
{
    return accept(listener, NULL, NULL);
}

int socket_platform_set_timeouts(socket_platform_socket_t client, unsigned int seconds)
{
#ifdef _WIN32
    /* Winsock uses a DWORD millisecond timeout for SO_RCVTIMEO/SO_SNDTIMEO. */
    DWORD milliseconds = (DWORD)seconds * SOCKET_PLATFORM_MILLISECONDS_PER_SECOND;
    const char* value  = (const char*)&milliseconds;
    int length         = (int)sizeof(milliseconds);
#else
    /* POSIX sockets use a timeval, not the Winsock millisecond integer. */
    struct timeval timeout = {0};
    timeout.tv_sec         = (time_t)seconds;
    timeout.tv_usec        = 0;
    const void* value      = &timeout;
    socklen_t length       = (socklen_t)sizeof(timeout);
#endif

    if (setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, value, length) != 0 ||
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, value, length) != 0)
    {
        return -1;
    }

    return 0;
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
