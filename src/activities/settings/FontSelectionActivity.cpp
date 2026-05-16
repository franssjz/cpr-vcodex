#include "FontSelectionActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <utility>

#include "CrossPointSettings.h"
#include "FontInstaller.h"
#include "MappedInputManager.h"
#include "SdCardFont.h"
#include "SdCardFontGlobals.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {
#define FONTS_MANIFEST_VERSION 1
#ifndef FONT_MANIFEST_URL
#define FONT_MANIFEST_URL_STRINGIFY_INNER(x) #x
#define FONT_MANIFEST_URL_STRINGIFY(x) FONT_MANIFEST_URL_STRINGIFY_INNER(x)
#define FONT_MANIFEST_URL                                                                                           \
  "https://github.com/crosspoint-reader/crosspoint-fonts/releases/download/sd-fonts-m" FONT_MANIFEST_URL_STRINGIFY( \
      FONTS_MANIFEST_VERSION) "-b" FONT_MANIFEST_URL_STRINGIFY(CPFONT_VERSION) "/fonts.json"
#endif

std::string shortStatus(const std::string& text) {
  return text.size() > 28 ? text.substr(0, 28) : text;
}
}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry, Mode mode)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry), mode_(mode) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));

  fonts_.push_back({I18N.get(StrId::STR_BOOKERLY), true, CrossPointSettings::BOOKERLY});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, CrossPointSettings::NOTOSANS});
  fonts_.push_back({I18N.get(StrId::STR_LEXEND), true, CrossPointSettings::LEXEND});

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  selectedIndex_ = 0;
  if (mode_ == Mode::Select) {
    if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
      const auto& families = registry_->getFamilies();
      for (int i = 0; i < static_cast<int>(families.size()); i++) {
        if (families[i].name == SETTINGS.sdFontFamilyName) {
          selectedIndex_ = CrossPointSettings::BUILTIN_FONT_COUNT + i;
          break;
        }
      }
    } else {
      selectedIndex_ = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
    }
  } else {
    downloadState_ = DownloadState::NotLoaded;
    statusMessage_.clear();
    if (WiFi.status() == WL_CONNECTED) {
      downloadState_ = DownloadState::Loading;
      statusMessage_ = tr(STR_LOADING_FONT_LIST);
      requestUpdateAndWait();
      if (fetchCatalog()) {
        downloadState_ = DownloadState::Ready;
        selectedIndex_ = 0;
      } else {
        downloadState_ = DownloadState::Error;
      }
    } else {
      startCatalogLoad();
    }
  }

  requestUpdate();
}

void FontSelectionActivity::onExit() {
  Activity::onExit();
}

int FontSelectionActivity::currentListSize() const {
  if (mode_ == Mode::Select) return static_cast<int>(fonts_.size());
  if (downloadState_ != DownloadState::Ready) return 0;
  return static_cast<int>(catalog_.size()) + (showDownloadAllRow() ? 1 : 0);
}

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (downloadState_ == DownloadState::Loading || downloadState_ == DownloadState::Downloading) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (mode_ == Mode::Select) {
      handleSelection();
      return;
    }

    if (downloadState_ == DownloadState::NotLoaded || downloadState_ == DownloadState::Error) {
      startCatalogLoad();
      return;
    }
    if (downloadState_ == DownloadState::Complete) {
      downloadState_ = DownloadState::Ready;
      requestUpdate();
      return;
    }
    handleCatalogAction();
    return;
  }

  const int listSize = currentListSize();
  if (listSize <= 0) return;

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });
}

void FontSelectionActivity::handleSelection() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(fonts_.size())) return;

  const auto& font = fonts_[selectedIndex_];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  finish();
}

void FontSelectionActivity::handleCatalogAction() {
  if (downloadState_ != DownloadState::Ready) return;
  if (isDownloadAllRow(selectedIndex_)) {
    confirmDownloadAll();
    return;
  }

  const int familyIndex = familyIndexFromListIndex(selectedIndex_);
  if (familyIndex < 0 || familyIndex >= static_cast<int>(catalog_.size())) return;

  const auto& family = catalog_[familyIndex];
  if (family.installed && !family.hasUpdate) {
    confirmDeleteSelectedCatalogFont();
  } else {
    downloadSelectedCatalogFont();
  }
}

void FontSelectionActivity::confirmDeleteSelectedCatalogFont() {
  const int familyIndex = familyIndexFromListIndex(selectedIndex_);
  if (familyIndex < 0 || familyIndex >= static_cast<int>(catalog_.size())) return;

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE), catalog_[familyIndex].name),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) handleDeleteCatalogFont();
        requestUpdate();
      });
}

