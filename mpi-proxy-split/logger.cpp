#include "logger.h"
#include "dmtcp.h"
#include "jconvert.h"
#include "kvdb.h"

#include <atomic>

using namespace dmtcp;

namespace Logger
{
size_t LoggerRingBufferSize = 100;

std::atomic<uint64_t> loggerRingBufferIdx(0);

static dmtcp::vector<dmtcp::string> loggerRingBuffer(LoggerRingBufferSize);
static bool enableLogging = true;

void init()
{
    // Test only: enable by default;
    return;

    const char *e = getenv("MANA_LOGGER_BUFFER_SIZE");
    if (e == NULL) {
        enableLogging = false;
        return;
    }

    LoggerRingBufferSize = atoi(e);
    if (LoggerRingBufferSize > 0) {
        loggerRingBuffer.resize(LoggerRingBufferSize);
    }
}

void record(dmtcp::string const& str)
{
    if (enableLogging) {
        uint64_t idx = loggerRingBufferIdx++;
        loggerRingBuffer[idx % LoggerRingBufferSize] = str;
    }
}

string getLogStr()
{
    ostringstream output;
    uint64_t startIdx = loggerRingBufferIdx;
    uint64_t numElements = LoggerRingBufferSize;

    if (startIdx < LoggerRingBufferSize) {
        numElements = startIdx;
        startIdx = 0;
    }

    for (size_t i = 0; i < numElements; i++) {
        size_t itemIdx = (startIdx + i) % LoggerRingBufferSize;
        output << (startIdx + i) << "-" << loggerRingBuffer[itemIdx] << "\n";
    }

    return std::move(output.str());
}

void publishLogToCoordinator()
{
    string str = getLogStr();
    string workerPath("/worker/" + string(dmtcp_get_uniquepid_str()));
    kvdb::set(workerPath, "MpiLog_Entries", jalib::XToString(loggerRingBufferIdx));
    kvdb::set(workerPath, "MpiLog", str);
}

}