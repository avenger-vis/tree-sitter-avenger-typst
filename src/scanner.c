// The external scanner of avenger-typst: the text of markup and inline raw text.
//
// Text is a run of characters between the other tokens of markup. A `*` or a `_` between two
// letters or digits continues it, as typst-syntax's `in_word` decides, and an automatic link
// such as `https://typst.app/` stays text, so its `//` doesn't start a comment.
//
// Raw text is inline: one backtick, the text, and one backtick on one line, or two backticks
// for empty raw text. Three backticks or more start a raw block, which labels don't support,
// and the scanner doesn't read them, so they are an error.

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <wctype.h>

enum TokenType {
    TEXT,
    RAW,
    CALL_PAREN,
    CALL_BRACKET,
    FIELD_DOT,
    ELSE,
    HASH,
    BLOCK_COMMENT,
    DOLLAR_OPEN,
    START,
    UNSUPPORTED,
    ERROR_SENTINEL,
};

void *tree_sitter_avenger_typst_external_scanner_create(void) { return NULL; }
void tree_sitter_avenger_typst_external_scanner_destroy(void *payload) { (void)payload; }
unsigned tree_sitter_avenger_typst_external_scanner_serialize(void *payload, char *buffer) {
    (void)payload;
    (void)buffer;
    return 0;
}
void tree_sitter_avenger_typst_external_scanner_deserialize(void *payload, const char *buffer,
                                                           unsigned length) {
    (void)payload;
    (void)buffer;
    (void)length;
}

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static bool is_newline(int32_t c) {
    return c == '\n' || c == '\r' || c == 0x0B || c == 0x0C || c == 0x85 || c == 0x2028 ||
           c == 0x2029;
}

// Typst's `is_space` of markup: spaces, tabs, and newlines.
static bool is_space(int32_t c) { return c == ' ' || c == '\t' || is_newline(c); }

// A letter or a digit outside the scripts that Typst doesn't count as wordy: Han, Hiragana,
// Katakana, and Hangul.
static bool is_wordy(int32_t c) {
    if (c == 0 || !iswalnum((wint_t)c)) return false;
    if (c >= 0x4E00 && c <= 0x9FFF) return false;   // CJK Unified Ideographs
    if (c >= 0x3400 && c <= 0x4DBF) return false;   // CJK Extension A
    if (c >= 0x3040 && c <= 0x30FF) return false;   // Hiragana and Katakana
    if (c >= 0xAC00 && c <= 0xD7AF) return false;   // Hangul syllables
    if (c >= 0x1100 && c <= 0x11FF) return false;   // Hangul Jamo
    return true;
}

// The characters that end text, or start another token at the start of text.
static bool ends_text(int32_t c) {
    switch (c) {
    case '\\': case '[': case ']': case '#': case '$': case '`':
        return true;
    default:
        return c == 0 || is_space(c);
    }
}

