#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "KOReaderDocumentId.h"
#include "ReaderPluginSession.h"
#include "TrustedTime.h"
#include "util/PluginEvents.h"

namespace {
struct CapturedEvent {
  pluginevents::Event type;
  std::map<std::string, std::string> vars;
};
uint32_t nowMs;
int64_t epoch;
bool subscribed;
int hashCalls;
std::string document;
std::vector<CapturedEvent> events;

class ReaderPluginSessionTest : public ::testing::Test {
 protected:
  ReaderPluginSession session;
  void SetUp() override {
    nowMs = 1000;
    epoch = 1800000000;
    subscribed = true;
    hashCalls = 0;
    document = "0123456789abcdef0123456789abcdef";
    events.clear();
  }
  void readForward() {
    session.rendered(1234);
    nowMs += 15000;
    epoch += 15;
    session.noteTurn(true, true);
    session.rendered(2345);
  }
};
}  // namespace

uint32_t millis() { return nowMs; }
int64_t trustedtime::trustedNow() { return epoch; }
std::string KOReaderDocumentId::calculate(const std::string&) {
  ++hashCalls;
  return document;
}
bool pluginevents::anySubscriber(Event) { return subscribed; }
void pluginevents::emit(Event event, const Var* vars, size_t count) {
  CapturedEvent captured{event, {}};
  for (size_t i = 0; i < count; ++i) captured.vars[vars[i].key] = vars[i].value;
  events.push_back(std::move(captured));
}

TEST_F(ReaderPluginSessionTest, UnrenderedBookEmitsNothing) {
  session.openOnceRendered("/book.epub");
  session.exit("/book.epub");
  EXPECT_TRUE(events.empty());
  EXPECT_EQ(hashCalls, 0);
}

TEST_F(ReaderPluginSessionTest, EmitsOrderedOpenSessionExitWithContentHash) {
  readForward();
  session.exit("/book.epub");
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].type, pluginevents::Event::ReaderOpen);
  EXPECT_EQ(events[1].type, pluginevents::Event::ReaderSession);
  EXPECT_EQ(events[1].vars.at("book"), "/book.epub");
  EXPECT_EQ(events[1].vars.at("document"), document);
  EXPECT_EQ(events[1].vars.at("duration_seconds"), "15");
  EXPECT_EQ(events[1].vars.at("start_progress_bp"), "1234");
  EXPECT_EQ(events[1].vars.at("end_progress_bp"), "2345");
  EXPECT_EQ(events[1].vars.at("progress_scale"), "10000");
  EXPECT_EQ(events[2].type, pluginevents::Event::ReaderExit);
  EXPECT_EQ(events[2].vars.at("percent"), "23");
  session.exit("/book.epub");
  EXPECT_EQ(events.size(), 3u);
}

TEST_F(ReaderPluginSessionTest, FlushDoesNotRepeatOpenOrReadingTime) {
  readForward();
  session.openOnceRendered("/book.epub");
  session.flush("/book.epub");
  session.flush("/book.epub");
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(hashCalls, 1);
  session.exit("/book.epub");
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events.back().type, pluginevents::Event::ReaderExit);
}

TEST_F(ReaderPluginSessionTest, InvalidHashSuppressesOnlySession) {
  document = "legacy:/book.epub";
  readForward();
  session.exit("/book.epub");
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, pluginevents::Event::ReaderOpen);
  EXPECT_EQ(events[1].type, pluginevents::Event::ReaderExit);
}

TEST_F(ReaderPluginSessionTest, NoSubscriberAvoidsHashing) {
  subscribed = false;
  readForward();
  session.exit("/book.epub");
  EXPECT_EQ(hashCalls, 0);
}

TEST_F(ReaderPluginSessionTest, NoTrustedClockSuppressesSession) {
  epoch = 0;
  readForward();
  session.exit("/book.epub");
  EXPECT_EQ(events.size(), 2u);
  EXPECT_EQ(hashCalls, 0);
}

TEST_F(ReaderPluginSessionTest, ExitProgressIsClamped) {
  session.rendered(12000);
  session.exit("/book.epub");
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events.back().vars.at("percent"), "100");
}
