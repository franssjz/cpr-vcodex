#include <gtest/gtest.h>

#include "KOReaderSyncAdapter.h"

namespace {
enum class Operation { Register, Authenticate, Get, Put };
constexpr Operation operations[] = {Operation::Register, Operation::Authenticate, Operation::Get, Operation::Put};

KOReaderSyncClient::Error invoke(Operation operation) {
  KOReaderProgress progress{};
  progress.document = "book-id";
  progress.progress = "/body/p[1]";
  switch (operation) {
    case Operation::Register:
      return KOReaderSyncClient::registerUser();
    case Operation::Authenticate:
      return KOReaderSyncClient::authenticate();
    case Operation::Get:
      return KOReaderSyncClient::getProgress(progress.document, progress);
    case Operation::Put:
      return KOReaderSyncClient::updateProgress(progress);
  }
  return KOReaderSyncClient::SERVER_ERROR;
}

class KOReaderSyncDiagnostics : public ::testing::Test {
 protected:
  void SetUp() override {
    KOReaderSyncClient::endPersistentSession();
    KOREADER_STORE = {};
    freeHeap = 90000;
    contiguousHeap = 60000;
    reply = {};
    failInit = false;
    performedRequests = initializedClients = cleanedClients = tlsQueries = 0;
    logs.clear();
    requestUrls.clear();
    transportEvents.clear();
    initializedConfigs.clear();
    g_lastResolvedProfile = EndpointProfile::UNKNOWN;
    beginRequest("test", KOREADER_STORE.getBaseUrl());
  }
  void TearDown() override { KOReaderSyncClient::endPersistentSession(); }
};

TEST_F(KOReaderSyncDiagnostics, EveryOperationMarksExplicit22516BytePreflightRejectionRecoverable) {
  contiguousHeap = 22516;
  for (const auto operation : operations) {
    SCOPED_TRACE(static_cast<int>(operation));
    EXPECT_EQ(invoke(operation), KOReaderSyncClient::NETWORK_ERROR);
    EXPECT_TRUE(KOReaderSyncClient::canRecoverFromLowMemory());
    EXPECT_EQ(KOReaderSyncClient::lastEspError, ESP_ERR_NO_MEM);
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 0);
    const std::string detail = KOReaderSyncClient::lastFailureDetail();
    EXPECT_NE(detail.find("22516"), std::string::npos);
    EXPECT_NE(detail.find("36864"), std::string::npos);
    EXPECT_NE(detail.find("https://active.example"), std::string::npos);
  }
  EXPECT_EQ(initializedClients, 0u);
  EXPECT_EQ(performedRequests, 0u);
}

TEST_F(KOReaderSyncDiagnostics, TransportErrorsPreserveTlsBeforeCleanupRegardlessOfHttpStatus) {
  reply.error = ESP_ERR_TIMEOUT;
  reply.tlsError = 0x8017;
  reply.tlsCode = -9984;
  reply.tlsFlags = 0x4;
  for (const int status : {-1, 0, 200}) {
    reply.status = status;
    for (const auto operation : operations) {
      SCOPED_TRACE(::testing::Message() << "status=" << status << " operation=" << static_cast<int>(operation));
      EXPECT_EQ(invoke(operation), KOReaderSyncClient::NETWORK_ERROR);
      EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
      EXPECT_EQ(KOReaderSyncClient::lastEspError, ESP_ERR_TIMEOUT);
      EXPECT_EQ(KOReaderSyncClient::lastHttpCode, status);
      EXPECT_EQ(KOReaderSyncClient::lastTlsError, 0x8017);
      EXPECT_EQ(KOReaderSyncClient::lastTlsCode, -9984);
      EXPECT_EQ(KOReaderSyncClient::lastTlsFlags, 0x4);
      const std::string detail = KOReaderSyncClient::lastFailureDetail();
      EXPECT_NE(detail.find("ESP_ERR_TIMEOUT"), std::string::npos);
      EXPECT_NE(detail.find("TLS 0x8017/-9984 flags 0x4"), std::string::npos);
      EXPECT_NE(detail.find(status > 0 ? "HTTP=200" : "HTTP=n/a"), std::string::npos);
      EXPECT_EQ(detail.find("HTTP -1"), std::string::npos);
      EXPECT_EQ(detail.find("expected JSON"), std::string::npos);
    }
  }
  ASSERT_EQ(transportEvents.size(), performedRequests * 3u);
  for (size_t i = 0; i < transportEvents.size(); i += 3) {
    EXPECT_EQ(transportEvents[i], "perform");
    EXPECT_EQ(transportEvents[i + 1], "tls");
    EXPECT_EQ(transportEvents[i + 2], "cleanup");
  }
}

