#pragma once

#include <SdCardFontRegistry.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FontSelectionActivity final : public Activity {
 public:
  enum class Mode { Select, Manage };

  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry, Mode mode = Mode::Select);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class DownloadState { NotLoaded, Loading, Ready, Downloading, Complete, Error };

  struct CatalogFile;
  struct CatalogFamily;

  void handleSelection();
  void handleCatalogAction();
  void confirmDeleteSelectedCatalogFont();
  void handleDeleteCatalogFont();
  void confirmDownloadAll();
  void startCatalogLoad();
  void onWifiSelectionComplete(const ActivityResult& result);
  bool fetchCatalog();
  void downloadSelectedCatalogFont();
  bool downloadCatalogFamily(CatalogFamily& family);
  bool verifyDownloadedFile(const char* path, const CatalogFile& file) const;
  bool getFileSize(const char* path, size_t& sizeOut) const;
  bool computeFileCrc32(const char* path, uint32_t& crcOut) const;
  bool showDownloadAllRow() const;
  bool isDownloadAllRow(int index) const;
  int familyIndexFromListIndex(int index) const;
  int currentListSize() const;
  size_t pendingDownloadSize() const;
  bool selectedCatalogFontInstalled() const;
  std::string currentCatalogValue(int index) const;
  static bool parseCrc32(const char* value, uint32_t& crcOut);
  static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);
  static std::string formatSize(size_t bytes);

  struct FontEntry {
    std::string name;
    bool isBuiltin;
    uint8_t settingIndex;  // index used by valueSetter
  };

  struct CatalogFile {
    std::string name;
    size_t size = 0;
    uint32_t crc32 = 0;
    bool hasCrc32 = false;
  };

  struct CatalogFamily {
    std::string name;
    std::string description;
    std::vector<CatalogFile> files;
    size_t totalSize = 0;
    bool installed = false;
    bool hasUpdate = false;
  };

  const SdCardFontRegistry* registry_;
  Mode mode_;
  DownloadState downloadState_ = DownloadState::NotLoaded;
  ButtonNavigator buttonNavigator_;
  std::vector<FontEntry> fonts_;
  std::vector<CatalogFamily> catalog_;
  std::string catalogBaseUrl_;
  std::string statusMessage_;
  size_t downloadFileIndex_ = 0;
  size_t downloadFileCount_ = 0;
  size_t downloadProgress_ = 0;
  size_t downloadTotal_ = 0;
  int selectedIndex_ = 0;
};
