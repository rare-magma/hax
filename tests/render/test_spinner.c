/* SPDX-License-Identifier: MIT */
#include <string.h>

#include "buf.h"
#include "harness.h"
#include "render/spinner.h"
#include "system/locale.h"
#include "terminal/ansi.h"

#define BRAILLE_PREFIX "\xE2\xA0"

static void test_glyph_is_one_braille_codepoint(void)
{
    const char *glyph = spinner_glyph_now();
    EXPECT(strncmp(glyph, BRAILLE_PREFIX, 2) == 0);
    EXPECT(glyph[3] == '\0');
}

static void test_status_shows_glyph_and_label(void)
{
    struct buf row;
    buf_init(&row);
    int cells = spinner_format_status(&row, "*", NULL, 0, 0, "working...", 0, 80);
    EXPECT_STR_EQ(row.data, ANSI_DIM "* working..." ANSI_RESET);
    EXPECT(cells == 12);
    buf_free(&row);
}

static void test_status_shows_context_and_highlights_warning(void)
{
    struct buf row;
    buf_init(&row);
    spinner_format_status(&row, "*", "67k / 266k (25%) · ~$0.038", 0, 0, "working...", -1,
                          80);
    EXPECT_STR_EQ(row.data,
                  ANSI_DIM "* 67k / 266k (25%) · ~$0.038 working..." ANSI_RESET);

    buf_reset(&row);
    spinner_format_status(&row, "*", "67k / 266k (25%) · ~$0.038", 0, 0, "working...",
                          151000, 100);
    EXPECT_STR_EQ(row.data, ANSI_DIM "* 2m 31s · 67k / 266k (25%) · ~$0.038 working..."
                            ANSI_RESET);

    buf_reset(&row);
    spinner_format_status(&row, "*", "300k / 262k (114%)", 13, 4, "working...", -1, 80);
    EXPECT_STR_EQ(row.data,
                  ANSI_DIM "* 300k / 262k (" ANSI_RED "114%" ANSI_FG_DEFAULT ") working..."
                           ANSI_RESET);

    buf_reset(&row);
    spinner_format_status(&row, "*", "67k / 266k (25%)", 0, 0, "[write] composing...", 151000,
                          20);
    EXPECT(strstr(row.data, "67k") == NULL);
    EXPECT(strstr(row.data, "2m 31s") == NULL);
    EXPECT(strstr(row.data, "[write]") != NULL);
    EXPECT(strchr(row.data, '\n') == NULL);

    buf_reset(&row);
    spinner_format_status(&row, "*", "context", 0, 0, "你好abc", -1, 8);
    EXPECT(strstr(row.data, "context") == NULL);
    EXPECT(strstr(row.data, "你...") != NULL);
    EXPECT(strchr(row.data, '\n') == NULL);
    buf_free(&row);
}

static void test_status_shows_elapsed_time_only_for_long_waits(void)
{
    struct buf row;
    buf_init(&row);
    spinner_format_status(&row, "*", NULL, 0, 0, "working...", 29999, 80);
    EXPECT(strstr(row.data, "\xC2\xB7") == NULL);

    buf_reset(&row);
    int cells = spinner_format_status(&row, "*", NULL, 0, 0, "working...", 75000, 80);
    EXPECT(strstr(row.data, " \xC2\xB7 working...") != NULL);
    EXPECT(cells > 12);
    buf_free(&row);
}

static void test_status_fits_its_budget(void)
{
    struct buf row;
    buf_init(&row);
    /* The elapsed time gives way first, then the label truncates. */
    int cells = spinner_format_status(&row, "*", NULL, 0, 0, "a long status label", 75000, 16);
    EXPECT(strstr(row.data, "\xC2\xB7") == NULL);
    EXPECT(cells <= 16);
    buf_free(&row);
}

/* Settles "thinking" so that it appears at SHOWN_AT_MS. */
#define SHOWN_AT_MS (1000 + SPINNER_LABEL_SETTLE_MS)

