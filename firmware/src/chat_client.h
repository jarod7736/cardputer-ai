#pragma once
#include <Arduino.h>
#include <functional>
#include <vector>

namespace chat_client {

struct Message {
  String role;     // "user" or "assistant"
  String content;
};

struct SendResult {
  bool ok;
  char error[80];
};

void begin();

// Append user_text as a user turn, send the full conversation to the
// proxy under the given profile_id, accumulate the streamed reply into
// assistant_out. The per-delta callback fires for each text chunk so
// the UI can paint as it arrives.
SendResult send(const String& profile_id,
                const String& user_text,
                std::function<void(const String&)> on_delta,
                String& assistant_out);

const std::vector<Message>& history();
void reset();

}  // namespace chat_client
