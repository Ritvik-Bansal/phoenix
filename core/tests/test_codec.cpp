// Protocol codec tests + hostile-input fuzz suite. Everything deterministic:
// fixed-seed xorshift PRNG, so failures reproduce exactly. The same binary is
// also built under ASan/UBSan (build-asan/) to prove the no-OOB claim.

#include <cstring>
#include <string>
#include <vector>

#include "phoenix/protocol.h"
#include "test_framework.h"

using namespace phoenix::proto;

namespace {

struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
  uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
  uint32_t below(uint32_t n) { return n ? static_cast<uint32_t>(next() % n) : 0; }
};

bool frameEq(const Frame& a, const Frame& b) {
  return a.type == b.type && a.seq == b.seq && a.flags == b.flags &&
         a.payload == b.payload;
}

std::vector<Frame> drain(StreamDecoder& d) {
  std::vector<Frame> out;
  Frame f;
  while (d.poll(f)) out.push_back(f);
  return out;
}

void feedInChunks(StreamDecoder& d, const std::vector<uint8_t>& bytes,
                  Rng& rng, uint32_t maxChunk) {
  size_t i = 0;
  while (i < bytes.size()) {
    const size_t n = 1 + rng.below(maxChunk);
    const size_t take = n < bytes.size() - i ? n : bytes.size() - i;
    d.feed(bytes.data() + i, take);
    i += take;
  }
}

Frame randomFrame(Rng& rng) {
  static const MsgType kTypes[] = {
      MsgType::AssistantText, MsgType::AssistantStreamChunk, MsgType::NavUpdate,
      MsgType::Clear,         MsgType::SetBrightness,        MsgType::Ping,
      MsgType::Ack,           MsgType::ButtonEvent,          MsgType::BatteryStatus};
  Frame f;
  f.type = kTypes[rng.below(9)];
  f.seq = static_cast<uint8_t>(rng.below(256));
  f.flags = static_cast<uint8_t>(rng.below(4));
  const uint32_t len = rng.below(100) == 0 ? 512 : rng.below(80);
  f.payload.resize(len);
  for (auto& b : f.payload) b = static_cast<uint8_t>(rng.below(256));
  return f;
}

}  // namespace

TEST(crc_check_vectors) {
  const char* check = "123456789";
  CHECK_EQ(crc16(reinterpret_cast<const uint8_t*>(check), 9), 0x29B1);
  // The worked example in PROTOCOL.md §7: SET_BRIGHTNESS(128), seq 7.
  const std::vector<uint8_t> expected = {0xA5, 0x05, 0x07, 0x00, 0x01,
                                         0x00, 0x80, 0x7D, 0x8C};
  CHECK(encodeFrame(makeSetBrightness(7, 128)) == expected);
}

TEST(round_trip_all_builders) {
  const std::vector<Frame> frames = {
      makeAssistantText(1, "Sunny, 22\xC2\xB0" "C all day"),
      makeAssistantChunk(2, "partial ", false),
      makeAssistantChunk(3, "", true),  // empty final chunk is legal
      makeNavUpdate(4, Maneuver::SlightRight, 250, "Market St"),
      makeNavUpdate(5, Maneuver::Arrive, 0, ""),
      makeClear(6, kClearAll),
      makeSetBrightness(7, 200),
      makePing(8, 1),
      makeAck(9, 8, kAckOk),
      makeButtonEvent(10, kButtonB, kPressLong),
      makeBatteryStatus(11, 87, 4012),
  };
  StreamDecoder d;
  for (const auto& f : frames) d.feed(encodeFrame(f));
  const auto got = drain(d);
  CHECK_EQ(got.size(), frames.size());
  for (size_t i = 0; i < frames.size() && i < got.size(); ++i) {
    CHECK(frameEq(got[i], frames[i]));
  }

  // Typed parsers see the right fields.
  const auto nav = parseNavUpdate(frames[3]);
  CHECK(nav.has_value());
  CHECK(nav->maneuver == Maneuver::SlightRight);
  CHECK_EQ(nav->distanceMeters, 250);
  CHECK_EQ(nav->street, "Market St");
  const auto batt = parseBatteryStatus(frames[10]);
  CHECK(batt.has_value());
  CHECK_EQ(batt->percent, 87);
  CHECK_EQ(batt->millivolts, 4012);
  const auto chunkFinal = frames[2];
  CHECK((chunkFinal.flags & kFlagStreamFinal) != 0);

  // Unknown maneuver byte degrades to Straight.
  Frame weird = makeNavUpdate(12, Maneuver::Left, 10, "x");
  weird.payload[0] = 200;
  const auto parsed = parseNavUpdate(weird);
  CHECK(parsed.has_value());
  CHECK(parsed->maneuver == Maneuver::Straight);
}

