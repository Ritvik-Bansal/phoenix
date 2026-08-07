#pragma once

// Plain-text scenario DSL. One command per line; full-line or trailing
// comments with '#'; strings quoted with "..." ('\"' and '\\' escapes).
// Commands:
//   name <text>                       scenario title
//   desc <text>                       one-line description
//   tick <n>                          advance n ticks (100 ms each)
//   connect | disconnect | bond
//   time YYYY-MM-DD HH:MM:SS          CTS-equivalent time set
//   battery <millivolts>
//   ancs-add uid=<n> cat=<category> [app=<bundle>] [appname=<s>] [title=<s>]
//            [subtitle=<s>] [msg=<s>] [flags=silent,important,preexisting,
//            positive,negative]
//   ancs-modify <same keys as ancs-add>
//   ancs-remove uid=<n>
//   assistant-text <s>                full reply (Phoenix TX frame)
//   assistant-chunk <s>               streamed fragment
//   assistant-chunk-final <s>         last fragment (STREAM_FINAL)
//   nav <maneuver> <meters|?> <street> maneuver: straight left right
//            slight-left slight-right sharp-left sharp-right uturn arrive
//   clear <all|assistant|nav|notification>
//   brightness <0-255>
//   ping <version>
//   button <a|b|c> [long]

#include <cstdint>
#include <string>
#include <vector>

#include "phoenix/ancs.h"
#include "phoenix/datetime.h"
#include "phoenix/protocol.h"

namespace sim {

struct Event {
  enum class Type {
    Tick,
    Connect,
    Disconnect,
    Bond,
    Time,
    Battery,
    AncsAdd,
    AncsModify,
    AncsRemove,
    AssistantText,
    AssistantChunk,
    Nav,
    Clear,
    Brightness,
    Ping,
    ButtonPress,
  };
  Type type{};
  int line = 0;             // source line, for labels/errors
  uint32_t number = 0;      // ticks / battery mv / uid for ancs-remove
  std::string text;         // assistant text / street
  bool flag = false;        // chunk-final / button long press
  phoenix::DateTime time;
  phoenix::ancs::AncsNotification notif;  // ancs-add / ancs-modify
  phoenix::proto::Maneuver maneuver{};
  uint16_t meters = 0;
  uint8_t byte = 0;         // clear mask / brightness / ping version / button
};

struct Scenario {
  std::string fileStem;
  std::string name;
  std::string desc;
  std::vector<Event> events;
};

// Parses a scenario file; on malformed input prints "<path>:<line>: <why>"
// to stderr and returns false.
bool parseScenarioFile(const std::string& path, Scenario& out);

}  // namespace sim
