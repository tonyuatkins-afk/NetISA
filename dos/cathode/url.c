/*
 * url.c - URL parsing, resolution, and encoding
 *
 * Handles scheme://host:port/path parsing, relative URL resolution
 * (7 forms), and URL encoding for GET form submission.
 */

#include "url.h"
#include <string.h>

/* Helper: case-insensitive prefix match, returns length matched or 0 */
static int prefix_match(const char *str, const char *prefix)
{
    int i = 0;
    while (prefix[i]) {
        char a = str[i];
        char b = prefix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
        i++;
    }
    return i;
}

/* Helper: safe string copy with null termination */
static void safe_copy(char *dst, const char *src, int max)
{
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

int url_parse(const char *url, url_parts_t *parts)
{
    const char *p = url;
    int len;

    memset(parts, 0, sizeof(url_parts_t));

    if (!url || !url[0]) return -1;

    /* about: scheme (no authority) */
    len = prefix_match(p, "about:");
    if (len > 0) {
        safe_copy(parts->scheme, "about", 8);
        safe_copy(parts->path, p + len, 128);
        parts->port = 0;
        return 0;
    }

    /* http:// or https:// */
    len = prefix_match(p, "https://");
    if (len > 0) {
        safe_copy(parts->scheme, "https", 8);
        parts->port = 443;
        p += len;
    } else {
        len = prefix_match(p, "http://");
        if (len > 0) {
            safe_copy(parts->scheme, "http", 8);
            parts->port = 80;
            p += len;
        } else {
            return -1;  /* unknown scheme */
        }
    }

    /* Extract host (up to ':', '/', or end) */
    {
        int i = 0;
        while (p[i] && p[i] != ':' && p[i] != '/' && i < 127) {
            parts->host[i] = p[i];
            i++;
        }
        parts->host[i] = '\0';
        p += i;
    }

    /* Optional port */
    if (*p == ':') {
        unsigned int port = 0;
        p++;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0 && port < 65536)
            parts->port = port;
    }

    /* Path (rest of URL, or "/" if empty) */
    if (*p == '/') {
        safe_copy(parts->path, p, 128);
    } else if (*p == '\0') {
        parts->path[0] = '/';
        parts->path[1] = '\0';
    } else {
        safe_copy(parts->path, p, 128);
    }

    return 0;
}

/* Append :PORT to result if base port differs from scheme default */
static void append_port_if_needed(char *result, int result_max,
                                  const url_parts_t *base)
{
    unsigned int def_port = 80;
    if (strcmp(base->scheme, "https") == 0) def_port = 443;
    if (base->port != def_port && base->port > 0) {
        char portbuf[8];
        int i = 0;
        unsigned int p = base->port;
        /* Manual int-to-string for C89 */
        char tmp[8];
        int t = 0;
        do { tmp[t++] = (char)('0' + (p % 10)); p /= 10; } while (p);
        while (t > 0) portbuf[i++] = tmp[--t];
        portbuf[i] = '\0';
        strncat(result, ":", result_max - (int)strlen(result) - 1);
        strncat(result, portbuf, result_max - (int)strlen(result) - 1);
    }
}

