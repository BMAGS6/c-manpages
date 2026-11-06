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
 * 7. Require C99, while also supporting newer C language modes where available.
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
    /* Expose lstat() in strict ISO C99 and newer compiler modes. */
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#include "socket_platform.h"

/*
 * C99 is our portable language baseline. The selected compiler dialect—not
 * these tests—determines which ISO C features are available. __STDC_VERSION__
 * is only used here to reject older modes and report the active one.
 */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
    #error "c-manpages-serve requires ISO C99 or newer"
#endif

#if __STDC_VERSION__ >= 202311L
    #define SERVER_C_STANDARD "C23 or newer"
#elif __STDC_VERSION__ >= 201710L
    #define SERVER_C_STANDARD "C17"
#elif __STDC_VERSION__ >= 201112L
    #define SERVER_C_STANDARD "C11"
#else
    #define SERVER_C_STANDARD "C99"
#endif

/*
 * Configuration shared by startup, path validation and response generation.
 * Keep these limits centralized so changing an I/O buffer does not require
 * hunting down separate numeric literals in unrelated functions.
 *
 * Each value describes the allocated byte capacity, not the length of a
 * string that fits inside it (which must also reserve one byte for '\0').
 */
#define SERVER_DEFAULT_PORT             8000u
#define SERVER_DEFAULT_ROOT             "build/html"
#define SERVER_INDEX_FILENAME           "index.html"
#define SERVER_FALLBACK_FILENAME        "c-static.7.html"
#define SERVER_REQUEST_LIMIT            8192u
#define SERVER_PATH_LIMIT                512u
#define SERVER_RESPONSE_HEADER_LIMIT    1024u
#define SERVER_FILESYSTEM_PATH_LIMIT    4096u
#define SERVER_FILE_BUFFER              8192u
#define SERVER_TIMEOUT_SECONDS             5u

/**
 * @brief Explicit numeric codes for the HTTP responses this server emits.
 *
 * The protocol assigns these numbers: their values are not arbitrary tuning
 * parameters. Named constants prevent accidentally using the wrong status
 * while keeping wire-format output unchanged.
 */
typedef enum http_status
{
    HTTP_STATUS_OK                    = 200,
    HTTP_STATUS_BAD_REQUEST           = 400,
    HTTP_STATUS_FORBIDDEN             = 403,
    HTTP_STATUS_NOT_FOUND             = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED    = 405,
    HTTP_STATUS_URI_TOO_LONG          = 414,
    HTTP_STATUS_HEADERS_TOO_LARGE     = 431,
    HTTP_STATUS_INTERNAL_ERROR        = 500
} http_status_e;

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
    char header[SERVER_RESPONSE_HEADER_LIMIT];
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
    const char* extra = status == HTTP_STATUS_METHOD_NOT_ALLOWED ? "Allow: GET, HEAD\r\n" : "";

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
 * @brief Convert a decimal string into a valid TCP port.
 *
 * @details
 * The input must contain one or more ASCII decimal digits, with no
 * leading or trailing whitespace, signs, or other characters.
 *
 * The conversion uses strtoul() to detect arithmetic overflow
 * independently of the width of unsigned long.
 *
 * @param text Null-terminated string containing the port number.
 * @param result Receives the validated port on success.
 *
 * @return 0 on success, or -1 if the input is invalid.
 *
 * @note The output parameter is not modified when validation fails.
 * @note NULL input and output pointers are rejected without dereferencing.
 */
