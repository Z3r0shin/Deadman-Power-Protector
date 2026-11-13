// Console tee: everything printed goes to the USB serial port and into a ring the web portal reads.
// Lines typed on USB or sent from the portal reach the same command dispatcher.
#pragma once
#include <Arduino.h>

class ConsoleTee : public Print {
 public:
  static constexpr size_t CAP = 8192;

  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override {
    portENTER_CRITICAL(&lock_);
    for (size_t i = 0; i < n; i++) ring_[(seq_ + i) % CAP] = (char) buf[i];
    seq_ += n;
    portEXIT_CRITICAL(&lock_);
    Serial.write(buf, n);
    return n;  // report success even with no USB host attached
  }

  uint32_t next() const { return seq_; }

  // Copies bytes [from, ...) into out (at most max, out holds max + 1), oldest first, NUL-terminated.
  // Returns the first byte number actually copied: later than `from` if those bytes were overwritten,
  // the oldest held if `from` is in the future (the board restarted under an open page).
  // No allocation while the lock is held.
  uint32_t copySince(uint32_t from, char *out, size_t max, uint32_t &next_out) {
    portENTER_CRITICAL(&lock_);
    uint32_t end = seq_;
    uint32_t oldest = end > CAP ? end - CAP : 0;
    if (from < oldest || from > end) from = oldest;
    uint32_t stop = end - from > max ? from + max : end;
    size_t n = 0;
    for (uint32_t i = from; i < stop; i++) out[n++] = ring_[i % CAP];
    portEXIT_CRITICAL(&lock_);
    out[n] = 0;
    next_out = stop;
    return from;
  }

 private:
  char ring_[CAP];
  volatile uint32_t seq_ = 0;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
};

extern ConsoleTee Con;