// Text from the cursor. `prev` is the character that the token already holds before the
// cursor, or 0 for none, as after a `.` that turned out to be no field's.
static bool scan_text(TSLexer *lexer, int32_t prev) {
    int32_t c = lexer->lookahead;
    if (prev == 0 && (ends_text(c) || c == '*' || c == '_')) return false;
    // Whether the text so far ends in `http:` or `https:`.
    char tail[7] = {0};
    bool in_link = false;
    bool any = prev != 0;
    for (;;) {
        c = lexer->lookahead;
        if (ends_text(c)) break;
        if (in_link) {
            advance(lexer);
            prev = c;
            any = true;
            lexer->mark_end(lexer);
            continue;
        }
        if (c == '/') {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '/' || lexer->lookahead == '*') {
                bool link = (tail[1] == 'h' && tail[2] == 't' && tail[3] == 't' && tail[4] == 'p' &&
                             tail[5] == ':') ||
                            (tail[0] == 'h' && tail[1] == 't' && tail[2] == 't' && tail[3] == 'p' &&
                             tail[4] == 's' && tail[5] == ':');
                if (!(link && lexer->lookahead == '/')) {
                    // A comment starts here: the text ends before the `/`.
                    return any;
                }
                in_link = true;
            }
            prev = '/';
            any = true;
            lexer->mark_end(lexer);
            for (int i = 0; i < 5; i++) tail[i] = tail[i + 1];
            tail[5] = '/';
            continue;
        }
        if (c == '*' || c == '_') {
            // In a word, the delimiter is text.
            if (!is_wordy(prev)) break;
            lexer->mark_end(lexer);
            advance(lexer);
            if (!is_wordy(lexer->lookahead)) {
                // A delimiter after the word: the text ends before it.
                return any;
            }
            prev = c;
            any = true;
            lexer->mark_end(lexer);
            for (int i = 0; i < 5; i++) tail[i] = tail[i + 1];
            tail[5] = (char)c;
            continue;
        }
        advance(lexer);
        prev = c;
        any = true;
        lexer->mark_end(lexer);
        for (int i = 0; i < 5; i++) tail[i] = tail[i + 1];
        tail[5] = c < 128 ? (char)c : '?';
    }
    return any;
}

static bool scan_raw(TSLexer *lexer) {
    if (lexer->lookahead != '`') return false;
    unsigned backticks = 0;
    while (lexer->lookahead == '`') {
        advance(lexer);
        backticks++;
    }
    if (backticks == 2) {
        lexer->mark_end(lexer);
        return true;
    }
    if (backticks != 1) return false;
    for (;;) {
        int32_t c = lexer->lookahead;
        if (c == 0 || is_newline(c)) return false;
        advance(lexer);
        if (c == '`') {
            lexer->mark_end(lexer);
            return true;
        }
    }
}

static bool is_id_start(int32_t c) { return c == '_' || iswalpha((wint_t)c); }

// A Typst identifier character: letters, digits, `_`, and `-`.
static bool is_id_continue(int32_t c) { return c == '_' || c == '-' || iswalnum((wint_t)c); }

// The result of a check that reads ahead: a match, no match before consuming anything, or no
// match after consuming characters, which then start text, since the scanner can't rewind.
typedef enum { NO_MATCH, MATCH, PARTIAL } Check;

// Whether the input at the cursor is the keyword `word`, not followed by an identifier's
// character. It consumes the input either way, and the token ends after the keyword.
static Check scan_word(TSLexer *lexer, const char *word, int32_t *last) {
    for (const char *w = word; *w; w++) {
        if (lexer->lookahead != *w) return w == word ? NO_MATCH : PARTIAL;
        *last = lexer->lookahead;
        advance(lexer);
    }
    if (is_id_continue(lexer->lookahead)) return PARTIAL;
    lexer->mark_end(lexer);
    return MATCH;
}

// The marker of a heading, a list item, an enumeration item, or a term at the cursor: `=`s,
// `-`, `+`, `/`, or digits and `.`, before whitespace or the end.
static Check scan_marker(TSLexer *lexer, int32_t *last) {
    int32_t c = lexer->lookahead;
    if (c == '=') {
        while (lexer->lookahead == '=') {
            *last = '=';
            advance(lexer);
        }
    } else if (c == '-' || c == '+' || c == '/') {
        // `//` and `/*` start comments, which aren't markers.
        if (c == '/') {
            lexer->mark_end(lexer);
            advance(lexer);
            if (lexer->lookahead == '/' || lexer->lookahead == '*') return NO_MATCH;
        } else {
            advance(lexer);
        }
        *last = c;
    } else if (c >= '0' && c <= '9') {
        while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
            *last = lexer->lookahead;
            advance(lexer);
        }
        if (lexer->lookahead != '.') return PARTIAL;
        *last = '.';
        advance(lexer);
    } else {
        return NO_MATCH;
    }
    if (lexer->lookahead != 0 && !is_space(lexer->lookahead)) return PARTIAL;
    lexer->mark_end(lexer);
    return MATCH;
}