void FontSelectionActivity::handleDeleteCatalogFont() {
  const int familyIndex = familyIndexFromListIndex(selectedIndex_);
  if (familyIndex < 0 || familyIndex >= static_cast<int>(catalog_.size())) return;

  auto& family = catalog_[familyIndex];
  FontInstaller installer(sdFontSystem.registry());
  const auto result = installer.deleteFamily(family.name.c_str());
  if (result != FontInstaller::Error::OK) {
    LOG_ERR("FONTUI", "Failed to uninstall font family: %s", family.name.c_str());
    statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
    downloadState_ = DownloadState::Error;
    return;
  }

  sdFontSystem.markRegistryDirty();
  sdFontSystem.refreshIfDirty();
  family.installed = false;
  family.hasUpdate = false;
  downloadState_ = DownloadState::Ready;
  requestUpdate();
}

void FontSelectionActivity::confirmDownloadAll() {
  const size_t size = pendingDownloadSize();
  std::string body = I18N.get(StrId::STR_DOWNLOAD_ALL_CONFIRM);
  if (size > 0) {
    body += ": ";
    body += formatSize(size);
  }
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DOWNLOAD_ALL), body),
                         [this](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             for (auto& family : catalog_) {
                               if (!family.installed || family.hasUpdate) {
                                 if (!downloadCatalogFamily(family)) return;
                               }
                             }
                             statusMessage_ = tr(STR_FONT_INSTALLED);
                             downloadState_ = DownloadState::Complete;
                           }
                           requestUpdate();
                         });
}

void FontSelectionActivity::startCatalogLoad() {
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(result); });
}

void FontSelectionActivity::onWifiSelectionComplete(const ActivityResult& result) {
  if (result.isCancelled) {
    statusMessage_ = tr(STR_CONNECTION_FAILED);
    downloadState_ = DownloadState::Error;
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    downloadState_ = DownloadState::Loading;
    statusMessage_ = tr(STR_LOADING_FONT_LIST);
  }
  requestUpdateAndWait();

  if (fetchCatalog()) {
    downloadState_ = DownloadState::Ready;
    selectedIndex_ = 0;
  } else {
    downloadState_ = DownloadState::Error;
  }
  requestUpdate();
}

bool FontSelectionActivity::fetchCatalog() {
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";
  const auto result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
  if (result != HttpDownloader::OK) {
    statusMessage_ = tr(STR_DOWNLOAD_FAILED);
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  FsFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    statusMessage_ = tr(STR_DOWNLOAD_FAILED);
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, manifestFile);
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);
  if (err) {
    statusMessage_ = tr(STR_INVALID_FONT_CATALOG);
    return false;
  }

  if ((doc["version"] | 0) != FONTS_MANIFEST_VERSION) {
    statusMessage_ = tr(STR_INVALID_FONT_CATALOG);
    return false;
  }

  catalogBaseUrl_ = doc["baseUrl"] | "";
  catalog_.clear();
  sdFontSystem.refreshIfDirty();
  FontInstaller installer(sdFontSystem.registry());

  for (JsonObject fObj : doc["families"].as<JsonArray>()) {
    CatalogFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";
    if (!FontInstaller::isValidFamilyName(family.name.c_str())) continue;

    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      CatalogFile file;
      file.name = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;
      if (!fileObj["crc32"].isNull()) {
        if (fileObj["crc32"].is<const char*>()) {
          file.hasCrc32 = parseCrc32(fileObj["crc32"] | "", file.crc32);
        } else {
          file.crc32 = fileObj["crc32"] | static_cast<uint32_t>(0);
          file.hasCrc32 = true;
        }
      }
      if (!FontInstaller::isValidCpfontFilename(file.name.c_str())) continue;
      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    if (!family.files.empty()) {
      family.installed = installer.isFamilyInstalled(family.name.c_str());
      if (family.installed) {
        for (const auto& file : family.files) {
          char path[160];
          FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), path, sizeof(path));
          size_t installedSize = 0;
          if (!getFileSize(path, installedSize) || (file.size > 0 && installedSize != file.size)) {
            family.hasUpdate = true;
            break;
          }
          if (file.hasCrc32) {
            uint32_t installedCrc = 0;
            if (!computeFileCrc32(path, installedCrc) || installedCrc != file.crc32) {
              family.hasUpdate = true;
              break;
            }
          }
        }
      }
      catalog_.push_back(std::move(family));
    }
  }

  if (catalog_.empty()) {
    statusMessage_ = tr(STR_NO_FONTS_AVAILABLE);
    return false;
  }
  return true;
}

void FontSelectionActivity::downloadSelectedCatalogFont() {
  const int familyIndex = familyIndexFromListIndex(selectedIndex_);
  if (familyIndex < 0 || familyIndex >= static_cast<int>(catalog_.size())) return;

  CatalogFamily& family = catalog_[familyIndex];
  if (downloadCatalogFamily(family)) {
    statusMessage_ = tr(STR_FONT_INSTALLED);
    downloadState_ = DownloadState::Complete;
    requestUpdate();
  }
}

