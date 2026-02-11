#include <new>

namespace
{
    struct alignas(32) AlignedPod
    {
        unsigned char payload[64];
    };
}

int RunNewDeleteSmoke()
{
    auto* scalar = new (std::nothrow) AlignedPod{};
    if (!scalar)
    {
        return 1;
    }
    delete scalar;

    auto* scalarAligned = new (std::align_val_t{32}, std::nothrow) AlignedPod{};
    if (!scalarAligned)
    {
        return 2;
    }
    delete scalarAligned;

    auto* arrayDefault = new (std::nothrow) AlignedPod[4];
    if (!arrayDefault)
    {
        return 3;
    }
    delete[] arrayDefault;

    auto* arrayAligned = new (std::align_val_t{32}, std::nothrow) AlignedPod[2];
    if (!arrayAligned)
    {
        return 4;
    }
    delete[] arrayAligned;

    auto* arrayAlignedNoThrow = new (std::align_val_t{16}, std::nothrow) AlignedPod[1];
    if (!arrayAlignedNoThrow)
    {
        return 5;
    }
    delete[] arrayAlignedNoThrow;

    return 0;
}
