#include <gtest/gtest.h>

#include <initializer_list>
#include <string>

#include "activities/reader/HighlightTextMatcher.h"

using HighlightTextMatcher::TokenFragmentMatch;

TEST(HighlightTextMatcherTest, JoinsLayoutInsertedHyphenAcrossFragments) {
  constexpr char token[] = "correctly";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("cor-", true, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);
  EXPECT_EQ(prefix.tokenBytes, 3U);

  const auto suffix =
      HighlightTextMatcher::matchTokenFragment("rectly", false, token, sizeof(token) - 1, prefix.tokenBytes);
  EXPECT_EQ(suffix.match, TokenFragmentMatch::COMPLETES_TOKEN);
}

TEST(HighlightTextMatcherTest, PreservesAuthoredHyphens) {
  constexpr char token[] = "wellknown";
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("well-", false, token, sizeof(token) - 1, 0).match,
            TokenFragmentMatch::MISMATCH);
}

TEST(HighlightTextMatcherTest, JoinsAdjacentEllipsisFragment) {
  constexpr char token[] = "it\xE2\x80\xA6";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("it", false, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("\xE2\x80\xA6", false, token, sizeof(token) - 1, prefix.tokenBytes)
                .match,
            TokenFragmentMatch::COMPLETES_TOKEN);
}

TEST(HighlightTextMatcherTest, NormalizesNbspBeforeEllipsis) {
  constexpr char token[] = "it \xE2\x80\xA6";
  const auto prefix = HighlightTextMatcher::matchTokenFragment("it", false, token, sizeof(token) - 1, 0);
  ASSERT_EQ(prefix.match, TokenFragmentMatch::CONTINUES_TOKEN);

  const auto ellipsis = HighlightTextMatcher::matchTokenFragment("\xC2\xA0\xE2\x80\xA6", false, token,
                                                                 sizeof(token) - 1, prefix.tokenBytes);
  EXPECT_EQ(ellipsis.match, TokenFragmentMatch::COMPLETES_TOKEN);
  EXPECT_EQ(ellipsis.tokenBytes, 4U);
}

TEST(HighlightTextMatcherTest, TokenizerKeepsSpaceBeforeEllipsisWithItsWord) {
  const char* cursor = "it \xE2\x80\xA6 done";
  const char* token = nullptr;
  size_t tokenLength = 0;
  ASSERT_TRUE(HighlightTextMatcher::nextToken(cursor, token, tokenLength));
  EXPECT_EQ(std::string(token, tokenLength), "it \xE2\x80\xA6");
  ASSERT_TRUE(HighlightTextMatcher::nextToken(cursor, token, tokenLength));
  EXPECT_EQ(std::string(token, tokenLength), "done");
}

TEST(HighlightTextMatcherTest, RejectsInsertedHyphenWithoutRemainder) {
  constexpr char token[] = "cor";
  EXPECT_EQ(HighlightTextMatcher::matchTokenFragment("cor-", true, token, sizeof(token) - 1, 0).match,
            TokenFragmentMatch::MISMATCH);
}

namespace {
using HighlightTextMatcher::Word;

HighlightTextMatcher::Match findPhrase(const char* snippet, const std::initializer_list<Word> words,
                                       const size_t startLimit = static_cast<size_t>(-1)) {
  return HighlightTextMatcher::findUnique(
      snippet, words.size(), [&](const size_t index) { return words.begin()[index]; }, startLimit);
}
}  // namespace

TEST(HighlightTextMatcherTest, FindsCompletePhraseWithoutStoredWordIndices) {
  const auto match = findPhrase("the selected phrase", {{"earlier"}, {"the"}, {"selected"}, {"phrase"}, {"later"}});
  ASSERT_TRUE(match.found);
  EXPECT_FALSE(match.ambiguous);
  EXPECT_EQ(match.start, 1U);
  EXPECT_EQ(match.end, 3U);
}