// An automatic link (`https://...`), a label (`<name>`), or a reference (`@name`) at the
// cursor, which labels don't support. Text that only starts like one, such as `<` before a
// space or `http` alone, is no match, and the text scanner reads it.
static Check scan_unsupported_markup(TSLexer *lexer, int32_t *last) {
    int32_t c = lexer->lookahead;
    if (c == '<' || c == '@') {
        *last = c;
        advance(lexer);
        if (!is_id_continue(lexer->lookahead)) return PARTIAL;
        while (is_id_continue(lexer->lookahead) || lexer->lookahead == ':' || lexer->lookahead == '.') {
            advance(lexer);
        }
        if (c == '<' && lexer->lookahead == '>') advance(lexer);
        lexer->mark_end(lexer);
        return MATCH;
    }
    if (c == 'h') {
        const char *prefix = "http";
        for (const char *p = prefix; *p; p++) {
            if (lexer->lookahead != *p) return p == prefix ? NO_MATCH : PARTIAL;
            *last = lexer->lookahead;
            advance(lexer);
        }
        if (lexer->lookahead == 's') {
            *last = 's';
            advance(lexer);
        }
        if (lexer->lookahead != ':') return PARTIAL;
        *last = ':';
        advance(lexer);
        if (lexer->lookahead != '/') return PARTIAL;
        // `http:/` is text; `http://` a link.
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead != '/') {
            *last = '/';
            return PARTIAL;
        }
        while (lexer->lookahead != 0 && !iswspace((wint_t)lexer->lookahead)) advance(lexer);
        lexer->mark_end(lexer);
        return MATCH;
    }
    return NO_MATCH;
}

