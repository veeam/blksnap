#include "TestConfig.h"
#include <memory>

static std::shared_ptr<TestConfig> ptrTestConfig;

void TestConfig::Set(const TestConfig& config)
{
    ptrTestConfig.reset(new TestConfig(config));
}

const TestConfig& TestConfig::Get()
{
    if (ptrTestConfig)
        return *ptrTestConfig;
    
    throw std::runtime_error("Test config doesn't init.");
}
