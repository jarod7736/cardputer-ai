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
// Arrow events are wider than char; queued as ints in a small ring.
static int      s_specials[8];
static uint8_t  s_specials_head = 0;
static uint8_t  s_specials_tail = 0;

static void enqueue_special(int code) {
  uint8_t next = (uint8_t)((s_specials_head + 1) % (sizeof(s_specials) / sizeof(s_specials[0])));
  if (next == s_specials_tail) return;
  s_specials[s_specials_head] = code;
  s_specials_head = next;
}

static int dequeue_special() {
  if (s_specials_tail == s_specials_head) return KB_NONE;
  int code = s_specials[s_specials_tail];
  s_specials_tail = (uint8_t)((s_specials_tail + 1) % (sizeof(s_specials) / sizeof(s_specials[0])));
  return code;
}

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
  s_specials_head = s_specials_tail = 0;
  s_pending_enter = s_pending_backspace = s_pending_escape = s_pending_tab = false;
}

int poll() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    auto state = M5Cardputer.Keyboard.keysState();
    if (state.enter)  s_pending_enter     = true;
    if (state.del)    s_pending_backspace = true;
    if (state.tab)    s_pending_tab       = true;
    if (state.fn) {
      // Cardputer arrows are Fn + ,./; (printed on those keycaps).
      // Library still emits the printable char in `word`, so dispatch
      // from there when Fn is held and swallow the printable.
      for (auto c : state.word) {
        switch (c) {
          case ',': enqueue_special(KB_LEFT);     break;
          case '.': enqueue_special(KB_DOWN);     break;
          case ';': enqueue_special(KB_UP);       break;
          case '/': enqueue_special(KB_RIGHT);    break;
          case '`': s_pending_escape = true;      break;  // Fn+` = Esc
          case 's': case 'S':
            enqueue_special(KB_SETTINGS);
            break;
          default: break;
        }
      }
    } else {
      for (auto c : state.word) {
        if (c >= 0x20 && c < 0x7F) enqueue(c);
      }
    }
  }

  if (s_pending_enter)     { s_pending_enter     = false; return KB_ENTER;     }
  if (s_pending_backspace) { s_pending_backspace = false; return KB_BACKSPACE; }
  if (s_pending_escape)    { s_pending_escape    = false; return KB_ESCAPE;    }
  if (s_pending_tab)       { s_pending_tab       = false; return KB_TAB;       }
  int special = dequeue_special();
  if (special != KB_NONE) return special;
  return dequeue();
}

}  // namespace keyboard_input
