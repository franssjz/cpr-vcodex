#include <expat.h>

// Compile the production parser unchanged, substituting only Expat's factory
// so tests can fail and account for its real allocations on every host.
extern XML_Parser opdsTestParserCreate(const XML_Char* encoding);
#define XML_ParserCreate opdsTestParserCreate
#include "../../lib/OpdsParser/OpdsParser.cpp"
#undef XML_ParserCreate
