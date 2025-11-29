#include "Core/Logger.hpp"

int RunLoggerOnlySmoke()
{
    dng::core::Logger::Info("Test", "Hello Logger");
    return 0;
}
