---
ticket: "N/A"
title: "Cathode v0.2 — Text-Mode Document Browser for DOS"
date: "2026-04-13"
source: "design"
---

# Cathode v0.2 — Text-Mode Document Browser for DOS

## Summary

Cathode v0.1 is a 21.8KB browser shell with keyboard navigation, 3 hardcoded stub pages, link selection, history, URL bar editing, and VGA palette fades. It proves the UI works. v0.2 transforms it into a functional text-mode document browser: fetch real HTML pages over HTTPS via the NetISA card, render structured content (articles, documentation, wikis, directory pages), navigate with keyboard and mouse.

**Cathode is not a general-purpose web browser.** It excels at structured HTML documents and is honest about what it cannot do: no JavaScript, no CSS layout, no POST forms, no media playback. Modern web applications will not work. Content-heavy sites — documentation, wikis, plain articles, directory listings — are the target. A future firmware-side content normalization layer (gateway mode) may extend reach to more sites.

**11 features, 12 new source modules, ~3,800 lines of new code.** Phased into 3 victory conditions.

This document has been red-teamed (3 rounds, 57 findings resolved) and product-reviewed. See Appendices A and B.

## Victory Conditions (Phased Delivery)

### Phase 1: Reader/Navigator Core (must ship first)
The browser works. Fetch, parse, render, navigate.

- HTML parser (structured documents: headings, paragraphs, links, lists, `<pre>`, `<hr>`, `<br>`, bold/italic/code)
- HTTP/1.0 page fetch via NetISA (with redirect following, timeout, cancellation)
- UTF-8 to CP437 mapping
- URL parsing and relative resolution
- Scroll bar
- Find on page (Ctrl+F)
- Bookmarks (Ctrl+D/Ctrl+B)
- Loading animation
- Word-boundary wrapping (promoted from deferred — matters for docs/URLs)
- Visible page truncation indicator at PAGE_MAX_ROWS

**Acceptance criteria (Phase 1 only):**
1. Cathode renders frozen HTML test fixtures readably (headings, paragraphs, links, lists, bold, italic, code, `<pre>`, `<hr>`)
2. Links are clickable (keyboard Tab/Enter). Navigation history works (Backspace = back).
3. Find on page (Ctrl+F) finds and highlights matches. N/Shift+N cycles.
4. Bookmarks persist across sessions in CATHODE.BMK. Ctrl+D adds, Ctrl+B shows.
5. Scroll bar accurately reflects position. Page Up/Down/Home/End work.
6. UTF-8 content renders correctly (accented characters, smart quotes, em-dashes).
7. HTTP redirects are followed (up to 5 hops). DNS and recv timeouts work.
8. Spinner animates during fetch. Escape cancels.
9. Pages exceeding 200 rows show `[Page truncated — 200 row limit]` footer.
10. Frozen fixture corpus passes. Live smoke tests attempted (results logged, not gating).
11. Builds cleanly with `wmake -f Makefile cathode` at `-w4`. EXE under 40KB.
12. Runs in DOSBox-X with VGA and EGA machine types.

**Success gate:** Phase 1 ships when criteria 1-12 pass. No Phase 2/3 features required.

### Phase 2: Interaction Polish (v0.2.1 — does not block Phase 1)
The browser feels good. Mouse, themes, then tabs.

- Mouse support (INT 33h) — click links, click URL bar, click scrollbar, right-click = back
- Color themes (amber/green/blue phosphor) — F2 cycles, persists in CATHODE.CFG

**Acceptance criteria (Phase 2):**
1. Mouse clicks follow links, focus URL bar, operate scrollbar.
2. Right-click navigates back.
3. F2 cycles through 4 themes with visible palette change.
4. Graceful degradation: no mouse driver = keyboard only, no VGA = no themes.

### Phase 2b: Tabs (v0.2.2 — does not block Phase 2)
Tabs are valuable but not core. A single-tab browser that works is infinitely better than a four-tab browser that almost works.

- Tabs (4 max, far-allocated, instant switching)
- Tab bar at row 0, Alt+1-4, Ctrl+T/W

**Acceptance criteria (Phase 2b):**
1. 4 tabs open simultaneously. Switching is instant (no re-fetch).
2. Ctrl+T opens new tab, Ctrl+W closes. Cannot close last tab.
3. Tab bar renders correctly. Mouse clicks switch tabs.

### Phase 3: Compatibility Expansion (v0.3+ — do not block core)
The browser handles harder content. These are the most likely schedule killers.

- Table rendering (buffered column-width calculation) — v0.3
- Form display + GET submission — v0.4
- Image half-block rendering (demo image + framework) — experimental until firmware gateway exists

**Acceptance criteria (Phase 3):**
1. `about:test` renders tables with box-drawing borders and correct column widths.
2. Form text input accepts typed text. GET form submission navigates with query params.
3. Demo image renders as half-block art on `about:test`.
4. At least one real site with a table renders correctly.

### Feature Flags

All Phase 2 and Phase 3 features are guarded by compile-time flags:

```c
/* cathode_cfg.h */
#define FEAT_MOUSE    1    /* INT 33h mouse support */
#define FEAT_TABS     1    /* Multi-tab browsing */
#define FEAT_THEMES   1    /* VGA DAC color themes */
#define FEAT_TABLES   1    /* Buffered table rendering */
#define FEAT_FORMS    1    /* Form field display and GET submission */
#define FEAT_IMAGES   1    /* Half-block image rendering */
```

Phase 1 features (parser, fetch, UTF-8, URL, scroll, search, bookmarks, spinner) have no flags — they are always compiled. This protects the core from feature-coupled regressions and allows incremental development.

## Performance Budgets

Hard failure thresholds. If any is exceeded, the feature must be optimized or descoped before shipping.

| Operation | Budget (4.77 MHz 8088) | Budget (286/12 MHz) | Notes |
|-----------|----------------------|---------------------|-------|
| Full page render (render_all) | < 100ms (5 fps) | < 20ms (50 fps) | 1,600 cells × 3 far reads + scrollbar + chrome |
| Tab switch (render only, no fetch) | < 100ms | < 20ms | Same as render — swap page pointer, re-render |
| HTML parse 10KB page | < 3 seconds | < 500ms | ~50,000 chars × ~150 cycles/char + far writes |
| HTML parse 50KB page | < 15 seconds | < 2.5 seconds | Linear scaling from 10KB |
| Search scan (full page) | < 500ms | < 100ms | 16,000 far reads for 200-row page |
| Mouse cursor reposition | < 2ms | < 1ms | 2 cell attribute writes only, no full render |
| Image render (40×20 demo) | < 50ms | < 10ms | 200 cells, no quantization (pre-indexed) |
| Image quantize (160×50 RGB) | < 2 seconds | < 400ms | 8,000 pixels × 16 comparisons |
| Page fetch (about: stub) | < 10ms | < 2ms | No network, string lookup only |
| Maximum page size | 200 rows | 200 rows | PAGE_MAX_ROWS. Larger pages truncated. |
| Maximum table size | 20 rows × 8 cols | Same | Larger tables truncated with "..." |
| Maximum fetch timeout | 30 seconds | 30 seconds | BIOS tick based |
| Maximum DNS timeout | 10 seconds | 10 seconds | BIOS tick based (Gemini review) |

### Double-Buffer Rendering (Gemini Review)

On XT-class machines, ISA bus writes to VGA memory at 0xB800 are slow (~1us per word vs ~250ns for conventional RAM). The current `render_page()` does ~1,600 individual `scr_putc()` calls, each writing 2 bytes to VGA. On an 8088 at 4.77 MHz, this interleaving of far reads (page buffer) and far writes (VGA) is the rendering bottleneck.

**Optimization:** Allocate a 4,000-byte far buffer (`80 * 25 * 2` = one full screen of char+attr pairs). `render_page()` writes to this buffer instead of VGA. After the render pass completes, blit the buffer to VGA in one `_fmemcpy(vga_base, render_buf, 4000)`. This:

- Eliminates tearing during tab switches and page loads
- Batches VGA writes for better ISA bus throughput
- Moves the expensive per-cell far reads away from VGA timing

The buffer costs 4KB far (negligible). `_fmemcpy` on a contiguous block is significantly faster than 1,600 scattered writes because the 8088's bus unit can pipeline word-aligned transfers. If the 100ms render budget is not met with direct VGA writes, this is the first optimization to apply.

## Real-Site Test Corpus

### Frozen Fixtures (deterministic, gating)

Captured HTML files stored in `cathode/test/` for repeatable testing in DOSBox-X without network. These are the Phase 1 acceptance gate.

| Fixture | Source | What it tests |
|---------|--------|---------------|
| `fixture_article.html` | Captured from lite.cnn.com | Headings, paragraphs, links. Minimal markup. |
| `fixture_manpage.html` | Captured from man.openbsd.org/ls | `<pre>` blocks, structured text. |
| `fixture_wiki.html` | Captured from simple.wikipedia.org/wiki/Computer | Headings, paragraphs, links, lists. Many links (tests MAX_LINKS). |
| `fixture_directory.html` | Captured from text.npr.org | Clean link list. Minimal page. |
| `fixture_redirect.html` | Crafted | Tests that redirect handling code path works (with mock header). |
| `fixture_malformed.html` | Crafted | Unclosed tags, misnested elements, missing quotes, bare `<`, 500-char attribute, 50-char tag name. |
| `fixture_truncation.html` | Crafted | 250+ rows of content. Must show truncation footer at row 200. |
| `fixture_utf8.html` | Crafted | Accented characters, smart quotes, em-dashes, bullets, non-breaking spaces. |

### Live Smoke Tests (non-deterministic, logged but not gating)

Attempted via NetISA or DOSBox-X network. Results logged for observation. These pages change over time — failures are noted, not blocking.

| Category | URL | Notes |
|----------|-----|-------|
| Simple article | `https://lite.cnn.com/` | May change layout. Check readability. |
| Documentation | `https://man.openbsd.org/ls` | Stable format. Good baseline. |
| Wiki | `https://simple.wikipedia.org/wiki/Computer` | Tests link density. Phase 3: tables. |
| Directory | `https://text.npr.org/` | Very clean. Should work perfectly. |
| Table-heavy | `https://www.timeanddate.com/worldclock/` | Phase 3 only. Expected to degrade in Phase 1. |
| Search form | `https://lite.duckduckgo.com/lite/` | Phase 3 only (forms). Phase 1: renders as text. |
| Redirect | `http://example.com/` | Tests HTTP → HTTPS redirect following. |

