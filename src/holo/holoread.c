/* Holotape reader model: the tape list, the entry list and the layout. Pure. */

#include <stddef.h>
#include <stdint.h>

#include <robco/klibc.h>

#include "holofmt.h"

#define ROW_HEADER  0u
#define ROW_SUB     1u
#define ROW_RULE1   2u
#define ROW_LIST    3u
#define ROW_RULE2   28u
#define ROW_FOOTER  29u

static const char header_text[] = "UOS HOLOTAPE READER";
static const char footer_tapes[] = "UP/DOWN move   ENTER open tape   BACK leave reader";
static const char footer_entries[] = "UP/DOWN move   ENTER read entry   BACK tape list";
static const char empty_tapes[] = "  No holotapes found in SYS$HOLO.";

int hf_reader_init(hf_reader_t *r, const hf_tape_info_t *tapes, uint32_t ntapes)
{
    if (r == NULL || ntapes > HF_TAPES_MAX || (tapes == NULL && ntapes > 0)) {
        return HF_E_ARG;
    }
    k_memset(r, 0, sizeof(*r));
    r->tapes = tapes;
    r->ntapes = ntapes;
    r->screen = HF_SCR_TAPES;
    return HF_OK;
}

/* Moves *sel by one with wrap and keeps it inside the window at *top. */
static void move(uint32_t *sel, uint32_t *top, uint32_t n, int down)
{
    if (n == 0) {
        return;
    }
    if (down) {
        *sel = (*sel + 1 >= n) ? 0 : *sel + 1;
    } else {
        *sel = (*sel == 0) ? n - 1 : *sel - 1;
    }
    if (*sel < *top) {
        *top = *sel;
    } else if (*sel >= *top + HF_LIST_ROWS) {
        *top = *sel - HF_LIST_ROWS + 1;
    }
}

static void clear(hf_action_t *act)
{
    act->kind = HF_ACT_NONE;
    act->tape = 0;
    act->title = NULL;
    act->text = NULL;
    act->len = 0;
}

int hf_reader_event(hf_reader_t *r, hf_event_t ev, hf_action_t *act)
{
    if (r == NULL || act == NULL) {
        return HF_E_ARG;
    }
    clear(act);
    switch (r->screen) {
    case HF_SCR_TAPES:
        if (ev == HF_EV_UP || ev == HF_EV_DOWN) {
            move(&r->sel_tape, &r->top_tape, r->ntapes, ev == HF_EV_DOWN);
        } else if (ev == HF_EV_ENTER) {
            if (r->ntapes == 0) {
                return HF_OK;
            }
            act->tape = r->sel_tape;
            act->kind = r->tapes[r->sel_tape].err == HF_OK ? HF_ACT_LOAD : HF_ACT_REFUSED;
        } else if (ev == HF_EV_BACK) {
            act->kind = HF_ACT_EXIT;
        } else {
            return HF_E_ARG;
        }
        return HF_OK;
    case HF_SCR_ENTRIES:
        if (ev == HF_EV_UP || ev == HF_EV_DOWN) {
            move(&r->sel_entry, &r->top_entry, r->tape.count, ev == HF_EV_DOWN);
        } else if (ev == HF_EV_ENTER) {
            act->kind = HF_ACT_SHOW;
            act->tape = r->open_tape;
            act->title = hf_entry_name(&r->tape, r->sel_entry);
            hf_entry_text(&r->tape, r->sel_entry, &act->text, &act->len);
            r->screen = HF_SCR_READING;
        } else if (ev == HF_EV_BACK) {
            act->kind = HF_ACT_CLOSE;
            act->tape = r->open_tape;
            k_memset(&r->tape, 0, sizeof(r->tape));
            r->screen = HF_SCR_TAPES;
        } else {
            return HF_E_ARG;
        }
        return HF_OK;
    case HF_SCR_READING:
        if (ev == HF_EV_BACK) {
            r->screen = HF_SCR_ENTRIES;
        } else if (ev != HF_EV_UP && ev != HF_EV_DOWN && ev != HF_EV_ENTER) {
            return HF_E_ARG;
        }
        return HF_OK;
    }
    return HF_E_STATE;
}

int hf_reader_loaded(hf_reader_t *r, const void *image, uint32_t size)
{
    hf_tape_t t;
    int rc;

    if (r == NULL) {
        return HF_E_ARG;
    }
    if (r->screen != HF_SCR_TAPES || r->ntapes == 0) {
        return HF_E_STATE;
    }
    rc = hf_open(&t, image, size);
    if (rc != HF_OK) {
        return rc;
    }
    r->tape = t;
    r->open_tape = r->sel_tape;
    r->sel_entry = 0;
    r->top_entry = 0;
    r->screen = HF_SCR_ENTRIES;
    return HF_OK;
}

static void put(char *line, const char *s)
{
    k_strncpy(line, s, HF_LINE_SIZE);
    line[HF_LINE_MAX] = '\0';
}

static void rule(char *line)
{
    k_memset(line, '-', HF_LINE_MAX);
    line[HF_LINE_MAX] = '\0';
}

static void tape_row(char *line, const hf_tape_info_t *ti, int selected)
{
    if (ti->err == HF_OK) {
        k_snprintf(line, HF_LINE_SIZE, "%c %-12.12s %.63s", selected ? '>' : ' ', ti->file,
                   ti->title);
    } else {
        k_snprintf(line, HF_LINE_SIZE, "%c %-12.12s DAMAGED: %.48s", selected ? '>' : ' ',
                   ti->file, hf_strerror(ti->err));
    }
}

int hf_layout(const hf_reader_t *r, char lines[][HF_LINE_SIZE], uint32_t nlines)
{
    uint32_t i, n, top, sel;

    if (r == NULL || lines == NULL) {
        return HF_E_ARG;
    }
    if (nlines < HF_ROWS) {
        return HF_E_BUF;
    }
    if (r->screen == HF_SCR_READING) {
        return HF_E_STATE;
    }
    for (i = 0; i < HF_ROWS; i++) {
        lines[i][0] = '\0';
    }
    put(lines[ROW_HEADER], header_text);
    rule(lines[ROW_RULE1]);
    rule(lines[ROW_RULE2]);

    if (r->screen == HF_SCR_TAPES) {
        n = r->ntapes;
        top = r->top_tape;
        sel = r->sel_tape;
        k_snprintf(lines[ROW_SUB], HF_LINE_SIZE, "Holotapes in SYS$HOLO: %u", (unsigned)n);
        put(lines[ROW_FOOTER], footer_tapes);
        if (n == 0) {
            put(lines[ROW_LIST], empty_tapes);
        }
        for (i = 0; i < HF_LIST_ROWS && top + i < n; i++) {
            tape_row(lines[ROW_LIST + i], &r->tapes[top + i], top + i == sel);
        }
    } else {
        n = r->tape.count;
        top = r->top_entry;
        sel = r->sel_entry;
        k_snprintf(lines[ROW_SUB], HF_LINE_SIZE, "Tape: %.63s", hf_title(&r->tape));
        put(lines[ROW_FOOTER], footer_entries);
        for (i = 0; i < HF_LIST_ROWS && top + i < n; i++) {
            k_snprintf(lines[ROW_LIST + i], HF_LINE_SIZE, "%c %02u  %.31s",
                       top + i == sel ? '>' : ' ', (unsigned)(top + i + 1),
                       hf_entry_name(&r->tape, top + i));
        }
    }
    return HF_OK;
}
