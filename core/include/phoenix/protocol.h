#pragma once

// Phoenix custom BLE service wire format — implements PROTOCOL.md exactly.
// The decoder parses bytes off a radio: it must survive truncation, garbage,
// corruption, and arbitrary fragmentation without ever reading out of bounds.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace phoenix {
namespace proto {

inline constexpr uint8_t kStartByte = 0xA5;
inline constexpr size_t kMaxPayload = 512;
inline constexpr size_t kHeaderSize = 6;  // start, type, seq, flags, len u16 LE
inline constexpr size_t kCrcSize = 2;
inline constexpr size_t kMaxFrameSize = kHeaderSize + kMaxPayload + kCrcSize;
// Decoder buffering cap (PROTOCOL.md §4.5) — larger than any legal frame.
inline constexpr size_t kDecoderBufferCap = 4096;

// GATT identity (PROTOCOL.md §1) — the single source for firmware and docs;
// the iOS app mirrors these in PhoenixUUIDs.swift.
inline constexpr const char* kServiceUuid = "CD310001-0101-4B2F-9456-982A27ED3560";
inline constexpr const char* kTxCharUuid = "CD310002-0101-4B2F-9456-982A27ED3560";
inline constexpr const char* kRxCharUuid = "CD310003-0101-4B2F-9456-982A27ED3560";

enum class MsgType : uint8_t {
  AssistantText = 0x01,
  AssistantStreamChunk = 0x02,
  NavUpdate = 0x03,
  Clear = 0x04,
  SetBrightness = 0x05,
  Ping = 0x06,
  Ack = 0x40,
  ButtonEvent = 0x41,
  BatteryStatus = 0x42,
};

// FLAGS bits (PROTOCOL.md §3).
inline constexpr uint8_t kFlagAckReq = 0x01;
inline constexpr uint8_t kFlagStreamFinal = 0x02;

// CLEAR payload bitmask.
inline constexpr uint8_t kClearAssistant = 0x01;
inline constexpr uint8_t kClearNav = 0x02;
inline constexpr uint8_t kClearNotification = 0x04;
inline constexpr uint8_t kClearAll = 0xFF;

// ACK status codes.
inline constexpr uint8_t kAckOk = 0;
inline constexpr uint8_t kAckBadVersion = 1;
inline constexpr uint8_t kAckBadType = 2;

// BUTTON_EVENT fields.
inline constexpr uint8_t kButtonA = 1;  // dismiss
inline constexpr uint8_t kButtonB = 2;  // next page / accept call
inline constexpr uint8_t kButtonC = 3;  // aux (status; long = sleep)
inline constexpr uint8_t kPressShort = 0;
inline constexpr uint8_t kPressLong = 1;

enum class Maneuver : uint8_t {
  Straight = 0,
  Left = 1,
  Right = 2,
  SlightLeft = 3,
  SlightRight = 4,
  SharpLeft = 5,
  SharpRight = 6,
  UTurn = 7,
  Arrive = 8,
};
inline constexpr uint16_t kDistanceUnknown = 0xFFFF;

struct Frame {
  MsgType type{};
  uint8_t seq = 0;
  uint8_t flags = 0;
  std::vector<uint8_t> payload;
};

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR.
// crc16 over "123456789" == 0x29B1 (verified in test_codec.cpp).
uint16_t crc16(const uint8_t* data, size_t n, uint16_t crc = 0xFFFF);

// Serializes a frame (start byte through CRC). Returns an empty vector iff
// the payload exceeds kMaxPayload — an unencodable frame, caller bug.
std::vector<uint8_t> encodeFrame(const Frame& f);

// Byte-stream reassembler/decoder. Feed arbitrary chunks in arrival order;
// poll() yields completed frames. Implements the resync rules of
// PROTOCOL.md §4 and never throws, crashes, or reads out of bounds.
class StreamDecoder {
 public:
  struct Stats {
    uint32_t framesOk = 0;
    uint32_t crcErrors = 0;
    uint32_t resyncs = 0;
    uint32_t oversizeLength = 0;
    uint32_t bytesDropped = 0;
  };

  void feed(const uint8_t* data, size_t n);
  void feed(const std::vector<uint8_t>& v) { feed(v.data(), v.size()); }
  bool poll(Frame& out);
  const Stats& stats() const { return stats_; }

 private:
  void parse();
  std::vector<uint8_t> buf_;
  std::vector<Frame> ready_;
  size_t readyHead_ = 0;
  Stats stats_;
};

// ---- Typed payloads (parsers are bounds-checked; nullopt on malformed) ----

struct NavUpdateMsg {
  Maneuver maneuver;
  uint16_t distanceMeters;
  std::string street;  // raw UTF-8; sanitize at the display boundary
};
struct AckMsg {
  uint8_t ackedSeq;
  uint8_t status;
};
struct ButtonEventMsg {
  uint8_t button;
  uint8_t action;
};
struct BatteryStatusMsg {
  uint8_t percent;
  uint16_t millivolts;
};

std::optional<NavUpdateMsg> parseNavUpdate(const Frame& f);
std::optional<AckMsg> parseAck(const Frame& f);
std::optional<ButtonEventMsg> parseButtonEvent(const Frame& f);
std::optional<BatteryStatusMsg> parseBatteryStatus(const Frame& f);
// CLEAR / SET_BRIGHTNESS / PING all carry exactly one byte.
std::optional<uint8_t> parseSingleByte(const Frame& f);

// ---- Frame builders ----

Frame makeAssistantText(uint8_t seq, const std::string& utf8);
Frame makeAssistantChunk(uint8_t seq, const std::string& utf8, bool final);
Frame makeNavUpdate(uint8_t seq, Maneuver m, uint16_t meters,
                    const std::string& street);
Frame makeClear(uint8_t seq, uint8_t mask);
Frame makeSetBrightness(uint8_t seq, uint8_t level);
Frame makePing(uint8_t seq, uint8_t protocolVersion);
Frame makeAck(uint8_t seq, uint8_t ackedSeq, uint8_t status);
Frame makeButtonEvent(uint8_t seq, uint8_t button, uint8_t action);
Frame makeBatteryStatus(uint8_t seq, uint8_t percent, uint16_t millivolts);

}  // namespace proto
}  // namespace phoenix
