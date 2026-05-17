#include "keyboard_input.h"

#include <M5Cardputer.h>

namespace keyboard_input {

// Queue of pending characters from the most recent "isChange" event.
// We drain one per poll() so the caller (which paints on each char)
// can keep the UI responsive instead of getting a whole word at once.
static char     s_queue[16];
static uint8_t  s_head = 0;
static uint8_t  s_tail = 0;
static bool     s_pending_enter     = false;
static bool     s_pending_backspace = false;
static bool     s_pending_escape    = false;
static bool     s_pending_tab       = false;

static void enqueue(char c) {
  uint8_t next = (uint8_t)((s_head + 1) % sizeof(s_queue));
  if (next == s_tail) return;          // queue full — drop
  s_queue[s_head] = c;
  s_head = next;
}

static int dequeue() {
  if (s_tail == s_head) return KB_NONE;
  char c = s_queue[s_tail];
  s_tail = (uint8_t)((s_tail + 1) % sizeof(s_queue));
  return (int)(unsigned char) c;
}

void begin() {
  s_head = s_tail = 0;
  s_pending_enter = s_pending_backspace = s_pending_escape = s_pending_tab = false;
}

int poll() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    auto state = M5Cardputer.Keyboard.keysState();
    // Special keys first — these flags fire on each fresh event.
    if (state.enter)  s_pending_enter     = true;
    if (state.del)    s_pending_backspace = true;
    if (state.tab)    s_pending_tab       = true;
    // word holds the printable characters reported this frame.
    for (auto c : state.word) {
      if (c >= 0x20 && c < 0x7F) enqueue(c);
    }
  }

  // Drain special keys before printables so the typing order matches what
  // the user pressed.
  if (s_pending_enter)     { s_pending_enter     = false; return KB_ENTER;     }
  if (s_pending_backspace) { s_pending_backspace = false; return KB_BACKSPACE; }
  if (s_pending_escape)    { s_pending_escape    = false; return KB_ESCAPE;    }
  if (s_pending_tab)       { s_pending_tab       = false; return KB_TAB;       }
  return dequeue();
}

}  // namespace keyboard_input
