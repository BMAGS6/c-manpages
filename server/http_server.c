/**
 * @file http_server.c
 * @brief Small, single-threaded, read-only HTTP documentation preview server.
 *
 * Design contract
 * ---------------
 * - Bind to IPv4 loopback only: no remote service, TLS, CGI, or uploads.
 * - Sleep in blocking accept() while idle; never spin or poll unnecessarily.
 * - Handle one HTTP/1.0 or HTTP/1.1 GET/HEAD request per TCP connection.
 * - Parse raw bytes strictly before interpreting any request field as text.
 * - Accept only flat, ASCII filenames from an author-controlled output root.
 * - Open the actual file handle without following symbolic/reparse links.
 * - Bound header size, total header-receive time, and individual socket I/O.
 * - Report HTTP status separately from whether the transfer completed.
 * - Remain valid ISO C99 while isolating POSIX and Windows calls in adapters.
 *
 * This is purpose-built for browsing generated, trusted C reference pages.
 * It is not an internet-facing or multi-user HTTP server.
 */
#ifndef _WIN32
    /* POSIX signals and socket implementations need feature-test macros. */
    #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <string.h>

#include "file_platform.h"
#include "http_parser.h"
#include "socket_platform.h"

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
 * Named configuration is intentional: these are engineering decisions rather
 * than wire-protocol values. Keep all limits in one place and explain units.
 */
#define SERVER_DEFAULT_PORT             8000u
#define SERVER_DEFAULT_ROOT             "build/html"
#define SERVER_INDEX_FILENAME           "index.html"
#define SERVER_FALLBACK_FILENAME        "c-static.7.html"
#define SERVER_RESPONSE_HEADER_LIMIT    1024u
#define SERVER_FILE_BUFFER              8192u
#define SERVER_IO_TIMEOUT_MS            5000u
#define SERVER_HEADER_DEADLINE_MS       5000u

/** @brief Numeric HTTP statuses; never confuse them with transfer completion. */
typedef enum http_status
{
    HTTP_STATUS_OK                    = 200,
    HTTP_STATUS_BAD_REQUEST           = 400,
    HTTP_STATUS_FORBIDDEN             = 403,
    HTTP_STATUS_NOT_FOUND             = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED    = 405,
    HTTP_STATUS_REQUEST_TIMEOUT       = 408,
    HTTP_STATUS_URI_TOO_LONG          = 414,
    HTTP_STATUS_HEADERS_TOO_LARGE     = 431,
    HTTP_STATUS_INTERNAL_ERROR        = 500
} http_status_e;

/** @brief Runtime options; document-root resources are opened separately. */
typedef struct server_config
{
    const char* root;
    uint16_t port;
    file_platform_root_t documents;
} server_config_t;

/**
 * @brief Result of serving a file after a request has been validated.
 *
 * status is the HTTP response status chosen. completed indicates whether
 * all promised bytes *and headers* were transmitted successfully. Once
 * headers have left the socket, a later read/send failure cannot change the
 * HTTP status; the only honest response is to close and log the truncation.
 */
typedef struct http_transfer_result
{
    uint64_t     expected_bytes;
    uint64_t     bytes_sent;
    const char*  failure_stage; /* Trusted static diagnostic; never user text. */
    unsigned int status;
    int          completed;
} http_transfer_result_t;

/**
 * @brief Fully transmit a buffer without assuming TCP send() is atomic.
 * @param sent_bytes Optional counter updated after every successful partial send.
 * @return 0 on complete transmission, -1 if send fails or disconnects.
 *
 * This loop is *not* busy waiting: socket_platform_send() blocks when the
 * kernel send queue cannot accept data and has a finite operation timeout.
 */
static int send_all(socket_platform_socket_t client,
                    const void* data,
                    size_t size,
                    uint64_t* sent_bytes)
{
    const unsigned char* bytes = data;
    size_t sent = 0;

    while (sent < size)
    {
        size_t remaining = size - sent;
        int    count     = (int)(remaining > (size_t)INT_MAX ? INT_MAX : remaining);
        int    result    = socket_platform_send(client, bytes + sent, count);

        if (result < 0 && socket_platform_is_interrupted(socket_platform_last_error()))
            continue;
        if (result <= 0)
            return -1;

        sent += (size_t)result;
        if (sent_bytes != NULL)
            *sent_bytes += (uint64_t)result;
    }
    return 0;
}

