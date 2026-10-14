#include "FlashcardReviewSession.h"

#include <HalStorage.h>
#include <MemoryBudget.h>

#include <climits>
#include <cstdint>

namespace {
constexpr char SESSION_PATH[] = "/.crosspoint/flashcard_resume.bin";
constexpr char TEMP_PATH[] = "/.crosspoint/flashcard_resume.bin.tmp";
constexpr uint32_t SESSION_MAGIC = 0x46525331;  // FRS1; versioned, little-endian on supported boards
constexpr size_t MAX_PENDING_CARDS = 600;
constexpr uint32_t HASH_SEED = 2166136261u;

uint32_t hashBytes(uint32_t hash, const void* data, const size_t length) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

uint32_t hashString(const uint32_t hash, const std::string& value) {
  return hashBytes(hash, value.c_str(), value.size() + 1);
}

// Bind indices to the same deck order, persisted ratings and study settings.
// An edited deck or an older progress file must start a fresh, valid session.
uint32_t fingerprint(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress,
                     const uint8_t studyMode, const uint8_t sessionSize) {
  uint32_t hash = hashString(hashString(HASH_SEED, deck.path), deck.deckId);
  hash = hashBytes(hash, &studyMode, sizeof(studyMode));
  hash = hashBytes(hash, &sessionSize, sizeof(sessionSize));
  for (const auto& card : deck.cards) hash = hashString(hash, card.key);
  for (const auto& item : progress) {
    hash = hashString(hash, item.key);
    const uint32_t fields[] = {item.seenCount,       item.successCount, item.failCount,   item.skipCount,
                               item.lastReviewedDay, item.dueDay,       item.intervalDays};
    hash = hashBytes(hash, fields, sizeof(fields));
  }
  return hash;
}

bool validCounters(const uint32_t* header, const size_t cardCount) {
  // Header: magic, fingerprint, remaining, initial size, side, reviewed,
  // correct, failed, skipped, new seen, scroll offset, payload checksum.
  if (cardCount > MAX_PENDING_CARDS || header[2] == 0 || header[2] > MAX_PENDING_CARDS || header[3] == 0 ||
      header[3] > cardCount || header[4] > 1 || header[9] > cardCount || header[10] > INT_MAX)
    return false;
  for (size_t i = 5; i <= 8; ++i) {
    if (header[i] > INT_MAX) return false;
  }
  return static_cast<uint64_t>(header[6]) + header[7] == header[5] &&
         static_cast<uint64_t>(header[5]) + header[8] < INT_MAX && header[9] <= header[5];
}
}  // namespace

bool FlashcardReviewSession::save(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress,
                                  const uint8_t studyMode, const uint8_t sessionSize) const {
  if (queueIndex >= queue.size() || queue.size() - queueIndex > MAX_PENDING_CARDS ||
      deck.cards.size() != progress.size())
    return false;
  uint32_t header[] = {SESSION_MAGIC,
                       fingerprint(deck, progress, studyMode, sessionSize),
                       static_cast<uint32_t>(queue.size() - queueIndex),
                       static_cast<uint32_t>(initialSessionSize),
                       showBack ? 1u : 0u,
                       static_cast<uint32_t>(reviewed),
                       static_cast<uint32_t>(correct),
                       static_cast<uint32_t>(failed),
                       static_cast<uint32_t>(skipped),
                       static_cast<uint32_t>(newSeen),
                       static_cast<uint32_t>(scrollLineOffset),
                       0};
  if (!validCounters(header, deck.cards.size())) return false;
  uint32_t checksum = hashBytes(HASH_SEED, header, sizeof(header) - sizeof(uint32_t));
  for (size_t i = queueIndex; i < queue.size(); ++i) {
    if (queue[i] < 0 || static_cast<size_t>(queue[i]) >= deck.cards.size()) return false;
    const uint16_t index = static_cast<uint16_t>(queue[i]);
    checksum = hashBytes(checksum, &index, sizeof(index));
  }
  header[11] = checksum;
  Storage.mkdir("/.crosspoint");
  Storage.remove(TEMP_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("FCR", TEMP_PATH, file)) return false;
  bool ok = file.write(header, sizeof(header)) == sizeof(header);
  for (size_t i = queueIndex; ok && i < queue.size(); ++i) {
    const uint16_t index = static_cast<uint16_t>(queue[i]);
    ok = file.write(&index, sizeof(index)) == sizeof(index);
  }
  file.flush();
  ok = file.close() && ok;
  if (ok && Storage.exists(SESSION_PATH)) ok = Storage.remove(SESSION_PATH);
  if (ok) ok = Storage.rename(TEMP_PATH, SESSION_PATH);
  if (!ok) Storage.remove(TEMP_PATH);
  return ok;
}

bool FlashcardReviewSession::load(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress,
                                  const uint8_t studyMode, const uint8_t sessionSize) {
  HalFile file;
  if (!Storage.openFileForRead("FCR", SESSION_PATH, file)) return false;
  uint32_t header[12]{};
  if (file.read(header, sizeof(header)) != sizeof(header) || header[0] != SESSION_MAGIC ||
      !validCounters(header, deck.cards.size()) || deck.cards.size() != progress.size() ||
      file.fileSize() != sizeof(header) + header[2] * sizeof(uint16_t) ||
      header[1] != fingerprint(deck, progress, studyMode, sessionSize))
    return false;
  const uint32_t bytes = header[2] * sizeof(int);
  if (!MemoryBudget::hasHeap(MemoryBudget::snapshot(), bytes + 16U * 1024U, bytes + 1024U)) return false;
  std::vector<int> pending;
  pending.reserve(header[2]);
  uint32_t checksum = hashBytes(HASH_SEED, header, sizeof(header) - sizeof(uint32_t));
  for (uint32_t i = 0; i < header[2]; ++i) {
    uint16_t index;
    if (file.read(&index, sizeof(index)) != sizeof(index) || index >= deck.cards.size()) return false;
    checksum = hashBytes(checksum, &index, sizeof(index));
    pending.push_back(index);
  }
  if (checksum != header[11]) return false;
  queue = std::move(pending);
  queueIndex = 0;
  initialSessionSize = static_cast<int>(header[3]);
  showBack = header[4] != 0;
  reviewed = static_cast<int>(header[5]);
  correct = static_cast<int>(header[6]);
  failed = static_cast<int>(header[7]);
  skipped = static_cast<int>(header[8]);
  newSeen = static_cast<int>(header[9]);
  scrollLineOffset = static_cast<int>(header[10]);
  return true;
}
