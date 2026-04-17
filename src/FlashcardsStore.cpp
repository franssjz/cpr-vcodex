#include "FlashcardsStore.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "util/BookIdentity.h"
#include "util/TimeUtils.h"

namespace {
constexpr char FLASHCARDS_INDEX_FILE[] = "/.crosspoint/flashcards_index.json";
constexpr uint32_t FLASHCARDS_INDEX_FORMAT_VERSION = 1;
constexpr uint32_t FLASHCARDS_STATE_FORMAT_VERSION = 1;
constexpr uint16_t MASTERY_INTERVAL_DAYS = 7;

std::string trimAscii(const std::string& value) {
  const size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string toLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string normalizeField(const std::string& value) {
  std::string trimmed = trimAscii(value);
  if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
    trimmed = trimmed.substr(1, trimmed.size() - 2);
  }
  return trimmed;
}

std::string fnv1aHex(const std::string& value) {
  uint32_t hash = 2166136261u;
  for (const unsigned char ch : value) {
    hash ^= ch;
    hash *= 16777619u;
  }

  char buffer[9];
  snprintf(buffer, sizeof(buffer), "%08x", hash);
  return buffer;
}

bool saveJsonDocumentToFile(const char* moduleName, const char* path, const JsonDocument& doc) {
  HalFile file;
  if (!Storage.openFileForWrite(moduleName, path, file)) {
    LOG_ERR(moduleName, "Could not open JSON file for write: %s", path);
    return false;
  }

  const size_t written = serializeJson(doc, file);
  file.flush();
  file.close();
  return written > 0;
}

bool loadJsonDocumentFromFile(const char* moduleName, const char* path, JsonDocument& doc) {
  if (!Storage.exists(path)) {
    return false;
  }

  const String json = Storage.readFile(path);
  if (json.isEmpty()) {
    LOG_ERR(moduleName, "JSON file empty: %s", path);
    return false;
  }

  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR(moduleName, "JSON parse error in %s: %s", path, error.c_str());
    return false;
  }
  return true;
}

std::vector<std::vector<std::string>> parseCsvRows(HalFile& file) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> currentRow;
  std::string currentField;
  bool inQuotes = false;
  int bufferedChar = -1;

  auto pushField = [&]() {
    currentRow.push_back(normalizeField(currentField));
    currentField.clear();
  };

  auto pushRow = [&]() {
    if (currentRow.empty()) {
      return;
    }

    bool hasValue = false;
    for (const auto& field : currentRow) {
      if (!field.empty()) {
        hasValue = true;
        break;
      }
    }
    if (hasValue) {
      rows.push_back(currentRow);
    }
    currentRow.clear();
  };

  while (bufferedChar >= 0 || file.available()) {
    const int raw = bufferedChar >= 0 ? bufferedChar : file.read();
    bufferedChar = -1;
    if (raw < 0) {
      break;
    }

    const char ch = static_cast<char>(raw);
    if (inQuotes) {
      if (ch == '"') {
        const int nextRaw = file.available() ? file.read() : -1;
        if (nextRaw == '"') {
          currentField.push_back('"');
        } else {
          inQuotes = false;
          bufferedChar = nextRaw;
        }
      } else {
        currentField.push_back(ch);
      }
      continue;
    }

    switch (ch) {
      case '"':
        inQuotes = true;
        break;
      case ',':
        pushField();
        break;
      case '\r':
        break;
      case '\n':
        pushField();
        pushRow();
        break;
      default:
        currentField.push_back(ch);
        break;
    }
  }

  if (!currentField.empty() || !currentRow.empty()) {
    pushField();
    pushRow();
  }

  return rows;
}

int getConfiguredSessionLimit() {
  switch (SETTINGS.flashcardSessionSize) {
    case CrossPointSettings::FLASHCARD_SESSION_10:
      return 10;
    case CrossPointSettings::FLASHCARD_SESSION_20:
      return 20;
    case CrossPointSettings::FLASHCARD_SESSION_30:
      return 30;
    case CrossPointSettings::FLASHCARD_SESSION_50:
      return 50;
    case CrossPointSettings::FLASHCARD_SESSION_ALL:
    default:
      return 0;
  }
}

bool isScheduledMode() {
  return SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_SCHEDULED;
}

bool isDueMode() {
  return SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_DUE;
}

bool isInfiniteMode() {
  return SETTINGS.flashcardStudyMode == CrossPointSettings::FLASHCARD_STUDY_INFINITE;
}

