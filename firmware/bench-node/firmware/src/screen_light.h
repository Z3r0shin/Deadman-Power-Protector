// When the idle screen is bright, dim or dark. Plain C++: the PC tests use it as is
// (test/test_light.cpp).
//   - Full bright for bright_ms after the latest key press, called-up page (status after a key, menu, warning,
//     alarm, message) or arrival. An arrival is being seen after bright_ms or more unseen.
//   - Then dim for as long as someone is seen.
//   - Unseen: dim until bright_ms + dark_ms after last seen, then dark until a key or an arrival.
#pragma once
#include <cstdint>

namespace light {

enum class Level : uint8_t { Bright, Dim, Dark };

class Clock {
 public:
  uint32_t bright_ms = 20 * 60000;
  uint32_t dark_ms = 20 * 60000;     // dim this long after bright_ms unseen, then dark

  void begin(uint32_t now) { active_at_ = seen_at_ = now; }
  void activity(uint32_t now) { active_at_ = now; }

  // Every frame, with whether someone is seen now.
  void tick(bool seen, uint32_t now) {
    // Keep both ages below 2^30 ms (12 days), so a long absence never wraps round to "just now".
    const uint32_t cap = 1UL << 30;
    if (now - active_at_ > cap) active_at_ = now - cap;
    if (now - seen_at_ > cap) seen_at_ = now - cap;
    if (seen) {
      if (now - seen_at_ >= bright_ms) active_at_ = now;   // back after a real absence: an arrival
      seen_at_ = now;
    }
    seen_ = seen;
  }

  Level level(uint32_t now) const {
    if (now - active_at_ < bright_ms) return Level::Bright;
    if (seen_ || now - seen_at_ < bright_ms + dark_ms) return Level::Dim;
    return Level::Dark;
  }

 private:
  uint32_t active_at_ = 0, seen_at_ = 0;
  bool seen_ = false;
};

}  // namespace light
