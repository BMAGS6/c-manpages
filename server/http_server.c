/**
 * @file http_server.c
 * @brief Small, single-threaded HTTP/1.1 preview server for C Manual Pages.
 *
 * Design goals
 * ------------
 * 1. Serve only generated, flat files from a trusted build/html directory.
 * 2. Bind to IPv4 loopback (127.0.0.1), never to every network interface.
 * 3. Sleep inside blocking accept() while no browser is connected.
 * 4. Serve one GET or HEAD request per connection, then close that connection.
 * 5. Reject URL encoding, path separators and nonportable filenames rather
 *    than attempting complex, error-prone path normalization.
 * 6. Use a fixed request buffer, fixed file buffer and bounded socket waits.
 * 7. Stay source-compatible with C17 and C23 on POSIX and Windows.
 *
 * This is a *development preview utility*, not a general-purpose production
 * HTTP daemon. In particular it has no TLS, authentication, persistent
 * connections, concurrency, reverse-proxy support or directory listings.
 *
 * The generated documentation currently uses flat c-*.7.html filenames and
 * embeds its stylesheet, making flat-file-only serving sufficient. The future
 * site-wide index.html will work without changing the server.
 */
#ifndef _WIN32
    /* Request lstat() declarations in a strict C17 compilation environment. */
    #define _POSIX_C_SOURCE 200809L
#endif

#include "socket_platform.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201710L
    #error "c-manpages-serve requires ISO C17 or a newer C standard"
#endif

/* Compile in either -std=c17 or -std=c23; no C23 feature is mandatory. */
#if __STDC_VERSION__ >= 202311L
    #define SERVER_C_STANDARD "C23 or newer"
#else
    #define SERVER_C_STANDARD "C17"
#endif

#define SERVER_DEFAULT_PORT    8000u
#define SERVER_REQUEST_LIMIT   8192u
#define SERVER_PATH_LIMIT       512u
#define SERVER_FILE_BUFFER     8192u
#define SERVER_TIMEOUT_SECONDS    5u

/**
 * @brief Save the server configuration after command-line validation.
 *
 * A directory is trusted as an author-controlled build output: the server
 * additionally validates that every requested filename is a single, safe
 * component and declines symlinks/reparse points as a defense in depth.
 */
typedef struct server_config
{
    const char* root;
    uint16_t    port;
}
server_config_t;

/**
 * @brief Send an entire byte sequence over a potentially partial TCP stream.
 * @param client Connected socket.
 * @param data Bytes to transmit.
 * @param size Number of bytes to transmit.
 * @return 0 for a complete send; -1 for disconnect, timeout or other error.
 *
 * send() is allowed to write fewer bytes than requested. The loop advances
 * only when it actually sends data, and waits in a blocking socket call when
 * the TCP send buffer cannot accept more. It is therefore not busy polling.
 */
static int send_all(socket_platform_socket_t client, const void* data, size_t size)
{
    const unsigned char* bytes = data;
    size_t sent = 0;

    while (sent < size)
    {
        size_t remaining = size - sent;
        int count        = (int)(remaining > (size_t)INT_MAX ? INT_MAX : remaining);
        int result       = socket_platform_send(client, bytes + sent, count);

        if (result <= 0)
            return -1;

        sent += (size_t)result;
    }

    return 0;
}

/**
 * @brief Send a complete response header without a response body.
 * @param status Standard HTTP numeric status code.
 * @param description Short standard reason phrase.
 * @param content_type MIME type including charset, if applicable.
 * @param length Decimal length of the body that a GET request would return.
 * @param extra_headers Optional CRLF-terminated trusted header string.
 * @return 0 if sent completely, -1 otherwise.
 */
static int send_headers(socket_platform_socket_t client,
                        unsigned int status,
                        const char* description,
                        const char* content_type,
                        long length,
                        const char* extra_headers)
{
    char header[1024];
    int count = snprintf(header,
                         sizeof(header),
                         "HTTP/1.1 %u %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %ld\r\n"
                         "Connection: close\r\n"
                         "Cache-Control: no-store\r\n"
                         "X-Content-Type-Options: nosniff\r\n"
                         "%s"
                         "\r\n",
                         status,
                         description,
                         content_type,
                         length,
                         extra_headers);

    if (count < 0 || (size_t)count >= sizeof(header))
        return -1;

    return send_all(client, header, (size_t)count);
}

