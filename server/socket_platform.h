/**
 * @file socket_platform.h
 * @brief Small, blocking TCP socket interface for POSIX and Windows.
 *
 * The HTTP implementation should not need to know whether a socket is a POSIX
 * file descriptor or a Winsock SOCKET. This header deliberately exposes only
 * operations required by our single-threaded documentation preview server.
 *
 * The type definitions and native socket headers are selected at compile time
 * using platform macros; no nonstandard C language extensions are required.
 */
#ifndef C_MANPAGES_SERVER_SOCKET_PLATFORM_H1
#define C_MANPAGES_SERVER_SOCKET_PLATFORM_H1 1

#include <stdint.h>

#ifdef _WIN32
/* winsock2.h must precede windows.h to avoid old Winsock declarations. */
#   include <winsock2.h>
#   include <ws2tcpip.h>

    typedef SOCKET socket_platform_socket_t;
#   define SOCKET_PLATFORM_INVALID INVALID_SOCKET
#else
    typedef int socket_platform_socket_t;
#   define SOCKET_PLATFORM_INVALID (-1)
#endif

#ifdef __cplusplus
    extern "C"{
#endif

/**
 * @brief Initialize the platform networking library.
 * @return 0 on success; -1 on failure.
 * @note POSIX does not require explicit socket initialization. Windows does.
 */
int socket_platform_initialize(void);


/**
 * @brief Release networking-library resources after the listener is closed.
 */
void socket_platform_shutdown(void);


/**
 * @brief Create a blocking IPv4 TCP listener bound to 127.0.0.1.
 * @param port TCP port in host byte order.
 * @return Listening socket, or SOCKET_PLATFORM_INVALID on error.
 * @note Binding only to loopback is an intentional security constraint.
 */
socket_platform_socket_t socket_platform_listen(uint16_t port);


/**
 * @brief Wait for one connection, allowing the OS to sleep until it arrives.
 * @param listener Previously opened listening socket.
 * @return Accepted socket, or SOCKET_PLATFORM_INVALID on error.
 * @note This is a blocking call, not a spin loop or timed polling operation.
 */
socket_platform_socket_t socket_platform_accept(socket_platform_socket_t listener);


/**
 * @brief Bound the duration of blocking client receive and send operations.
 * @param client Connected socket to configure.
 * @param seconds Positive per-operation timeout in whole seconds.
 * @return 0 on success; -1 on failure.
 * @note These timeouts do not apply to the listening socket's accept call.
 */
int socket_platform_set_timeouts(socket_platform_socket_t client, unsigned int seconds);


/**
 * @brief Receive at most count bytes from a connected TCP socket.
 * @return Number of bytes received, 0 at EOF, or -1 on socket error.
 * @note The caller only passes buffers small enough to fit in an int.
 */
int socket_platform_receive(socket_platform_socket_t client, void* buffer, int count);


/**
 * @brief Send at most count bytes over a connected TCP socket.
 * @return Number of bytes sent, or -1 on socket error.
 * @note TCP can send fewer bytes than requested; callers must retry.
 */
int socket_platform_send(socket_platform_socket_t client, const void* buffer, int count);


/**
 * @brief Close a socket on either supported platform.
 */
void socket_platform_close(socket_platform_socket_t socket);


/**
 * @brief Obtain the current thread's last platform socket error number.
 */
int socket_platform_last_error(void);


/**
 * @brief Check whether a failed socket call was interrupted by a signal.
 * @return Nonzero if it can safely be retried.
 */
int socket_platform_was_interrupted(void);


#ifdef __cplusplus
    }   /* extern "C" */
#endif
#endif /* C_MANPAGES_SERVER_SOCKET_PLATFORM_H1 */