TEST(HighlightTextMatcherTest, FindsLegacySyllableSpacedKoreanInCurrentWholeWords) {
  const auto match = findPhrase("희 진 은 그 렇 게 첫 번 째 루 이 를 만 났 다 .",
                                {{"앞문장."}, {"희진은"}, {"그렇게"}, {"첫번째"}, {"루이를"}, {"만났다."}});
  ASSERT_TRUE(match.found);
  EXPECT_EQ(match.start, 1U);
  EXPECT_EQ(match.end, 5U);
}

TEST(HighlightTextMatcherTest, FindsCurrentKoreanPhraseAcrossSyllableAndPunctuationFragments) {
  const auto match =
      findPhrase("“희진은 만났다.”", {{"“"}, {"희"}, {"진"}, {"은"}, {"만"}, {"났"}, {"다"}, {"."}, {"”"}});
  ASSERT_TRUE(match.found);
  EXPECT_EQ(match.start, 0U);
  EXPECT_EQ(match.end, 8U);
}

TEST(HighlightTextMatcherTest, PreservesLatinWordBoundariesInBothDirections) {
  EXPECT_FALSE(findPhrase("a part", {{"apart"}}).found);
  EXPECT_FALSE(findPhrase("apart", {{"a"}, {"part"}}).found);
  EXPECT_FALSE(findPhrase("élan vital", {{"élanvital"}}).found);
  EXPECT_FALSE(findPhrase("élanvital", {{"élan"}, {"vital"}}).found);
  EXPECT_TRUE(findPhrase("a part", {{"a"}, {"part"}}).found);
}

TEST(HighlightTextMatcherTest, PreservesLatinBoundariesInsideMixedKoreanPhrase) {
  EXPECT_TRUE(findPhrase("한 글 a part", {{"한글"}, {"a"}, {"part"}}).found);
  EXPECT_FALSE(findPhrase("한 글 a part", {{"한글"}, {"apart"}}).found);
  EXPECT_FALSE(findPhrase("한글 apart", {{"한"}, {"글"}, {"a"}, {"part"}}).found);
}

TEST(HighlightTextMatcherTest, MatchesLayoutHyphenatedPhraseAcrossRenderedWords) {
  const auto match = findPhrase("it correctly wraps", {{"it"}, {"cor-", true}, {"rectly"}, {"wraps"}});
  ASSERT_TRUE(match.found);
  EXPECT_EQ(match.start, 0U);
  EXPECT_EQ(match.end, 3U);
  EXPECT_FALSE(findPhrase("cor", {{"cor-", true}, {"rectly"}}).found);
  EXPECT_FALSE(findPhrase("rectly", {{"cor-", true}, {"rectly"}}).found);
}

TEST(HighlightTextMatcherTest, MatchesLiteralLegacyHyphenOnlyWhenItExistsInCurrentLayout) {
  const auto match = findPhrase("cor- rectly", {{"cor-", true}, {"rectly"}});
  ASSERT_TRUE(match.found);
  EXPECT_EQ(match.start, 0U);
  EXPECT_EQ(match.end, 1U);
  EXPECT_FALSE(findPhrase("cor- rectly", {{"correctly"}}).found);
  EXPECT_FALSE(findPhrase("cor-", {{"cor-", true}, {"rectly"}}).found);
}

TEST(HighlightTextMatcherTest, MatchesMixedLiteralAndCanonicalHyphensWithoutDroppingAuthoredHyphens) {
  const std::initializer_list<Word> words = {{"cor-", true}, {"rectly"}, {"hyphen-", true},
                                             {"ation"},      {"well-"},  {"known"}};
  EXPECT_TRUE(findPhrase("cor- rectly hyphenation well-known", words).found);
  EXPECT_TRUE(findPhrase("correctly hyphen- ation well-known", words).found);
  EXPECT_TRUE(findPhrase("cor- rectly hyphen- ation well-known", words).found);
  EXPECT_FALSE(findPhrase("correctly hyphenation wellknown", words).found);
}