Pages that will NOT work (documented, not bugs): any JS-dependent SPA, any page requiring CSS for layout, any page requiring POST login, any page with CAPTCHA.

## Parser Decomposition

`html.c` is split into 4 files to reduce blast radius (product review recommendation):

| File | Responsibility | Est. Lines |
|------|---------------|-----------|
| `htmltok.c` / `htmltok.h` | Tokenizer: state machine, byte-level parsing, tag/attribute accumulation, entity decoding, UTF-8 feed | ~400 |
| `htmlout.c` / `htmlout.h` | Layout emitter: emit_char, emit_newline, whitespace collapsing, word wrap, style stack, link tracking | ~300 |
| `htmltbl.c` / `htmltbl.h` | Table handler: buffer allocation, cell accumulation, column width calc, CP437 border rendering, flush to page | ~250 |
| `htmlform.c` / `htmlform.h` | Form handler: field creation from `<input>`/`<select>`/`<textarea>`, field type dispatch | ~150 |

`htmltok.c` drives parsing. It calls into `htmlout.c` for text emission and tag dispatch. Tag dispatch calls `htmltbl.c` for table tags and `htmlform.c` for form tags. All share the `html_parser_t` struct (passed by pointer).

## Gateway Mode (Future — Firmware Scope)

Broad web usefulness probably requires a content normalization layer. The natural location is the ESP32-S3 firmware on the NetISA card, which already handles TLS and HTTP. A "gateway mode" would:

1. Fetch the page on the ESP32
2. Strip `<script>`, `<style>`, CSS classes, inline styles
3. Simplify HTML to the tag subset Cathode supports
4. Transcode images to pre-quantized 16-color indexed format
5. Deliver clean, Cathode-safe HTML to the DOS client

This is firmware scope, not browser scope. Cathode receives the same HTML format either way — gateway mode just produces better HTML from worse sources. The browser design does not depend on gateway mode, but broad usefulness beyond document-class sites probably does.

This feature is out of scope for v0.2 but architecturally anticipated.

## Constraints

| Constraint | Value |
|------------|-------|
| CPU target | 8088 (4.77 MHz), compatible through Pentium |
| Compiler | OpenWatcom 2.0, C89 (`-0 -ms -ox`) |
| Memory model | Small (64KB DGROUP limit) |
| Display | 80×25 text mode (mode 3), CP437 character set |
| Color | 16 text-mode colors via VGA DAC (graceful degradation on EGA/CGA/MDA) |
| Audio | None (browser has no audio requirements) |
| Network | NetISA INT 63h API — TLS 1.3 sessions via `ni_session_open/send/recv` |
| Far heap | ~525KB available in conventional memory after DOS + program |

## Architecture Overview

### Data Flow

```
URL input (keyboard/mouse/link click)
  → url_parse() + url_resolve()
  → fetch_page() [HTTP/1.0 GET via NetISA, or stub lookup]
    → recv loop with spinner animation
    → html_parse_chunk() called per recv block
      → utf8_feed() decodes each byte to CP437
      → streaming cell emitter writes to page_buffer_t
      → table buffering mode for <table>...</table>
      → image_render_to_page() for <img> tags
      → form_field_t creation for <input>/<select>/<textarea>
  → html_finish() flushes pending state
  → render_all() displays page with scrollbar, tab bar, search highlights
```

### Module Dependency Graph

```
main.c
  ├── tabs.c      → page.c, browser.c
  ├── mouse.c     (standalone, INT 33h)
  ├── theme.c     (standalone, VGA DAC)
  ├── input.c     → browser.c, search.c, bookmark.c, tabs.c, mouse.c
  ├── render.c    → page.c, search.c, tabs.c
  └── browser.c
        ├── fetch.c    → url.c, stub_pages.c, netisa_stub.c / netisa.c
        ├── html.c     → page.c, utf8.c, image.c, url.c
        ├── bookmark.c → html.c, page.c
        └── search.c   → page.c
```

### New Files

| File | Purpose | Est. Lines |
|------|---------|-----------|
| `mouse.c` / `mouse.h` | INT 33h mouse driver, polling, software cursor, click dispatch | ~150 |
| `htmltok.c` / `htmltok.h` | Tokenizer: state machine, tag/attr parsing, entity decoding | ~400 |
| `htmlout.c` / `htmlout.h` | Layout emitter: text flow, style stack, link tracking | ~300 |
| `htmltbl.c` / `htmltbl.h` | Table handler: buffering, column calc, CP437 borders | ~250 |
| `htmlform.c` / `htmlform.h` | Form handler: field creation from input/select/textarea | ~150 |
| `fetch.c` / `fetch.h` | HTTP/1.0 GET, header parsing, redirect following, stub swap | ~350 |
| `image.c` / `image.h` | CP437 half-block renderer, RGB quantizer, demo image data | ~250 |
| `tabs.c` / `tabs.h` | Tab lifecycle, per-tab far-allocated state, tab bar UI | ~200 |
| `bookmark.c` / `bookmark.h` | CATHODE.BMK file I/O, about:bookmarks page builder | ~150 |
| `search.c` / `search.h` | Find-on-page, match highlighting, N/Shift+N cycling | ~120 |
| `theme.c` / `theme.h` | 4 VGA DAC palette themes, F2 cycling, CATHODE.CFG persistence | ~70 |
| `url.c` / `url.h` | URL parsing, relative resolution, URL encoding | ~200 |
| `utf8.c` / `utf8.h` | Multi-byte UTF-8 decoder, codepoint-to-CP437 mapping table | ~120 |

### Modified Files

| File | Changes |
|------|---------|
| `main.c` | Non-blocking event loop (mouse+keyboard polling), tab/theme/mouse init |
| `input.c` | Mouse click dispatch, new keybindings, focus mode dispatch, form field editing |
| `render.c` | Scroll bar column (col 79), tab bar row (row 0), search highlights, image cells, form field rendering |
| `browser.c` | Delegates to tabs for state, uses fetch pipeline, removes direct stub calls |
| `page.h` | New cell types (CELL_IMAGE, CELL_BUTTON, CELL_CHECKBOX, etc.), form_field_t far array |
| `page.c` | Form field far allocation, page_add_field(), page_get_field() |
| `stub_pages.c` | Rewritten: exports `stub_get_html(url)` returning `const char __far *` HTML strings |
| `urlbar.h` | KEY_LEFT/KEY_RIGHT added (currently missing from input.h) |
| `input.h` | Alt+1-4 scan codes (0x7800-0x7B00), Ctrl key codes |
| `lib/screen.h` | Add `scr_getattr(x,y)` and `scr_setattr(x,y,attr)` for mouse cursor |
| `lib/screen.c` | Implement scr_getattr/scr_setattr (trivial VGA buffer read/write) |
| `cathode_cfg.h` | NEW: Feature flags (FEAT_MOUSE, FEAT_TABS, FEAT_THEMES, FEAT_TABLES, FEAT_FORMS, FEAT_IMAGES) |
| `Makefile` | 13 new .obj files (4 html + 9 others), updated dependencies |

---

## Component Designs

### 1. Mouse Support (`mouse.c` / `mouse.h`)

**Detection:** INT 33h AX=0000h. Returns AX=FFFFh if mouse driver present, BX=button count. Called once at startup. If no driver, all mouse functions become no-ops.

**Polling:** INT 33h AX=0003h returns CX=X position (0–639), DX=Y position (0–199), BX=button state. Convert to text cells: `col = CX / 8`, `row = DX / 8`. Called every iteration of the main event loop.

**Software cursor:** INT 33h's built-in text cursor flickers. Instead: save the VGA attribute byte at the mouse cell, replace it with a highlight (swap foreground/background). On mouse move, restore the old attribute and highlight the new cell. Mouse cursor is hidden before any render pass and re-shown after, to avoid rendering artifacts.

**Click dispatch (in input.c):**

| Click target | Action |
|--------------|--------|
| Content area (rows 3–22, cols 0–77) | Check cell type. CELL_LINK: follow link. CELL_INPUT/BUTTON/CHECKBOX/RADIO/SELECT: focus form field. Col 78 is `<pre>` truncation indicator only. Plain text: no action. |
| URL bar (row 1) | Start URL editing, position cursor at click column. |
| Tab bar (row 0) | Switch to clicked tab (each tab occupies a 10-char slot). |
| Scroll bar (col 79, rows 3–22) | Scroll to proportional position: `scroll_pos = (click_row - 3) * max_scroll / 19`. |
| Right-click anywhere | Go back (equivalent to Backspace). |

**Interface:**

```c
#define MOUSE_NONE    0
#define MOUSE_MOVED   1
#define MOUSE_LCLICK  2
#define MOUSE_RCLICK  4

typedef struct {
    int present;
    int x, y;
    int prev_x, prev_y;
    int buttons;
    int event;
} mouse_state_t;

int  mouse_init(mouse_state_t *m);
void mouse_poll(mouse_state_t *m);
void mouse_show(mouse_state_t *m);
void mouse_hide(mouse_state_t *m);
```

**DGROUP cost:** 20 bytes.

---

### 2. UTF-8 Decoder + CP437 Mapping (`utf8.c` / `utf8.h`)

**Problem:** The modern web is UTF-8. Cathode displays CP437. Without translation, every accented character, smart quote, em-dash, and bullet renders as garbage.

**Decoder:** State machine accumulating multi-byte UTF-8 sequences.

- 0x00–0x7F: ASCII. Pass through (identical in CP437).
- 0xC0–0xDF + 1 continuation byte: 2-byte sequence → codepoint 0x80–0x7FF.
- 0xE0–0xEF + 2 continuation bytes: 3-byte sequence → codepoint 0x800–0xFFFF.
- 0xF0–0xF7 + 3 continuation bytes: 4-byte (emoji, CJK) → emit `?`.

**Mapping table:** `static const unsigned char __far utf8_to_cp437[128]` covers Latin-1 Supplement (U+0080–U+00FF):

| Unicode Range | CP437 Mapping |
|---------------|---------------|
| U+00C0–U+00C5 (A with accents) | CP437 A-accent equivalents |
| U+00E0–U+00E5 (a with accents) | CP437 a-accent equivalents |
| U+00C7/U+00E7 (C/c cedilla) | 0x80/0x87 |
| U+00D1/U+00F1 (N/n tilde) | 0xA5/0xA4 |
| U+00B0 (degree) | 0xF8 |
| U+00B1 (plus-minus) | 0xF1 |
| U+00A3 (pound) | 0x9C |
| U+00A5 (yen) | 0x9D |
| U+00A9 (copyright) | `c` (best effort) |
| U+00AE (registered) | `R` (best effort) |

