/**
 * @file http_parser_harness.c
 * @brief Deterministic mutation/stress harness for the pure HTTP parser.
 *
 * Compile beside http_parser.c, optionally with AddressSanitizer/UBSan.
 * No network operations and no randomness from the host are required. This
 * makes failures reproducible across C99 compilers and operating systems.
 */
#include "../server/http_parser.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HARNESS_ITERATIONS 25000u
#define HARNESS_BUFFER_SIZE 640u

/**
 * @brief A deterministic xorshift32 generator for parser-input mutation.
 * @note Not cryptographic; only reproducibility and cheap byte variation matter.
 */
static uint32_t next_random(uint32_t* seed)
{
    uint32_t value = *seed;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *seed = value;
    return value;
}

/**
 * @brief Mutate valid headers, then inspect every return without buffer overrun.
 * @return Zero on success; nonzero if a parser invariant is violated.
 */
int main(void)
{
    static const unsigned char valid[] =
        "GET /c-static.7.html HTTP/1.1\r\nHost: localhost\r\n\r\n";
    unsigned char bytes[HARNESS_BUFFER_SIZE];
    http_request_t request;
    uint32_t seed = UINT32_C(0x1729a11);
    size_t iteration;

    if (http_parse_headers(valid, sizeof(valid) - 1u, &request) != HTTP_PARSE_OK ||
        strcmp(request.target, "/c-static.7.html") != 0)
        return 1;

    for (iteration = 0u; iteration < HARNESS_ITERATIONS; iteration++)
    {
        size_t length;
        size_t position;
        size_t changes;
        size_t index;
        http_parse_result_e result;

        /* Initialize the entire buffer so even enlarged mutation spans never
         * read indeterminate object representations under sanitizers.
         */
        memset(bytes, 0xa5, sizeof(bytes));
        memcpy(bytes, valid, sizeof(valid) - 1u);
        length = sizeof(valid) - 1u;
        changes = 1u + (next_random(&seed) % 24u);

        for (index = 0u; index < changes; index++)
        {
            if ((next_random(&seed) & 15u) == 0u)
                length = 1u + (next_random(&seed) % HARNESS_BUFFER_SIZE);
            position = next_random(&seed) % length;
            bytes[position] = (unsigned char)next_random(&seed);
        }

        /* Pass deliberately non-NUL-terminated byte spans to catch any
         * accidental reliance on strlen()/strstr() in parser entry points.
         */
        (void)http_find_header_end(bytes, length);
        result = http_parse_headers(bytes, length, &request);
        if (result < HTTP_PARSE_OK || result > HTTP_PARSE_URI_TOO_LONG)
        {
            (void)fprintf(stderr, "Invalid parser result on iteration %zu\n", iteration);
            return 2;
        }
    }

    return 0;
}
