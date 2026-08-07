#include "phoenix/protocol.h"

namespace phoenix {
namespace proto {

uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc) {
  for (size_t i = 0; i < n; ++i) {
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000u) {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
      } else {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

std::vector<uint8_t> encodeFrame(const Frame& f) {
  if (f.payload.size() > kMaxPayload) return {};
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + f.payload.size() + kCrcSize);
  out.push_back(kStartByte);
  out.push_back(static_cast<uint8_t>(f.type));
  out.push_back(f.seq);
  out.push_back(f.flags);
  out.push_back(static_cast<uint8_t>(f.payload.size() & 0xFF));
  out.push_back(static_cast<uint8_t>(f.payload.size() >> 8));
  out.insert(out.end(), f.payload.begin(), f.payload.end());
  const uint16_t crc = crc16(out.data() + 1, out.size() - 1);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>(crc >> 8));
  return out;
}

void StreamDecoder::feed(const uint8_t* data, size_t n) {
  if (data == nullptr || n == 0) return;
  // Bounded buffering (PROTOCOL.md §4.5): on overflow drop the oldest bytes.
  if (n >= kDecoderBufferCap) {
    stats_.bytesDropped += static_cast<uint32_t>(buf_.size() + n - kDecoderBufferCap);
    ++stats_.resyncs;
    buf_.clear();
    data += n - kDecoderBufferCap;
    n = kDecoderBufferCap;
  } else if (buf_.size() + n > kDecoderBufferCap) {
    const size_t excess = buf_.size() + n - kDecoderBufferCap;
    stats_.bytesDropped += static_cast<uint32_t>(excess);
    ++stats_.resyncs;
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(excess));
  }
  buf_.insert(buf_.end(), data, data + n);
  parse();
}

void StreamDecoder::parse() {
  size_t pos = 0;
  const size_t size = buf_.size();
  while (pos < size) {
    if (buf_[pos] != kStartByte) {  // garbage before a frame: discard
      ++pos;
      ++stats_.bytesDropped;
      continue;
    }
    if (size - pos < kHeaderSize) break;  // wait for the full header
    const uint16_t len = static_cast<uint16_t>(buf_[pos + 4]) |
                         static_cast<uint16_t>(buf_[pos + 5] << 8);
    if (len > kMaxPayload) {
      // Invalid by construction: drop only the start byte and rescan — a real
      // frame may begin inside the garbage.
      ++stats_.oversizeLength;
      ++stats_.resyncs;
      ++pos;
      ++stats_.bytesDropped;
      continue;
    }
    const size_t total = kHeaderSize + len + kCrcSize;
    if (size - pos < total) break;  // wait for payload + CRC
    const uint16_t want =
        static_cast<uint16_t>(buf_[pos + total - 2]) |
        static_cast<uint16_t>(buf_[pos + total - 1] << 8);
    const uint16_t got = crc16(buf_.data() + pos + 1, kHeaderSize - 1 + len);
    if (want != got) {
      ++stats_.crcErrors;
      ++stats_.resyncs;
      ++pos;
      ++stats_.bytesDropped;
      continue;
    }
    Frame f;
    f.type = static_cast<MsgType>(buf_[pos + 1]);
    f.seq = buf_[pos + 2];
    f.flags = buf_[pos + 3];
    f.payload.assign(buf_.begin() + static_cast<long>(pos + kHeaderSize),
                     buf_.begin() + static_cast<long>(pos + kHeaderSize + len));
    ready_.push_back(std::move(f));
    ++stats_.framesOk;
    pos += total;
  }
  buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(pos));
}

bool StreamDecoder::poll(Frame& out) {
  if (readyHead_ >= ready_.size()) {
    ready_.clear();
    readyHead_ = 0;
    return false;
  }
  out = std::move(ready_[readyHead_]);
  ++readyHead_;
  return true;
}

// ---- Typed payload parsers ----

std::optional<NavUpdateMsg> parseNavUpdate(const Frame& f) {
  if (f.type != MsgType::NavUpdate || f.payload.size() < 3) return std::nullopt;
  NavUpdateMsg m;
  const uint8_t raw = f.payload[0];
  m.maneuver = raw <= static_cast<uint8_t>(Maneuver::Arrive)
                   ? static_cast<Maneuver>(raw)
                   : Maneuver::Straight;  // unknown renders as forward
  m.distanceMeters = static_cast<uint16_t>(f.payload[1]) |
                     static_cast<uint16_t>(f.payload[2] << 8);
  m.street.assign(f.payload.begin() + 3, f.payload.end());
  return m;
}

