#ifdef SIMULATOR

#include "network/CrossPointWebServer.h"

CrossPointWebServer::CrossPointWebServer() = default;
CrossPointWebServer::~CrossPointWebServer() = default;

void CrossPointWebServer::begin() {}
void CrossPointWebServer::stop() {}
void CrossPointWebServer::handleClient() {}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  return {};
}

#endif