TEST_F(KOReaderSyncDiagnostics, MissingHttpResponseNeverBecomesServerStatusOrSuccess) {
  for (const int status : {-1, 0}) {
    reply.status = status;
    for (const auto operation : operations) {
      EXPECT_EQ(invoke(operation), KOReaderSyncClient::NETWORK_ERROR);
      EXPECT_EQ(KOReaderSyncClient::lastEspError, ESP_OK);
      EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
      EXPECT_NE(std::string(KOReaderSyncClient::lastFailureDetail()).find("HTTP=n/a"), std::string::npos);
    }
  }
  EXPECT_EQ(tlsQueries, 0u);
}

TEST_F(KOReaderSyncDiagnostics, LowMemoryAfterAnyPerformedAttemptCannotTriggerFreshRetry) {
  reply.error = ESP_ERR_HTTP_CONNECT;
  reply.status = -1;
  reply.heapAfterPerform = 22516;
  for (const auto operation : {Operation::Authenticate, Operation::Get, Operation::Put}) {
    SCOPED_TRACE(static_cast<int>(operation));
    contiguousHeap = 60000;
    const auto before = performedRequests;
    EXPECT_EQ(invoke(operation), KOReaderSyncClient::NETWORK_ERROR);
    EXPECT_EQ(performedRequests, before + 1);
    EXPECT_EQ(KOReaderSyncClient::lastEspError, ESP_ERR_NO_MEM);
    EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
  }
}

TEST_F(KOReaderSyncDiagnostics, ClientAllocationFailureIsNotPreflightRejection) {
  failInit = true;
  for (const auto operation : operations) {
    EXPECT_EQ(invoke(operation), KOReaderSyncClient::NETWORK_ERROR);
    EXPECT_EQ(KOReaderSyncClient::lastEspError, ESP_ERR_NO_MEM);
    EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
    EXPECT_NE(std::string(KOReaderSyncClient::lastFailureDetail()).find("ESP_ERR_NO_MEM"), std::string::npos);
  }
  EXPECT_EQ(performedRequests, 0u);
}

TEST_F(KOReaderSyncDiagnostics, NewOperationClearsRecoveryAndTlsStateEvenWithoutCredentials) {
  contiguousHeap = 22516;
  EXPECT_EQ(KOReaderSyncClient::authenticate(), KOReaderSyncClient::NETWORK_ERROR);
  ASSERT_TRUE(KOReaderSyncClient::canRecoverFromLowMemory());
  KOREADER_STORE.password.clear();
  for (const auto operation : operations) {
    EXPECT_EQ(invoke(operation), KOReaderSyncClient::NO_CREDENTIALS);
    EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
    EXPECT_EQ(KOReaderSyncClient::lastEspError, 0);
    EXPECT_EQ(KOReaderSyncClient::lastHttpCode, 0);
    EXPECT_EQ(KOReaderSyncClient::lastTlsError, 0);
    EXPECT_STREQ(KOReaderSyncClient::lastFailureDetail(), "");
  }
  EXPECT_EQ(initializedClients, 0u);
}

