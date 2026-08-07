#include "scenario.h"

#include <cstdio>
#include <fstream>

namespace sim {
namespace {

using phoenix::ancs::CategoryId;
using phoenix::proto::Maneuver;

// Splits a line into tokens. A token is a run of non-space characters where
// any "..." segment (escapes: \" and \\) is folded in verbatim — so
// `title="Ravi B"` is one token `title=Ravi B`. An unquoted '#' starts a
// comment.
bool tokenize(const std::string& line, std::vector<std::string>& out,
              std::string& err) {
  size_t i = 0;
  while (i < line.size()) {
    if (line[i] == ' ' || line[i] == '\t') {
      ++i;
      continue;
    }
    if (line[i] == '#') break;  // comment
    std::string tok;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') {
      if (line[i] == '#') break;
      if (line[i] == '"') {
        ++i;
        bool closed = false;
        while (i < line.size()) {
          if (line[i] == '\\' && i + 1 < line.size() &&
              (line[i + 1] == '"' || line[i + 1] == '\\')) {
            tok.push_back(line[i + 1]);
            i += 2;
          } else if (line[i] == '"') {
            ++i;
            closed = true;
            break;
          } else {
            tok.push_back(line[i]);
            ++i;
          }
        }
        if (!closed) {
          err = "unterminated string";
          return false;
        }
      } else {
        tok.push_back(line[i]);
        ++i;
      }
    }
    out.push_back(tok);
  }
  return true;
}

bool parseUint(const std::string& s, uint32_t& out) {
  if (s.empty()) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint64_t>(c - '0');
    if (v > 0xFFFFFFFFull) return false;
  }
  out = static_cast<uint32_t>(v);
  return true;
}

bool parseCategory(const std::string& s, CategoryId& out) {
  static const struct {
    const char* name;
    CategoryId id;
  } kMap[] = {
      {"other", CategoryId::Other},
      {"call", CategoryId::IncomingCall},
      {"incoming-call", CategoryId::IncomingCall},
      {"missed-call", CategoryId::MissedCall},
      {"voicemail", CategoryId::Voicemail},
      {"social", CategoryId::Social},
      {"schedule", CategoryId::Schedule},
      {"email", CategoryId::Email},
      {"news", CategoryId::News},
      {"health", CategoryId::HealthAndFitness},
      {"finance", CategoryId::BusinessAndFinance},
      {"location", CategoryId::Location},
      {"entertainment", CategoryId::Entertainment},
  };
  for (const auto& m : kMap) {
    if (s == m.name) {
      out = m.id;
      return true;
    }
  }
  return false;
}

bool parseManeuver(const std::string& s, Maneuver& out) {
  static const struct {
    const char* name;
    Maneuver m;
  } kMap[] = {
      {"straight", Maneuver::Straight},
      {"left", Maneuver::Left},
      {"right", Maneuver::Right},
      {"slight-left", Maneuver::SlightLeft},
      {"slight-right", Maneuver::SlightRight},
      {"sharp-left", Maneuver::SharpLeft},
      {"sharp-right", Maneuver::SharpRight},
      {"uturn", Maneuver::UTurn},
      {"arrive", Maneuver::Arrive},
  };
  for (const auto& m : kMap) {
    if (s == m.name) {
      out = m.m;
      return true;
    }
  }
  return false;
}

bool parseFlags(const std::string& s, uint8_t& out, std::string& err) {
  using namespace phoenix::ancs;
  out = 0;
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    const std::string part =
        s.substr(start, comma == std::string::npos ? std::string::npos
                                                   : comma - start);
    if (part == "silent") out |= kFlagSilent;
    else if (part == "important") out |= kFlagImportant;
    else if (part == "preexisting") out |= kFlagPreExisting;
    else if (part == "positive") out |= kFlagPositiveAction;
    else if (part == "negative") out |= kFlagNegativeAction;
    else if (!part.empty()) {
      err = "unknown flag '" + part + "'";
      return false;
    }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return true;
}

// Fills an AncsNotification from key=value tokens.
bool parseAncsKeys(const std::vector<std::string>& toks, size_t from,
                   phoenix::ancs::AncsNotification& n, bool& haveUid,
                   std::string& err) {
  haveUid = false;
  for (size_t i = from; i < toks.size(); ++i) {
    const std::string& t = toks[i];
    const size_t eq = t.find('=');
    if (eq == std::string::npos) {
      err = "expected key=value, got '" + t + "'";
      return false;
    }
    const std::string key = t.substr(0, eq);
    const std::string val = t.substr(eq + 1);
    if (key == "uid") {
      uint32_t uid;
      if (!parseUint(val, uid)) {
        err = "bad uid";
        return false;
      }
      n.uid = uid;
      haveUid = true;
    } else if (key == "cat") {
      if (!parseCategory(val, n.category)) {
        err = "unknown category '" + val + "'";
        return false;
      }
    } else if (key == "app") {
      n.appIdentifier = val;
    } else if (key == "appname") {
      n.appDisplayName = val;
    } else if (key == "title") {
      n.title = val;
    } else if (key == "subtitle") {
      n.subtitle = val;
    } else if (key == "msg") {
      n.message = val;
    } else if (key == "date") {
      n.date = val;
    } else if (key == "flags") {
      if (!parseFlags(val, n.eventFlags, err)) return false;
    } else {
      err = "unknown key '" + key + "'";
      return false;
    }
  }
  return true;
}

std::string joinFrom(const std::vector<std::string>& toks, size_t from) {
  std::string out;
  for (size_t i = from; i < toks.size(); ++i) {
    if (i > from) out += ' ';
    out += toks[i];
  }
  return out;
}

}  // namespace