TEST(HighlightTextMatcherTest, DoesNotInventUnmarkedLatinContinuationsOrRemoveAuthoredHyphens) {
  EXPECT_FALSE(findPhrase("correctly", {{"cor"}, {"rectly"}}).found);
  EXPECT_FALSE(findPhrase("wellknown", {{"well-"}, {"known"}}).found);
  EXPECT_TRUE(findPhrase("well-known", {{"well-"}, {"known"}}).found);
  EXPECT_FALSE(findPhrase("correctly", {{"cor", true}, {"rectly"}}).found);
}

TEST(HighlightTextMatcherTest, MatchesPunctuationFragmentsAndNbspWithoutDroppingPunctuation) {
  const auto match = findPhrase("it … is done.", {{"it"}, {"\xC2\xA0\xE2\x80\xA6"}, {"is"}, {"done"}, {"."}});
  ASSERT_TRUE(match.found);
  EXPECT_EQ(match.end, 4U);
  EXPECT_TRUE(findPhrase("it…", {{"it"}, {"\xE2\x80\xA6"}}).found);
  EXPECT_FALSE(findPhrase("it!", {{"it"}, {"?"}}).found);
}

TEST(HighlightTextMatcherTest, CollapsesWhitespaceAndNbspButKeepsWordBoundaries) {
  EXPECT_TRUE(findPhrase("  one\t two\nthree  ", {{"one"}, {"two\xC2\xA0three"}}).found);
  EXPECT_TRUE(findPhrase("one\xC2\xA0two", {{"one"}, {"two"}}).found);
  EXPECT_FALSE(findPhrase("onetwo", {{"one\xC2\xA0two"}}).found);
}

TEST(HighlightTextMatcherTest, RefusesPartialRenderedWordEndpoints) {
  EXPECT_FALSE(findPhrase("correct", {{"correctly"}}).found);
  EXPECT_FALSE(findPhrase("rectly", {{"correctly"}}).found);
  EXPECT_FALSE(findPhrase("희 진", {{"희진은"}}).found);
  EXPECT_FALSE(findPhrase("진 은", {{"희진은"}}).found);
}

TEST(HighlightTextMatcherTest, RefusesRepeatedPhrasesInsteadOfChoosingOldIndex) {
  const auto match = findPhrase("희 진 은 만 났 다 .", {{"희진은"}, {"만났다."}, {"사이"}, {"희진은"}, {"만났다."}});
  EXPECT_FALSE(match.found);
  EXPECT_TRUE(match.ambiguous);
}

TEST(HighlightTextMatcherTest, StartLimitFindsPhraseCrossingPageBoundaryWithoutCountingLaterPageStarts) {
  const std::initializer_list<Word> words = {{"before"}, {"the"}, {"selected"}, {"phrase"},
                                             {"after"},  {"the"}, {"selected"}, {"phrase"}};
  const auto firstPage = findPhrase("the selected phrase", words, 3);
  ASSERT_TRUE(firstPage.found);
  EXPECT_EQ(firstPage.start, 1U);
  EXPECT_EQ(firstPage.end, 3U);
  EXPECT_FALSE(findPhrase("the selected phrase", words).found);
  EXPECT_TRUE(findPhrase("the selected phrase", words).ambiguous);
  EXPECT_FALSE(findPhrase("the selected phrase", words, 1).found);
  EXPECT_FALSE(findPhrase("the selected phrase", words, 0).found);
}

TEST(HighlightTextMatcherTest, RejectsEmptyOrTruncatedUtf8WithoutMatchingPrefix) {
  EXPECT_FALSE(findPhrase(nullptr, {{"one"}}).found);
  EXPECT_FALSE(findPhrase(" \t\n", {{"one"}}).found);
  EXPECT_FALSE(findPhrase("one", {}).found);
  EXPECT_FALSE(findPhrase("one", {{nullptr}}).found);
  EXPECT_FALSE(findPhrase("one\xE2\x80", {{"one"}}).found);
  EXPECT_FALSE(findPhrase("one", {{"one\xE2\x80"}}).found);
  EXPECT_FALSE(findPhrase("one\xC0\xA0two", {{"one"}, {"two"}}).found);
}