template <typename T>
void shuffleVector(std::vector<T>& values) {
  if (values.size() < 2) {
    return;
  }
  for (int index = static_cast<int>(values.size()) - 1; index > 0; --index) {
    const int swapIndex = random(index + 1);
    std::swap(values[index], values[swapIndex]);
  }
}
}  // namespace

FlashcardsStore FlashcardsStore::instance;

FlashcardDeckRecord* FlashcardsStore::findDeckRecordInternal(const std::string& deckId) {
  for (auto& record : knownDecks) {
    if (record.deckId == deckId) {
      return &record;
    }
  }
  return nullptr;
}

const FlashcardDeckRecord* FlashcardsStore::findDeckRecordInternal(const std::string& deckId) const {
  for (const auto& record : knownDecks) {
    if (record.deckId == deckId) {
      return &record;
    }
  }
  return nullptr;
}

std::string FlashcardsStore::getIndexPath() const { return FLASHCARDS_INDEX_FILE; }

std::string FlashcardsStore::getStatePath(const std::string& deckId) const {
  return BookIdentity::getStableDataFilePath(deckId, "flashcards.json");
}

uint32_t FlashcardsStore::getReferenceTimestamp() const {
  uint32_t timestamp = TimeUtils::getAuthoritativeTimestamp();
  if (TimeUtils::isClockValid(timestamp)) {
    return timestamp;
  }

  timestamp = APP_STATE.lastKnownValidTimestamp;
  if (TimeUtils::isClockValid(timestamp)) {
    return timestamp;
  }

  return 0;
}

uint32_t FlashcardsStore::getReferenceDayOrdinal() const {
  const uint32_t timestamp = getReferenceTimestamp();
  return TimeUtils::isClockValid(timestamp) ? TimeUtils::getLocalDayOrdinal(timestamp) : 0;
}

