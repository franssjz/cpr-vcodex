#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <type_traits>

enum class KOReaderRecoveryOperation : uint8_t { Authenticate, Register, ManualSync };

// References the existing credential store; no password or password hash is
// copied into RTC memory or a second on-disk record.
struct KOReaderRecoveryRequest {
  KOReaderRecoveryOperation operation;
  uint8_t profileIndex;
  uint16_t reserved;
  uint32_t profileIdentity;
};

namespace KOReaderRecovery {
constexpr uint32_t MAGIC = 0x4B4F5243;
constexpr uint32_t VERSION = 1;
constexpr size_t MAX_PROFILES = 8;

struct Record {
  uint32_t magic;
  uint32_t version;
  KOReaderRecoveryRequest request;
  uint32_t checksum;
};
static_assert(std::is_trivial_v<Record>, "RTC recovery record must not be initialized at startup");
static_assert(sizeof(Record) == 20, "Keep the one-shot RTC recovery record small");

inline uint32_t appendWord(uint32_t hash, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    hash = (hash ^ (value & 0xffU)) * 16777619U;
    value >>= 8;
  }
  return hash;
}

inline uint32_t profileIdentity(const std::string_view username, const std::string_view resolvedServer) {
  uint32_t hash = 2166136261U;
  for (const auto field : {username, resolvedServer}) {
    hash = appendWord(hash, static_cast<uint32_t>(field.size()));
    for (const unsigned char byte : field) hash = (hash ^ byte) * 16777619U;
  }
  return hash;
}

inline bool validRequest(const KOReaderRecoveryRequest& request) {
  return request.profileIndex < MAX_PROFILES && request.reserved == 0 &&
         (request.operation == KOReaderRecoveryOperation::Authenticate ||
          request.operation == KOReaderRecoveryOperation::Register ||
          request.operation == KOReaderRecoveryOperation::ManualSync);
}

inline uint32_t checksum(const Record& record) {
  uint32_t hash = appendWord(2166136261U, record.magic);
  hash = appendWord(hash, record.version);
  hash = appendWord(hash, static_cast<uint8_t>(record.request.operation));
  hash = appendWord(hash, record.request.profileIndex);
  hash = appendWord(hash, record.request.reserved);
  return appendWord(hash, record.request.profileIdentity);
}

inline bool arm(Record& record, const KOReaderRecoveryRequest& request) {
  record = {};
  if (!validRequest(request)) return false;
  record = {MAGIC, VERSION, request, 0};
  record.checksum = checksum(record);
  return true;
}

// Clear even invalid/stale data before doing any work that may reset the device.
inline bool consume(Record& record, const bool matchingBoot, KOReaderRecoveryRequest& request) {
  const Record snapshot = record;
  record = {};
  if (!matchingBoot || snapshot.magic != MAGIC || snapshot.version != VERSION || !validRequest(snapshot.request) ||
      snapshot.checksum != checksum(snapshot)) {
    return false;
  }
  request = snapshot.request;
  return true;
}

inline bool matchesProfile(const KOReaderRecoveryRequest& request, const size_t profileCount,
                           const int activeProfileIndex, const uint32_t currentIdentity) {
  return validRequest(request) && request.profileIndex < profileCount && request.profileIdentity == currentIdentity &&
         (request.operation == KOReaderRecoveryOperation::Register || request.profileIndex == activeProfileIndex);
}

// A resumed activity must surface a second failure instead of arming another boot.
inline bool shouldRecover(const bool resumed, const bool preflightRejected) { return !resumed && preflightRejected; }
}  // namespace KOReaderRecovery
