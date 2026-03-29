#include "Core/CoreMinimal.hpp"

#ifdef DNG_LOG_INFO
#error "CoreMinimal.hpp must not pull Logger.hpp"
#endif

#ifdef DNG_LOG_ERROR
#error "CoreMinimal.hpp must not pull Logger.hpp"
#endif

namespace
{
    int CoreMinimalHeaderOnly()
    {
        dng::u32 value = 1u;
        DNG_UNUSED(value);
        static_assert(DNG_ARRAY_COUNT("ok") == 3, "CoreMinimal macros must remain available");
        return 0;
    }
}