TEST(split_invariance_any_fragmentation) {
  Rng rng(1234);
  std::vector<Frame> frames;
  std::vector<uint8_t> stream;
  for (int i = 0; i < 25; ++i) {
    frames.push_back(randomFrame(rng));
    const auto bytes = encodeFrame(frames.back());
    stream.insert(stream.end(), bytes.begin(), bytes.end());
  }

  // Reference: one big feed.
  StreamDecoder ref;
  ref.feed(stream);
  const auto expect = drain(ref);
  CHECK_EQ(expect.size(), size_t(25));

  // Byte-at-a-time.
  StreamDecoder one;
  for (uint8_t b : stream) one.feed(&b, 1);
  auto got = drain(one);
  CHECK_EQ(got.size(), expect.size());
  for (size_t i = 0; i < got.size(); ++i) CHECK(frameEq(got[i], expect[i]));

  // 100 random fragmentations, polling mid-stream too.
  for (int trial = 0; trial < 100; ++trial) {
    StreamDecoder d;
    std::vector<Frame> collected;
    size_t i = 0;
    while (i < stream.size()) {
      const size_t n = 1 + rng.below(17);
      const size_t take = n < stream.size() - i ? n : stream.size() - i;
      d.feed(stream.data() + i, take);
      i += take;
      Frame f;
      while (d.poll(f)) collected.push_back(f);
    }
    CHECK_EQ(collected.size(), expect.size());
    for (size_t k = 0; k < collected.size(); ++k) {
      CHECK(frameEq(collected[k], expect[k]));
    }
    if (testfw::g_failures) return;  // one bad trial is enough output
  }
}

TEST(garbage_between_frames_resyncs) {
  Rng rng(99);
  std::vector<uint8_t> stream;
  std::vector<Frame> frames;
  uint32_t garbageBytes = 0;
  for (int i = 0; i < 10; ++i) {
    // Garbage run free of the start byte: recovery must be total.
    const uint32_t glen = rng.below(40);
    garbageBytes += glen;
    for (uint32_t g = 0; g < glen; ++g) {
      uint8_t b = static_cast<uint8_t>(rng.below(256));
      if (b == kStartByte) b = 0x00;
      stream.push_back(b);
    }
    frames.push_back(randomFrame(rng));
    const auto bytes = encodeFrame(frames.back());
    stream.insert(stream.end(), bytes.begin(), bytes.end());
  }
  StreamDecoder d;
  feedInChunks(d, stream, rng, 13);
  const auto got = drain(d);
  CHECK_EQ(got.size(), frames.size());
  for (size_t i = 0; i < got.size(); ++i) CHECK(frameEq(got[i], frames[i]));
  CHECK_EQ(d.stats().bytesDropped, garbageBytes);
  CHECK_EQ(d.stats().crcErrors, uint32_t(0));
}

TEST(corruption_never_emits_and_always_recovers) {
  const Frame original = makeNavUpdate(42, Maneuver::Left, 1234, "Elm Street");
  const auto clean = encodeFrame(original);
  const Frame sentinel = makeBatteryStatus(77, 55, 3777);
  const auto sentinelBytes = encodeFrame(sentinel);
  const std::vector<uint8_t> filler(kMaxFrameSize, 0x00);  // completes any
                                                           // claimed length
  for (size_t i = 0; i < clean.size(); ++i) {
    for (uint8_t flip : {0x01, 0x80}) {
      auto bytes = clean;
      bytes[i] = static_cast<uint8_t>(bytes[i] ^ flip);
      StreamDecoder d;
      d.feed(bytes);
      d.feed(filler);
      d.feed(sentinelBytes);
      bool sawSentinel = false;
      Frame f;
      while (d.poll(f)) {
        // The corrupted frame must never surface with the original payload.
        CHECK(!frameEq(f, original));
        if (frameEq(f, sentinel)) sawSentinel = true;
      }
      CHECK(sawSentinel);
      if (testfw::g_failures) {
        std::printf("  (first failure at corrupt index %zu flip 0x%02X)\n", i, flip);
        return;
      }
    }
  }
}

TEST(truncation_all_prefixes) {
  const Frame f = makeAssistantText(9, "The pen is mightier");
  const auto bytes = encodeFrame(f);
  for (size_t cut = 1; cut < bytes.size(); ++cut) {
    StreamDecoder d;
    d.feed(bytes.data(), cut);
    Frame out;
    CHECK(!d.poll(out));  // nothing emitted from a truncated frame
    d.feed(bytes.data() + cut, bytes.size() - cut);
    CHECK(d.poll(out));
    CHECK(frameEq(out, f));
    CHECK(!d.poll(out));
    if (testfw::g_failures) {
      std::printf("  (first failure at prefix %zu)\n", cut);
      return;
    }
  }
}

TEST(oversize_length_resyncs) {
  std::vector<uint8_t> bytes = {kStartByte, 0x01, 0x00, 0x00, 0x01, 0x02};  // len 513
  const Frame good = makePing(3, 1);
  const auto goodBytes = encodeFrame(good);
  bytes.insert(bytes.end(), goodBytes.begin(), goodBytes.end());
  StreamDecoder d;
  d.feed(bytes);
  const auto got = drain(d);
  CHECK_EQ(got.size(), size_t(1));
  CHECK(frameEq(got[0], good));
  CHECK(d.stats().oversizeLength >= 1);
}