**Extended mappings** (beyond Latin-1, handled by switch statement):

| Codepoint | CP437 |
|-----------|-------|
| U+2013 (en-dash) | `-` (0x2D) |
| U+2014 (em-dash) | `-` (0x2D) |
| U+2018/U+2019 (smart single quotes) | `'` (0x27) |
| U+201C/U+201D (smart double quotes) | `"` (0x22) |
| U+2022 (bullet) | 0x07 (CP437 bullet) |
| U+2026 (ellipsis) | `.` (single period) |
| U+00A0 (non-breaking space) | 0x20 (space) |
| U+2500–U+257F (box drawing) | Nearest CP437 box character |
| Everything else | `?` |

**Interface:**

```c
typedef struct {
    unsigned char state;
    unsigned long codepoint;
} utf8_decoder_t;

void          utf8_init(utf8_decoder_t *d);
unsigned char utf8_feed(utf8_decoder_t *d, unsigned char byte);
```

`utf8_feed` returns a CP437 character when a complete codepoint is decoded, or 0 if still accumulating a multi-byte sequence. The HTML parser calls this for every byte of text content.

**DGROUP cost:** 5 bytes (decoder state). Mapping table is `__far`.

---

### 3. URL Parser + Resolver (`url.c` / `url.h`)

**Parsing:** Splits a URL into scheme, host, port, and path components.

```c
#define URL_MAX 255

typedef struct {
    char scheme[8];       /* "https", "http", "about" */
    char host[128];
    unsigned int port;    /* 443 default for https, 80 for http */
    char path[128];
} url_parts_t;

int  url_parse(const char *url, url_parts_t *parts);
void url_resolve(const char *base_url, const char *relative, char *result);
void url_encode(const char *input, char *output, int max_len);
```

**Resolution rules for `url_resolve`:**

| Relative URL form | Resolution |
|-------------------|------------|
| `https://...` or `http://...` | Absolute — return as-is |
| `//host/path` | Protocol-relative — prepend base scheme |
| `/path` | Absolute path — use base scheme + host |
| `../path` | Walk up base path directory |
| `./path` or bare `name` | Relative to base path directory |
| `#fragment` | Append to base URL |
| `?query` | Replace base query, keep base path |

**URL encoding (`url_encode`):** For GET form submission. Space → `+`, characters outside `[A-Za-z0-9_.-]` → `%XX`. Used to build query strings.

**DGROUP cost:** ~12 bytes for a working url_parts_t (stack-allocated in functions).

---

### 4. HTTP Fetch Layer (`fetch.c` / `fetch.h`)

**Protocol:** HTTP/1.0 with `Connection: close`. This avoids chunked transfer encoding entirely — HTTP/1.0 responses use Content-Length or close-on-completion. Every server supports HTTP/1.0 fallback. Upgrade to HTTP/1.1 is a future option.

**Request template:**

```
GET <path> HTTP/1.0\r\n
Host: <hostname>\r\n
User-Agent: Cathode/0.2 (DOS; NetISA)\r\n
Accept: text/html, text/plain\r\n
Accept-Encoding: identity\r\n
Connection: close\r\n
\r\n
```

**Response parsing:** Read line-by-line until blank `\r\n`. Extract:

| Header | Use |
|--------|-----|
| Status line (`HTTP/1.x NNN`) | Status code for error handling, redirect detection |
| `Content-Type` | Detect `charset=utf-8` vs `charset=iso-8859-1` |
| `Content-Length` | Progress calculation (optional) |
| `Location` | Redirect target (301, 302, 307) |

**Redirect handling:** Follow up to 5 hops. Resolve `Location` header against current URL via `url_resolve`. Re-issue GET to resolved URL.

**Streaming callback design:**

```c
#define FETCH_OK        0
#define FETCH_ERR_DNS   1
#define FETCH_ERR_CONN  2
#define FETCH_ERR_HTTP  3
#define FETCH_ERR_REDIR 4
#define FETCH_CANCELLED 5

typedef struct {
    int status_code;
    int content_length;
    int is_utf8;
    int bytes_received;
    char redirect_url[URL_MAX + 1];
} fetch_state_t;

typedef void (*fetch_cb_t)(const char far *chunk, int len, void *userdata);

int fetch_page(const char *url, fetch_cb_t callback,
               void *userdata, fetch_state_t *state);
```

The recv loop calls `ni_session_recv` into a 512-byte stack-local buffer, then invokes the callback (which feeds the HTML parser). Between recv calls, the loop:
1. Updates the status bar spinner
2. Checks `scr_kbhit()` for Escape (cancel fetch)

This is where the loading animation actually works — the recv loop is the blocking point, and polling between recvs gives us animation + cancellation.

**URL routing (red-team fix):** `fetch_page()` is the single entry point for all URL fetching. It checks the scheme:

- `about:` → calls `stub_get_html(url)` in stub_pages.c, which returns a `const char __far *` HTML string. The string is passed to the callback directly (no network).
- `http://` or `https://` → DNS resolve via `ni_dns_resolve()`, open TLS session via `ni_session_open()`, send HTTP/1.0 GET, recv response.
- Anything else → returns `FETCH_ERR_HTTP`.

This makes fetch.c depend on both stub_pages.c (for about: URLs) and netisa.h (for network URLs). The stub_pages.c module exports a single function: `const char __far *stub_get_html(const char *url)` which returns NULL for unknown URLs.

**DGROUP cost:** ~280 bytes for fetch_state_t (static — includes redirect_url[256]). Recv buffer (512 bytes) is stack-local during fetch only.

---

### 5. Streaming HTML Parser (`html.c` / `html.h`)

The largest and most complex new module. ~900 lines.

**Architecture:** Character-by-character state machine. Processes input bytes, tracks formatting context, and emits cells directly to `page_buffer_t`. Single-pass for all content except tables, which use a buffered sub-mode.

**Parser state machine:**

```
PS_TEXT       → Accumulate text characters, emit to page cells
PS_TAG_OPEN   → Saw '<', determine if tag name, close tag, or comment
PS_TAG_NAME   → Accumulating tag name characters
PS_TAG_ATTRS  → Parsing attribute name="value" pairs
PS_TAG_DONE   → Saw '>' or '/>', dispatch the accumulated tag
PS_ENTITY     → Saw '&', accumulating entity name until ';'
PS_COMMENT    → Inside <!-- -->, skip all content
```

**Parser state structure (~510 bytes DGROUP):**

**Critical: Stack safety.** The parser struct MUST be declared `static` in browser.c, not stack-allocated. At ~510 bytes, placing it on the stack alongside the 512-byte recv buffer, url_parts_t (~268 bytes), and fetch_state_t would consume ~1,570 of the 2,000-byte stack — leaving only 430 bytes for the entire call chain. `fetch_state_t` is also `static`. Only the recv buffer is stack-local (temporary, freed on return).

```c
#define HTML_MAX_TAG     16
#define HTML_MAX_ATTR    128
#define HTML_STYLE_DEPTH 8

typedef struct {
    /* State machine */
    unsigned char state;
    unsigned char in_head, in_pre, in_script;

    /* Tag accumulation */
    char tag_name[HTML_MAX_TAG];
    int tag_len;
    int tag_is_close;

    /* Attribute accumulation */
    char attr_name[HTML_MAX_TAG];
    char attr_val[HTML_MAX_ATTR];
    int attr_name_len, attr_val_len;
    unsigned char attr_state;

    /* Output cursor */
    int row, col;
    int indent;
    int line_has_content;

    /* Style stack (bounds-checked: push ignores if depth >= max, pop ignores if depth <= 0) */
    unsigned char style_stack[HTML_STYLE_DEPTH];
    int style_depth;
    unsigned char current_attr;

    /* Link state */
    char link_href[HTML_MAX_ATTR];
    int link_start_row, link_start_col;
    int in_link;

    /* Form state */
    char form_action[HTML_MAX_ATTR];
    int form_id;

    /* List state */
    int list_depth;
    int list_ordered[4];
    int list_counter[4];

    /* Table buffering */
    unsigned char in_table;
    void far *table_buf;

    /* Entity */
    char entity_buf[10];
    int entity_len;

    /* UTF-8 */
    utf8_decoder_t utf8;

    /* Target */
    page_buffer_t *page;
} html_parser_t;
```

**Tag dispatch — block elements:**

| Tag | Action |
|-----|--------|
| `<h1>`–`<h6>` | Newline, push ATTR_HEADING style. On close: pop style, blank line. |
| `<p>` | Paragraph break (blank line if line has content). |
| `<br>` | Emit newline (self-closing). |
| `<hr>` | Full-width horizontal rule using CP437 0xC4. |
| `<div>` | Newline if line has content. |
| `<pre>` | Set `in_pre=1`, preserve whitespace/newlines. Use monospace attribute. |
| `<blockquote>` | Indent +4. On close: indent -4. |
| `<ul>` | Push unordered list, indent +2. |
| `<ol>` | Push ordered list with counter=1, indent +2. |
| `<li>` | Newline, emit bullet (0x07) or counter digit, space. |

**Tag dispatch — inline elements:**

| Tag | Action |
|-----|--------|
| `<a href="...">` | Save href, record start position, push ATTR_LINK. |
| `</a>` | Register link via `page_add_link()`, pop style. |
| `<b>` / `<strong>` | Push ATTR_BOLD (SCR_WHITE on SCR_BLACK). |
| `<i>` / `<em>` | Push ATTR_DIM (italic approximation in text mode). |
| `<u>` | Push cyan foreground (underline approximation). |
| `<code>` | Push green-on-black monospace style. |

**Tag dispatch — suppressed:**

| Tag | Action |
|-----|--------|
| `<script>` | Set `in_script=1`. Suppress all output until `</script>`. |
| `<style>` | Set `in_script=1`. Suppress all output until `</style>`. |
| `<head>` | Set `in_head=1`. Suppress output except `<title>` → `page->title`. |

**Tag dispatch — tables (buffered mode):**

When `<table>` is encountered:
1. Allocate far buffer via `_fmalloc`:

```c
#define TBL_MAX_ROWS 20
#define TBL_MAX_COLS 8
#define TBL_CELL_MAX 40

typedef struct {
    char text[TBL_CELL_MAX];
    int len;
    unsigned char is_header;
} tbl_cell_t;

typedef struct {
    tbl_cell_t far *cells;    /* [TBL_MAX_ROWS * TBL_MAX_COLS] */
    int row_count, col_count;
    int cur_row, cur_col;
} table_buf_t;
```

