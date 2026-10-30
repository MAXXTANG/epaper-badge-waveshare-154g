#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Adafruit Bluefruit Connect !I image protocol. Header and RGB565 words are LE.
// The trailing checksum is the one's complement of the sum of all preceding bytes.
class ImageReceiver {
 public:
  static constexpr uint16_t kMaxSide = 512;
  static constexpr size_t kCapacity = size_t(kMaxSide) * kMaxSide * 3;
  enum class Result { Pending, Complete, BadHeader, BadChecksum };
  explicit ImageReceiver(uint8_t* pixels) : pixels_(pixels) { reset(); }
  void reset() {
    headerCount_ = received_ = expected_ = 0;
    width_ = height_ = 0;
    depth_ = checksum_ = 0;
    ready_ = false;
  }
  bool active() const { return headerCount_ != 0 && !ready_; }
  bool ready() const { return ready_; }
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  uint8_t depth() const { return depth_; }
  size_t received() const { return received_; }
  size_t expected() const { return expected_; }
  const uint8_t* pixels() const { return pixels_; }
  Result feed(const uint8_t* data, size_t size) {
    if (ready_) return Result::Pending;
    for (size_t i = 0; i < size; ++i) {
      const uint8_t b = data[i];
      if (headerCount_ == 0) {
        if (b != '!') continue;
        header_[0] = b;
        headerCount_ = 1;
        checksum_ = b;
      } else if (headerCount_ == 1) {
        if (b != 'I') {
          reset();
          if (b == '!') { header_[0] = b; headerCount_ = 1; checksum_ = b; }
          continue;
        }
        header_[headerCount_++] = b;
        checksum_ += b;
      } else if (headerCount_ < sizeof(header_)) {
        header_[headerCount_++] = b;
        checksum_ += b;
        if (headerCount_ == sizeof(header_)) {
          depth_ = header_[2];
          width_ = uint16_t(header_[3]) | (uint16_t(header_[4]) << 8);
          height_ = uint16_t(header_[5]) | (uint16_t(header_[6]) << 8);
          if ((depth_ != 16 && depth_ != 24) || !width_ || !height_ ||
              width_ > kMaxSide || height_ > kMaxSide) {
            reset();
            return Result::BadHeader;
          }
          expected_ = size_t(width_) * height_ * (depth_ / 8);
        }
      } else if (received_ < expected_) {
        // Copy the remaining payload in one bounded operation per BLE write.
        const size_t count = (size - i < expected_ - received_) ?
                                 size - i : expected_ - received_;
        memcpy(pixels_ + received_, data + i, count);
        for (size_t j = 0; j < count; ++j) checksum_ += data[i + j];
        received_ += count;
        i += count - 1;
      } else {
        if (uint8_t(checksum_ + b) != 0xFF) {
          reset();
          return Result::BadChecksum;
        }
        ready_ = true;
        return Result::Complete;
      }
    }
    return Result::Pending;
  }
 private:
  uint8_t* pixels_;
  uint8_t header_[7];
  size_t headerCount_, received_, expected_;
  uint16_t width_, height_;
  uint8_t depth_, checksum_;
  bool ready_;
};
