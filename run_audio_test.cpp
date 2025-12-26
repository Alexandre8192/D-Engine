
#include "tests/Audio_smoke.cpp"
#include <cstdio>

int main() {
    int result = RunAudioSmoke();
    if (result == 0) {
        std::printf("Audio smoke test passed.\n");
        return 0;
    } else {
        std::printf("Audio smoke test FAILED with error code: %d\n", result);
        return result;
    }
}
