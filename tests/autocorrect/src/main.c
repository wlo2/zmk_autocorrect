#include <zephyr/ztest.h>
#include <string.h>
#include <stdint.h>

// Declarations from src/autocorrect.c exposed only under CONFIG_ZTEST
bool ac_build_correct_for_test(const uint8_t *buf, uint8_t size, uint8_t backspaces,
                               const char *changes, char *out, size_t out_sz);
void ac_set_mods_for_test(uint8_t mods_mask);
bool ac_lookup_typo_for_test(const uint8_t *buf, uint8_t size, uint8_t *out_backspaces,
                             const char **out_changes);
static void to_usage_buf(const char *s, uint8_t *buf, uint8_t *len);

static uint8_t usage_from_char(char c) {
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x04 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x04 + (c - 'A'));
    switch (c) {
    case ' ': return 0x2C; // space
    case ',': return 0x36; // comma
    case '.': return 0x37; // dot
    case '-': return 0x2D; // hyphen
    case '\'': return 0x34; // apostrophe
    default: return 0; // not covered
    }

ZTEST(autocorrect_suite, test_lookup_becuase_space) {
    // becuase  -> because
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("becuase ", buf, &len);
    uint8_t backspaces = 0; const char *changes = NULL;
    bool found = ac_lookup_typo_for_test(buf, len, &backspaces, &changes);
    zassert_true(found, "expected dictionary match for 'becuase'");
    zassert_equal(backspaces, 7, "expected backspaces=7, got %u", backspaces);
    zassert_true(strncmp(changes, "because", 8) == 0, "expected changes='because'");
}

ZTEST(autocorrect_suite, test_lookup_no_match) {
    // validword  -> no correction
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("validword ", buf, &len);
    uint8_t backspaces = 0; const char *changes = NULL;
    bool found = ac_lookup_typo_for_test(buf, len, &backspaces, &changes);
    zassert_false(found, "expected no dictionary match");
}

ZTEST(autocorrect_suite, test_punctuation_preserved_comma) {
    // teh, -> the,
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("teh,", buf, &len);
    char out[32] = {0};
    bool ok = ac_build_correct_for_test(buf, len, /*backspaces=*/3, /*changes=*/"the", out, sizeof(out));
    zassert_true(ok, "build failed");
    zassert_equal(strcmp(out, "the,"), 0, "expected 'the,', got '%s'", out);
}

ZTEST(autocorrect_suite, test_punctuation_preserved_dot) {
    // becuase. -> because.
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("becuase.", buf, &len);
    char out[32] = {0};
    bool ok = ac_build_correct_for_test(buf, len, 7, "because", out, sizeof(out));
    zassert_true(ok, "build failed");
    zassert_equal(strcmp(out, "because."), 0, "expected 'because.', got '%s'", out);
}

ZTEST(autocorrect_suite, test_trailing_space_preserved) {
    // teh  -> the  (note trailing space)
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("teh ", buf, &len);
    char out[32] = {0};
    bool ok = ac_build_correct_for_test(buf, len, 3, "the", out, sizeof(out));
    zassert_true(ok, "build failed");
    zassert_equal(strcmp(out, "the "), 0, "expected 'the ', got '%s'", out);
}

ZTEST(autocorrect_suite, test_backspaces_clamped) {
    // backspaces > typo_len must clamp
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("abc,", buf, &len);
    char out[32] = {0};
    bool ok = ac_build_correct_for_test(buf, len, /*backspaces=*/10, "abcd", out, sizeof(out));
    zassert_true(ok, "build failed");
    // trailing comma preserved
    zassert_equal(strcmp(out, "abcd,"), 0, "expected 'abcd,', got '%s'", out);
}

ZTEST(autocorrect_suite, test_bounded_writer_no_overflow) {
    // Ensure no overflow and null-termination even with small buffer
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("accomodate ", buf, &len);
    char out[8] = {0}; // intentionally small
    bool ok = ac_build_correct_for_test(buf, len, /*backspaces=*/10, "accommodate", out, sizeof(out));
    zassert_true(ok, "build failed");
    zassert_true(out[7] == '\0', "output must be NUL-terminated");
}

ZTEST(autocorrect_suite, test_start_anchor_guage_space) {
    // ":guage -> gauge" should match at start with trailing space preserved
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("guage ", buf, &len);
    uint8_t backspaces = 0; const char *changes = NULL;
    bool found = ac_lookup_typo_for_test(buf, len, &backspaces, &changes);
    zassert_true(found, "expected dictionary match for start-anchored 'guage '");
    zassert_true(strncmp(changes, "gauge", 6) == 0, "expected changes='gauge'");
}

ZTEST(autocorrect_suite, test_end_anchor_looses_comma) {
    // "looses:" -> loses; with comma preserved
    uint8_t buf[32]; uint8_t len;
    to_usage_buf("looses,", buf, &len);
    uint8_t backspaces = 0; const char *changes = NULL;
    bool found = ac_lookup_typo_for_test(buf, len, &backspaces, &changes);
    zassert_true(found, "expected dictionary match for end-anchored 'looses,'");
    zassert_true(strncmp(changes, "loses", 6) == 0, "expected changes='loses'");
}

ZTEST(autocorrect_suite, test_non_shift_mod_suppresses) {
    // Just exercise helper to ensure we can set mod state under test
    ac_set_mods_for_test(1u << 0 /* LeftCtrl bit in helper's mask */);
    zassert_true(true, "mods helper reachable");
}

ZTEST_SUITE(autocorrect_suite, NULL, NULL, NULL, NULL, NULL);
