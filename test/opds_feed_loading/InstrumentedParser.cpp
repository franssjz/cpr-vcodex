// Compile the production parser unchanged, replacing only the Expat boundary
// needed to observe resource overlap and inject a constructor allocation failure.
#include <OpdsParser.h>

#include "TestState.h"

static XML_Parser testParserCreate(const XML_Char* encoding) {
  ++feedTest.parserCreations;
  feedTest.events.emplace_back("parser-create");
  feedTest.parserDuringHttp |= feedTest.httpActive;
  if (feedTest.failParserCreation) return nullptr;
  auto* parser = XML_ParserCreate(encoding);
  if (parser) ++feedTest.liveParsers;
  return parser;
}

static void testParserFree(XML_Parser parser) {
  if (parser) --feedTest.liveParsers;
  XML_ParserFree(parser);
}

static XML_Status testParse(XML_Parser parser, const char* data, int length, int finalChunk) {
  if (finalChunk) {
    feedTest.events.emplace_back("parser-flush");
    feedTest.flushWithOpenFile |= feedTest.openReaders != 0;
  }
  return XML_Parse(parser, data, length, finalChunk);
}

#define XML_ParserCreate testParserCreate
#define XML_ParserFree testParserFree
#define XML_Parse testParse
#include "../../lib/OpdsParser/OpdsParser.cpp"
#undef XML_ParserCreate
#undef XML_ParserFree
#undef XML_Parse
