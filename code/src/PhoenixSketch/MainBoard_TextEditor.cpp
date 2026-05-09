#include "SDT.h"
#include "MainBoard_TextEditor.h"
#include "FrontPanel_USBKeyboard.h"
#include <RA8875.h>  // for RA8875 type and RA8875_* color constants

extern RA8875 tft;  /* defined in MainBoard_Display.cpp */

/*
 * USB-keyboard text editor. See MainBoard_TextEditor.h for protocol.
 *
 * Internal state held as file-static globals: a snapshot of the original
 * target (for cancel-revert), a working buffer (the in-progress edit),
 * the target pointer + maxLen + prompt + onCommit pointers from Begin,
 * and an active flag.
 */

static bool         s_active           = false;
static char        *s_target           = NULL;
static size_t       s_targetMax        = 0;       /* including nul */
static const char  *s_prompt           = NULL;
static void       (*s_onCommit)(void)  = NULL;

/* Working buffer. The user's edits land here; on commit we strncpy back
 * to s_target. On cancel, target is left unchanged (we never touched it
 * after the snapshot). */
static char         s_buffer[TEXT_EDITOR_MAX_BUFFER];
static int          s_len              = 0;

/* Snapshot of the target at Begin time -- not strictly required since we
 * only modify s_target on commit, but kept for symmetry with future
 * edit-in-place implementations. */
static char         s_snapshot[TEXT_EDITOR_MAX_BUFFER];

void TextEditorBegin(char *target, size_t maxLen, const char *prompt,
                     void (*onCommit)(void)) {
    if (target == NULL || maxLen == 0) return;

    /* Cap maxLen at our internal buffer size. */
    if (maxLen > sizeof(s_buffer)) maxLen = sizeof(s_buffer);

    s_target    = target;
    s_targetMax = maxLen;
    s_prompt    = prompt;
    s_onCommit  = onCommit;

    /* Snapshot + load into working buffer. */
    strncpy(s_snapshot, target, sizeof(s_snapshot) - 1);
    s_snapshot[sizeof(s_snapshot) - 1] = '\0';
    strncpy(s_buffer, target, sizeof(s_buffer) - 1);
    s_buffer[sizeof(s_buffer) - 1] = '\0';
    s_len = (int)strlen(s_buffer);

    s_active = true;
}

bool TextEditorIsActive(void) {
    return s_active;
}

void TextEditorTick(void) {
    if (!s_active) return;

    /* Drain whatever the USB keyboard has buffered. KeyboardReadChar
     * returns 0 when empty, so this loop terminates naturally. */
    uint8_t c;
    while ((c = KeyboardReadChar()) != 0) {
        if (c == 127) {
            /* Backspace */
            if (s_len > 0) {
                s_len--;
                s_buffer[s_len] = '\0';
            }
        } else if (c >= 32 && c <= 126) {
            /* Printable ASCII */
            if (s_len < (int)s_targetMax - 1
                && s_len < (int)sizeof(s_buffer) - 1) {
                s_buffer[s_len++] = (char)c;
                s_buffer[s_len] = '\0';
            }
        }
        /* Other control chars (CR, LF, ESC, etc.) ignored -- commit and
         * cancel are bound to front-panel buttons in Loop.cpp. */
    }
}

void TextEditorCommit(void) {
    if (!s_active || s_target == NULL) {
        s_active = false;
        return;
    }
    /* Copy buffer back to target, respecting target's maxLen. */
    strncpy(s_target, s_buffer, s_targetMax - 1);
    s_target[s_targetMax - 1] = '\0';
    /* Strip trailing spaces / control bytes (matches the CL/GR CAT
     * write handlers' behavior so callsigns don't end up with stray
     * keyboard padding). */
    int len = (int)strlen(s_target);
    while (len > 0 && (s_target[len - 1] == ' '
                       || (unsigned char)s_target[len - 1] < 0x20)) {
        s_target[--len] = '\0';
    }

    void (*cb)(void) = s_onCommit;
    s_active = false;
    s_target = NULL;
    s_onCommit = NULL;
    if (cb) cb();
}

void TextEditorCancel(void) {
    /* Target was never modified; just clear state. */
    s_active = false;
    s_target = NULL;
    s_onCommit = NULL;
}

/* ============================================================
 * Render
 * ============================================================ */

void TextEditorRender(void) {
    if (!s_active) return;

    /* Full-screen takeover. Black background, white text. */
    tft.fillRect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, RA8875_BLACK);

    tft.setFontDefault();
    tft.setFontScale((enum RA8875tsize)1);  /* slightly larger for readability */
    int charW = tft.getFontWidth();
    int charH = tft.getFontHeight();
    if (charW <= 0) charW = 16;
    if (charH <= 0) charH = 32;

    /* Title / prompt */
    tft.setTextColor(RA8875_YELLOW);
    tft.setCursor(20, 30);
    tft.print(s_prompt ? s_prompt : "Edit:");

    /* Outline the edit box */
    int boxX = 20;
    int boxY = 30 + charH + 10;
    int boxW = WINDOW_WIDTH - 40;
    int boxH = charH + 16;
    tft.drawRect(boxX, boxY, boxW, boxH, RA8875_WHITE);

    /* Buffer contents */
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor(boxX + 8, boxY + 8);
    tft.print(s_buffer);

    /* Caret -- a thin vertical bar after the last char */
    int caretX = boxX + 8 + s_len * charW;
    if (caretX > boxX + boxW - 4) caretX = boxX + boxW - 4;
    tft.drawFastVLine(caretX, boxY + 6, charH + 4, RA8875_GREEN);

    /* Hint footer */
    tft.setFontScale((enum RA8875tsize)0);
    int hintCharH = tft.getFontHeight();
    if (hintCharH <= 0) hintCharH = 16;
    tft.setTextColor(DARKGREY);
    tft.setCursor(20, WINDOW_HEIGHT - hintCharH - 20);
    tft.print("USB keyboard: type chars, BS = delete");
    tft.setCursor(20, WINDOW_HEIGHT - hintCharH - 4);
    tft.setTextColor(RA8875_GREEN);
    tft.print("MENU button = save     HOME button = cancel");
}