std::string FlashcardsStore::getTitleFromPath(const std::string& path) {
  const size_t slashPos = path.find_last_of('/');
  const std::string filename = slashPos == std::string::npos ? path : path.substr(slashPos + 1);
  const size_t dotPos = filename.rfind('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}

std::vector<FlashcardDeckRecord> FlashcardsStore::getRecentDecks() const {
  std::vector<FlashcardDeckRecord> decks;
  decks.reserve(recentDeckIds.size());

  for (const auto& deckId : recentDeckIds) {
    if (const auto* record = findDeckRecordInternal(deckId)) {
      decks.push_back(*record);
    }
  }

  return decks;
}

bool FlashcardsStore::removeRecentDeck(const std::string& deckIdOrPath) {
  std::string deckId = deckIdOrPath;
  if (findDeckRecordInternal(deckId) == nullptr) {
    const auto normalizedPath = BookIdentity::normalizePath(deckIdOrPath);
    deckId = normalizedPath.empty() ? deckIdOrPath : BookIdentity::resolveStableBookId(normalizedPath);
  }

  const auto oldSize = recentDeckIds.size();
  recentDeckIds.erase(
      std::remove_if(recentDeckIds.begin(), recentDeckIds.end(), [&](const std::string& value) { return value == deckId; }),
      recentDeckIds.end());
  if (recentDeckIds.size() == oldSize) {
    return false;
  }
  saveToFile();
  return true;
}

bool FlashcardsStore::resetDeckStats(const std::string& deckIdOrPath) {
  std::string deckId = deckIdOrPath;
  if (findDeckRecordInternal(deckId) == nullptr) {
    const auto normalizedPath = BookIdentity::normalizePath(deckIdOrPath);
    deckId = normalizedPath.empty() ? deckIdOrPath : BookIdentity::resolveStableBookId(normalizedPath);
  }

  FlashcardDeckRecord* record = findDeckRecordInternal(deckId);
  if (!record) {
    return false;
  }

  const std::string statePath = getStatePath(deckId);
  if (Storage.exists(statePath.c_str())) {
    Storage.remove(statePath.c_str());
  }

  FlashcardDeck deck;
  const bool deckLoaded = loadDeck(record->path, deck, nullptr);
  record->title = deckLoaded ? deck.title : getTitleFromPath(record->path);
  record->lastReviewedAt = 0;
  record->sessionCount = 0;
  record->totalCards = deckLoaded ? static_cast<uint32_t>(deck.cards.size()) : 0;
  record->seenCards = 0;
  record->masteredCards = 0;
  record->dueCards = 0;
  record->totalReviewed = 0;
  record->totalCorrect = 0;
  record->totalWrong = 0;
  record->totalSkipped = 0;

  return saveToFile();
}

bool FlashcardsStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["formatVersion"] = FLASHCARDS_INDEX_FORMAT_VERSION;

  JsonArray recent = doc["recentDeckIds"].to<JsonArray>();
  for (const auto& deckId : recentDeckIds) {
    recent.add(deckId);
  }

  JsonArray decks = doc["decks"].to<JsonArray>();
  for (const auto& record : knownDecks) {
    JsonObject obj = decks.add<JsonObject>();
    obj["deckId"] = record.deckId;
    obj["path"] = record.path;
    obj["title"] = record.title;
    obj["lastOpenedAt"] = record.lastOpenedAt;
    obj["lastReviewedAt"] = record.lastReviewedAt;
    obj["sessionCount"] = record.sessionCount;
    obj["totalCards"] = record.totalCards;
    obj["seenCards"] = record.seenCards;
    obj["masteredCards"] = record.masteredCards;
    obj["dueCards"] = record.dueCards;
    obj["totalReviewed"] = record.totalReviewed;
    obj["totalCorrect"] = record.totalCorrect;
    obj["totalWrong"] = record.totalWrong;
    obj["totalSkipped"] = record.totalSkipped;
  }

  return saveJsonDocumentToFile("FCS", getIndexPath().c_str(), doc);
}

bool FlashcardsStore::loadFromFile() {
  knownDecks.clear();
  recentDeckIds.clear();

  JsonDocument doc;
  if (!loadJsonDocumentFromFile("FCS", getIndexPath().c_str(), doc)) {
    return false;
  }

  for (JsonVariant value : doc["recentDeckIds"].as<JsonArray>()) {
    const std::string deckId = value | std::string("");
    if (!deckId.empty()) {
      recentDeckIds.push_back(deckId);
    }
  }

  for (JsonObject obj : doc["decks"].as<JsonArray>()) {
    FlashcardDeckRecord record;
    record.deckId = obj["deckId"] | std::string("");
    record.path = obj["path"] | std::string("");
    record.title = obj["title"] | std::string("");
    if (record.deckId.empty() || record.path.empty()) {
      continue;
    }
    record.lastOpenedAt = obj["lastOpenedAt"] | static_cast<uint32_t>(0);
    record.lastReviewedAt = obj["lastReviewedAt"] | static_cast<uint32_t>(0);
    record.sessionCount = obj["sessionCount"] | static_cast<uint32_t>(0);
    record.totalCards = obj["totalCards"] | static_cast<uint32_t>(0);
    record.seenCards = obj["seenCards"] | static_cast<uint32_t>(0);
    record.masteredCards = obj["masteredCards"] | static_cast<uint32_t>(0);
    record.dueCards = obj["dueCards"] | static_cast<uint32_t>(0);
    record.totalReviewed = obj["totalReviewed"] | static_cast<uint32_t>(0);
    record.totalCorrect = obj["totalCorrect"] | static_cast<uint32_t>(0);
    record.totalWrong = obj["totalWrong"] | static_cast<uint32_t>(0);
    record.totalSkipped = obj["totalSkipped"] | static_cast<uint32_t>(0);
    knownDecks.push_back(std::move(record));
  }

  recentDeckIds.erase(std::remove_if(recentDeckIds.begin(), recentDeckIds.end(), [this](const std::string& deckId) {
                    return findDeckRecordInternal(deckId) == nullptr;
                  }),
                  recentDeckIds.end());
  if (recentDeckIds.size() > MAX_RECENT_DECKS) {
    recentDeckIds.resize(MAX_RECENT_DECKS);
  }

  return true;
}

bool FlashcardsStore::loadDeck(const std::string& path, FlashcardDeck& deck, std::string* error) const {
  deck = {};
  const std::string normalizedPath = BookIdentity::normalizePath(path);
  if (normalizedPath.empty() || !Storage.exists(normalizedPath.c_str())) {
    if (error) *error = "Deck file not found";
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("FCS", normalizedPath, file)) {
    if (error) *error = "Could not open deck";
    return false;
  }

  auto rows = parseCsvRows(file);
  file.close();
  if (rows.empty()) {
    if (error) *error = "Deck is empty";
    return false;
  }

  int idColumn = -1;
  int frontColumn = 0;
  int backColumn = 1;
  const auto maybeHeader = rows.front();
  bool hasNamedHeader = false;
  for (int index = 0; index < static_cast<int>(maybeHeader.size()); ++index) {
    const std::string field = toLowerAscii(trimAscii(maybeHeader[index]));
    if (field == "id" || field == "card_id") {
      idColumn = index;
      hasNamedHeader = true;
    } else if (field == "front" || field == "question") {
      frontColumn = index;
      hasNamedHeader = true;
    } else if (field == "back" || field == "answer") {
      backColumn = index;
      hasNamedHeader = true;
    }
  }
  if (hasNamedHeader) {
    rows.erase(rows.begin());
  }

  if (frontColumn == backColumn) {
    if (error) *error = "Deck needs front and back columns";
    return false;
  }

  deck.path = normalizedPath;
  deck.deckId = BookIdentity::resolveStableBookId(normalizedPath);
  deck.title = getTitleFromPath(normalizedPath);
  deck.cards.reserve(rows.size());

  for (const auto& row : rows) {
    const int maxColumn = std::max(frontColumn, backColumn);
    if (static_cast<int>(row.size()) <= maxColumn) {
      continue;
    }

    const std::string front = trimAscii(row[frontColumn]);
    const std::string back = trimAscii(row[backColumn]);
    if (front.empty() && back.empty()) {
      continue;
    }

    std::string key;
    if (idColumn >= 0 && idColumn < static_cast<int>(row.size())) {
      key = trimAscii(row[idColumn]);
    }
    if (key.empty()) {
      key = fnv1aHex(front + "\x1f" + back);
    }

    deck.cards.push_back(FlashcardCard{key, front, back});
  }

  if (deck.cards.empty()) {
    if (error) *error = "No valid cards found";
    return false;
  }

  return true;
}

bool FlashcardsStore::loadDeckProgress(const FlashcardDeck& deck, std::vector<FlashcardCardProgress>& progress) const {
  progress.clear();
  progress.reserve(deck.cards.size());

  JsonDocument doc;
  std::vector<FlashcardCardProgress> saved;
  if (loadJsonDocumentFromFile("FCS", getStatePath(deck.deckId).c_str(), doc)) {
    for (JsonObject obj : doc["cards"].as<JsonArray>()) {
      FlashcardCardProgress item;
      item.key = obj["key"] | std::string("");
      if (item.key.empty()) {
        continue;
      }
      item.seenCount = obj["seenCount"] | static_cast<uint32_t>(0);
      item.successCount = obj["successCount"] | static_cast<uint32_t>(0);
      item.failCount = obj["failCount"] | static_cast<uint32_t>(0);
      item.skipCount = obj["skipCount"] | static_cast<uint32_t>(0);
      item.lastReviewedDay = obj["lastReviewedDay"] | static_cast<uint32_t>(0);
      item.dueDay = obj["dueDay"] | static_cast<uint32_t>(0);
      item.intervalDays = obj["intervalDays"] | static_cast<uint16_t>(0);
      saved.push_back(std::move(item));
    }
  }

  for (const auto& card : deck.cards) {
    auto it = std::find_if(saved.begin(), saved.end(),
                           [&card](const FlashcardCardProgress& value) { return value.key == card.key; });
    if (it != saved.end()) {
      progress.push_back(*it);
    } else {
      progress.push_back(FlashcardCardProgress{card.key});
    }
  }
  return true;
}

bool FlashcardsStore::saveDeckProgress(const FlashcardDeck& deck, const std::vector<FlashcardCardProgress>& progress) {
  BookIdentity::ensureStableDataDir(deck.deckId);

  JsonDocument doc;
  doc["formatVersion"] = FLASHCARDS_STATE_FORMAT_VERSION;
  doc["deckId"] = deck.deckId;
  doc["path"] = deck.path;
  doc["title"] = deck.title;

  JsonArray cards = doc["cards"].to<JsonArray>();
  for (const auto& item : progress) {
    JsonObject obj = cards.add<JsonObject>();
    obj["key"] = item.key;
    obj["seenCount"] = item.seenCount;
    obj["successCount"] = item.successCount;
    obj["failCount"] = item.failCount;
    obj["skipCount"] = item.skipCount;
    obj["lastReviewedDay"] = item.lastReviewedDay;
    obj["dueDay"] = item.dueDay;
    obj["intervalDays"] = item.intervalDays;
  }

  return saveJsonDocumentToFile("FCS", getStatePath(deck.deckId).c_str(), doc);
}

FlashcardDeckMetrics FlashcardsStore::buildMetrics(const FlashcardDeck& deck,
                                                   const std::vector<FlashcardCardProgress>& progress) const {
  FlashcardDeckMetrics metrics;
  metrics.totalCards = static_cast<int>(deck.cards.size());

  for (const auto& item : progress) {
    const bool seen = item.seenCount > 0;
    if (seen) {
      metrics.seenCards++;
      metrics.totalReviewed += static_cast<int>(item.successCount + item.failCount + item.skipCount);
      metrics.totalCorrect += static_cast<int>(item.successCount);
      metrics.totalWrong += static_cast<int>(item.failCount);
      metrics.totalSkipped += static_cast<int>(item.skipCount);
      if (item.intervalDays >= MASTERY_INTERVAL_DAYS && item.successCount > item.failCount) {
        metrics.masteredCards++;
      }
      const uint32_t today = getReferenceDayOrdinal();
      if (today == 0 || item.dueDay == 0 || item.dueDay <= today) {
        metrics.dueCards++;
      }
    }
  }

  metrics.unseenCards = std::max(0, metrics.totalCards - metrics.seenCards);
  const int answered = metrics.totalCorrect + metrics.totalWrong;
  metrics.successRatePercent = answered > 0 ? (metrics.totalCorrect * 100) / answered : 0;

  if (const auto* record = findDeckRecordInternal(deck.deckId)) {
    metrics.lastOpenedAt = record->lastOpenedAt;
    metrics.lastReviewedAt = record->lastReviewedAt;
    metrics.sessionCount = record->sessionCount;
  }

  return metrics;
}

std::vector<int> FlashcardsStore::buildSessionQueue(const FlashcardDeck& deck,
                                                    const std::vector<FlashcardCardProgress>& progress) const {
  std::vector<int> queue;
  if (isInfiniteMode()) {
    queue.resize(deck.cards.size());
    for (int index = 0; index < static_cast<int>(deck.cards.size()); ++index) {
      queue[index] = index;
    }
    shuffleVector(queue);
    return queue;
  }

  const int sessionLimit = getConfiguredSessionLimit();

  if (isDueMode()) {
    std::vector<int> dueCards;
    std::vector<int> unseenCards;

    const uint32_t today = getReferenceDayOrdinal();
    for (int index = 0; index < static_cast<int>(deck.cards.size()) && index < static_cast<int>(progress.size());
         ++index) {
      const auto& item = progress[index];
      if (item.seenCount == 0) {
        unseenCards.push_back(index);
        continue;
      }

      if (today == 0 || item.dueDay == 0 || item.dueDay <= today) {
        dueCards.push_back(index);
      }
    }

    shuffleVector(dueCards);
    shuffleVector(unseenCards);

    queue.insert(queue.end(), dueCards.begin(), dueCards.end());
    if (sessionLimit <= 0 || static_cast<int>(queue.size()) < sessionLimit) {
      const int remaining = sessionLimit <= 0 ? static_cast<int>(unseenCards.size())
                                              : std::max(0, sessionLimit - static_cast<int>(queue.size()));
      queue.insert(queue.end(), unseenCards.begin(),
                   unseenCards.begin() + std::min(remaining, static_cast<int>(unseenCards.size())));
    }
  } else {
    queue.resize(deck.cards.size());
    for (int index = 0; index < static_cast<int>(deck.cards.size()); ++index) {
      queue[index] = index;
    }
    shuffleVector(queue);
  }

  if (sessionLimit > 0 && static_cast<int>(queue.size()) > sessionLimit) {
    queue.resize(sessionLimit);
  }
  return queue;
}

void FlashcardsStore::registerDeckOpened(const FlashcardDeck& deck, const FlashcardDeckMetrics& metrics) {
  const uint32_t timestamp = getReferenceTimestamp();
  FlashcardDeckRecord* record = findDeckRecordInternal(deck.deckId);
  if (!record) {
    knownDecks.push_back({});
    record = &knownDecks.back();
    record->deckId = deck.deckId;
  }

  record->path = deck.path;
  record->title = deck.title;
  record->lastOpenedAt = timestamp;
  record->totalCards = static_cast<uint32_t>(metrics.totalCards);
  record->seenCards = static_cast<uint32_t>(metrics.seenCards);
  record->masteredCards = static_cast<uint32_t>(metrics.masteredCards);
  record->dueCards = static_cast<uint32_t>(metrics.dueCards);
  record->totalReviewed = static_cast<uint32_t>(metrics.totalReviewed);
  record->totalCorrect = static_cast<uint32_t>(metrics.totalCorrect);
  record->totalWrong = static_cast<uint32_t>(metrics.totalWrong);
  record->totalSkipped = static_cast<uint32_t>(metrics.totalSkipped);

  recentDeckIds.erase(std::remove(recentDeckIds.begin(), recentDeckIds.end(), deck.deckId), recentDeckIds.end());
  recentDeckIds.insert(recentDeckIds.begin(), deck.deckId);
  if (recentDeckIds.size() > MAX_RECENT_DECKS) {
    recentDeckIds.resize(MAX_RECENT_DECKS);
  }

  saveToFile();
}

void FlashcardsStore::registerSession(const FlashcardDeck& deck, const FlashcardDeckMetrics& metrics) {
  const uint32_t timestamp = getReferenceTimestamp();
  FlashcardDeckRecord* record = findDeckRecordInternal(deck.deckId);
  if (!record) {
    knownDecks.push_back({});
    record = &knownDecks.back();
    record->deckId = deck.deckId;
  }

  record->path = deck.path;
  record->title = deck.title;
  record->lastOpenedAt = timestamp;
  record->lastReviewedAt = timestamp;
  record->sessionCount += 1;
  record->totalCards = static_cast<uint32_t>(metrics.totalCards);
  record->seenCards = static_cast<uint32_t>(metrics.seenCards);
  record->masteredCards = static_cast<uint32_t>(metrics.masteredCards);
  record->dueCards = static_cast<uint32_t>(metrics.dueCards);
  record->totalReviewed = static_cast<uint32_t>(metrics.totalReviewed);
  record->totalCorrect = static_cast<uint32_t>(metrics.totalCorrect);
  record->totalWrong = static_cast<uint32_t>(metrics.totalWrong);
  record->totalSkipped = static_cast<uint32_t>(metrics.totalSkipped);

  recentDeckIds.erase(std::remove(recentDeckIds.begin(), recentDeckIds.end(), deck.deckId), recentDeckIds.end());
  recentDeckIds.insert(recentDeckIds.begin(), deck.deckId);
  if (recentDeckIds.size() > MAX_RECENT_DECKS) {
    recentDeckIds.resize(MAX_RECENT_DECKS);
  }

  saveToFile();
}

const FlashcardDeckRecord* FlashcardsStore::findDeckRecord(const std::string& deckIdOrPath) const {
  if (const auto* record = findDeckRecordInternal(deckIdOrPath)) {
    return record;
  }

  const auto normalizedPath = BookIdentity::normalizePath(deckIdOrPath);
  if (!normalizedPath.empty()) {
    const std::string deckId = BookIdentity::resolveStableBookId(normalizedPath);
    if (const auto* record = findDeckRecordInternal(deckId)) {
      return record;
    }
  }

  return findDeckRecordInternal(deckIdOrPath);
}

void FlashcardsStore::markCardSuccess(FlashcardCardProgress& progress) const {
  const uint32_t today = getReferenceDayOrdinal();
  progress.seenCount += 1;
  progress.successCount += 1;
  progress.lastReviewedDay = today;
  if (progress.intervalDays == 0) {
    progress.intervalDays = 1;
  } else if (progress.intervalDays == 1) {
    progress.intervalDays = 2;
  } else {
    progress.intervalDays = std::min<uint16_t>(365, progress.intervalDays * 2);
  }
  progress.dueDay = today == 0 ? progress.intervalDays : today + progress.intervalDays;
}

void FlashcardsStore::markCardFailure(FlashcardCardProgress& progress) const {
  const uint32_t today = getReferenceDayOrdinal();
  progress.seenCount += 1;
  progress.failCount += 1;
  progress.lastReviewedDay = today;
  progress.intervalDays = 0;
  progress.dueDay = today;
}

void FlashcardsStore::markCardSkipped(FlashcardCardProgress& progress) const {
  const uint32_t today = getReferenceDayOrdinal();
  progress.seenCount += 1;
  progress.skipCount += 1;
  progress.lastReviewedDay = today;
  progress.dueDay = today;
}
