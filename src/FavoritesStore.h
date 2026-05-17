#pragma once

#include <string>
#include <vector>

struct FavoriteBook {
  std::string bookId;
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;

  bool operator==(const FavoriteBook& other) const {
    return !bookId.empty() && !other.bookId.empty() ? bookId == other.bookId : path == other.path;
  }
};

class FavoritesStore;
namespace JsonSettingsIO {
bool saveFavorites(const FavoritesStore& store, const char* path);
bool loadFavorites(FavoritesStore& store, const char* json);
}  // namespace JsonSettingsIO

class FavoritesStore {
  static FavoritesStore instance;

  std::vector<FavoriteBook> favoriteBooks;

  friend bool JsonSettingsIO::saveFavorites(const FavoritesStore&, const char*);
  friend bool JsonSettingsIO::loadFavorites(FavoritesStore&, const char*);

 public:
  ~FavoritesStore() = default;

  static FavoritesStore& getInstance() { return instance; }

  bool addBook(const std::string& path, const std::string& title = "", const std::string& author = "",
               const std::string& coverBmpPath = "", const std::string& bookId = "");
  void updateBook(const std::string& path, const std::string& title, const std::string& author,
                  const std::string& coverBmpPath, const std::string& bookId = "");
  bool updateBookPath(const std::string& oldKey, const std::string& newPath, const std::string& title = "",
                      const std::string& author = "", const std::string& coverBmpPath = "",
                      const std::string& bookId = "");
  bool removeBook(const std::string& key);
  bool toggleBook(const std::string& path);
  bool isFavorite(const std::string& key) const;
  bool moveBook(int fromIndex, int toIndex);

  const std::vector<FavoriteBook>& getBooks() const { return favoriteBooks; }
  int getCount() const { return static_cast<int>(favoriteBooks.size()); }

  bool saveToFile() const;
  bool loadFromFile();
  FavoriteBook getDataFromBook(std::string path) const;

 private:
  int findBookIndex(const std::string& path, const std::string& bookId) const;
  void normalizeBook(FavoriteBook& book);
  void normalizeBooks();
};

#define FAVORITES FavoritesStore::getInstance()
