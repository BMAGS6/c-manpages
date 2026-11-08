/**
 * @file http_parser.h
 * @brief Bounded, allocation-free parsing of the HTTP subset used for previews.
 *
 * This module knows nothing about sockets, filenames, or platform APIs. A
 * caller provides one complete header block (including its terminating CRLF)
 * and receives either a validated request or a failure classification.
 * Keeping the parser pure makes malformed-input testing and fuzzing simple.
 */
#ifndef C_MANPAGES_SERVER_HTTP_PARSER_H1
#define C_MANPAGES_SERVER_HTTP_PARSER_H1 1

#include <stddef.h>

/** Maximum bytes in an entire HTTP header block, including CRLFCRLF. */
#define SERVER_REQUEST_LIMIT 8192u
/** Maximum bytes reserved for a request target, including the NUL byte. */
#define SERVER_PATH_LIMIT 512u

/**
 * @brief Minimal request representation; no pointers into the input buffer.
 *
 * The parser accepts HTTP/1.0 and HTTP/1.1, origin-form request targets,
 * and GET or HEAD. It rejects request-body framing and all folded headers.
 */
typedef struct http_request
{
    char target[SERVER_PATH_LIMIT];
    int  head_only;
    int  http_11;
} http_request_t;

/**
 * @brief Outcomes of a complete-header parse.
 *
 * The HTTP layer maps these to 200, 400, 405, or 414 responses. The parser
 * never emits network traffic and never modifies the input byte buffer.
 */
typedef enum http_parse_result
{
    HTTP_PARSE_OK = 0,
    HTTP_PARSE_BAD_REQUEST,
    HTTP_PARSE_METHOD_NOT_ALLOWED,
    HTTP_PARSE_URI_TOO_LONG
} http_parse_result_e;

/**
 * @brief Locate the CRLFCRLF marker without interpreting untrusted data as C text.
 * @param bytes Arbitrary bytes received from TCP.
 * @param length Number of initialized input bytes.
 * @return Header length ending just after CRLFCRLF, or zero if incomplete.
 * @note May be called repeatedly as a fixed receive buffer fills.
 */
size_t http_find_header_end(const unsigned char* bytes, size_t length);

/**
 * @brief Validate a complete HTTP header block and extract a supported request.
 * @param bytes Input data, not required to be NUL-terminated.
 * @param length Exact length up to and including the final CRLFCRLF.
 * @param result Initialized only on HTTP_PARSE_OK.
 * @return HTTP_PARSE_OK or a specific rejection category.
 *
 * A syntactically valid but unsupported method returns METHOD_NOT_ALLOWED.
 * Malformed methods, versions, headers and message framing return BAD_REQUEST.
 * Header names are case-insensitive, but HTTP method tokens are case-sensitive.
 */
http_parse_result_e http_parse_headers(const unsigned char* bytes,
                                       size_t length,
                                       http_request_t* result);

#endif /* C_MANPAGES_SERVER_HTTP_PARSER_H1 */