bool FontSelectionActivity::downloadCatalogFamily(CatalogFamily& family) {
  const bool wasInstalled = family.installed;
  FontInstaller installer(sdFontSystem.registry());
  if (!installer.ensureFamilyDir(family.name.c_str())) {
    statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
    downloadState_ = DownloadState::Error;
    requestUpdate();
    return false;
  }

  downloadState_ = DownloadState::Downloading;
  downloadFileIndex_ = 0;
  downloadFileCount_ = family.files.size();
  downloadProgress_ = 0;
  downloadTotal_ = 0;
  statusMessage_ = family.name;
  requestUpdateAndWait();

  for (size_t i = 0; i < family.files.size(); ++i) {
    const auto& file = family.files[i];
    downloadFileIndex_ = i;
    downloadProgress_ = 0;
    downloadTotal_ = file.size;
    requestUpdateAndWait();

    char destPath[160];
    FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), destPath, sizeof(destPath));
    char tmpPath[168];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", destPath);
    Storage.remove(tmpPath);
    const std::string url = catalogBaseUrl_ + file.name;
    const auto result = HttpDownloader::downloadToFile(url, tmpPath, [this](size_t done, size_t total) {
      downloadProgress_ = done;
      downloadTotal_ = total;
      requestUpdate(true);
    });

    if (result != HttpDownloader::OK || !verifyDownloadedFile(tmpPath, file)) {
      Storage.remove(tmpPath);
      if (!wasInstalled) installer.deleteFamily(family.name.c_str());
      family.installed = wasInstalled;
      statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
      downloadState_ = DownloadState::Error;
      requestUpdate();
      return false;
    }
    char bakPath[168];
    snprintf(bakPath, sizeof(bakPath), "%s.bak", destPath);
    const bool hadExisting = Storage.exists(destPath);
    if (hadExisting) {
      Storage.remove(bakPath);
      if (!Storage.rename(destPath, bakPath)) {
        Storage.remove(tmpPath);
        statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
        downloadState_ = DownloadState::Error;
        requestUpdate();
        return false;
      }
    }
    if (!Storage.rename(tmpPath, destPath)) {
      Storage.remove(tmpPath);
      if (hadExisting) Storage.rename(bakPath, destPath);
      if (!wasInstalled) installer.deleteFamily(family.name.c_str());
      family.installed = wasInstalled;
      statusMessage_ = tr(STR_FONT_INSTALL_FAILED);
      downloadState_ = DownloadState::Error;
      requestUpdate();
      return false;
    }
    if (hadExisting) Storage.remove(bakPath);
  }

  installer.refreshRegistry();
  sdFontSystem.markRegistryDirty();
  sdFontSystem.refreshIfDirty();
  family.installed = true;
  family.hasUpdate = false;
  downloadState_ = DownloadState::Ready;
  requestUpdate();
  return true;
}

bool FontSelectionActivity::verifyDownloadedFile(const char* path, const CatalogFile& file) const {
  FontInstaller installer(sdFontSystem.registry());
  if (!installer.validateCpfontFile(path)) return false;
  if (file.size > 0) {
    size_t actualSize = 0;
    if (!getFileSize(path, actualSize) || actualSize != file.size) return false;
  }
  if (file.hasCrc32) {
    uint32_t actualCrc = 0;
    if (!computeFileCrc32(path, actualCrc) || actualCrc != file.crc32) return false;
  }
  return true;
}

bool FontSelectionActivity::getFileSize(const char* path, size_t& sizeOut) const {
  FsFile file;
  if (!Storage.openFileForRead("FONT", path, file)) return false;
  sizeOut = file.fileSize();
  file.close();
  return true;
}

bool FontSelectionActivity::computeFileCrc32(const char* path, uint32_t& crcOut) const {
  FsFile file;
  if (!Storage.openFileForRead("FONT", path, file)) return false;

  uint8_t buffer[128];
  uint32_t crc = 0xFFFFFFFFUL;
  while (file.available() > 0) {
    const int readLen = file.read(buffer, sizeof(buffer));
    if (readLen <= 0) {
      file.close();
      return false;
    }
    crc = crc32Update(crc, buffer, static_cast<size_t>(readLen));
  }
  file.close();
  crcOut = crc ^ 0xFFFFFFFFUL;
  return true;
}

bool FontSelectionActivity::showDownloadAllRow() const {
  for (const auto& family : catalog_) {
    if (!family.installed || family.hasUpdate) return true;
  }
  return false;
}

bool FontSelectionActivity::isDownloadAllRow(const int index) const { return showDownloadAllRow() && index == 0; }