static void settle_thinking(struct spinner_label *label)
{
    spinner_label_init(label, NULL);
    spinner_label_request(label, "thinking", "thinking...", 1000);
    spinner_label_settle(label, SHOWN_AT_MS);
}

static void test_label_set_holds_until_the_next_set(void)
{
    struct spinner_label label;
    spinner_label_init(&label, NULL);
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_request(&label, "thinking", "thinking...", 1000);

    EXPECT(spinner_label_set(&label, "compacting", "compacting..."));
    EXPECT(label.pending_key == NULL);
    EXPECT(!spinner_label_request(&label, "working", "working...", 1000));
    spinner_label_request(&label, "thinking", "thinking...", 1000);
    spinner_label_settle(&label, 1000 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "compacting...");
    EXPECT(!spinner_label_set(&label, "compacting", "compacting..."));

    EXPECT(spinner_label_set(&label, "working", "working..."));
    spinner_label_request(&label, "thinking", "thinking...", 5000);
    spinner_label_settle(&label, 5000 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "thinking...");
    spinner_label_free(&label);
}

static void test_label_request_waits_until_stable(void)
{
    struct spinner_label label;
    spinner_label_init(&label, NULL);

    EXPECT(!spinner_label_request(&label, "thinking", "thinking...", 1000));
    spinner_label_settle(&label, 1000 + SPINNER_LABEL_SETTLE_MS - 1);
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_settle(&label, 1000 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "thinking...");
    EXPECT_STR_EQ(label.key, "thinking");
    spinner_label_free(&label);
}

static void test_label_repeats_update_text_but_keep_settling_time(void)
{
    struct spinner_label label;
    spinner_label_init(&label, NULL);

    spinner_label_request(&label, "processing", "processing... 10%", 1000);
    spinner_label_request(&label, "processing", "processing... 50%", 2500);
    spinner_label_settle(&label, 1000 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "processing... 50%");

    EXPECT(spinner_label_request(&label, "processing", "processing... 90%", 4000));
    EXPECT_STR_EQ(label.text, "processing... 90%");
    spinner_label_free(&label);
}

static void test_label_churn_shows_nothing_until_a_request_holds(void)
{
    struct spinner_label label;
    spinner_label_init(&label, NULL);

    spinner_label_request(&label, "a", "a...", 1000);
    spinner_label_request(&label, "b", "b...", 1500);
    spinner_label_request(&label, "a", "a...", 2500);
    spinner_label_settle(&label, 2500 + SPINNER_LABEL_SETTLE_MS - 1);
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_settle(&label, 2500 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "a...");
    spinner_label_free(&label);
}

static void test_label_gives_way_at_once(void)
{
    struct spinner_label label;
    settle_thinking(&label);

    EXPECT(spinner_label_request(&label, "working", "working...", SHOWN_AT_MS + 1));
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_free(&label);
}

static void test_label_gives_way_to_another_request_through_default(void)
{
    struct spinner_label label;
    settle_thinking(&label);

    EXPECT(spinner_label_request(&label, "compose", "[bash] composing...", SHOWN_AT_MS + 1));
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_settle(&label, SHOWN_AT_MS + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "working...");
    spinner_label_settle(&label, SHOWN_AT_MS + 1 + SPINNER_LABEL_SETTLE_MS);
    EXPECT_STR_EQ(label.text, "[bash] composing...");
    spinner_label_free(&label);
}

int main(void)
{
    locale_init_utf8();
    test_glyph_is_one_braille_codepoint();
    test_status_shows_glyph_and_label();
    test_status_shows_context_and_highlights_warning();
    test_status_shows_elapsed_time_only_for_long_waits();
    test_status_fits_its_budget();
    test_label_set_holds_until_the_next_set();
    test_label_request_waits_until_stable();
    test_label_repeats_update_text_but_keep_settling_time();
    test_label_churn_shows_nothing_until_a_request_holds();
    test_label_gives_way_at_once();
    test_label_gives_way_to_another_request_through_default();
    T_REPORT();
}