TEST(unknown_type_decodes_structurally) {
  Frame f;
  f.type = static_cast<MsgType>(0x77);
  f.seq = 5;
  f.payload = {1, 2, 3};
  StreamDecoder d;
  d.feed(encodeFrame(f));
  Frame out;
  CHECK(d.poll(out));
  CHECK_EQ(static_cast<int>(out.type), 0x77);
  CHECK(out.payload == f.payload);
}

TEST(buffer_cap_bounded) {
  StreamDecoder d;
  // 10k of A5-free garbage in chunks: buffering must stay bounded.
  const std::vector<uint8_t> junk(1000, 0x11);
  for (int i = 0; i < 10; ++i) d.feed(junk);
  // One chunk larger than the whole cap.
  const std::vector<uint8_t> huge(8000, 0x22);
  d.feed(huge);
  CHECK(d.stats().bytesDropped >= 8000);
  // Decoder is still alive and exact.
  const Frame good = makeClear(1, kClearNav);
  d.feed(encodeFrame(good));
  const auto got = drain(d);
  CHECK_EQ(got.size(), size_t(1));
  CHECK(frameEq(got[0], good));
}

TEST(typed_parsers_reject_malformed) {
  Frame f;
  f.type = MsgType::NavUpdate;
  for (size_t n : {size_t(0), size_t(1), size_t(2)}) {
    f.payload.assign(n, 0);
    CHECK(!parseNavUpdate(f).has_value());
  }
  f.payload.assign(3, 0);
  CHECK(parseNavUpdate(f).has_value());  // empty street is legal

  Frame ack;
  ack.type = MsgType::Ack;
  ack.payload = {1};
  CHECK(!parseAck(ack).has_value());
  ack.payload = {1, 2, 3};
  CHECK(!parseAck(ack).has_value());
  ack.payload = {1, 2};
  CHECK(parseAck(ack).has_value());
  ack.type = MsgType::Ping;  // right size, wrong type
  CHECK(!parseAck(ack).has_value());

  Frame batt;
  batt.type = MsgType::BatteryStatus;
  batt.payload = {50, 0};
  CHECK(!parseBatteryStatus(batt).has_value());

  Frame single;
  single.type = MsgType::SetBrightness;
  single.payload = {};
  CHECK(!parseSingleByte(single).has_value());
  single.payload = {9, 9};
  CHECK(!parseSingleByte(single).has_value());
  single.payload = {9};
  CHECK_EQ(parseSingleByte(single).value(), 9);
}

TEST(encode_rejects_oversize_payload) {
  Frame f;
  f.type = MsgType::AssistantText;
  f.payload.assign(kMaxPayload, 'x');
  CHECK(!encodeFrame(f).empty());  // exactly max is fine
  f.payload.push_back('x');
  CHECK(encodeFrame(f).empty());
}

TEST(fuzz_pure_random_bytes) {
  Rng rng(0xF00D);
  StreamDecoder persistent;
  for (int iter = 0; iter < 1500; ++iter) {
    const uint32_t len = rng.below(700);
    std::vector<uint8_t> junk(len);
    for (auto& b : junk) b = static_cast<uint8_t>(rng.below(256));
    feedInChunks(persistent, junk, rng, 31);
    if (rng.below(4) == 0) drain(persistent);

    StreamDecoder fresh;  // also from a cold state every time
    fresh.feed(junk);
    drain(fresh);
  }
  // The persistent decoder must not be wedged: flush any pending claimed
  // frame, then a real frame decodes.
  const std::vector<uint8_t> flush(kMaxFrameSize, 0x00);
  persistent.feed(flush);
  drain(persistent);
  const Frame probe = makePing(1, 1);
  persistent.feed(encodeFrame(probe));
  persistent.feed(encodeFrame(probe));
  const auto got = drain(persistent);
  bool sawProbe = false;
  for (const auto& g : got) {
    if (frameEq(g, probe)) sawProbe = true;
  }
  CHECK(sawProbe);
}

TEST(fuzz_mutated_streams) {
  Rng rng(0xBEEF);
  for (int iter = 0; iter < 300; ++iter) {
    std::vector<uint8_t> stream;
    const uint32_t frameCount = 3 + rng.below(10);
    for (uint32_t i = 0; i < frameCount; ++i) {
      const auto bytes = encodeFrame(randomFrame(rng));
      stream.insert(stream.end(), bytes.begin(), bytes.end());
    }
    const uint32_t mutations = 1 + rng.below(8);
    for (uint32_t m = 0; m < mutations && !stream.empty(); ++m) {
      stream[rng.below(static_cast<uint32_t>(stream.size()))] =
          static_cast<uint8_t>(rng.below(256));
    }
    StreamDecoder d;
    feedInChunks(d, stream, rng, 23);
    Frame f;
    while (d.poll(f)) {
      CHECK(f.payload.size() <= kMaxPayload);  // structural invariant
    }
    // Stats stay coherent: every emitted frame was once counted OK.
    CHECK(d.stats().framesOk <= frameCount + mutations + 1);
  }
}

TESTFW_MAIN
