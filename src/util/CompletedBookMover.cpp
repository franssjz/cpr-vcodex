#include "CompletedBookMover.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <string>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FavoritesStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "BookIdentity.h"

namespace {
constexpr char FINISHED_BOOKS_DIRECTORY[] = "/finished_books";

bool isInFinishedBooksDirectory(const std::string& normalizedPath) {
  return normalizedPath == FINISHED_BOOKS_DIRECTORY ||
         normalizedPath.rfind(std::string(FINISHED_BOOKS_DIRECTORY) + "/", 0) == 0;
}

std::string getFileName(const std::string& path) {
  const size_t slashPos = path.find_last_of('/');
  return slashPos == std::string::npos ? path : path.substr(slashPos + 1);
}

std::string withoutExtension(const std::string& filename) {
  const size_t dotPos = filename.find_last_of('.');
  return dotPos == std::string::npos ? filename : filename.substr(0, dotPos);
}

std::string extensionOf(const std::string& filename) {
  const size_t dotPos = filename.find_last_of('.');
  return dotPos == std::string::npos ? "" : filename.substr(dotPos);
}

std::string makeUniqueDestinationPath(const std::string& sourcePath) {
  const std::string filename = getFileName(sourcePath);
  const std::string stem = withoutExtension(filename);
  const std::string extension = extensionOf(filename);

  std::string candidate = std::string(FINISHED_BOOKS_DIRECTORY) + "/" + filename;
  for (int copyIndex = 2; Storage.exists(candidate.c_str()) && copyIndex < 100; ++copyIndex) {
    candidate =
        std::string(FINISHED_BOOKS_DIRECTORY) + "/" + stem + " (" + std::to_string(copyIndex) + ")" + extension;
  }
  return candidate;
}
}  // namespace

namespace CompletedBookMover {

MoveResult moveCompletedBookIfEnabled(const std::string& sourcePath, const std::string& title,
                                      const std::string& author, const std::string& coverBmpPath,
                                      const std::string& bookId) {
  MoveResult result;
  result.sourcePath = BookIdentity::normalizePath(sourcePath);
  result.destinationPath = result.sourcePath;

  if (!SETTINGS.moveCompletedBooks || result.sourcePath.empty() || isInFinishedBooksDirectory(result.sourcePath)) {
    return result;
  }

  const auto* statsBook = READING_STATS.findBook(!bookId.empty() ? bookId : result.sourcePath);
  if (!statsBook || !statsBook->completed) {
    return result;
  }

  result.attempted = true;
  if (!Storage.exists(result.sourcePath.c_str())) {
    LOG_ERR("CBM", "Completed book source missing: %s", result.sourcePath.c_str());
    return result;
  }

  Storage.mkdir(FINISHED_BOOKS_DIRECTORY);
  const std::string destinationPath = makeUniqueDestinationPath(result.sourcePath);
  if (destinationPath.empty() || Storage.exists(destinationPath.c_str())) {
    LOG_ERR("CBM", "Could not create unique completed-book path for: %s", result.sourcePath.c_str());
    return result;
  }

  if (!Storage.rename(result.sourcePath.c_str(), destinationPath.c_str())) {
    LOG_ERR("CBM", "Failed to move completed book: %s -> %s", result.sourcePath.c_str(), destinationPath.c_str());
    return result;
  }

  const std::string resolvedTitle = !title.empty() ? title : statsBook->title;
  const std::string resolvedAuthor = !author.empty() ? author : statsBook->author;
  const std::string resolvedCover = !coverBmpPath.empty() ? coverBmpPath : statsBook->coverBmpPath;
  const std::string resolvedBookId = !bookId.empty() ? bookId : statsBook->bookId;

  READING_STATS.updateBookPath(result.sourcePath, destinationPath, resolvedTitle, resolvedAuthor, resolvedCover,
                               resolvedBookId);
  RECENT_BOOKS.updateBookPath(result.sourcePath, destinationPath, resolvedTitle, resolvedAuthor, resolvedCover,
                              resolvedBookId);
  FAVORITES.updateBookPath(result.sourcePath, destinationPath, resolvedTitle, resolvedAuthor, resolvedCover,
                           resolvedBookId);

  if (APP_STATE.openEpubPath == result.sourcePath) {
    APP_STATE.openEpubPath = destinationPath;
    APP_STATE.saveToFile();
  }

  result.destinationPath = destinationPath;
  result.moved = true;
  LOG_INF("CBM", "Moved completed book: %s -> %s", result.sourcePath.c_str(), result.destinationPath.c_str());
  return result;
}

}  // namespace CompletedBookMover