Buffer size: 20 × 8 × 42 = 6,720 bytes far.

2. Text inside `<td>`/`<th>` accumulates into `cells[row][col].text` instead of page buffer.
3. `<tr>` advances row, resets column. `<td>`/`<th>` advances column.
4. Tables exceeding 20 rows or 8 columns are truncated with a "..." indicator.

On `</table>`:
1. Calculate column widths: `width[i] = max(cell_len) across all rows in column i`.
2. Clamp total width to `78 - indent`. If overflow, proportionally shrink columns.
3. Emit complete table to page buffer with CP437 box-drawing borders (single-line style: 0xDA/0xBF/0xC0/0xD9 corners, 0xC4 horizontal, 0xB3 vertical, 0xC2/0xC1/0xC3/0xB4/0xC5 junctions).
4. Header row separated by a double-line junction row.
5. Free the table buffer.

If `_fmalloc` fails for the table buffer, fall back to rendering table content as flowing text with `|` column separators — degraded but functional.

**Nested tables (red-team fix):** Not supported. When `in_table == 1` and a new `<table>` is encountered, it is ignored (text flows into the current table cell). The inner `</table>` is also ignored (does not terminate the outer table). This is a documented limitation.

**Links inside table cells (red-team fix):** Not supported in v0.2. Table cells store plain text only (the tbl_cell_t buffer has no link metadata). `<a href>` tags inside `<td>` render as plain text — the href is lost. Supporting links in tables would require buffering cell metadata alongside text, doubling the table buffer size. Deferred to v0.3.

**`<pre>` width handling (red-team fix):** In `<pre>` mode, text that exceeds column 78 is hard-truncated (not wrapped). A `»` indicator character is placed at column 78 when truncation occurs on a line, signaling to the user that content extends beyond the visible width. This preserves code block structure better than wrapping.

**MAX_LINKS overflow (red-team fix):** When `page_add_link()` fails because `MAX_LINKS` (64) is reached, the `</a>` handler retroactively walks cells from `link_start_row, link_start_col` to the current position and changes their meta type from `CELL_LINK` back to `CELL_TEXT`. This prevents orphaned link cells that point to non-existent link entries.

**Tag dispatch — forms:**

| Tag | Action |
|-----|--------|
| `<form action="..." method="...">` | Save action URL, increment form_id. Default method GET. |
| `<input type="text" name="..." value="..." size="N">` | Emit `[___________]` (N chars wide, default 20). Register form_field_t. |
| `<input type="password" ...>` | Same rendering as text. Field type tracks password for `*` display on edit. |
| `<input type="submit" value="...">` | Emit `[ value ]` as button. Register as CELL_BUTTON. |
| `<input type="checkbox" name="...">` | Emit `[ ]` or `[x]`. Register as CELL_CHECKBOX. |
| `<input type="radio" name="...">` | Emit `( )` or `(*)`. Register as CELL_RADIO. |
| `<input type="hidden" name="..." value="...">` | No rendering. Store name/value in field array. |
| `<select name="...">` | Buffer `<option>` text. Render `[first_option v]`. Register as CELL_SELECT. |
| `<textarea name="..." rows="N" cols="N">` | Render bordered box (N cols × M rows). Register field. |
| `<button>` | Treated as submit. |
| `</form>` | End form scope. |

**Form field storage:** Far-allocated array in `page_buffer_t`:

```c
#define MAX_FORM_FIELDS 24
#define FIELD_NAME_MAX  32
#define FIELD_VALUE_MAX 128

typedef struct {
    unsigned char type;
    char name[FIELD_NAME_MAX];
    char value[FIELD_VALUE_MAX];
    int row, col, width;
    int form_id;
    unsigned char checked;
} form_field_t;
```

170 bytes × 24 fields = 4,080 bytes far per page.

**Form submission:** GET-method forms: build URL from action + `?` + URL-encoded field values, then navigate. POST-method forms: display "POST not supported" in status bar. Honest limitation — POST requires content-type handling and request body construction that can be added in a future version.

**Tag dispatch — images:**

`<img src="..." alt="..." width="N" height="N">`:

1. Always emit `[IMG: alt text]` in ATTR_DIM on the current line.
2. If `src` matches a hardcoded demo image identifier (e.g., `about:demo`), call `image_render_to_page()` to embed half-block art on the following rows.
3. For real URLs: the image rendering infrastructure is in place but requires firmware-side image transcoding to deliver pre-quantized 16-color indexed pixel data. Until then, alt-text display.
4. Advance parser row cursor by `ceil(height / 16)` if dimensions are known, else by 0 (no space reservation for unknown images).

**Entity handling:**

On `&`: switch to PS_ENTITY, accumulate chars until `;` or non-alpha (max 10).

| Entity | Result |
|--------|--------|
| `&amp;` | `&` |
| `&lt;` | `<` |
| `&gt;` | `>` |
| `&quot;` | `"` |
| `&nbsp;` | ` ` (space) |
| `&copy;` | `c` (best effort in CP437) |
| `&#NNN;` | Decimal codepoint → CP437 via utf8_to_cp437 table |
| `&#xHH;` | Hex codepoint → CP437 via utf8_to_cp437 table |
| Unknown | Emit raw `&text;` as-is |

**Text emission:**

```c
static void emit_char(html_parser_t *p, char ch)
{
    if (p->col >= 78) {
        if (p->in_pre) {
            /* <pre>: truncate with indicator, don't wrap */
            page_set_cell(p->page, p->row, 78, (char)0xAF, ATTR_DIM,
                          CELL_TEXT, 0);  /* >> indicator */
            return;
        }
        /* Normal text: word wrap (col 79 = scrollbar) */
        p->row++;
        p->col = p->indent;
    }
    if (p->row >= PAGE_MAX_ROWS - 1) return;  /* reserve last row for truncation footer */

    /* Whitespace collapsing (disabled in <pre>) */
    if (!p->in_pre && (ch == ' ' || ch == '\t' || ch == '\n')) {
        if (!p->line_has_content) return;
        if (p->col > p->indent) {
            page_cell_t prev = page_get_cell(p->page, p->row, p->col - 1);
            if (prev.ch == ' ') return;
        }
        ch = ' ';
    }

    page_set_cell(p->page, p->row, p->col, ch, p->current_attr,
                  p->in_link ? CELL_LINK : CELL_TEXT,
                  p->in_link ? (unsigned short)p->page->link_count : 0);
    p->col++;
    p->line_has_content = 1;
}
```

**Public interface:**

```c
void html_init(html_parser_t *p, page_buffer_t *page);
void html_parse_chunk(html_parser_t *p, const char far *data, int len);
void html_finish(html_parser_t *p);
```

`html_finish()` flushes any pending table buffer, closes open tags, and — if the parser hit `PAGE_MAX_ROWS - 1` during parsing — writes a visible truncation footer on the last row:

```c
/* In html_finish(), after flushing pending state */
if (p->row >= PAGE_MAX_ROWS - 1) {
    put_str_to_page(p->page, PAGE_MAX_ROWS - 1, 0,
                    "[Page truncated at 200 rows]", ATTR_DIM);
    p->page->total_rows = PAGE_MAX_ROWS;
}
```

This turns a hidden limit into an honest product behavior.

---

### 6. Image Half-Block Renderer (`image.c` / `image.h`)

**Technique:** CP437 half-block characters encode 2 vertical pixels per text cell using foreground and background colors from the 4-bit VGA text-mode attribute byte.

| Top pixel | Bottom pixel | Character | Attribute |
|-----------|-------------|-----------|-----------|
| Color A | Color A | 0xDB (full block) | `A \| (A << 4)` |
| Color A | Color B | 0xDF (upper half) | `A \| (B << 4)` |
| Color B | Color A | 0xDC (lower half) | `A \| (B << 4)` |
| Black | Black | 0x20 (space) | 0x00 |

This yields 160×50 pseudo-pixel resolution on an 80×25 text display.

**Image data structure:**

```c
typedef struct {
    unsigned short width, height;    /* in pseudo-pixels */
    unsigned char far *pixels;       /* [width * height] palette indices 0–15 */
} image_t;
```

**VGA 16-color palette (default text mode):**

```c
static const unsigned char __far vga_pal[16][3] = {
    {0,0,0},     {0,0,170},   {0,170,0},   {0,170,170},
    {170,0,0},   {170,0,170}, {170,85,0},   {170,170,170},
    {85,85,85},  {85,85,255}, {85,255,85},  {85,255,255},
    {255,85,85}, {255,85,255},{255,255,85},  {255,255,255}
};
```

**Quantizer:** Euclidean distance in RGB space (integer-only, no FPU). For each input pixel, compare against all 16 palette entries, pick closest. ~150 cycles/pixel on 8088. For pre-quantized images (palette index already 0–15), the quantizer is bypassed.

**Demo image:** A `static const unsigned char __far` pixel array embedded in image.c. Small test pattern or logo (e.g., 40×20 pixels = 800 bytes far). Referenced as `about:demo` in stub page HTML. Proves the rendering pipeline works without network delivery.

**Page embedding:** `image_render_to_page()` writes CELL_IMAGE cells directly into the page buffer. The HTML parser advances its row cursor by `ceil(height / 2)` text rows after the image.

**Interface:**

```c
void          image_render_to_page(page_buffer_t *page, int row, int col,
                                   image_t *img);
unsigned char image_quantize_rgb(unsigned char r, unsigned char g,
                                 unsigned char b);
void          image_free(image_t *img);
```

**DGROUP cost:** 8 bytes (pointer to current image being rendered). All image data is `__far`.

---

### 7. Tab System (`tabs.c` / `tabs.h`)

**Per-tab state (far-allocated):**

```c
#define TAB_MAX      4
#define TAB_HIST_MAX 10

typedef struct {
    page_buffer_t *page;
    urlbar_t urlbar;
    char history[TAB_HIST_MAX][256];
    int history_pos, history_count;
    char title[40];
    int active;
} tab_t;
```

Per-tab memory: ~2,660 bytes (near fields) + 80KB (page buffer far arrays) = ~83KB total far per tab. 4 tabs = ~332KB far.

**Tab manager (near heap):**

```c
typedef struct {
    tab_t far *tabs[TAB_MAX];
    int count;
    int active;
} tab_mgr_t;
```

