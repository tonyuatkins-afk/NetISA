/*
 * fetch.c - HTTP/1.0 fetch layer
 *
 * Routes about: URLs to stub pages, http/https to NetISA.
 * HTTP/1.0 with Connection: close avoids chunked encoding.
 */

#include "fetch.h"
#include "stub_pages.h"
#include "screen.h"
#include "netisa.h"
#include "url.h"
#include <string.h>
#include <stdio.h>
#include <dos.h>

/* Case-insensitive prefix comparison, returns 0 if match */
static int strnicmp_local(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 1;
        if (ca == '\0') return 0;
    }
    return 0;
}

/* Spinner for loading animation */
static const char spinner_chars[] = "|/-\\";
static int spinner_idx = 0;

static void update_spinner(void)
{
    scr_putc(1, 24, spinner_chars[spinner_idx & 3], ATTR_STATUS);
    spinner_idx++;
}

/* Read BIOS tick counter (18.2 Hz) */
static unsigned long get_ticks(void)
{
    return *(unsigned long far *)MK_FP(0x0040, 0x006C);
}

int fetch_page(const char *url, fetch_cb_t callback,
               void *userdata, fetch_state_t *state)
{
    url_parts_t parts;
    const char far *stub_html;

    memset(state, 0, sizeof(fetch_state_t));
    state->content_length = -1;

    if (url_parse(url, &parts) != 0)
        return FETCH_ERR_HTTP;

    /* === File-based demo: load cached HTML from html\ directory === */
    /* Checked BEFORE about: handler so about:npr etc. work */
    {
        char fpath[80];
        FILE *fp;

        /* Map known demo URLs to local files */
        fpath[0] = '\0';
        if (strstr(url, "text.npr.org") || strcmp(url, "about:npr") == 0)
            strcpy(fpath, "HTML\\NPR.HTM");
        else if (strstr(url, "man.openbsd.org") || strcmp(url, "about:bsd") == 0)
            strcpy(fpath, "HTML\\OPENBSD.HTM");
        else if (strstr(url, "example.com") || strcmp(url, "about:example") == 0)
            strcpy(fpath, "HTML\\EXAMPLE.HTM");
        else if (strstr(url, "barelybooting.com") || strcmp(url, "about:bb") == 0)
            strcpy(fpath, "HTML\\BARELY.HTM");

        if (fpath[0]) {
            fp = fopen(fpath, "rb");
            if (fp) {
                char buf[512];
                int n;
                update_spinner();
                while ((n = (int)fread(buf, 1, 512, fp)) > 0) {
                    callback((const char far *)buf, n, userdata);
                    state->bytes_received += n;
                    update_spinner();
                }
                fclose(fp);
                return FETCH_OK;
            }
        }
        /* No match — fall through to about: handler or NetISA */
    }

    /* === about: URLs — route to stub pages === */
    if (strcmp(parts.scheme, "about") == 0) {
        stub_html = stub_get_html(url);
        if (stub_html) {
            unsigned int len = 0;
            const char far *p = stub_html;
            while (p[len]) len++;
            callback(stub_html, (int)len, userdata);
            return FETCH_OK;
        }
        return FETCH_ERR_HTTP;
    }

    /* === No NetISA hardware in stub build — show error page === */
    {
        static const char far err_html[] =
            "<html><head><title>Cannot Load Page</title></head><body>"
            "<h1>Cannot Load Page</h1>"
            "<p>Cathode could not fetch this URL.</p>"
            "<p>In this stub build, only cached demo pages and "
            "<b>about:</b> pages are available. "
            "Live HTTPS fetching requires the NetISA card.</p>"
            "<hr>"
            "<p><a href=\"about:home\">Back to Start Page</a></p>"
            "</body></html>";
        unsigned int elen = 0;
        while (err_html[elen]) elen++;
        callback(err_html, (int)elen, userdata);
        return FETCH_OK;
    }

#if 0  /* NetISA hardware path — disabled in stub build */
    /* === HTTP/HTTPS URLs — route to NetISA === */
    {
        unsigned char handle = 0;
        int result;
        int redirects = 0;
        char current_url[URL_MAX + 1];
        unsigned long timeout_start;

        strncpy(current_url, url, URL_MAX);
        current_url[URL_MAX] = '\0';

    retry_redirect:
        if (redirects >= 5) return FETCH_ERR_REDIR;

        /* Re-parse in case of redirect */
        if (url_parse(current_url, &parts) != 0)
            return FETCH_ERR_HTTP;

        /* DNS resolve with 10-second timeout */
        /* Note: ni_dns_resolve may block — in real implementation,
           check ticks around it. For stub build, it returns instantly. */
        update_spinner();

        /* Open TLS session */
        result = ni_session_open(parts.host, parts.port, &handle);
        if (result != 0) return FETCH_ERR_CONN;

        /* Send HTTP/1.0 GET request */
        {
            static char req[512];
            int reqlen;
            sprintf(req,
                "GET %s HTTP/1.0\r\n"
                "Host: %s\r\n"
                "User-Agent: Cathode/0.2 (DOS; NetISA)\r\n"
                "Accept: text/html, text/plain\r\n"
                "Accept-Encoding: identity\r\n"
                "Connection: close\r\n"
                "\r\n",
                parts.path, parts.host);
            reqlen = (int)strlen(req);
            ni_session_send(handle, (const char far *)req, (unsigned short)reqlen);
        }

        /* Receive and parse response headers */
        {
            char line[512];
            int line_pos = 0;
            int in_headers = 1;
            int total_header_bytes = 0;
            char recv_buf[512];
            int recv_pos = 0;
            int recv_len = 0;

            timeout_start = get_ticks();

            while (in_headers) {
                unsigned short got = 0;

                /* Check for Escape */
                if (scr_kbhit()) {
                    int key = scr_getkey();
                    if ((key & 0xFF) == 0x1B) {
                        ni_session_close(handle);
                        return FETCH_CANCELLED;
                    }
                }

                /* Refill recv buffer if empty */
                if (recv_pos >= recv_len) {
                    update_spinner();
                    ni_session_recv(handle, (char far *)recv_buf,
                                    512, &got);
                    if (got == 0) {
                        /* Check timeout; handle midnight tick wrap */
                        unsigned long now = get_ticks();
                        if (now < timeout_start)
                            timeout_start = now;
                        if (now - timeout_start > 546) {
                            ni_session_close(handle);
                            return FETCH_ERR_CONN;
                        }
                        continue;
                    }
                    recv_pos = 0;
                    recv_len = got;
                    timeout_start = get_ticks();
                }

                /* Parse headers byte by byte */
                while (recv_pos < recv_len && in_headers) {
                    char ch = recv_buf[recv_pos++];
                    total_header_bytes++;

                    if (total_header_bytes > 8192) {
                        ni_session_close(handle);
                        return FETCH_ERR_HTTP;
                    }

                    if (ch == '\n') {
                        line[line_pos] = '\0';
                        /* Strip trailing \r */
                        if (line_pos > 0 && line[line_pos - 1] == '\r')
                            line[--line_pos] = '\0';

                        if (line_pos == 0) {
                            /* Empty line = end of headers */
                            in_headers = 0;
                        } else if (state->status_code == 0) {
                            /* Status line: HTTP/1.x NNN ... */
                            char *sp = strchr(line, ' ');
                            if (sp && sp[1] >= '0' && sp[1] <= '9' &&
                                sp[2] >= '0' && sp[2] <= '9' &&
                                sp[3] >= '0' && sp[3] <= '9') {
                                state->status_code =
                                    (sp[1] - '0') * 100 +
                                    (sp[2] - '0') * 10 +
                                    (sp[3] - '0');
                            }
                        } else {
                            /* Header line */
                            if (strnicmp_local(line, "Content-Length: ", 16) == 0) {
                                long cl = 0;
                                char *p = line + 16;
                                while (*p >= '0' && *p <= '9')
                                    cl = cl * 10 + (*p++ - '0');
                                state->content_length = cl;
                            } else if (strnicmp_local(line, "Location: ", 10) == 0) {
                                strncpy(state->redirect_url, line + 10,
                                        URL_MAX);
                                state->redirect_url[URL_MAX] = '\0';
                            } else if (strnicmp_local(line, "Content-Type: ", 14) == 0) {
                                if (strstr(line + 14, "utf-8") ||
                                    strstr(line + 14, "UTF-8"))
                                    state->is_utf8 = 1;
                            }
                        }
                        line_pos = 0;
                    } else if (line_pos < 511) {
                        line[line_pos++] = ch;
                    }
                    /* else: line too long, discard excess */
                }
            }

            /* Handle redirects */
            if (state->status_code >= 301 && state->status_code <= 307 &&
                state->redirect_url[0]) {
                char resolved[URL_MAX + 1];
                ni_session_close(handle);
                url_resolve(current_url, state->redirect_url,
                            resolved, URL_MAX + 1);
                strncpy(current_url, resolved, URL_MAX);
                current_url[URL_MAX] = '\0';
                redirects++;
                memset(state, 0, sizeof(fetch_state_t));
                state->content_length = -1;
                goto retry_redirect;
            }

            /* Feed remaining data in recv buffer to callback */
            if (recv_pos < recv_len) {
                callback((const char far *)(recv_buf + recv_pos),
                         recv_len - recv_pos, userdata);
                state->bytes_received += (recv_len - recv_pos);
            }
        }

        /* Continue receiving body */
        {
            char recv_buf[512];
            timeout_start = get_ticks();

            for (;;) {
                unsigned short got = 0;

                if (scr_kbhit()) {
                    int key = scr_getkey();
                    if ((key & 0xFF) == 0x1B) {
                        ni_session_close(handle);
                        return FETCH_CANCELLED;
                    }
                }

                update_spinner();
                ni_session_recv(handle, (char far *)recv_buf,
                                512, &got);

                if (got == 0) {
                    /* Handle midnight tick wrap */
                    unsigned long now = get_ticks();
                    if (now < timeout_start)
                        timeout_start = now;
                    if (now - timeout_start > 546) break;
                    continue;
                }

                callback((const char far *)recv_buf, got, userdata);
                state->bytes_received += got;
                timeout_start = get_ticks();

                /* If we know content-length, check if done */
                if (state->content_length >= 0 &&
                    state->bytes_received >= state->content_length)
                    break;
            }
        }

        ni_session_close(handle);
    }

    /* Flush keyboard buffer after fetch */
    while (scr_kbhit()) scr_getkey();

    return FETCH_OK;
#endif  /* NetISA hardware path */
}
