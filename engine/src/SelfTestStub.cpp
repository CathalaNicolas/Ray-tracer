#include "SelfTest.hpp"

#include <iostream>

// Full engine self-test (WGL GpuRayTracer host) is Windows-only.
// Linux Diligent bring-up uses diligent_smoke + the interactive editor instead.
int runSelfTests()
{
    std::cerr << "Self-test requires the Windows OpenGL still host; skipped on this platform.\n";
    return 0;
}