/**
 * @brief Return a small plain-text error page, without reflecting user input.
 * @param head_only Suppress the body for an HTTP HEAD request.
 *
 * The error string comes exclusively from our own constants, not a request
 * path, preventing untrusted request text from reaching the HTML response.
 */
static void send_error(socket_platform_socket_t client,
                       unsigned int status,
                       const char* reason,
                       int head_only)
{
    static const char body[] = "C Manual Pages: request could not be served.\n";
    const char* extra = status == 405u ? "Allow: GET, HEAD\r\n" : "";

    if (send_headers(client,
                     status,
                     reason,
                     "text/plain; charset=utf-8",
                     (long)(sizeof(body) - 1u),
                     extra) != 0)
    {
        return;
    }

    if (!head_only)
        (void)send_all(client, body, sizeof(body) - 1u);
}

/**
 * @brief Receive one bounded HTTP request header block.
 * @return 0 on success, -1 for EOF/error, -2 when request headers are too big.
 *
 * We only support one request per connection. A browser normally sends the
 * entire header block immediately; the receive timeout prevents a connected
 * but silent client from monopolizing this single-threaded server forever.
 */
static int read_request(socket_platform_socket_t client, char* request, size_t capacity)
{
    size_t used = 0;

    while (used + 1u < capacity)
    {
        int count = socket_platform_receive(client,
                                            request + used,
                                            (int)(capacity - used - 1u));
        if (count <= 0)
            return -1;

        used += (size_t)count;
        request[used] = '\0';

        if (strstr(request, "\r\n\r\n") != NULL)
            return 0;
    }

    return -2;
}

/**
 * @brief Parse a decimal TCP port in the supported range [1, 65535].
 * @return 0 on success, -1 on invalid input.
 */
static int parse_port(const char* text, uint16_t* result)
{
    char* end = NULL;
    unsigned long value;

    if (text[0] == '\0' || text[0] == '-' || text[0] == '+')
        return -1;

    errno = 0;
    value = strtoul(text, &end, 10);
    int err = errno;

    if (err != 0 || *end != '\0' || value == 0u || value > 65535u)
    {
        return -1;
    }

    *result = (uint16_t)value;
    return 0;
}

/**
 * @brief Decide the MIME type based on a trusted, validated flat filename.
 * @note Unknown extensions are not served, avoiding accidental file exposure.
 */
static const char* mime_type_for(const char* filename)
{
    const char* extension = strrchr(filename, '.');

    if (extension == NULL)
        return NULL;

    if (strcmp(extension, ".html") == 0 || strcmp(extension, ".htm") == 0)
        return "text/html; charset=utf-8";

    if (strcmp(extension, ".css") == 0)
        return "text/css; charset=utf-8";

    if (strcmp(extension, ".js") == 0)
        return "text/javascript; charset=utf-8";

    if (strcmp(extension, ".svg") == 0)
        return "image/svg+xml";

    if (strcmp(extension, ".png") == 0)
        return "image/png";

    if (strcmp(extension, ".jpg") == 0 || strcmp(extension, ".jpeg") == 0)
        return "image/jpeg";

    if (strcmp(extension, ".webp") == 0)
        return "image/webp";

    if (strcmp(extension, ".ico") == 0)
        return "image/x-icon";

    if (strcmp(extension, ".txt") == 0)
        return "text/plain; charset=utf-8";

    return NULL;
}

/**
 * @brief Extract one safe filename from an HTTP origin-form request target.
 * @param target Target beginning with /, optionally followed by ?query.
 * @param filename Output array of at least SERVER_PATH_LIMIT bytes.
 * @return 0 on success, -1 if path syntax is unsafe or unsupported.
 *
 * Only flat ASCII filenames consisting of letters, digits, dot, underscore
 * and hyphen are supported. Slashes, backslashes, percent encoding, control
 * characters and dot-prefixed names are rejected. We intentionally don't
 * decode URL escapes: accepting encoded slashes would otherwise introduce
 * a second, easily mishandled path interpretation.
 */