bool tree_sitter_avenger_typst_external_scanner_scan(void *payload, TSLexer *lexer,
                                                     const bool *valid_symbols) {
    (void)payload;
    // Error recovery offers every token; the scanner reads none of them there.
    if (valid_symbols[ERROR_SENTINEL]) return false;

    // The `(`, `[`, and `.` that continue a code expression are adjacent to it, so they come
    // before any whitespace, and before text could take them.
    if (lexer->lookahead == '(' && valid_symbols[CALL_PAREN]) {
        advance(lexer);
        lexer->result_symbol = CALL_PAREN;
        return true;
    }
    if (lexer->lookahead == '[' && valid_symbols[CALL_BRACKET]) {
        advance(lexer);
        lexer->result_symbol = CALL_BRACKET;
        return true;
    }
    if (lexer->lookahead == '.' && valid_symbols[FIELD_DOT]) {
        // A field's `.` comes before its name; a `.` before anything else ends the expression.
        lexer->mark_end(lexer);
        advance(lexer);
        if (is_id_start(lexer->lookahead)) {
            lexer->mark_end(lexer);
            lexer->result_symbol = FIELD_DOT;
            return true;
        }
        // The `.` ends the expression, and starts text, as in `#value.`
        if (valid_symbols[TEXT]) {
            lexer->mark_end(lexer);
            scan_text(lexer, '.');
            lexer->result_symbol = TEXT;
            return true;
        }
        return false;
    }

    // Whitespace is an extra, which the scanner skips itself, since it runs first. In markup,
    // two newlines are a paragraph break, which labels don't support.
    unsigned newlines = 0;
    while (is_space(lexer->lookahead) || iswspace((wint_t)lexer->lookahead)) {
        if (lexer->lookahead == '\n') newlines++;
        lexer->advance(lexer, true);
    }
    // A block comment, which nests, as `/* a /* b */ c */`. A `/` before anything else is text,
    // a fraction, or the start of a line comment, which the other rules read.
    if (lexer->lookahead == '/') {
        lexer->mark_end(lexer);
        advance(lexer);
        if (lexer->lookahead == '*' && valid_symbols[BLOCK_COMMENT]) {
            advance(lexer);
            unsigned depth = 1;
            int32_t prev = 0;
            while (lexer->lookahead != 0 && depth > 0) {
                int32_t c = lexer->lookahead;
                advance(lexer);
                if (prev == '*' && c == '/') {
                    depth--;
                    prev = 0;
                } else if (prev == '/' && c == '*') {
                    depth++;
                    prev = 0;
                } else {
                    prev = c;
                }
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = BLOCK_COMMENT;
            return true;
        }
        if (lexer->lookahead == '/' || lexer->lookahead == '*' || !valid_symbols[TEXT]) return false;
        // At the start of a label or of a line, `/ ` is the marker of a term.
        if ((valid_symbols[START] || newlines == 1) &&
            (lexer->lookahead == 0 || is_space(lexer->lookahead))) {
            lexer->mark_end(lexer);
            lexer->result_symbol = UNSUPPORTED;
            return true;
        }
        // `/` and what follows it is text.
        lexer->mark_end(lexer);
        scan_text(lexer, '/');
        lexer->result_symbol = TEXT;
        return true;
    }

    // The `#` of a code expression, which the expression follows directly: `# x` is an error.
    if (lexer->lookahead == '#' && valid_symbols[HASH]) {
        advance(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol =
            lexer->lookahead == 0 || iswspace((wint_t)lexer->lookahead) ? UNSUPPORTED : HASH;
        return true;
    }

    int32_t last = 0;
    Check check = NO_MATCH;
    // A code expression ends at a newline, so an `else` after one is text.
    if (valid_symbols[ELSE] && newlines == 0 && lexer->lookahead == 'e') {
        check = scan_word(lexer, "else", &last);
        if (check == MATCH) {
            lexer->result_symbol = ELSE;
            return true;
        }
    }
    if (valid_symbols[TEXT] && check == NO_MATCH) {
        if (newlines >= 2) {
            lexer->mark_end(lexer);
            lexer->result_symbol = UNSUPPORTED;
            return true;
        }
        // At the start of a label or of a line, the markers of headings and lists.
        if (valid_symbols[START] || newlines == 1) check = scan_marker(lexer, &last);
        if (check == NO_MATCH) check = scan_unsupported_markup(lexer, &last);
        if (check == MATCH) {
            lexer->result_symbol = UNSUPPORTED;
            return true;
        }
    }
    if (check == PARTIAL) {
        // The characters that the checks read start text.
        if (!valid_symbols[TEXT]) return false;
        lexer->mark_end(lexer);
        scan_text(lexer, last);
        lexer->result_symbol = TEXT;
        return true;
    }

    // The `$` that opens an equation. Math with whitespace at both ends is a block equation,
    // which labels don't support, so the scanner reads ahead to the closing `$`.
    if (valid_symbols[DOLLAR_OPEN] && lexer->lookahead == '$') {
        advance(lexer);
        lexer->mark_end(lexer);
        lexer->result_symbol = DOLLAR_OPEN;
        if (!iswspace((wint_t)lexer->lookahead)) return true;
        int32_t prev = ' ';
        bool in_string = false;
        while (lexer->lookahead != 0) {
            int32_t c = lexer->lookahead;
            if (c == '\\') {
                advance(lexer);
                prev = lexer->lookahead;
                advance(lexer);
                continue;
            }
            if (c == '"') in_string = !in_string;
            if (c == '$' && !in_string) {
                if (iswspace((wint_t)prev)) {
                    advance(lexer);
                    lexer->mark_end(lexer);
                    lexer->result_symbol = UNSUPPORTED;
                }
                return true;
            }
            prev = c;
            advance(lexer);
        }
        return true;
    }

    if (valid_symbols[RAW] && lexer->lookahead == '`') {
        if (scan_raw(lexer)) {
            lexer->result_symbol = RAW;
            return true;
        }
        return false;
    }
    if (valid_symbols[TEXT]) {
        if (scan_text(lexer, 0)) {
            lexer->result_symbol = TEXT;
            return true;
        }
    }
    return false;
}