**Tab bar rendering (row 0):**

```
[1:Start Pa][2:Test    ][3:Help   ][4:       ]   Cathode v0.2
```

Each tab slot: 10 characters. Active tab: ATTR_STATUS (highlighted). Inactive tabs: ATTR_DIM. Empty slots: ATTR_BORDER with placeholder. Version label right-aligned.

**Lifecycle:**

- `tabs_init()`: Allocate tab 0 with about:home. Pre-allocate all 4 `tab_t` structs (small, 2.6KB each) to avoid fragmentation. Page buffers allocated lazily on `tabs_new()`.
- `tabs_new()`: Allocate page buffer for next free slot. Navigate to about:home. Return -1 if `page_alloc()` fails ("Not enough memory" in status bar).
- `tabs_close()`: Free page buffer. Switch to nearest active tab. Cannot close last tab.
- `tabs_switch()`: Change `active` index. Trigger full re-render.

**Concurrency model:** Blocking. When a tab is fetching a page, the entire browser blocks until fetch completes (or user presses Escape to cancel). Tabs are for keeping multiple loaded pages in memory and switching between them instantly — not for background loading. This is an honest limitation of single-threaded DOS.

**History migration:** The old `browser_state_t.history[20][256]` (5,120 bytes DGROUP) is eliminated. History now lives in per-tab far-allocated structs. **Net DGROUP savings: ~5KB.**

**DGROUP cost:** tab_mgr_t = 20 bytes. Net change: **-5,100 bytes** (saves far more than it costs).

---

### 8. Bookmarks (`bookmark.c` / `bookmark.h`)

**File format:** `CATHODE.BMK`, plain text, one URL per line, max 32 entries.

```
https://barelybooting.com
about:help
https://example.com
```

**Interface:**

```c
#define BM_MAX  32
#define BM_FILE "CATHODE.BMK"

int  bookmark_add(const char *url);
int  bookmark_remove(int index);
int  bookmark_count(void);
void bookmark_build_page(page_buffer_t *page);
```

**`bookmark_build_page`:** Reads CATHODE.BMK line by line, constructs an HTML string (`<h1>Bookmarks</h1><ul><li><a href="...">...</a></li>...</ul>`), feeds it through the HTML parser. The bookmarks page gets full browser rendering — clickable links, proper styling.

**Keybindings:** Ctrl+D appends current page URL to file (shows "Bookmarked" in status bar). Ctrl+B navigates to `about:bookmarks`.

**DGROUP cost:** ~10 bytes. File I/O uses stack-local buffers.

---

### 9. Find on Page (`search.c` / `search.h`)

**Interface:**

```c
#define SEARCH_MAX 60

typedef struct {
    char query[SEARCH_MAX + 1];
    int query_len;
    int active;
    int editing;
    int match_row, match_col;
    int match_count;
    int cursor;
} search_state_t;

void search_start(search_state_t *s);
int  search_handle_key(search_state_t *s, int key);
void search_find_next(search_state_t *s, page_buffer_t *page, int direction);
int  search_is_match(search_state_t *s, page_buffer_t *page, int row, int col);
```

**Search bar:** Ctrl+F replaces URL bar (row 1) with `Find: [query_________]`. Editing follows same pattern as URL bar (character insert, delete, cursor movement). Enter triggers first search. Escape cancels and restores URL bar.

**Scanning:** Linear scan of `page->cells[]` far array. Case-insensitive character comparison. Start from current match + 1 (N key) or - 1 (Shift+N). Wrap at page boundaries.

**Highlighting:** During `render_page()`, each cell is checked against active search state. Cells within a match span get `ATTR_SEARCH_HIT` (SCR_BLACK on SCR_YELLOW). The current match gets `ATTR_SEARCH_CUR` (SCR_BLACK on SCR_WHITE).

**Auto-scroll:** Finding a match scrolls the viewport to make the match row visible (centered if possible).

**DGROUP cost:** 75 bytes.

---

### 10. Color Themes (`theme.c` / `theme.h`)

**Four themes** stored as VGA DAC palette data:

| Theme | Description |
|-------|-------------|
| Classic | Default VGA 16-color palette. No DAC reprogramming. |
| Amber | All 16 colors mapped to amber gradient (dark brown → bright amber). Monochrome amber CRT. |
| Green | All 16 colors mapped to green gradient (dark green → bright green). Green phosphor terminal. |
| Blue | All 16 colors mapped to navy-to-cyan gradient. Cold terminal aesthetic. |

**Palette storage:** `static const unsigned char __far themes[4][48]` = 192 bytes far. Each theme is 16 colors × 3 RGB bytes = 48 bytes.

**Application:** Write 16 DAC entries via VGA ports 3C8h/3C9h. Wait for vertical retrace (port 3DAh bit 3) to avoid flicker.

**Persistence:** Current theme saved to `CATHODE.CFG` (1-byte file). Loaded at startup. Missing file defaults to Classic.

**Graceful degradation:** `theme_init()` detects VGA via INT 10h AX=1A00h. On non-VGA hardware, theme cycling is disabled. F2 shows "Themes require VGA" in status bar.

**Interface:**

```c
void theme_init(void);
void theme_apply(int theme_id);
void theme_cycle(void);
int  theme_current(void);
```

**DGROUP cost:** 4 bytes.

---

### 11. Scroll Bar + Loading Animation

**Scroll bar:** Column 79 of the content area (rows 3–22). `render_page()` renders content columns 0–78, then draws the scrollbar at column 79. The HTML parser wraps at col 78 (78 characters of effective content width). `PAGE_COLS` remains 80 (page buffer is 80 wide) but the renderer stops content at col 78.

- Track: CP437 0xB0 (light shade) in ATTR_DIM
- Thumb: CP437 0xDB (full block) in ATTR_BORDER
- Thumb position: `thumb_row = scroll_pos * 19 / max_scroll`
- Hidden when content fits on one screen (no scrollbar needed)

Mouse click on col 79 scrolls proportionally: `scroll_pos = (click_row - 3) * max_scroll / 19`.

