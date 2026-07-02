#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

inline bool writeAtomicPath(const char* moduleName, const std::string& finalPath, const uint8_t* data, size_t len) {
  const std::string tmpPath = finalPath + ".tmp";

  {
    HalFile file;
    if (!Storage.openFileForWrite(moduleName, tmpPath, file)) {
      LOG_ERR(moduleName, "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }

    const size_t written = file.write(data, len);
    if (written != len) {
      LOG_ERR(moduleName, "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      file.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }

    file.flush();
    file.close();
  }

  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR(moduleName, "Failed to rename temp progress into place: %s", finalPath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }

  return true;
}

inline bool writeAtomic(const char* moduleName, const std::string& cachePath, const uint8_t* data, size_t len) {
  return writeAtomicPath(moduleName, cachePath + "/progress.bin", data, len);
}

}  // namespace ProgressFile