std::optional<AckMsg> parseAck(const Frame& f) {
  if (f.type != MsgType::Ack || f.payload.size() != 2) return std::nullopt;
  return AckMsg{f.payload[0], f.payload[1]};
}

std::optional<ButtonEventMsg> parseButtonEvent(const Frame& f) {
  if (f.type != MsgType::ButtonEvent || f.payload.size() != 2) {
    return std::nullopt;
  }
  return ButtonEventMsg{f.payload[0], f.payload[1]};
}

std::optional<BatteryStatusMsg> parseBatteryStatus(const Frame& f) {
  if (f.type != MsgType::BatteryStatus || f.payload.size() != 3) {
    return std::nullopt;
  }
  return BatteryStatusMsg{
      f.payload[0], static_cast<uint16_t>(static_cast<uint16_t>(f.payload[1]) |
                                          static_cast<uint16_t>(f.payload[2] << 8))};
}

std::optional<uint8_t> parseSingleByte(const Frame& f) {
  if (f.payload.size() != 1) return std::nullopt;
  return f.payload[0];
}

// ---- Builders ----

namespace {
Frame makeWithText(MsgType type, uint8_t seq, uint8_t flags,
                   const std::string& text) {
  Frame f;
  f.type = type;
  f.seq = seq;
  f.flags = flags;
  f.payload.assign(text.begin(), text.end());
  return f;
}
}  // namespace

Frame makeAssistantText(uint8_t seq, const std::string& utf8) {
  return makeWithText(MsgType::AssistantText, seq, 0, utf8);
}

Frame makeAssistantChunk(uint8_t seq, const std::string& utf8, bool final) {
  return makeWithText(MsgType::AssistantStreamChunk, seq,
                      final ? kFlagStreamFinal : 0, utf8);
}

Frame makeNavUpdate(uint8_t seq, Maneuver m, uint16_t meters,
                    const std::string& street) {
  Frame f;
  f.type = MsgType::NavUpdate;
  f.seq = seq;
  f.payload.push_back(static_cast<uint8_t>(m));
  f.payload.push_back(static_cast<uint8_t>(meters & 0xFF));
  f.payload.push_back(static_cast<uint8_t>(meters >> 8));
  f.payload.insert(f.payload.end(), street.begin(), street.end());
  return f;
}

Frame makeClear(uint8_t seq, uint8_t mask) {
  Frame f;
  f.type = MsgType::Clear;
  f.seq = seq;
  f.payload.push_back(mask);
  return f;
}

Frame makeSetBrightness(uint8_t seq, uint8_t level) {
  Frame f;
  f.type = MsgType::SetBrightness;
  f.seq = seq;
  f.payload.push_back(level);
  return f;
}

Frame makePing(uint8_t seq, uint8_t protocolVersion) {
  Frame f;
  f.type = MsgType::Ping;
  f.seq = seq;
  f.payload.push_back(protocolVersion);
  return f;
}

Frame makeAck(uint8_t seq, uint8_t ackedSeq, uint8_t status) {
  Frame f;
  f.type = MsgType::Ack;
  f.seq = seq;
  f.payload.push_back(ackedSeq);
  f.payload.push_back(status);
  return f;
}

Frame makeButtonEvent(uint8_t seq, uint8_t button, uint8_t action) {
  Frame f;
  f.type = MsgType::ButtonEvent;
  f.seq = seq;
  f.payload.push_back(button);
  f.payload.push_back(action);
  return f;
}

Frame makeBatteryStatus(uint8_t seq, uint8_t percent, uint16_t millivolts) {
  Frame f;
  f.type = MsgType::BatteryStatus;
  f.seq = seq;
  f.payload.push_back(percent);
  f.payload.push_back(static_cast<uint8_t>(millivolts & 0xFF));
  f.payload.push_back(static_cast<uint8_t>(millivolts >> 8));
  return f;
}

}  // namespace proto
}  // namespace phoenix