**Loading animation:** Spinner character `|/-\` at position (1, 24) on the status bar. Incremented between `ni_session_recv()` calls in the fetch loop. Also serves as a visual indicator that the browser hasn't frozen during a fetch.

**DGROUP cost:** 5 bytes.

---

### 12. Main Event Loop + Input Rewrite

**Non-blocking loop (replaces blocking `scr_getkey()`):**

```c
while (b.running) {
    int dirty = 0;

    /* Poll mouse — cursor movement is handled cheaply (2 cell writes),
       only clicks trigger full re-render (red-team fix: B7) */
    if (b.mouse.present) {
        mouse_poll(&b.mouse);
        if (b.mouse.event & (MOUSE_LCLICK | MOUSE_RCLICK)) {
            input_handle_mouse(&b, &b.mouse);
            dirty = 1;
        } else if (b.mouse.event & MOUSE_MOVED) {
            /* Reposition software cursor only — no full render */
            mouse_hide(&b.mouse);   /* restore old cell attr */
            mouse_show(&b.mouse);   /* highlight new cell attr */
        }
    }

    if (scr_kbhit()) {
        int key = scr_getkey();
        input_handle_key(&b, key);
        dirty = 1;
    }

    if (dirty && b.running) {
        mouse_hide(&b.mouse);
        render_all(tabs_current(&b.tabs)->page, ...);
        mouse_show(&b.mouse);
    }

    if (!dirty) {
        _asm { int 28h }   /* DOS idle — yields to TSRs */
    }
}
```

**Revised `browser_state_t`:**

```c
typedef struct {
    tab_mgr_t tabs;
    mouse_state_t mouse;
    search_state_t search;
    int running;
    char status_msg[80];
} browser_state_t;
```

History, URL bar, and page pointer have moved into per-tab state. The browser struct is now a lightweight coordinator.

**Focus mode priority (in input.c):**

1. `search.editing` → search bar handles keys
2. Active tab's `urlbar.editing` → URL bar handles keys
3. Active page's `focused_field >= 0` → form field handles keys
4. Default → normal browsing (scrolling, link selection, keybindings)

**New keybinding table:**

| Key | Normal | URL Edit | Search | Form Edit |
|-----|--------|----------|--------|-----------|
| Esc | Quit | Cancel | Cancel search | Unfocus |
| Enter | Follow link | Navigate | Find first | Submit (on button) |
| Tab | Next link/field | — | — | Next field |
| Shift+Tab | Prev link/field | — | — | Prev field |
| Ctrl+L / F6 | Focus URL | — | — | Focus URL |
| Ctrl+F | Search | — | — | — |
| Ctrl+D | Bookmark | — | — | — |
| Ctrl+B | Bookmarks page | — | — | — |
| Ctrl+T | New tab | — | — | — |
| Ctrl+W | Close tab | — | — | — |
| Alt+1–4 | Switch tab | — | — | — |
| F2 | Cycle theme | Cycle theme | Cycle theme | Cycle theme |
| F5 | Reload | — | — | — |
| Backspace | Go back | Delete char | Delete char | Delete char |
| N | — | — | Find next | Type char |
| Shift+N | — | — | Find prev | Type char |

---

### 13. Modified Page Buffer + Stub Pages

**New cell types in `page.h`:**

```c
#define CELL_TEXT       0
#define CELL_LINK       1
#define CELL_HEADING    2
#define CELL_INPUT      3
#define CELL_BOLD       4
#define CELL_IMAGE      5
#define CELL_BUTTON     6
#define CELL_CHECKBOX   7
#define CELL_RADIO      8
#define CELL_SELECT     9
```

**Form field far array added to `page_buffer_t`:**

```c
typedef struct {
    page_cell_t far *cells;
    unsigned char far *meta;
    unsigned short far *linkmap;
    form_field_t far *fields;       /* NEW */
    int field_count;                /* NEW */
    int focused_field;              /* NEW: -1 = none */
    int total_rows, scroll_pos;
    char title[80];
    char url[256];
    page_link_t links[MAX_LINKS];
    int link_count, selected_link;
} page_buffer_t;
```

**`_fmalloc` error handling strategy:**

- **Critical allocations** (cells, meta, linkmap): If any fail, `page_alloc()` returns NULL. Caller shows "Out of memory" error page.
- **Non-critical allocations** (fields, table_buf): If fail, set sentinel value (`field_count = -1`, `table_buf = NULL`). Features degrade gracefully — forms not interactive, tables render as flowing text.

**Form field rendering overlay (red-team fix):**

The HTML parser writes `[___________]` placeholder cells during parsing. When the user types into a focused field, `form_field_t.value` is updated but the page buffer cells still show underscores. `render_page()` has a form field overlay pass after the main cell loop:

```c
/* After main cell rendering loop in render_page() */
for (i = 0; i < page->field_count; i++) {
    form_field_t far *f = &page->fields[i];
    int vrow = f->row - page->scroll_pos;
    if (vrow < 0 || vrow >= PAGE_VIEWPORT) continue;
    render_form_field(f, vrow, i == page->focused_field);
}
```

`render_form_field()` overwrites the cell range at the field's position with the field's current `value[]`, padded with underscores. The focused field gets `ATTR_INPUT` highlighting and a visible cursor via `scr_cursor_pos()`. Password fields render `*` characters instead of the actual value.

**Stub pages rewritten as HTML:**

`stub_pages.c` now provides `static const char __far` HTML strings for `about:home`, `about:test`, `about:help`, and `about:bookmarks`. These are fed through the HTML parser via the fetch callback, exercising the full rendering pipeline.

The `about:test` page exercises every feature: headings, paragraphs, bold, italic, code, links, horizontal rules, ordered and unordered lists, a table, form fields (text input, checkbox, submit button), and a demo image.

---

## Memory Budget

### DGROUP (Near Heap)

| Component | v0.1 | v0.2 | Delta |
|-----------|------|------|-------|
| browser_state_t (history removed) | 5,528 | 200 | -5,328 |
| tab_mgr_t | — | 20 | +20 |
| mouse_state_t | — | 20 | +20 |
| search_state_t | — | 75 | +75 |
| html_parser_t (static) | — | 510 | +510 |
| fetch_state_t (static) | — | 280 | +280 |
| Theme state | — | 4 | +4 |
| Spinner | — | 5 | +5 |
| screen.c statics | 771 | 771 | 0 |
| String literals | 2,692 | ~1,500 | -1,192 |
| Stack | 1,500 | 2,000 | +500 |
| **Total** | **~10,500** | **~5,885** | **-4,615** |

v0.2 uses **less** DGROUP than v0.1, primarily because history (5KB) moves to far heap.

### Far Heap

| Component | Bytes |
|-----------|-------|
| Page buffer per tab (cells+meta+linkmap+fields) | ~84KB |
| 4 tabs × 84KB | 336KB |
| Tab structs (4 × 2.6KB) | 10.4KB |
| Table buffer (temporary, during parse) | 6.7KB |
| Demo image pixels | 0.8KB |
| Theme palettes | 0.2KB |
| UTF-8 mapping table | 0.4KB |
| HTML string literals (`__far`) | ~3KB |
| **Total far** | **~358KB** |

Available conventional memory after DOS + program: ~525KB. Far heap usage: **68%**. Adequate headroom.

---

## API Surface

| Module | Public Interface |
|--------|-----------------|
| `mouse` | `mouse_init(m)`, `mouse_poll(m)`, `mouse_show(m)`, `mouse_hide(m)` |
| `utf8` | `utf8_init(d)`, `utf8_feed(d, byte) → cp437_char` |
| `url` | `url_parse(url, parts)`, `url_resolve(base, rel, result)`, `url_encode(in, out, max)` |
| `fetch` | `fetch_page(url, callback, userdata, state) → status` |
| `html` | `html_init(p, page)`, `html_parse_chunk(p, data, len)`, `html_finish(p)` |
| `image` | `image_render_to_page(page, row, col, img)`, `image_quantize_rgb(r,g,b)`, `image_free(img)` |
| `tabs` | `tabs_init(mgr)`, `tabs_new(mgr)`, `tabs_close(mgr, idx)`, `tabs_switch(mgr, idx)`, `tabs_current(mgr)` |
| `bookmark` | `bookmark_add(url)`, `bookmark_remove(idx)`, `bookmark_count()`, `bookmark_build_page(page)` |
| `search` | `search_start(s)`, `search_handle_key(s, key)`, `search_find_next(s, page, dir)`, `search_is_match(s, page, r, c)` |
| `theme` | `theme_init()`, `theme_apply(id)`, `theme_cycle()`, `theme_current()` |

## Invariants

### Checkable by Inspection

- DGROUP usage under 64KB (~5KB estimated, 8% of limit)
- All `_fmalloc` return values checked for NULL
- No C99 features (all declarations before statements, `/* */` comments only)
- All source files compile with `-0 -ms -ox -w4 -zq`
- `PAGE_MAX_ROWS` bounds checked in `emit_char()`
- Tab count capped at `TAB_MAX` (4)
- Form field count capped at `MAX_FORM_FIELDS` (24)
- Link count capped at `MAX_LINKS` (64)
- Entity buffer capped at 10 characters
- Table buffer capped at 20 rows × 8 columns
- Redirect hops capped at 5
- History entries capped at `TAB_HIST_MAX` (10) per tab
- Mouse polling only when `mouse.present == 1`
- Theme cycling only when VGA detected

### Requires Testing

- HTML parser produces correct cells for all supported tag types
- Table column width calculation handles: empty cells, single column, width overflow, missing `</td>`
- UTF-8 decoder handles multi-byte sequences split across recv chunk boundaries
- URL resolver handles all relative forms: `../`, `./`, absolute path, protocol-relative, fragment, query
- Mouse click coordinates correctly account for scroll position in content area
- Theme palette changes preserve VGA state (mode, cursor) on apply and on exit
- Tab close followed by tab open does not leak far memory
- Find wraps correctly at page start/end boundaries
- Form GET submission correctly URL-encodes special characters
- Graceful degradation: non-VGA systems run without themes, no-mouse systems run keyboard-only
- Page fetch cancellation (Escape during fetch) leaves browser in consistent state
- Pages exceeding PAGE_MAX_ROWS are truncated without buffer overflow
- Malformed HTML (unclosed tags, misnested tags, missing attribute quotes) does not crash

## Acceptance Criteria (Per-Phase)

**See Victory Conditions section for the authoritative per-phase acceptance criteria.** Phase 1 criteria are the shipping gate. Phase 2, 2b, and 3 criteria are independent — each phase ships when its own criteria pass, with no dependency on later phases.

Cross-cutting criteria (all phases):
- Builds cleanly with `wmake -f Makefile cathode` at `-w4`
- Runs in DOSBox-X with VGA and EGA machine types
- No feature-flag combination causes a build failure

## Testing Strategy

**Stub pages as test corpus:** The rewritten `about:test` page exercises every HTML feature. This is the primary integration test — if it renders correctly, the parser handles headings, paragraphs, bold, italic, code, links, lists, tables, forms, and images.

**DOSBox-X machine types:** Test with `machine=svga_s3` (VGA, full features), `machine=ega` (no themes, no image colors beyond 16), and `machine=hercules` (MDA, monochrome — verify graceful degradation).

**Malformed HTML testing:** Add an `about:broken` stub page with intentionally malformed HTML (unclosed tags, misnested `<b><i></b></i>`, missing quotes, bare `<` in text) to verify the parser doesn't crash.

**Edge cases to verify:**
- Empty page (zero content)
- Page exceeding PAGE_MAX_ROWS (200 rows)
- Table with more columns than TBL_MAX_COLS
- Search with no matches
- Bookmark file missing on first run
- All 4 tabs open, attempt to open 5th
- Mouse not present (no INT 33h driver)
- Escape during fetch
- Nested tables (should render inner table as flowing text)
- `<pre>` block wider than 78 columns (should truncate with indicator)
- Link inside table cell (should render as plain text)
- Form field scrolled off-screen then back (value should persist)
- MAX_LINKS (32) exceeded on a page with many links
- Malformed HTML: unclosed `<script>` tag (should not blank entire page — add safety limit)
- Network timeout after 30 seconds of no data

## Known Limitations (v0.2)

1. **Nested tables not supported.** Inner `<table>` tags inside table cells are ignored; content flows into the outer cell as plain text.
2. **Links inside table cells not supported.** `<a>` tags within `<td>` render as plain text — href is lost.
3. **POST form submission not supported.** GET-method forms (search boxes) work. POST forms display "POST not supported" in status bar.
4. **No background tab loading.** Page fetches block the entire browser. Tabs hold already-loaded pages for instant switching, but there is no concurrent downloading.
5. **`<pre>` blocks truncated at 78 columns.** Content beyond col 78 is not displayed. A `»` indicator shows truncation occurred.
6. **Image display requires firmware support.** Only the hardcoded demo image renders as half-block art. Real `<img src>` URLs show alt-text only until the ESP32 firmware adds image transcoding.
7. **No CSS support.** All styling is based on HTML tag semantics mapped to fixed CP437/VGA text attributes.
8. **No JavaScript support.** Script tags are stripped entirely.
9. **Maximum 64 links per page.** Links beyond this render as plain text with `[+N more links]` indicator in status bar.
10. **Maximum 24 form fields per page.** Fields beyond this are not interactive.
11. **Table limit: 20 rows x 8 columns.** Larger tables are truncated with a "..." indicator.
12. **Word wrap is character-level, not word-level.** Long words break mid-character at column 78. No word-boundary look-ahead.
13. **Style nesting limited to 8 levels.** Deeper nesting silently flattens to the outermost attribute.

## Red-Team Mitigations (additional implementation notes)

**Network timeout:** The recv loop in fetch.c reads the BIOS tick counter (0040:006C) at the start of each recv iteration. If no data is received for 546 ticks (~30 seconds), the fetch is aborted with `FETCH_ERR_CONN`.

**DNS timeout (Gemini review):** `ni_dns_resolve()` could block indefinitely before the fetch loop begins. Apply the same BIOS tick timeout: 182 ticks (~10 seconds) for DNS resolution. If exceeded, return `FETCH_ERR_DNS`. The DNS call is the first step in `fetch_page()` for non-`about:` URLs.

**Key buffer flush:** After a blocking page fetch completes, flush the BIOS keyboard buffer: `while (scr_kbhit()) scr_getkey();`. This prevents queued keystrokes (pressed during the fetch) from triggering unintended actions.

**Disk full on bookmark save:** `bookmark_add()` checks the return value of `fprintf`. On failure, shows "Bookmark save failed" in status bar.

**browser_navigate() glue code:** The new `browser_navigate()` in browser.c:
1. Gets current tab via `tabs_current(&b->tabs)`
2. Clears the current page buffer
3. For `about:home` specifically: calls `build_home_logo(page)` to write the CP437 block-art CATHODE logo procedurally (rows 1-6), then sets the parser's starting row to 7. This preserves the distinctive logo that cannot be expressed in HTML (red-team fix: B9).
4. Initializes the static `html_parser_t` with the page buffer as target
5. Calls `fetch_page(url, html_callback, &parser, &fetch_state)` where `html_callback` feeds chunks to `html_parse_chunk()`
6. Calls `html_finish()` to flush pending state
7. Updates the tab's URL bar and history
8. Returns (page is now rendered in the page buffer, ready for display)

**Initialization order in main.c (red-team fix: B10):**
1. `scr_init()` — save video mode, set mode 3
2. `theme_init()` — detect VGA, load CATHODE.CFG, apply saved theme palette
3. `tabs_init()` — create tab 0
4. `mouse_init()` — detect INT 33h driver
5. `browser_navigate(about:home)` — render start page
6. `render_all()` — first frame
7. `scr_fade_in()` — fade from black to themed palette (reads current DAC state)

**Tab cycling across links and form fields (red-team fix: B5):**
Tab/Shift+Tab must cycle through both links and form fields in document order. Algorithm: on each Tab press, find the next focusable element by comparing row/col positions:
1. Find the next link after current position (scan `links[]` for the first link with `start_row > current_row`, or same row with `start_col > current_col`)
2. Find the next form field after current position (same scan of `fields[]`)
3. Pick whichever comes first in document order (lower row, or same row + lower col)
4. If neither found, wrap to the first focusable element on the page
This is O(n) per Tab press where n = link_count + field_count, max 56. Negligible on any CPU.

**Tab bar click bounds check (red-team fix: B4):**
Click dispatch for row 0: `tab_idx = col / 10; if (tab_idx >= mgr->count || !mgr->tabs[tab_idx]->active) return;` Clicks in the gap between the last tab and the version label are ignored.

**Style stack bounds check (red-team fix: B3):**
All push operations: `if (style_depth >= HTML_STYLE_DEPTH) return;` — silently ignore, use current attribute.
All pop operations: `if (style_depth <= 0) return;` — do nothing.
Style stack overflow is not an error condition — it means deep nesting is silently rendered with the outermost attribute. This is the same behavior as running out of colors on a monochrome terminal.

**bookmark_build_page clarification (red-team fix: B8):**
`bookmark_build_page()` does NOT call the HTML parser directly. It constructs an HTML string in a `static const char __far` buffer and returns a pointer. `stub_get_html("about:bookmarks")` calls `bookmark_build_page()` and returns the result. The fetch pipeline feeds the string to the parser — no reentrancy risk.

**Word wrap (red-team fix: B2):**
`emit_char()` wraps character-by-character at col 78. There is no word-boundary wrapping. Long words and URLs will break mid-character. This is a known limitation documented below. Word-boundary wrapping would require a look-ahead buffer and backtracking, adding ~100 lines and ~80 bytes of DGROUP for the word buffer. Deferred to v0.3.

**UTF-8 overlong sequences (red-team fix: B1):**
The UTF-8 decoder rejects overlong sequences (e.g., `0xC0 0xBC` for U+003C) by checking that the decoded codepoint is within the minimum range for its byte length: 2-byte sequences must decode to U+0080+, 3-byte to U+0800+, 4-byte to U+10000+. Overlong sequences emit `?`. This is defense-in-depth — the parser is already safe because tag detection happens on raw bytes before UTF-8 decoding.

---

## Appendix B: Round 3 Fixes — Implementation Specifications

A third and final red-team review found 31 issues (1 critical, 8 high, 13 medium, 9 low). The critical and high findings are resolved below. Medium/low items are noted inline.

### Migration: v0.1 → v0.2 Structs and Interfaces

**`browser_state_t` replacement (F2, F3):** The v0.1 `browser_state_t` in browser.h is completely replaced. The following fields are REMOVED: `page_buffer_t *current_page`, `char history[HISTORY_MAX][256]`, `int history_pos`, `int history_count`, `urlbar_t urlbar`, `int menu_open`. The `#define HISTORY_MAX 20` is removed. The new struct is as specified in Component 12. To access the active page: `tabs_current(&b->tabs)->page`. To access the URL bar: `tabs_current(&b->tabs)->urlbar`. To access history: through the active tab's `history[]` array.