bool parseScenarioFile(const std::string& path, Scenario& out) {
  std::ifstream in(path);
  if (!in) {
    std::fprintf(stderr, "%s: cannot open\n", path.c_str());
    return false;
  }
  const size_t slash = path.find_last_of('/');
  std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos) stem = stem.substr(0, dot);
  out.fileStem = stem;
  out.name = stem;

  std::string line;
  int lineNo = 0;
  auto fail = [&](const std::string& why) {
    std::fprintf(stderr, "%s:%d: %s\n", path.c_str(), lineNo, why.c_str());
    return false;
  };

  while (std::getline(in, line)) {
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::vector<std::string> toks;
    std::string err;
    if (!tokenize(line, toks, err)) return fail(err);
    if (toks.empty()) continue;
    const std::string& cmd = toks[0];

    Event e;
    e.line = lineNo;

    if (cmd == "name") {
      out.name = joinFrom(toks, 1);
      continue;
    }
    if (cmd == "desc") {
      out.desc = joinFrom(toks, 1);
      continue;
    }
    if (cmd == "tick") {
      if (toks.size() != 2 || !parseUint(toks[1], e.number) || e.number == 0) {
        return fail("usage: tick <n>");
      }
      e.type = Event::Type::Tick;
    } else if (cmd == "connect") {
      e.type = Event::Type::Connect;
    } else if (cmd == "disconnect") {
      e.type = Event::Type::Disconnect;
    } else if (cmd == "bond") {
      e.type = Event::Type::Bond;
    } else if (cmd == "time") {
      if (toks.size() != 3) return fail("usage: time YYYY-MM-DD HH:MM:SS");
      int y, mo, d, h, mi, s;
      if (std::sscanf(toks[1].c_str(), "%d-%d-%d", &y, &mo, &d) != 3 ||
          std::sscanf(toks[2].c_str(), "%d:%d:%d", &h, &mi, &s) != 3) {
        return fail("bad time format");
      }
      e.type = Event::Type::Time;
      e.time = {y, mo, d, h, mi, s};
    } else if (cmd == "battery") {
      if (toks.size() != 2 || !parseUint(toks[1], e.number)) {
        return fail("usage: battery <millivolts>");
      }
      e.type = Event::Type::Battery;
    } else if (cmd == "ancs-add" || cmd == "ancs-modify") {
      bool haveUid = false;
      if (!parseAncsKeys(toks, 1, e.notif, haveUid, err)) return fail(err);
      if (!haveUid) return fail("ancs event needs uid=");
      e.type = cmd == "ancs-add" ? Event::Type::AncsAdd : Event::Type::AncsModify;
    } else if (cmd == "ancs-remove") {
      phoenix::ancs::AncsNotification n;
      bool haveUid = false;
      if (!parseAncsKeys(toks, 1, n, haveUid, err)) return fail(err);
      if (!haveUid) return fail("ancs-remove needs uid=");
      e.type = Event::Type::AncsRemove;
      e.number = n.uid;
    } else if (cmd == "assistant-text") {
      e.type = Event::Type::AssistantText;
      e.text = joinFrom(toks, 1);
    } else if (cmd == "assistant-chunk" || cmd == "assistant-chunk-final") {
      e.type = Event::Type::AssistantChunk;
      e.flag = cmd == "assistant-chunk-final";
      e.text = joinFrom(toks, 1);
    } else if (cmd == "nav") {
      if (toks.size() < 3) return fail("usage: nav <maneuver> <meters|?> <street>");
      if (!parseManeuver(toks[1], e.maneuver)) {
        return fail("unknown maneuver '" + toks[1] + "'");
      }
      if (toks[2] == "?") {
        e.meters = phoenix::proto::kDistanceUnknown;
      } else {
        uint32_t m;
        if (!parseUint(toks[2], m) || m > 0xFFFF) return fail("bad meters");
        e.meters = static_cast<uint16_t>(m);
      }
      e.type = Event::Type::Nav;
      e.text = joinFrom(toks, 3);
    } else if (cmd == "clear") {
      if (toks.size() != 2) return fail("usage: clear <all|assistant|nav|notification>");
      using namespace phoenix::proto;
      if (toks[1] == "all") e.byte = kClearAll;
      else if (toks[1] == "assistant") e.byte = kClearAssistant;
      else if (toks[1] == "nav") e.byte = kClearNav;
      else if (toks[1] == "notification") e.byte = kClearNotification;
      else return fail("unknown clear target");
      e.type = Event::Type::Clear;
    } else if (cmd == "brightness") {
      if (toks.size() != 2 || !parseUint(toks[1], e.number) || e.number > 255) {
        return fail("usage: brightness <0-255>");
      }
      e.type = Event::Type::Brightness;
      e.byte = static_cast<uint8_t>(e.number);
    } else if (cmd == "ping") {
      if (toks.size() != 2 || !parseUint(toks[1], e.number) || e.number > 255) {
        return fail("usage: ping <version>");
      }
      e.type = Event::Type::Ping;
      e.byte = static_cast<uint8_t>(e.number);
    } else if (cmd == "button") {
      if (toks.size() < 2 || toks.size() > 3) return fail("usage: button <a|b|c> [long]");
      if (toks[1] == "a") e.byte = 1;
      else if (toks[1] == "b") e.byte = 2;
      else if (toks[1] == "c") e.byte = 3;
      else return fail("unknown button '" + toks[1] + "'");
      e.flag = toks.size() == 3 && toks[2] == "long";
      if (toks.size() == 3 && !e.flag) return fail("expected 'long'");
      e.type = Event::Type::ButtonPress;
    } else {
      return fail("unknown command '" + cmd + "'");
    }
    out.events.push_back(std::move(e));
  }
  if (out.events.empty()) {
    std::fprintf(stderr, "%s: no events\n", path.c_str());
    return false;
  }
  return true;
}

}  // namespace sim