static int decode_target(const char* target, char filename[SERVER_PATH_LIMIT])
{
    size_t index = 0;
    const char* cursor;

    if (target[0] != '/')
        return -1;

    cursor = target + 1;
    if (*cursor == '\0' || *cursor == '?')
    {
        /* Phase C will generate index.html; fall back to the first manual. */
        (void)strcpy(filename, "index.html");
        return 0;
    }

    for (; *cursor != '\0' && *cursor != '?'; cursor++)
    {
        unsigned char character = (unsigned char)*cursor;
        int valid = (character >= 'a' && character <= 'z') ||
                    (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') ||
                     character == '.' || character == '-' || character == '_';

        if (!valid || index + 1u >= SERVER_PATH_LIMIT)
            return -1;

        filename[index++] = (char)character;
    }

    filename[index] = '\0';
    if (index == 0u || filename[0] == '.')
        return -1;

    return 0;
}

/**
 * @brief Check the requested file is ordinary and not a symbolic link.
 * @return Nonzero for an ordinary file, zero otherwise.
 *
 * This additional check is not meant to guarantee safety against a malicious
 * actor racing file replacement inside the trusted build directory. We do
 * not expose this development server to untrusted networks.
 */
static int is_safe_regular_file(const char* path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
#else
    struct stat details;

    return lstat(path, &details) == 0 && S_ISREG(details.st_mode);
#endif
}

/**
 * @brief Stream a single generated file, or produce the relevant HTTP error.
 * @param filename Validated flat filename, never a caller-provided path.
 * @param head_only Nonzero for HEAD, where only headers are written.
 * @return HTTP status for diagnostic logging.
 */
static unsigned int serve_file(socket_platform_socket_t client,
                               const server_config_t* config,
                               const char* filename,
                               int head_only)
{
    char path[4096];
    unsigned char buffer[SERVER_FILE_BUFFER];
    const char* type = mime_type_for(filename);
    FILE* file;
    long  length;
    int   count;

    if (type == NULL)
    {
        send_error(client, 403u, "Forbidden", head_only);
        return 403u;
    }

    count = snprintf(path, sizeof(path), "%s/%s", config->root, filename);
    if (count < 0 || (size_t)count >= sizeof(path))
    {
        send_error(client, 414u, "URI Too Long", head_only);
        return 414u;
    }

    if (!is_safe_regular_file(path))
    {
        send_error(client, 404u, "Not Found", head_only);
        return 404u;
    }

    file = fopen(path, "rb");
    if (file == NULL)
    {
        send_error(client, 404u, "Not Found", head_only);
        return 404u;
    }

    /* fseek/ftell is adequate for these small, generated documentation files.
     * Files larger than LONG_MAX are deliberately not a supported use case.
     */
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        fseek(file, 0L, SEEK_SET) != 0)
    {
        (void)fclose(file);
        send_error(client, 500u, "Internal Server Error", head_only);
        return 500u;
    }

    if (send_headers(client, 200u, "OK", type, length, "") != 0)
    {
        (void)fclose(file);
        return 200u;
    }

    if (!head_only)
    {
        size_t received;

        while ((received = fread(buffer, 1u, sizeof(buffer), file)) > 0u)
            if (send_all(client, buffer, received) != 0)
                break;
    }

    (void)fclose(file);
    return 200u;
}

/**
 * @brief Process exactly one HTTP request from the accepted connection.
 *
 * The deliberately limited parser recognizes HTTP/1.0 and HTTP/1.1 request
 * lines, ignores request headers, and always returns Connection: close. No
 * request bodies are read or processed. This is sufficient for static browser
 * previews and avoids the complexity of HTTP request framing and pipelining.
 */