/**
 * @brief Construct and transmit a self-delimited HTTP response header.
 * @param length Exact GET body length; also advertised on HEAD responses.
 * @param extra_headers Trusted CRLF-terminated literal headers, or "".
 * @return 0 on success, -1 on formatting or socket failure.
 */
static int send_headers(socket_platform_socket_t client,
                        unsigned int status,
                        const char* reason,
                        const char* content_type,
                        uint64_t length,
                        const char* extra_headers)
{
    char header[SERVER_RESPONSE_HEADER_LIMIT];
    int count = snprintf(header,
                         sizeof(header),
                         "HTTP/1.1 %u %s\r\n"
                         "Content-Type: %s\r\n"
                         "Content-Length: %" PRIu64 "\r\n"
                         "Connection: close\r\n"
                         "Cache-Control: no-store\r\n"
                         "X-Content-Type-Options: nosniff\r\n"
                         "%s"
                         "\r\n",
                         status,
                         reason,
                         content_type,
                         length,
                         extra_headers);

    if (count < 0 || (size_t)count >= sizeof(header))
        return -1;
    return send_all(client, header, (size_t)count, NULL);
}

/**
 * @brief Emit a small fixed error response without reflecting request text.
 * @return 0 on complete response, -1 if the peer disconnected or send failed.
 * @note HEAD errors carry GET's Content-Length but omit the response body.
 */
static int send_error(socket_platform_socket_t client,
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
                     sizeof(body) - 1u,
                     extra) != 0)
        return -1;

    return head_only ? 0 : send_all(client, body, sizeof(body) - 1u, NULL);
}

/**
 * @brief Outcomes while reading the request header from an untrusted peer.
 *
 * The timeout result includes both SO_RCVTIMEO expiration and our total
 * monotonic deadline. Incomplete/closed requests are syntactically invalid.
 */
typedef enum request_read_result
{
    REQUEST_READ_OK = 0,
    REQUEST_READ_BAD,
    REQUEST_READ_TOO_LARGE,
    REQUEST_READ_TIMEOUT,
    REQUEST_READ_IO_ERROR
} request_read_result_e;

/**
 * @brief Receive until CRLFCRLF, bounded by both bytes and elapsed time.
 *
 * The socket has a per-operation timeout, but a client could otherwise send
 * one byte just before every timeout and monopolize this sequential server.
 * Before every recv, we therefore recompute the remaining time against a
 * monotonic clock and set SO_RCVTIMEO accordingly. No sleeping/polling loop
 * is introduced. Extra bytes after CRLFCRLF are discarded when we close the
 * connection: request bodies and pipelining are intentionally unsupported.
 */
