#ifndef MAINBOARD_TEXTEDITOR_H
#define MAINBOARD_TEXTEDITOR_H
#include "SDT.h"

/*
 * USB-keyboard-driven text editor.
 *
 * A "soft modal" that takes over the display and keyboard when active --
 * no UISm state is added, so the StateSmith-generated UISm.cpp/.h aren't
 * touched. Driven by a global flag that Loop.cpp checks on every iteration
 * and that MainBoard_Display.cpp::DrawDisplay checks before normal-state
 * dispatch.
 *
 * Lifecycle:
 *   1. Caller (e.g. an FT8 menu entry) invokes TextEditorBegin(target,
 *      maxLen, prompt, onCommit) -- snapshots target into an internal
 *      buffer for cancel-revert, sets the active flag.
 *   2. Loop.cpp calls TextEditorTick() each iteration. It drains the USB
 *      keyboard ring buffer (KeyboardReadChar) and updates the buffer:
 *        - Printable ASCII (32-126): append (if room).
 *        - Backspace (127): erase last char.
 *        - Other control chars: ignored.
 *      Loop.cpp also intercepts the HOME_SCREEN button (cancel) and
 *      MENU_OPTION_SELECT button (commit) when the editor is active.
 *   3. MainBoard_Display.cpp::DrawDisplay calls TextEditorRender() before
 *      its normal switch when the editor is active, so the editor takes
 *      over the screen.
 *   4. On commit: the buffer contents are copied back into target, the
 *      onCommit callback is invoked (typically SaveDataToStorage), and
 *      the editor exits.
 *   5. On cancel (HOME button): target is left as it was when Begin was
 *      called (snapshot restore), no callback fires.
 *
 * USB-keyboard requirement: typing chars requires the USB keyboard
 * (USB_HOST_INPUT_ENABLED in Config.h). Without it, the editor activates
 * but the user can only commit (with whatever target had) or cancel via
 * the front-panel buttons.
 */

#define TEXT_EDITOR_MAX_BUFFER  64

/**
 * @brief Begin editing a string in the text-editor modal.
 * @param target    Pointer to the string to edit. Must remain valid until
 *                  the editor exits. Edited in-place on commit.
 * @param maxLen    Maximum length of target (including the nul terminator).
 *                  Will not write past target[maxLen-1].
 * @param prompt    Short label shown at the top of the editor (e.g. "Callsign:").
 *                  Pass NULL for no prompt.
 * @param onCommit  Optional callback invoked AFTER the buffer is copied
 *                  back into target on commit. Typically used to call
 *                  SaveDataToStorage. Pass NULL if not needed.
 * @note If the editor is already active, the prior session is silently
 *       aborted (no commit, no cancel callback) and the new one starts.
 */
void TextEditorBegin(char *target, size_t maxLen, const char *prompt,
                     void (*onCommit)(void));

/**
 * @brief Whether the text editor is currently active and should take
 *        precedence over the normal display + button dispatch.
 */
bool TextEditorIsActive(void);

/**
 * @brief Drain pending USB keyboard chars and apply to the buffer.
 * @note Called once per main-loop iteration from Loop.cpp. No-op when
 *       the editor is inactive.
 */
void TextEditorTick(void);

/**
 * @brief Render the editor (full-screen takeover).
 * @note Called from MainBoard_Display.cpp::DrawDisplay when active.
 */
void TextEditorRender(void);

/**
 * @brief Commit the current edit and exit. Copies the internal buffer
 *        back into target, fires onCommit callback if any.
 * @note Typically invoked by the MENU_OPTION_SELECT button via Loop.cpp.
 */
void TextEditorCommit(void);

/**
 * @brief Cancel the current edit and exit. Restores target to its
 *        pre-Begin contents.
 * @note Typically invoked by the HOME_SCREEN button via Loop.cpp.
 */
void TextEditorCancel(void);

#endif /* MAINBOARD_TEXTEDITOR_H */
