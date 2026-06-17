#include "BookCacheUtils.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include <string_view>

bool isBookCacheDirectoryName(const char* name) {
  if (name == nullptr) {
    return false;
  }

  const std::string_view item{name};
  return item.rfind("epub_", 0) == 0 || item.rfind("txt_", 0) == 0 || item.rfind("xtc_", 0) == 0;
}

void clearBookCache(const std::string& path) {
  if (FsHelpers::hasEpubExtension(path)) {
    Epub(path, "/.crosspoint").clearCache();
    LOG_DBG("BOOK_CACHE", "Cleared epub cache for: %s", path.c_str());
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc(path, "/.crosspoint").clearCache();
    LOG_DBG("BOOK_CACHE", "Cleared xtc cache for: %s", path.c_str());
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    Txt(path, "/.crosspoint").clearCache();
    LOG_DBG("BOOK_CACHE", "Cleared txt cache for: %s", path.c_str());
  }
}