static int parse_port(const char* text, uint16_t* result)
{
    const unsigned char* cursor;
    char* end = NULL;
    unsigned long value;
    int conversion_error;

    if (text == NULL || result == NULL || *text == '\0')
        return -1;

    cursor = (const unsigned char*)text;

    /* Reject characters that strtoul() might otherwise accept. */
    for (; *cursor != '\0'; cursor++)
        if (*cursor < '0' || *cursor > '9')
            return -1;

    /*
     * Clear any earlier error before conversion, then capture errno
     * immediately so later library calls cannot overwrite our result.
     */
    errno = 0;
    value = strtoul(text, &end, 10);
    conversion_error = errno;

    if (conversion_error != 0 ||
        *end  != '\0'         ||
        value == 0u           ||
        value > UINT16_MAX)
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
        (void)strcpy(filename, SERVER_INDEX_FILENAME);
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
    char path[SERVER_FILESYSTEM_PATH_LIMIT];
    unsigned char buffer[SERVER_FILE_BUFFER];
    const char* type = mime_type_for(filename);
    FILE* file;
    long  length;
    int   count;

    if (type == NULL)
    {
        send_error(client, HTTP_STATUS_FORBIDDEN, "Forbidden", head_only);
        return HTTP_STATUS_FORBIDDEN;
    }

    count = snprintf(path, sizeof(path), "%s/%s", config->root, filename);
    if (count < 0 || (size_t)count >= sizeof(path))
    {
        send_error(client, HTTP_STATUS_URI_TOO_LONG, "URI Too Long", head_only);
        return HTTP_STATUS_URI_TOO_LONG;
    }

    if (!is_safe_regular_file(path))
    {
        send_error(client, HTTP_STATUS_NOT_FOUND, "Not Found", head_only);
        return HTTP_STATUS_NOT_FOUND;
    }

    file = fopen(path, "rb");
    if (file == NULL)
    {
        send_error(client, HTTP_STATUS_NOT_FOUND, "Not Found", head_only);
        return HTTP_STATUS_NOT_FOUND;
    }

    /* fseek/ftell is adequate for these small, generated documentation files.
     * Files larger than LONG_MAX are deliberately not a supported use case.
     */
    if (fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0 ||
        fseek(file, 0L, SEEK_SET) != 0)
    {
        (void)fclose(file);
        send_error(client, HTTP_STATUS_INTERNAL_ERROR, "Internal Server Error", head_only);
        return HTTP_STATUS_INTERNAL_ERROR;
    }

    if (send_headers(client, HTTP_STATUS_OK, "OK", type, length, "") != 0)
    {
        (void)fclose(file);
        return HTTP_STATUS_OK;
    }

    if (!head_only)
    {
        size_t received;

        while ((received = fread(buffer, 1u, sizeof(buffer), file)) > 0u)
            if (send_all(client, buffer, received) != 0)
                break;
    }

    (void)fclose(file);
    return HTTP_STATUS_OK;
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
        send_error(client, parsed == -2 ? HTTP_STATUS_HEADERS_TOO_LARGE : HTTP_STATUS_BAD_REQUEST,
                   parsed == -2 ? "Request Header Fields Too Large" : "Bad Request", 0);
        return;
    }

    line_end = strstr(request, "\r\n");
    if (line_end == NULL)
    {
        send_error(client, HTTP_STATUS_BAD_REQUEST, "Bad Request", 0);
        return;
    }

    *line_end = '\0';
    parsed = sscanf(request, "%15s %511s %15s %1s", method, target, version, extra);
    if (parsed != 3 ||
        (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0))
    {
        send_error(client, HTTP_STATUS_BAD_REQUEST, "Bad Request", 0);
        return;
    }

    head_only = strcmp(method, "HEAD") == 0;
    if (strcmp(method, "GET") != 0 && !head_only)
    {
        send_error(client, HTTP_STATUS_METHOD_NOT_ALLOWED, "Method Not Allowed", 0);
        return;
    }

    if (decode_target(target, filename) != 0)
    {
        send_error(client, HTTP_STATUS_BAD_REQUEST, "Bad Request", head_only);
        return;
    }

    if (strcmp(filename, SERVER_INDEX_FILENAME) == 0)
    {
        /* Until Phase C creates an index, '/' opens the static manual. */
        char candidate[SERVER_FILESYSTEM_PATH_LIMIT];
        int count = snprintf(candidate, sizeof(candidate), "%s/" SERVER_INDEX_FILENAME, config->root);

        if (count < 0 || (size_t)count >= sizeof(candidate) ||
            !is_safe_regular_file(candidate))
        {
            (void)strcpy(filename, SERVER_FALLBACK_FILENAME);
        }
    }

    result = serve_file(client, config, filename, head_only);
    printf("%s /%s -> %u\n", method, filename, result);
    fflush(stdout);
}

/**
 * @brief Print command-line syntax and the configured default values.
 * @param stream stdout for --help, stderr for invalid arguments.
 * @param program Program name as passed through argv[0].
 *
 * Using an explicit stream keeps argument errors on stderr while normal
 * informational output remains on stdout.
 */
static void print_usage(FILE* stream, const char* program)
{
    /*
     * Both the displayed defaults and actual configuration come from the
     * same constants. Invalid input is explained on stderr; --help is sent
     * to stdout so it behaves naturally in shell scripts.
     */
    (void)fprintf(stream,
                  "Usage: %s [--root DIRECTORY] [--port 1-%u]\n"
                  "Local-only C Manual Pages HTTP preview server.\n"
                  "Defaults: --root %s --port %u\n",
                  program,
                  (unsigned int)UINT16_MAX,
                  SERVER_DEFAULT_ROOT,
                  SERVER_DEFAULT_PORT);
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
        .root = SERVER_DEFAULT_ROOT,
        .port = SERVER_DEFAULT_PORT
    };
    socket_platform_socket_t listener;
    int argument;

    for (argument = 1; argument < argc; argument++)
    {
        if (strcmp(argv[argument], "--help") == 0)
        {
            print_usage(stdout, argv[0]);
            return EXIT_SUCCESS;
        }

        if (strcmp(argv[argument], "--root") == 0 &&
            argument + 1 < argc                   &&
            argv[argument + 1][0] != '\0')
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
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (socket_platform_initialize() != 0)
    {
        fprintf(stderr, "Could not initialize the networking library.\n");
        return EXIT_FAILURE;
    }

#ifndef _WIN32
    /*
     * Browsers can disconnect in the middle of a send. Ignoring SIGPIPE means
     * send() reports EPIPE instead of abruptly killing the whole process.
     */
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


server_main_loop:
    {
        socket_platform_socket_t client = socket_platform_accept(listener);

        if (client == SOCKET_PLATFORM_INVALID)
        {
            if (socket_platform_was_interrupted())
                goto server_main_loop;

            fprintf(stderr, "Accept failed (socket error %d).\n",
                    socket_platform_last_error());
            goto fail;
        }

        handle_client(client, &config);
        socket_platform_close(client);
        goto server_main_loop;
    }

fail:
    socket_platform_close(listener);
    socket_platform_shutdown();
    return EXIT_FAILURE;
}