int FontSelectionActivity::familyIndexFromListIndex(const int index) const {
  const int familyIndex = index - (showDownloadAllRow() ? 1 : 0);
  return familyIndex >= 0 && familyIndex < static_cast<int>(catalog_.size()) ? familyIndex : -1;
}

size_t FontSelectionActivity::pendingDownloadSize() const {
  size_t total = 0;
  for (const auto& family : catalog_) {
    if (!family.installed || family.hasUpdate) total += family.totalSize;
  }
  return total;
}

bool FontSelectionActivity::selectedCatalogFontInstalled() const {
  const int familyIndex = familyIndexFromListIndex(selectedIndex_);
  return familyIndex >= 0 && familyIndex < static_cast<int>(catalog_.size()) && catalog_[familyIndex].installed &&
         !catalog_[familyIndex].hasUpdate;
}

std::string FontSelectionActivity::currentCatalogValue(const int index) const {
  if (isDownloadAllRow(index)) return formatSize(pendingDownloadSize());
  const int familyIndex = familyIndexFromListIndex(index);
  if (familyIndex < 0 || familyIndex >= static_cast<int>(catalog_.size())) return "";
  const auto& family = catalog_[familyIndex];
  if (family.hasUpdate) return tr(STR_UPDATE_AVAILABLE);
  return family.installed ? tr(STR_INSTALLED) : formatSize(family.totalSize);
}

bool FontSelectionActivity::parseCrc32(const char* value, uint32_t& crcOut) {
  if (value == nullptr || value[0] == '\0') return false;
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 16);
  if (end == value || *end != '\0') return false;
  crcOut = static_cast<uint32_t>(parsed);
  return true;
}

uint32_t FontSelectionActivity::crc32Update(uint32_t crc, const uint8_t* data, const size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & static_cast<uint32_t>(-static_cast<int32_t>(crc & 1)));
    }
  }
  return crc;
}

std::string FontSelectionActivity::formatSize(const size_t bytes) {
  char buf[24];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  const char* title = mode_ == Mode::Manage ? tr(STR_FONT_BROWSER) : tr(STR_FONT_FAMILY);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (mode_ == Mode::Manage) {
    if (downloadState_ != DownloadState::Ready) {
      const char* message = statusMessage_.empty() ? tr(STR_FONT_BROWSER) : statusMessage_.c_str();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2, message,
                                true, EpdFontFamily::BOLD);
      if (downloadState_ == DownloadState::Downloading) {
        const std::string progress =
            std::to_string(downloadFileIndex_ + 1) + "/" + std::to_string(downloadFileCount_) + " " +
            formatSize(downloadProgress_) + "/" + formatSize(downloadTotal_);
        renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 18, progress.c_str());
      }
      const auto actionLabel = downloadState_ == DownloadState::Complete ? tr(STR_OK_BUTTON) : tr(STR_RETRY);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), actionLabel, "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer();
      return;
    }

    const int itemCount = currentListSize();
    if (itemCount == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2,
                                tr(STR_NO_FONTS_AVAILABLE), true, EpdFontFamily::BOLD);
    } else {
      GUI.drawList(
          renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex_,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index)) return tr(STR_DOWNLOAD_ALL);
            const int familyIndex = familyIndexFromListIndex(index);
            return familyIndex >= 0 ? catalog_[familyIndex].name : "";
          },
          [this](int index) -> std::string {
            if (isDownloadAllRow(index)) return tr(STR_DOWNLOAD_ALL_CONFIRM);
            const int familyIndex = familyIndexFromListIndex(index);
            return familyIndex >= 0 ? shortStatus(catalog_[familyIndex].description) : "";
          },
          nullptr, [this](int index) { return currentCatalogValue(index); }, true);
    }

    const char* actionLabel = tr(STR_DOWNLOAD);
    if (isDownloadAllRow(selectedIndex_)) {
      actionLabel = tr(STR_DOWNLOAD);
    } else if (selectedCatalogFontInstalled()) {
      actionLabel = tr(STR_DELETE);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), actionLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  int currentFontIndex = 0;
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == SETTINGS.sdFontFamilyName) {
        currentFontIndex = CrossPointSettings::BUILTIN_FONT_COUNT + i;
        break;
      }
    }
  } else {
    currentFontIndex = SETTINGS.fontFamily < CrossPointSettings::BUILTIN_FONT_COUNT ? SETTINGS.fontFamily : 0;
  }

  if (fonts_.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - renderer.getLineHeight(UI_10_FONT_ID) / 2,
                              tr(STR_NO_CUSTOM_FONTS), true, EpdFontFamily::BOLD);
  } else {
    const int itemCount = static_cast<int>(fonts_.size());
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, itemCount, selectedIndex_,
        [this](int index) { return fonts_[index].name; },
        nullptr, nullptr,
        [this, currentFontIndex](int index) -> std::string { return index == currentFontIndex ? tr(STR_SELECTED) : ""; },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
