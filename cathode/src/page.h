/*
 * page.h - Page buffer types for Cathode browser
 *
 * Heavy data (cells, meta, link_map) are allocated separately on the
 * far heap since each can exceed 64KB in small model. The page_buffer_t
 * struct itself is small and stays on the near heap.
 */

#ifndef PAGE_H
#define PAGE_H

/* Page dimensions */
#define PAGE_COLS       80
#define PAGE_MAX_ROWS   200     /* 200 rows = 10 screenfuls */
#define PAGE_VIEWPORT   20      /* visible rows (screen rows 3-22) */

/* Cell types for navigation metadata */
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

/* A single rendered cell: character + attribute */
typedef struct {
    char ch;
    unsigned char attr;
} page_cell_t;

/* Link table entry */
#define LINK_URL_MAX    96
#define MAX_LINKS       64

typedef struct {
    char url[LINK_URL_MAX];
    int start_row, start_col;
    int end_row, end_col;
} page_link_t;

/* Form field entry (far-allocated array per page) */
#define MAX_FORM_FIELDS 24
#define FIELD_NAME_MAX  32
#define FIELD_VALUE_MAX 128

typedef struct {
    unsigned char type;             /* CELL_INPUT, CELL_BUTTON, etc. */
    char name[FIELD_NAME_MAX];
    char value[FIELD_VALUE_MAX];
    int row, col, width;
    int form_id;
    unsigned char checked;          /* for checkbox/radio */
} form_field_t;

/* Page buffer: header with far pointers to heavy data */
typedef struct {
    /* Far-allocated arrays (80 * 200 each) */
    page_cell_t far *cells;     /* [MAX_ROWS * COLS] char+attr, 32KB */
    unsigned char far *meta;    /* [MAX_ROWS * COLS] cell type, 16KB */
    unsigned short far *linkmap;/* [MAX_ROWS * COLS] link IDs, 32KB */
    form_field_t far *fields;   /* [MAX_FORM_FIELDS], ~4KB */

    int total_rows;
    int scroll_pos;
    char title[80];
    char url[256];

    page_link_t links[MAX_LINKS];
    int link_count;
    int selected_link;

    int field_count;            /* -1 if fields alloc failed */
    int focused_field;          /* -1 = no field focused */
} page_buffer_t;

/* Access helpers (inline row*80+col indexing) */
#define PAGE_IDX(row, col)  ((unsigned int)(row) * PAGE_COLS + (col))

/* Page buffer management */
page_buffer_t *page_alloc(void);
void page_free(page_buffer_t *page);
void page_clear(page_buffer_t *page);
void page_set_cell(page_buffer_t *page, int row, int col,
                   char ch, unsigned char attr, unsigned char type,
                   unsigned short link_id);
void page_add_link(page_buffer_t *page, const char *url,
                   int sr, int sc, int er, int ec);

/* Read accessors */
page_cell_t page_get_cell(page_buffer_t *page, int row, int col);
unsigned char page_get_meta(page_buffer_t *page, int row, int col);
unsigned short page_get_linkid(page_buffer_t *page, int row, int col);

#endif /* PAGE_H */