**`stub_fetch_page()` replaced by `stub_get_html()` (F4):** The existing function `int stub_fetch_page(const char *url, page_buffer_t *page)` is deleted. The new export:

```c
/* stub_pages.h */
const char far *stub_get_html(const char *url);
```

Returns a far pointer to a static HTML string for known `about:` URLs, or NULL for unknown URLs. Called by `fetch_page()` when the URL scheme is `about:`.

**`render_titlebar()` replaced by `render_tabbar()` (F5):** The existing `render_titlebar()` in render.c is deleted. The new function:

```c
/* render.h */
#define LAYOUT_TAB_ROW      0   /* was LAYOUT_TITLE_ROW */
#define LAYOUT_URL_ROW      1   /* unchanged */
#define LAYOUT_SEP1_ROW     2   /* unchanged */
#define LAYOUT_CONTENT_TOP  3   /* unchanged */
#define LAYOUT_CONTENT_BOT  22  /* unchanged */
#define LAYOUT_SEP2_ROW     23  /* unchanged */
#define LAYOUT_STATUS_ROW   24  /* unchanged */

void render_tabbar(tab_mgr_t *mgr);
```

`render_all()` calls `render_tabbar()` instead of `render_titlebar()`. The status bar (row 24) gains the page title on the left side (where scroll position was).

### Missing Function Prototypes

**`input_handle_mouse()` (F6):**

```c
/* input.h */
void input_handle_mouse(browser_state_t *b, mouse_state_t *m);
```

Dispatches based on click row/col per the click dispatch table. Called from the main event loop only on LCLICK/RCLICK events.

**`render_form_field()` (F7):**

```c
/* render.c (static, not exported) */
static void render_form_field(form_field_t far *f, int vrow, int focused);
```

Rendering rules per field type:
- `CELL_INPUT` (text): Write `f->value` left-aligned in the field region, pad with underscores to `f->width`. If `focused`, use `ATTR_INPUT` (bright on dark) and position cursor at value end.
- Password: Same as text but write `*` for each character in value.
- `CELL_BUTTON`: Write `[ value ]` centered. If focused, use `ATTR_LINK_SEL`.
- `CELL_CHECKBOX`: Write `[x]` if checked, `[ ]` if not. Toggle on Space/Enter/click.
- `CELL_RADIO`: Write `(*)` if selected, `( )` if not.
- `CELL_SELECT`: Write `[current_value v]`. On Enter/click when focused, show option list as overlay (future — v0.2 cycles options with Up/Down).

**`build_home_logo()` (F8):**

```c
/* stub_pages.c (or browser.c) */
void build_home_logo(page_buffer_t *page);
```

Writes the CP437 block-art CATHODE logo to page rows 1-6, cols 13-66, using the existing gradient attributes (ATTR_BORDER, ATTR_SELECTED, ATTR_HIGHLIGHT). After calling this, the html_parser_t must have its `row` field set to 7 before `html_parse_chunk` is called. This is done in `browser_navigate()` after `html_init()`: `parser.row = 7; parser.col = 0;`.

**`tabs_current()` return type (F9):**

```c
/* tabs.h */
tab_t far *tabs_current(tab_mgr_t *mgr);
```

Returns a far pointer. Callers access fields via far pointer dereferencing. On 8088, each `->field` access through a far pointer costs ~12 extra cycles. For the render loop (which runs once per frame), this is acceptable. For hot paths, cache the page pointer locally: `page_buffer_t *page = tabs_current(&mgr)->page;` — this is a near pointer to a near struct (the page_buffer_t header itself is near-allocated).

**`scr_getattr()` / `scr_setattr()` (F35):**

```c
/* screen.h */
unsigned char scr_getattr(int x, int y);
void scr_setattr(int x, int y, unsigned char attr);
```

Both trivial: read/write the attribute byte at VGA buffer address `0xB800:(y*80+x)*2+1`.

### Security: Buffer Overflow Guards

**HTML tag name overflow (F12):** In PS_TAG_NAME state, when `tag_len >= HTML_MAX_TAG - 1`, stop accumulating characters into `tag_name[]`. Continue scanning until `>`, space, or `/` to find the end of the tag. The tag will fail to match any known tag and be treated as unknown (ignored). No buffer overflow.

**HTML attribute value overflow (F13):** In attribute value accumulation, when `attr_val_len >= HTML_MAX_ATTR - 1`, stop accumulating. Continue scanning until the closing quote or `>`. Truncated values are functionally harmless — a truncated `href` produces a broken link, which is the correct degraded behavior.

**HTTP header line length (F15):** Header parsing uses a 512-byte stack-local line buffer. Headers are read character-by-character from the recv buffer. When the line exceeds 511 bytes, excess characters are discarded until `\n` is found. Total header byte limit: 8,192 bytes. If exceeded, abort fetch with `FETCH_ERR_HTTP`.

**Bookmark file line length (F14):** `bookmark_build_page()` reads lines with `fgets(buf, URL_MAX + 2, fp)` — buffer size is 257 bytes. Lines longer than 255 chars are split by `fgets`, producing a truncated URL. This is acceptable — URLs > 255 chars exceed URL_MAX and would be truncated anyway.

**URL parse component overflow (F16):** `url_parse()` truncates host and path to their field sizes (127 chars each). Excess characters are silently discarded. `strncpy` with explicit null termination.

### Bookmark Buffer Problem (F26)

The round 2 text saying bookmark_build_page "constructs an HTML string in a `static const char __far` buffer" is wrong. The correct specification:

`bookmark_build_page()` allocates a temporary buffer via `_fmalloc(BM_BUF_SIZE)` where `BM_BUF_SIZE = 12288` (12KB — sufficient for 32 bookmarks with full-length URLs plus HTML wrapper). It constructs the HTML string in this buffer via `_fmemcpy` and manual string building. The function returns the far pointer. The caller (`stub_get_html`) stores this pointer. After `html_finish()` completes in `browser_navigate()`, the buffer is freed via `_ffree()`.

If `_fmalloc` fails, `bookmark_build_page()` returns a minimal static fallback: `"<h1>Bookmarks</h1><p>Not enough memory to display bookmarks.</p>"`.

Buffer size math: 32 bookmarks x (`<li><a href="` + 255 URL + `">` + 255 URL + `</a></li>\n`) = 32 x ~530 = ~17KB worst case. **Revised: `BM_BUF_SIZE = 20480` (20KB).** This comfortably fits 32 max-length bookmarks. Freed immediately after parsing.

