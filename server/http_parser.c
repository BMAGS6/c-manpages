/**
 * @file http_parser.c
 * @brief Strict HTTP/1.x request parser with no allocation or string scanning.
 *
 * RFC 9112 permits recipients to be more forgiving about some legacy syntax.
 * We deliberately choose the strict interpretation to keep one small request
 * format unambiguous. This is a direct localhost origin server, not a proxy:
 * it accepts no request bodies, persistent requests, or absolute-form URLs.
 */
#include "http_parser.h"

#include <string.h>

/** @brief Identify one ASCII HTTP token character (RFC 9110's tchar). */
static int is_token_char(unsigned char value)
{
    if ((value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9'))
        return 1;

    return value != 0u && strchr("!#$%&'*+-.^_`|~", (int)value) != NULL;
}

/** @brief ASCII case folding without locale-dependent ctype() behavior. */
static unsigned char ascii_lower(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

/** @brief Compare a bounded field name against an ASCII spelling. */
static int equals_ascii(const unsigned char* begin, size_t length, const char* text)
{
    size_t index;

    if (strlen(text) != length)
        return 0;

    for (index = 0; index < length; index++)
        if (ascii_lower(begin[index]) != (unsigned char)text[index])
            return 0;

    return 1;
}

/**
 * @brief Validate a local Host authority without resolving names or routing.
 *
 * We accept a nonempty ASCII reg-name or bracketed IP literal, optionally
 * followed by a decimal port. The request is still served only on loopback;
 * the value is never used as a filesystem path or a redirect destination.
 * We reject userinfo, commas, whitespace, URI delimiters, and malformed ports.
 */
static int valid_host(const unsigned char* value, size_t length)
{
    size_t index = 0;
    size_t end = length;

    while (index < end && (value[index] == ' ' || value[index] == '\t'))
        index++;
    while (end > index && (value[end - 1u] == ' ' || value[end - 1u] == '\t'))
        end--;
    if (index == end)
        return 0;

    if (value[index] == '[')
    {
        size_t start = ++index;
        while (index < end && value[index] != ']')
        {
            unsigned char c = value[index++];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F') || c == ':' || c == '.'))
                return 0;
        }
        if (index == start || index == end)
            return 0;
        index++; /* closing bracket */
    }
    else
    {
        size_t start = index;
        while (index < end && value[index] != ':')
        {
            unsigned char c = value[index++];
            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '.' || c == '-'))
                return 0;
        }
        if (index == start)
            return 0;
    }

    if (index < end)
    {
        if (value[index++] != ':')
            return 0;
        /* An empty port is valid URI authority syntax; a nonempty one is decimal. */
        while (index < end)
        {
            if (value[index] < '0' || value[index] > '9')
                return 0;
            index++;
        }
    }
    return 1;
}

/**
 * @brief Find the next CRLF within the supplied bounded byte sequence.
 * @param begin Offset where a new line begins.
 * @return Index of the CR, or length for an invalid/missing line ending.
 */
static size_t line_end_at(const unsigned char* bytes, size_t begin, size_t length)
{
    size_t index;

    for (index = begin; index + 1u < length; index++)
    {
        if (bytes[index] == '\r' && bytes[index + 1u] == '\n')
            return index;
        if (bytes[index] == '\r' || bytes[index] == '\n' || bytes[index] == '\0')
            return length;
    }
    return length;
}

size_t http_find_header_end(const unsigned char* bytes, size_t length)
{
    size_t index;

    if (bytes == NULL || length < 4u)
        return 0;

    for (index = 0; index + 3u < length; index++)
        if (bytes[index] == '\r' && bytes[index + 1u] == '\n' &&
            bytes[index + 2u] == '\r' && bytes[index + 3u] == '\n')
            return index + 4u;

    return 0;
}

http_parse_result_e http_parse_headers(const unsigned char* bytes,
                                       size_t length,
                                       http_request_t* result)
{
    size_t line_end;
    size_t first_space;
    size_t second_space;
    size_t cursor;
    size_t host_count = 0;
    http_request_t parsed = {{0}, 0, 0};
    int unsupported_method;

    if (bytes == NULL || result == NULL || length < 4u ||
        length > SERVER_REQUEST_LIMIT || http_find_header_end(bytes, length) != length)
        return HTTP_PARSE_BAD_REQUEST;

    line_end = line_end_at(bytes, 0u, length);
    if (line_end == length)
        return HTTP_PARSE_BAD_REQUEST;

    /* Request-line = method SP request-target SP HTTP-version CRLF. */
    for (first_space = 0u; first_space < line_end && bytes[first_space] != ' '; first_space++)
        if (!is_token_char(bytes[first_space]))
            return HTTP_PARSE_BAD_REQUEST;
    if (first_space == 0u || first_space == line_end)
        return HTTP_PARSE_BAD_REQUEST;

    for (second_space = first_space + 1u;
         second_space < line_end && bytes[second_space] != ' ';
         second_space++)
        if (bytes[second_space] < 0x21u || bytes[second_space] > 0x7eu)
            return HTTP_PARSE_BAD_REQUEST;
    if (second_space == first_space + 1u || second_space == line_end ||
        second_space + 1u >= line_end)
        return HTTP_PARSE_BAD_REQUEST;

    if (second_space - first_space >= SERVER_PATH_LIMIT)
        return HTTP_PARSE_URI_TOO_LONG;

    if (line_end - (second_space + 1u) == 8u &&
        memcmp(bytes + second_space + 1u, "HTTP/1.1", 8u) == 0)
        parsed.http_11 = 1;
    else if (line_end - (second_space + 1u) != 8u ||
             memcmp(bytes + second_space + 1u, "HTTP/1.0", 8u) != 0)
        return HTTP_PARSE_BAD_REQUEST;

    parsed.head_only = first_space == 4u && memcmp(bytes, "HEAD", 4u) == 0;
    unsupported_method = !parsed.head_only &&
                         !(first_space == 3u && memcmp(bytes, "GET", 3u) == 0);

    memcpy(parsed.target, bytes + first_space + 1u, second_space - first_space - 1u);
    parsed.target[second_space - first_space - 1u] = '\0';

    /* Every header has exactly one colon, token field-name, and valid value.
     * In particular, a line beginning with SP/HTAB (obs-fold) is rejected.
     */
    cursor = line_end + 2u;
    while (cursor < length - 2u)
    {
        size_t field_end = line_end_at(bytes, cursor, length);
        size_t colon = cursor;
        size_t index;

        if (field_end == length || field_end == cursor)
            return HTTP_PARSE_BAD_REQUEST;
        while (colon < field_end && bytes[colon] != ':')
        {
            if (!is_token_char(bytes[colon]))
                return HTTP_PARSE_BAD_REQUEST;
            colon++;
        }
        if (colon == cursor || colon == field_end)
            return HTTP_PARSE_BAD_REQUEST;

        for (index = colon + 1u; index < field_end; index++)
        {
            unsigned char value = bytes[index];
            if ((value < 0x20u && value != '\t') || value == 0x7fu)
                return HTTP_PARSE_BAD_REQUEST;
        }

        if (equals_ascii(bytes + cursor, colon - cursor, "host"))
        {
            host_count++;
            if (host_count != 1u ||
                !valid_host(bytes + colon + 1u, field_end - colon - 1u))
                return HTTP_PARSE_BAD_REQUEST;
        }
        else if (equals_ascii(bytes + cursor, colon - cursor, "content-length") ||
                 equals_ascii(bytes + cursor, colon - cursor, "transfer-encoding"))
        {
            /* No request bodies: disallow ambiguous message framing entirely. */
            return HTTP_PARSE_BAD_REQUEST;
        }

        cursor = field_end + 2u;
    }

    if (cursor != length - 2u || (parsed.http_11 && host_count != 1u))
        return HTTP_PARSE_BAD_REQUEST;

    if (unsupported_method)
        return HTTP_PARSE_METHOD_NOT_ALLOWED;

    *result = parsed;
    return HTTP_PARSE_OK;
}