static request_read_result_e read_request(socket_platform_socket_t client,
                                          unsigned char request[SERVER_REQUEST_LIMIT],
                                          size_t* header_size)
{
    size_t used = 0;
    uint64_t started;

    if (socket_platform_monotonic_ms(&started) != 0)
        return REQUEST_READ_IO_ERROR;

    while (used < SERVER_REQUEST_LIMIT)
    {
        uint64_t now;
        uint64_t elapsed;
        unsigned int remaining;
        int count;
        size_t end;
        size_t scan_start = used > 3u ? used - 3u : 0u;

        if (socket_platform_monotonic_ms(&now) != 0)
            return REQUEST_READ_IO_ERROR;

        elapsed = now >= started ? now - started : 0u;
        if (elapsed >= SERVER_HEADER_DEADLINE_MS)
            return REQUEST_READ_TIMEOUT;

        remaining = (unsigned int)(SERVER_HEADER_DEADLINE_MS - elapsed);
        if (remaining > SERVER_IO_TIMEOUT_MS)
            remaining = SERVER_IO_TIMEOUT_MS;

        if (socket_platform_set_receive_timeout(client, remaining) != 0)
            return REQUEST_READ_IO_ERROR;

        count = socket_platform_receive(client,
                                        request + used,
                                        (int)(SERVER_REQUEST_LIMIT - used));
        if (count == 0)
            return REQUEST_READ_BAD;
        if (count < 0)
        {
            int error_code = socket_platform_last_error();
            if (socket_platform_is_interrupted(error_code))
                continue;
            /* Preserve a genuine socket failure rather than disguising it as a timeout. */
            return socket_platform_is_timeout(error_code) ?
                   REQUEST_READ_TIMEOUT : REQUEST_READ_IO_ERROR;
        }

        used += (size_t)count;
        /* Only inspect the new bytes plus three from the previous read.
         * This keeps adversarial one-byte receives linear, not quadratic,
         * while detecting CRLFCRLF split across TCP receive boundaries.
         */
        end = http_find_header_end(request + scan_start, used - scan_start);
        if (end != 0u)
        {
            *header_size = scan_start + end;
            return REQUEST_READ_OK;
        }
    }

    return REQUEST_READ_TOO_LARGE;
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
 * @brief Choose the MIME type of a validated, regular file by its extension.
 * @return Trusted literal, or NULL if the extension is unsupported.
 * @note Unknown extensions are denied even if the filesystem file exists.
 */
static const char* mime_type_for(const char* filename)
{
    const char* extension = strrchr(filename, '.');

    static const struct mime_entry
    {
        const char* suffix;
        const char* type;
    } types[] =
    {
        {".html", "text/html; charset=utf-8"},
        {".htm",  "text/html; charset=utf-8"},
        {".css",  "text/css; charset=utf-8"},
        {".js",   "text/javascript; charset=utf-8"},
        {".svg",  "image/svg+xml"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".webp", "image/webp"},
        {".ico",  "image/x-icon"},
        {".txt",  "text/plain; charset=utf-8"}
    };

    size_t index;

    if (extension == NULL)
        return NULL;

    for (index = 0u; index < sizeof(types) / sizeof(types[0]); index++)
        if (strcmp(extension, types[index].suffix) == 0)
            return types[index].type;

    return NULL;
}

/**
 * @brief Copy a strict origin-form target into a safe, flat filename.
 * @param target NUL-terminated, validated HTTP target.
 * @param filename Receives one ASCII filename; never a parent path.
 * @param root_requested Receives whether the browser requested '/' itself.
 * @return 0 on success; -1 on invalid or unsupported target syntax.
 *
 * We do not decode percent escapes and do not support directories. The
 * narrow grammar prevents alternate path interpretations on Unix/Windows,
 * including encoded '/', '\\', path segments, NUL bytes and drive names.
 */
static int decode_target(const char* target,
                         char filename[SERVER_PATH_LIMIT],
                         int* root_requested)
{
    const char* cursor;
    size_t index = 0u;

    if (target[0] != '/')
        return -1;

    cursor = target + 1;
    *root_requested = *cursor == '\0' || *cursor == '?';
    if (*root_requested)
    {
        (void)strcpy(filename, SERVER_INDEX_FILENAME);
        return 0;
    }

    while (*cursor != '\0' && *cursor != '?')
    {
        unsigned char c = (unsigned char)*cursor++;
        int valid = (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                     c == '.' || c == '-' || c == '_';

        if (!valid || index + 1u >= SERVER_PATH_LIMIT)
            return -1;
        filename[index++] = (char)c;
    }

    filename[index] = '\0';
    if (index == 0u || filename[0] == '.')
        return -1;

    /* Fragments are never transmitted in HTTP request targets. */
    for (; *cursor != '\0'; cursor++)
        if (*cursor == '#')
            return -1;

    return 0;
}

/**
 * @brief Transfer a validated static file and report status and completion.
 *
 * The file handle is obtained before sending headers. The first read is also
 * performed before HTTP 200, allowing an immediately unreadable file to yield
 * HTTP 500. Mid-stream errors cannot change an already-sent response: they
 * are detected, recorded, and result in a closed (truncated) connection.
 *
 * @param filename Mutable filename for '/' fallback to c-static.7.html.
 * @param root_requested Only the root URL may use the old-page fallback.
 */
static http_transfer_result_t serve_file(socket_platform_socket_t client,
                                         const server_config_t* config,
                                         char filename[SERVER_PATH_LIMIT],
                                         int head_only,
                                         int root_requested)
{
    http_transfer_result_t result =
    {
        .status         = HTTP_STATUS_INTERNAL_ERROR,
        .expected_bytes = 0u,
        .bytes_sent     = 0u,
        .completed      = 0,
        .failure_stage  = NULL
    };
    const char*            type   = mime_type_for(filename);
    file_platform_handle_t file   = FILE_PLATFORM_INVALID;
    file_platform_result_e opened;

    unsigned char buffer[SERVER_FILE_BUFFER];
    int first_read = 0;

    if (type == NULL)
    {
        result.status = HTTP_STATUS_FORBIDDEN;
        result.completed = send_error(client, result.status, "Forbidden", head_only) == 0;
        return result;
    }

    opened = file_platform_open(&config->documents, filename, &file, &result.expected_bytes);
    if (opened == FILE_PLATFORM_NOT_FOUND && root_requested)
    {
        /* Only an actual '/' request may fall back. /index.html returns 404. */
        (void)strcpy(filename, SERVER_FALLBACK_FILENAME);
        type = mime_type_for(filename);
        opened = file_platform_open(&config->documents, filename, &file, &result.expected_bytes);
    }
    if (opened != FILE_PLATFORM_OK)
    {
        result.status = opened == FILE_PLATFORM_NOT_FOUND ?
                        HTTP_STATUS_NOT_FOUND : HTTP_STATUS_INTERNAL_ERROR;
        result.completed = send_error(client,
                                      result.status,
                                      result.status == HTTP_STATUS_NOT_FOUND ? "Not Found"
                                                                             : "Internal Server Error",
                                      head_only) == 0;
        return result;
    }

    /* Check the first read before committing to success on the wire. */
    if (!head_only && result.expected_bytes > 0u)
    {
        first_read = file_platform_read(file, buffer, sizeof(buffer));
        if (first_read <= 0)
        {
            (void)file_platform_close(file);
            result.status = HTTP_STATUS_INTERNAL_ERROR;
            result.completed = send_error(client,
                                          result.status,
                                          "Internal Server Error",
                                          head_only) == 0;
            return result;
        }
    }

    result.status = HTTP_STATUS_OK;
    if (send_headers(client, result.status, "OK", type, result.expected_bytes, "") != 0)
    {
        result.failure_stage = "HTTP header send";
        goto cleanup;
    }

    if (!head_only)
    {
        uint64_t remaining = result.expected_bytes;
        int count = first_read;

        while (remaining > 0u)
        {
            size_t transmit;

            if (count <= 0)
            {
                result.failure_stage = count < 0 ? "file read" : "premature file EOF";
                break;
            }

            transmit = (uint64_t)count > remaining ? (size_t)remaining : (size_t)count;
            if (send_all(client, buffer, transmit, &result.bytes_sent) != 0)
            {
                result.failure_stage = "body send";
                break;
            }
            remaining -= (uint64_t)transmit;
            if (remaining == 0u)
                break;

            count = file_platform_read(file, buffer, sizeof(buffer));
        }
        if (remaining != 0u)
            goto cleanup;
    }

    result.completed = 1;

cleanup:
    if (file_platform_close(file) != 0)
    {
        result.completed = 0;
        result.failure_stage = "file close";
    }
    return result;
}

/**
 * @brief Process a single accepted connection, never reusing it for requests.
 *
 * All network inputs are parsed as bytes before the validated request target
 * reaches decode_target(). Malformed requests receive fixed error text with
 * a connection close. A failed response write is recorded as incomplete,
 * never silently reported as a successful file transfer.
 */
static void handle_client(socket_platform_socket_t client, const server_config_t* config)
{
    unsigned char received[SERVER_REQUEST_LIMIT];
    size_t header_size = 0;
    char filename[SERVER_PATH_LIMIT];
    http_request_t request;
    http_parse_result_e parsed;
    request_read_result_e read_result;
    http_transfer_result_t transfer;
    int root_requested = 0;

    if (socket_platform_set_timeouts(client,
                                     SERVER_IO_TIMEOUT_MS,
                                     SERVER_IO_TIMEOUT_MS) != 0)
    {
        (void)fprintf(stderr, "Could not configure client socket timeouts.\n");
        return;
    }

    read_result = read_request(client, received, &header_size);
    if (read_result != REQUEST_READ_OK)
    {
        unsigned int status = HTTP_STATUS_BAD_REQUEST;
        const char* reason = "Bad Request";

        if (read_result == REQUEST_READ_TOO_LARGE)
        {
            status = HTTP_STATUS_HEADERS_TOO_LARGE;
            reason = "Request Header Fields Too Large";
        }
        else if (read_result == REQUEST_READ_TIMEOUT)
        {
            status = HTTP_STATUS_REQUEST_TIMEOUT;
            reason = "Request Timeout";
        }
        else if (read_result == REQUEST_READ_IO_ERROR)
        {
            status = HTTP_STATUS_INTERNAL_ERROR;
            reason = "Internal Server Error";
        }
        (void)send_error(client, status, reason, 0);
        return;
    }

    parsed = http_parse_headers(received, header_size, &request);
    if (parsed != HTTP_PARSE_OK)
    {
        unsigned int status = parsed == HTTP_PARSE_METHOD_NOT_ALLOWED ?
                              HTTP_STATUS_METHOD_NOT_ALLOWED :
                              parsed == HTTP_PARSE_URI_TOO_LONG ?
                                  HTTP_STATUS_URI_TOO_LONG : HTTP_STATUS_BAD_REQUEST;
        const char* reason = status == HTTP_STATUS_METHOD_NOT_ALLOWED ? "Method Not Allowed" :
                             status == HTTP_STATUS_URI_TOO_LONG ? "URI Too Long" : "Bad Request";
        (void)send_error(client, status, reason, 0);
        return;
    }

    if (decode_target(request.target, filename, &root_requested) != 0)
    {
        (void)send_error(client, HTTP_STATUS_BAD_REQUEST, "Bad Request", request.head_only);
        return;
    }

    transfer = serve_file(client, config, filename, request.head_only, root_requested);
    (void)printf("%s /%s -> %u (%" PRIu64 "/%" PRIu64 " bytes%s%s%s)\n",
                 request.head_only ? "HEAD" : "GET",
                 filename,
                 transfer.status,
                 transfer.bytes_sent,
                 transfer.expected_bytes,
                 transfer.completed ? "" : ", incomplete",
                 transfer.failure_stage == NULL ? "" : ": ",
                 transfer.failure_stage == NULL ? "" : transfer.failure_stage);
    (void)fflush(stdout);
}

/**
 * @brief Print CLI syntax and derived default values to the requested stream.
 * @note Errors go to stderr, while --help goes to stdout.
 */
static void print_usage(FILE* stream, const char* program)
{
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
 * @brief Configure a trusted root and run the sequential, blocking listener.
 *
 * The document root is opened *before* any network listener exists, so a
 * broken path does not create a server that just replies with 404 forever.
 * Shutdown on Ctrl+C is process termination; the OS releases outstanding
 * descriptors. On runtime errors the explicit cleanup path releases them.
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
            argument + 1 < argc && argv[argument + 1][0] != '\0')
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
        (void)fprintf(stderr, "Invalid argument: %s\n", argv[argument]);
        print_usage(stderr, argv[0]);
        return EXIT_FAILURE;
    }

    if (file_platform_open_root(&config.documents, config.root) != 0)
    {
        (void)fprintf(stderr, "Cannot open document root: %s\n", config.root);
        return EXIT_FAILURE;
    }

    if (socket_platform_initialize() != 0)
    {
        (void)fprintf(stderr, "Could not initialize networking.\n");
        file_platform_close_root(&config.documents);
        return EXIT_FAILURE;
    }

#ifndef _WIN32
    /* A disconnected browser should give send() EPIPE, not terminate the server. */
    (void)signal(SIGPIPE, SIG_IGN);
#endif

    listener = socket_platform_listen(config.port);
    if (listener == SOCKET_PLATFORM_INVALID)
    {
        int error_code = socket_platform_last_error();
        (void)fprintf(stderr,
                      "Cannot listen on 127.0.0.1:%u (socket error %d).\n",
                      (unsigned int)config.port,
                      error_code);
        socket_platform_shutdown();
        file_platform_close_root(&config.documents);
        return EXIT_FAILURE;
    }

    (void)printf("C Manual Pages -- Local Documentation Server (%s)\n"
                 "Serving: %s\n"
                 "Address: http://127.0.0.1:%u/\n"
                 "Press Ctrl+C to stop.\n",
                 SERVER_C_STANDARD,
                 config.root,
                 (unsigned int)config.port);
    (void)fflush(stdout);

    for (;;)
    {
        socket_platform_socket_t client = socket_platform_accept(listener);
        if (client == SOCKET_PLATFORM_INVALID)
        {
            int error_code = socket_platform_last_error();
            if (socket_platform_is_interrupted(error_code))
                continue;
            (void)fprintf(stderr, "Accept failed (socket error %d).\n", error_code);
            break;
        }
        handle_client(client, &config);
        socket_platform_close(client);
    }

    socket_platform_close(listener);
    socket_platform_shutdown();
    file_platform_close_root(&config.documents);
    return EXIT_FAILURE;
}