TEST_F(KOReaderSyncDiagnostics, RegistrationUsesExplicitTargetAndProtocolWithoutLoggingCredentials) {
  KOREADER_STORE.baseUrl = "http://active.example";
  contiguousHeap = 22516;
  EXPECT_EQ(KOReaderSyncClient::registerUser("new-reader", "secret-hash",
                                             "https://url-user:url-secret@signup.example/private?token=secret"),
            KOReaderSyncClient::NETWORK_ERROR);
  EXPECT_TRUE(KOReaderSyncClient::canRecoverFromLowMemory());
  EXPECT_NE(std::string(KOReaderSyncClient::lastFailureDetail()).find("https://signup.example"), std::string::npos);
  EXPECT_EQ(performedRequests, 0u);

  KOREADER_STORE.baseUrl = "https://active.example";
  reply.status = 201;
  EXPECT_EQ(KOReaderSyncClient::registerUser("new-reader", "secret-hash", "http://signup.example"),
            KOReaderSyncClient::OK);
  EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
  ASSERT_EQ(initializedConfigs.size(), 1u);
  EXPECT_EQ(initializedConfigs.back().crt_bundle_attach, nullptr);

  contiguousHeap = 60000;
  reply.error = ESP_ERR_TIMEOUT;
  reply.status = -1;
  reply.tlsError = 0x8017;
  reply.tlsCode = -9984;
  EXPECT_EQ(KOReaderSyncClient::registerUser("new-reader", "secret-hash",
                                             "https://url-user:url-secret@signup.example/private?token=secret"),
            KOReaderSyncClient::NETWORK_ERROR);
  const std::string detail = KOReaderSyncClient::lastFailureDetail();
  EXPECT_NE(detail.find("https://signup.example"), std::string::npos);
  EXPECT_EQ(detail.find("active.example"), std::string::npos);
  EXPECT_EQ(detail.find("secret"), std::string::npos);
  for (const auto& log : logs) {
    EXPECT_EQ(log.find("secret"), std::string::npos);
    EXPECT_EQ(log.find("url-user"), std::string::npos);
  }
}

TEST_F(KOReaderSyncDiagnostics, CertificateVerificationAndRequestLimitsRemainEnabled) {
  reply.status = 401;
  EXPECT_EQ(KOReaderSyncClient::authenticate(), KOReaderSyncClient::AUTH_FAILED);
  reply.status = 201;
  EXPECT_EQ(KOReaderSyncClient::registerUser(), KOReaderSyncClient::OK);
  ASSERT_EQ(initializedConfigs.size(), 2u);
  for (const auto& config : initializedConfigs) {
    EXPECT_EQ(config.crt_bundle_attach, &esp_crt_bundle_attach);
    EXPECT_EQ(config.timeout_ms, 5000);
    EXPECT_EQ(config.buffer_size, 1024);
    EXPECT_EQ(config.buffer_size_tx, 512);
    EXPECT_EQ(config.max_redirection_count, 3);
  }
}

TEST_F(KOReaderSyncDiagnostics, SuccessfulOperationClearsPreviousTransportDiagnostics) {
  reply.error = ESP_ERR_TIMEOUT;
  reply.status = -1;
  reply.tlsError = 0x8017;
  reply.tlsCode = -9984;
  reply.tlsFlags = 4;
  EXPECT_EQ(KOReaderSyncClient::registerUser(), KOReaderSyncClient::NETWORK_ERROR);
  reply = {};
  reply.status = 201;
  EXPECT_EQ(KOReaderSyncClient::registerUser(), KOReaderSyncClient::OK);
  EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
  EXPECT_EQ(KOReaderSyncClient::lastEspError, 0);
  EXPECT_EQ(KOReaderSyncClient::lastTlsError, 0);
  EXPECT_EQ(KOReaderSyncClient::lastTlsCode, 0);
  EXPECT_EQ(KOReaderSyncClient::lastTlsFlags, 0);
}

TEST_F(KOReaderSyncDiagnostics, ClosingPersistentSessionDoesNotDiscardFailureEvidence) {
  reply.error = ESP_ERR_TIMEOUT;
  reply.status = -1;
  reply.tlsError = 0x8017;
  reply.tlsCode = -9984;
  KOReaderSyncClient::beginPersistentSession();
  EXPECT_EQ(KOReaderSyncClient::authenticate(), KOReaderSyncClient::NETWORK_ERROR);
  const std::string before = KOReaderSyncClient::lastFailureDetail();
  KOReaderSyncClient::endPersistentSession();
  EXPECT_EQ(std::string(KOReaderSyncClient::lastFailureDetail()), before);
  EXPECT_EQ(KOReaderSyncClient::lastTlsCode, -9984);
  EXPECT_FALSE(KOReaderSyncClient::canRecoverFromLowMemory());
}
}  // namespace