static void handle_client(socket_platform_socket_t client, const server_config_t* config)
{
    char  request[SERVER_REQUEST_LIMIT];
    char  method[16];
    char  target[SERVER_PATH_LIMIT];
    char  version[16];
    char  extra[2];
    char  filename[SERVER_PATH_LIMIT];
    char* line_end;
    int   parsed;
    int   head_only;
    unsigned int result;

    if (socket_platform_set_timeouts(client, SERVER_TIMEOUT_SECONDS) != 0)
        return;

    parsed = read_request(client, request, sizeof(request));
    if (parsed != 0)
    {
        send_error(client, parsed == -2 ? 431u : 400u,
                   parsed == -2 ? "Request Header Fields Too Large" : "Bad Request", 0);
        return;
    }

    line_end = strstr(request, "\r\n");
    if (line_end == NULL)
    {
        send_error(client, 400u, "Bad Request", 0);
        return;
    }

    *line_end = '\0';
    parsed = sscanf(request, "%15s %511s %15s %1s", method, target, version, extra);
    if (parsed != 3 ||
        (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0))
    {
        send_error(client, 400u, "Bad Request", 0);
        return;
    }

    head_only = strcmp(method, "HEAD") == 0;
    if (strcmp(method, "GET") != 0 && !head_only)
    {
        send_error(client, 405u, "Method Not Allowed", 0);
        return;
    }

    if (decode_target(target, filename) != 0)
    {
        send_error(client, 400u, "Bad Request", head_only);
        return;
    }

    if (strcmp(filename, "index.html") == 0)
    {
        /* Until Phase C creates an index, '/' opens the static manual. */
        char candidate[4096];
        int count = snprintf(candidate, sizeof(candidate), "%s/index.html", config->root);

        if (count < 0 || (size_t)count >= sizeof(candidate) ||
            !is_safe_regular_file(candidate))
        {
            (void)strcpy(filename, "c-static.7.html");
        }
    }

    result = serve_file(client, config, filename, head_only);
    printf("%s /%s -> %u\n", method, filename, result);
    fflush(stdout);
}

/**
 * @brief Explain the available command-line options.
 */
static void print_usage(const char* program)
{
    printf("Usage: %s [--root DIRECTORY] [--port 1-65535]\n"
           "Local-only C Manual Pages HTTP preview server.\n"
           "Defaults: --root build/html --port 8000\n",
           program);
}

/**
 * @brief Start the server and block waiting for local HTTP requests.
 * @return EXIT_SUCCESS on normal exit or EXIT_FAILURE on configuration errors.
 *
 * There is deliberately no select/poll loop: accept() blocks inside the OS
 * until a client connects. Connections are handled sequentially with blocking
 * I/O and bounded per-operation waits. CPU use should be near zero at idle.
 */
int main(int argc, char* argv[])
{
    server_config_t config =
    {
        .root = "build/html",
        .port = SERVER_DEFAULT_PORT
    };
    socket_platform_socket_t listener;
    int argument;

    for (argument = 1; argument < argc; argument++)
    {
        if (strcmp(argv[argument], "--help") == 0)
        {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[argument], "--root") == 0 && argument + 1 < argc)
        {
            config.root = argv[++argument];
            continue;
        }
        if (strcmp(argv[argument], "--port") == 0 && argument + 1 < argc &&
            parse_port(argv[argument + 1], &config.port) == 0)
        {
            argument++;
            continue;
        }

        fprintf(stderr, "Invalid argument: %s\n", argv[argument]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (socket_platform_initialize() != 0)
    {
        fprintf(stderr, "Could not initialize the networking library.\n");
        return EXIT_FAILURE;
    }

#ifndef _WIN32
    /* Browsers can disconnect in the middle of a send. Ignoring SIGPIPE means
     * send() reports EPIPE instead of abruptly killing the whole process. */
    (void)signal(SIGPIPE, SIG_IGN);
#endif

    listener = socket_platform_listen(config.port);
    if (listener == SOCKET_PLATFORM_INVALID)
    {
        fprintf(stderr, "Cannot listen on 127.0.0.1:%u (socket error %d).\n",
                (unsigned int)config.port,
                socket_platform_last_error());
        socket_platform_shutdown();
        return EXIT_FAILURE;
    }

    printf("C Manual Pages -- Local Documentation Server (%s)\n"
           "Serving: %s\n"
           "Address: http://127.0.0.1:%u/\n"
           "Press Ctrl+C to stop.\n",
           SERVER_C_STANDARD,
           config.root,
           (unsigned int)config.port);
    fflush(stdout);


    for (;;)
    {
        socket_platform_socket_t client = socket_platform_accept(listener);

        if (client == SOCKET_PLATFORM_INVALID)
        {
            if (socket_platform_was_interrupted())
                continue;

            fprintf(stderr, "Accept failed (socket error %d).\n",
                    socket_platform_last_error());
            break;
        }

        handle_client(client, &config);
        socket_platform_close(client);
    }

    socket_platform_close(listener);
    socket_platform_shutdown();
    return EXIT_FAILURE;
}