void url_resolve(const char *base_url, const char *relative,
                 char *result, int result_max)
{
    url_parts_t base;
    int rlen;

    if (!relative || !relative[0]) {
        safe_copy(result, base_url, result_max);
        return;
    }

    rlen = (int)strlen(relative);

    /* Absolute URL: starts with scheme:// */
    if (prefix_match(relative, "http://") ||
        prefix_match(relative, "https://") ||
        prefix_match(relative, "about:")) {
        safe_copy(result, relative, result_max);
        return;
    }

    /* Protocol-relative: //host/path */
    if (rlen >= 2 && relative[0] == '/' && relative[1] == '/') {
        url_parse(base_url, &base);
        result[0] = '\0';
        safe_copy(result, base.scheme, result_max);
        strncat(result, ":", result_max - (int)strlen(result) - 1);
        strncat(result, relative, result_max - (int)strlen(result) - 1);
        return;
    }

    /* Fragment only: #... */
    if (relative[0] == '#') {
        safe_copy(result, base_url, result_max);
        /* Strip existing fragment from base */
        {
            char *f = strchr(result, '#');
            if (f) *f = '\0';
        }
        strncat(result, relative, result_max - (int)strlen(result) - 1);
        return;
    }

    /* Parse base to get components */
    if (url_parse(base_url, &base) != 0) {
        safe_copy(result, relative, result_max);
        return;
    }

    /* Special handling for about: scheme — no host component */
    if (strcmp(base.scheme, "about") == 0) {
        const char *rel = relative;
        /* Strip leading slashes for about: URLs */
        while (*rel == '/') rel++;
        result[0] = '\0';
        safe_copy(result, "about:", result_max);
        strncat(result, rel, result_max - (int)strlen(result) - 1);
        return;
    }

    /* Absolute path: /path */
    if (relative[0] == '/') {
        result[0] = '\0';
        safe_copy(result, base.scheme, result_max);
        strncat(result, "://", result_max - (int)strlen(result) - 1);
        strncat(result, base.host, result_max - (int)strlen(result) - 1);
        append_port_if_needed(result, result_max, &base);
        strncat(result, relative, result_max - (int)strlen(result) - 1);
        return;
    }

    /* Query: ?query */
    if (relative[0] == '?') {
        char *q;
        result[0] = '\0';
        safe_copy(result, base.scheme, result_max);
        strncat(result, "://", result_max - (int)strlen(result) - 1);
        strncat(result, base.host, result_max - (int)strlen(result) - 1);
        append_port_if_needed(result, result_max, &base);
        /* Use base path up to any existing query */
        {
            char pathbuf[128];
            safe_copy(pathbuf, base.path, 128);
            q = strchr(pathbuf, '?');
            if (q) *q = '\0';
            strncat(result, pathbuf, result_max - (int)strlen(result) - 1);
        }
        strncat(result, relative, result_max - (int)strlen(result) - 1);
        return;
    }

    /* Relative path: ./name, ../name, or bare name */
    {
        char dir[128];
        const char *rel = relative;
        char *last_slash;

        /* Get directory of base path */
        safe_copy(dir, base.path, 128);
        last_slash = strrchr(dir, '/');
        if (last_slash)
            *(last_slash + 1) = '\0';
        else {
            dir[0] = '/';
            dir[1] = '\0';
        }

        /* Handle ./ prefix */
        if (rel[0] == '.' && rel[1] == '/') {
            rel += 2;
        }

        /* Handle ../ prefixes */
        while (rel[0] == '.' && rel[1] == '.' && rel[2] == '/') {
            int dlen = (int)strlen(dir);
            /* Remove trailing slash, then find previous slash */
            if (dlen > 1) {
                dir[dlen - 1] = '\0';
                last_slash = strrchr(dir, '/');
                if (last_slash)
                    *(last_slash + 1) = '\0';
                else {
                    dir[0] = '/';
                    dir[1] = '\0';
                }
            }
            rel += 3;
        }

        result[0] = '\0';
        safe_copy(result, base.scheme, result_max);
        strncat(result, "://", result_max - (int)strlen(result) - 1);
        strncat(result, base.host, result_max - (int)strlen(result) - 1);
        append_port_if_needed(result, result_max, &base);
        strncat(result, dir, result_max - (int)strlen(result) - 1);
        strncat(result, rel, result_max - (int)strlen(result) - 1);
    }
}

void url_encode(const char *input, char *output, int max_len)
{
    static const char hex[] = "0123456789ABCDEF";
    int i = 0, o = 0;

    while (input[i] && o < max_len - 3) {
        char ch = input[i];

        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            output[o++] = ch;
        } else if (ch == ' ') {
            output[o++] = '+';
        } else {
            output[o++] = '%';
            output[o++] = hex[(unsigned char)ch >> 4];
            output[o++] = hex[(unsigned char)ch & 0x0F];
        }
        i++;
    }
    output[o] = '\0';
}