### Feature Interactions

**Search highlight vs link highlight (F17):** Search highlighting takes priority. In `render_page()`, the rendering order is: (1) apply cell attribute from page buffer, (2) override with link highlight if CELL_LINK and selected, (3) override with search highlight if within a match. Step 3 always wins. This means search matches on links show in search colors, not link colors.

**Typing into off-screen form field (F18):** When a form field is focused and the user scrolls it off-screen, typing still updates `field->value` (the form field overlay just skips rendering). On the next keystroke that modifies the field, auto-scroll the viewport to make the focused field visible: `page->scroll_pos = max(0, field->row - PAGE_VIEWPORT / 2)`.

**Back navigation re-fetches (F20):** Acknowledged as a known limitation. History stores URLs only, not page content. Back/Forward triggers a re-fetch. For `about:` pages this is instant (stub lookup). For `http:`/`https:` pages, this means a network round-trip. A page cache would require storing rendered page buffers per history entry (~80KB each), which exceeds available far memory with 4 tabs. This is the correct tradeoff for the memory constraints.

**Bookmarking about: URLs (F19):** Allowed. `about:home`, `about:help`, `about:test` are valid bookmark targets. They're instant to load and useful as quick-access links.

### Link ID Walk-Back Algorithm (F33)

When `page_add_link()` fails at MAX_LINKS, the `</a>` handler walks back to reset cells:

```c
/* In html.c, on </a> when page_add_link fails */
{
    int r = p->link_start_row;
    int c = p->link_start_col;
    while (r < p->row || (r == p->row && c < p->col)) {
        if (r < PAGE_MAX_ROWS && c < PAGE_COLS)
            p->page->meta[PAGE_IDX(r, c)] = CELL_TEXT;
        c++;
        if (c >= PAGE_COLS) { c = 0; r++; }
    }
}
```

This iterates every cell from start to current position, resetting meta type. It handles multi-row links correctly by wrapping at PAGE_COLS.

### url_resolve Buffer Size (F11)

`url_resolve(base, relative, result)` writes to `result` which must be at least `URL_MAX + 1` (256) bytes. The function truncates output to URL_MAX characters. Callers declare `char resolved[URL_MAX + 1]`.

### Table Buffer Two-Level Allocation (F22)

```c
table_buf_t far *tb = (table_buf_t far *)_fmalloc(sizeof(table_buf_t));
if (!tb) { /* fall back to flowing text */ }
tb->cells = (tbl_cell_t far *)_fmalloc(TBL_MAX_ROWS * TBL_MAX_COLS * sizeof(tbl_cell_t));
if (!tb->cells) { _ffree(tb); /* fall back */ }
```

Two separate `_fmalloc` calls. Both must succeed. Freed together on `</table>` or on fallback.

### Exit Cleanup (F27, F28, F29)

`browser_shutdown()` iterates all tabs and calls `tabs_close()` for each, freeing page buffers and tab structs. Mouse cursor attribute is restored by `mouse_hide()` before `scr_fade_out()`. `scr_shutdown()` restores the original video mode via INT 10h, which resets the VGA palette to defaults. Theme palette does not persist after exit.

Cleanup order: `scr_fade_out()` → `mouse_hide()` → `browser_shutdown()` → `scr_shutdown()`.

### `__far` vs `far` Keyword (F25)

Both are synonyms in OpenWatcom. The codebase uses `far` (matching existing page.c style). The design document is normalized to `far` for consistency.

---

## Appendix A: Red-Team Review Summary

Three adversarial reviews were conducted. Round 1 found 14 issues. Round 2 found 12 new issues. Round 3 found 31 issues (including verification of prior fixes). All have been addressed.

### Round 1 Findings

| Finding | Severity | Resolution |
|---------|----------|------------|
| Stack overflow: html_parser_t + recv buffer on stack exceeds 2KB | Critical | Made html_parser_t and fetch_state_t static |
| Form field editing pipeline undesigned | Critical | Added form field overlay pass in render_page() |
| Nested tables undefined | High | Documented: inner tables ignored, text flows into outer cell |
| Column 79 / PAGE_COLS inconsistency | High | Clarified: PAGE_COLS stays 80, renderer stops at 78, scrollbar at 79 |
| Tab bar vs title bar layout conflict | High | Tab bar replaces title bar at row 0, page title in tab label |
| about: URL routing ambiguous | High | fetch_page() is single entry point, branches on scheme |
| Mouse cursor needs scr_getattr/scr_setattr | Medium | Added to screen.h/screen.c modification list |
| Links in table cells silently stripped | Medium | Documented limitation, deferred to v0.3 |
| `<pre>` truncation at 78 cols | Medium | Hard truncate with indicator character |
| MAX_LINKS overflow creates ghost cells | Medium | `</a>` handler walks back and resets cell types |
| Network timeout missing | Medium | 30-second BIOS tick timeout in recv loop |
| Key buffer flush after fetch | Low | Flush keyboard buffer after fetch completes |
| html_parser_t size underestimated (420 → 510) | Low | Corrected in design |
| Scope estimate low (3,000 → 3,200) | Low | Updated estimate |

### Round 2 Findings

| Finding | Severity | Resolution |
|---------|----------|------------|
| Style stack overflow — no bounds check on push/pop (B3) | High | Push checks `depth < max`, pop checks `depth > 0`. Silent flatten. |
| Tab bar click gap dispatches to invalid tab index (B4) | High | Bounds check: `if (tab_idx >= count) return;` |
| Tab cycling across links + form fields has no merge algorithm (B5) | High | Document-order scan: compare next-link vs next-field row/col, pick earliest |
| Mouse movement triggers full re-render — 8088 perf killer (B7) | High | Split: MOUSE_MOVED only repositions cursor (2 cell writes), clicks trigger full render |
| bookmark_build_page may cause parser reentrancy (B8) | Medium | Clarified: returns HTML string, does not call parser directly |
| CP437 block art CATHODE logo lost in HTML migration (B9) | Medium | Preserve procedural logo for about:home, parser starts below it |
| Theme init vs fade-in ordering — visual pop risk (B10) | Medium | Specified init order: scr_init → theme_init → navigate → render → fade_in |
| Layout constants not updated for tab bar (A5) | Medium | LAYOUT_TITLE_ROW becomes LAYOUT_TAB_ROW, no numeric change (still row 0) |
| UTF-8 overlong sequence rejection not specified (B1) | Low | Decoder rejects overlongs, emits `?` |
| Word wrap mid-word not documented (B2) | Low | Added to Known Limitations |
| Content click area includes col 78 truncation column (A4) | Low | Click dispatch: content cols 0-77 for link/field detection |
| Scope estimate still low: 3,200 vs likely 3,800-4,200 (C) | Low | Acknowledged — html.c and fetch.c will likely exceed estimates |

**Rounds 1+2 total: 26 findings. All addressed.**

### Round 3 Findings

| Finding | Severity | Resolution |
|---------|----------|------------|
| Bookmark buffer: `static const` can't hold dynamic content (F26) | Critical | _fmalloc'd 20KB buffer, freed after parse. Fallback on alloc failure. |
| browser_state_t: no migration spec from v0.1 to v0.2 (F2) | High | Explicit replacement spec in Appendix B |
| stub_fetch_page vs stub_get_html: incompatible interfaces (F4) | High | Prototype for stub_get_html in Appendix B |
| render_titlebar() vs tab bar: no replacement spec (F5) | High | render_tabbar() prototype + layout constants in Appendix B |
| input_handle_mouse(): no prototype (F6) | High | Prototype in Appendix B |
| render_form_field(): no prototype or rendering rules (F7) | High | Prototype + per-type rules in Appendix B |
| tag_name[16] overflow on long tag names (F12) | High | Truncation at MAX_TAG-1, skip excess, treat as unknown |
| attr_val[128] overflow on long attribute values (F13) | High | Truncation at MAX_ATTR-1, skip to closing quote |
| HTTP header line length unbounded (F15) | High | 512-byte line buffer, discard excess. 8KB total header limit. |
| HISTORY_MAX 20 vs TAB_HIST_MAX 10 mismatch (F3) | Medium | HISTORY_MAX removed, TAB_HIST_MAX replaces it |
| build_home_logo() no prototype or module (F8) | Medium | Prototype in Appendix B, lives in stub_pages.c |
| tabs_current() return type ambiguous (F9) | Medium | Returns `tab_t far *`, documented in Appendix B |
| Bookmark file line length unbounded (F14) | Medium | fgets with 257-byte buffer, truncates long lines |
| url_parse component overflow (F16) | Medium | strncpy with explicit truncation |
| Search vs link highlight priority (F17) | Medium | Search wins (render order: cell → link → search) |
| Typing into off-screen form field (F18) | Medium | Auto-scroll viewport to focused field |
| Back navigation re-fetches from network (F20) | Medium | Acknowledged limitation — no page cache |
| Two-level far alloc for table buffer (F22) | Medium | Explicit two-call pattern in Appendix B |
| Link overflow walk-back algorithm unspecified (F33) | Medium | Algorithm with pseudocode in Appendix B |
| scr_getattr/scr_setattr prototypes missing (F35) | Medium | Prototypes in Appendix B |
| fetch_state_t size: 20 bytes vs 280 bytes (F36) | Medium | Corrected to 280 bytes |
| Stale appendix totals self-contradicting (F1) | Low | Removed stale line |
| Link ID invariant during emission implicit (F10) | Low | Documented: link_count is prospective ID during `<a>` |
| url_resolve buffer size unspecified (F11) | Low | Specified: URL_MAX+1 bytes |
| Bookmarking about: URLs intentional? (F19) | Low | Allowed — they're valid, instant-load URLs |
| Mouse hide/show not inside render_all() (F21) | Low | By design: caller manages mouse around render |
| __far vs far inconsistency (F25) | Low | Normalized to `far` (matching existing codebase) |
| No tabs_shutdown() (F27) | Low | browser_shutdown() iterates and frees all tabs |
| No mouse_shutdown() (F28) | Low | mouse_hide() before scr_fade_out() restores attribute |
| Palette depends on mode reset (F29) | Low | scr_shutdown() mode reset resets palette — documented |
| Click area col 78 stale in main table (F34) | Low | Fixed: cols 0-77 |

**Rounds 1+2+3 grand total: 57 findings. 1 critical, 12 high, 22 medium, 22 low. All addressed.**
