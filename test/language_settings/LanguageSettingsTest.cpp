#include <HalStorage.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>

class LanguageSettings : public testing::Test {
 protected:
  std::filesystem::path root;
  void SetUp() override {
    root = std::filesystem::temp_directory_path() /
           (std::string("cpr-language-") + testing::UnitTest::GetInstance()->current_test_info()->name());
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / ".crosspoint");
    Storage.setTestRootPath(root);
    I18N.setLanguage(Language::EN);
  }
  void TearDown() override {
    Storage.clearTestRootPath();
    std::filesystem::remove_all(root);
  }
  void legacy(std::initializer_list<uint8_t> bytes) {
    std::ofstream file(root / ".crosspoint/language.bin", std::ios::binary);
    for (const auto byte : bytes) file.put(static_cast<char>(byte));
  }
};

TEST_F(LanguageSettings, MigratesEveryPublishedLegacyOrdinal) {
  const char* codes[] = {"EN", "ES", "FR", "DE", "CS", "PT", "RU", "SV", "RO", "CA", "UK", "BE",
                         "IT", "PL", "FI", "DA", "NL", "TR", "KK", "HU", "LT", "SI", "VI"};
  for (uint8_t index = 0; index < std::size(codes); ++index) {
    legacy({1, index});
    ASSERT_TRUE(I18N.loadSettings());
    EXPECT_STREQ(LANGUAGE_CODES[static_cast<uint8_t>(I18N.getLanguage())], codes[index]);
  }
}

TEST_F(LanguageSettings, RejectsTruncatedUnknownAndInvalidLegacyFiles) {
  for (const auto bytes : {std::initializer_list<uint8_t>{}, {1}, {2, 1}, {1, 255}, {1, 23}}) {
    legacy(bytes);
    I18N.setLanguage(Language::EN);
    EXPECT_FALSE(I18N.loadSettings());
    EXPECT_EQ(I18N.getLanguage(), Language::EN);
  }
}

TEST_F(LanguageSettings, MissingFileKeepsDefault) {
  EXPECT_FALSE(I18N.loadSettings());
  EXPECT_EQ(I18N.getLanguage(), Language::EN);
}

TEST_F(LanguageSettings, SelectionDoesNotOverwriteLegacyMigrationEvidence) {
  legacy({1, 1});
  I18N.setLanguage(Language::RU);
  EXPECT_EQ(I18N.getLanguage(), Language::RU);
  ASSERT_TRUE(I18N.loadSettings());
  EXPECT_EQ(I18N.getLanguage(), Language::ES);
}

TEST_F(LanguageSettings, CodesAndMenuCoverAllLanguagesExactlyOnce) {
  std::set<uint8_t> seen;
  for (const uint8_t index : SORTED_LANGUAGE_INDICES) {
    ASSERT_LT(index, getLanguageCount());
    EXPECT_TRUE(seen.insert(index).second);
    const auto language = static_cast<Language>(index);
    EXPECT_EQ(I18n::languageFromCode(LANGUAGE_CODES[index]), language);
    EXPECT_STRNE(I18N.getLanguageName(language), "???");
  }
  EXPECT_EQ(seen.size(), getLanguageCount());
  EXPECT_EQ(I18n::languageFromCode(nullptr), Language::EN);
  EXPECT_EQ(I18n::languageFromCode("unknown"), Language::EN);
  I18N.setLanguage(Language::ES);
  EXPECT_STREQ(tr(STR_SETTINGS_TITLE), "Ajustes");
}
