#pragma once

// Firmware/stack version, shown on the splash and status screens and reported
// over the Phoenix protocol.
namespace phoenix {

inline constexpr const char* kFirmwareVersion = "1.0.0";
inline constexpr unsigned kProtocolVersion = 1;

}  // namespace phoenix
