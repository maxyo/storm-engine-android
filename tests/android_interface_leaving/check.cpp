// An interface whose script segment was unloaded publishes no further model (mobile_interface_leaving.hpp).
#include "mobile_interface_leaving.hpp"

#include <cstdio>

static int failures = 0;
static void Expect(bool ok, const char *what)
{
    if (!ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    storm::mobile::InterfaceLeaving leaving;
    Expect(!leaving.Update(5, true), "an interface whose script is loaded publishes");
    Expect(!leaving.Update(5, true), "and keeps publishing");
    Expect(leaving.Update(5, false), "the frame its script is gone, it publishes nothing");
    Expect(leaving.Update(5, false), "nor in the frames until the next interface");
    Expect(leaving.Update(5, true), "a segment loaded again by something else does not bring the opening back");
    Expect(!leaving.Update(6, true), "the next opening publishes");
    Expect(!leaving.Update(7, false), "an opening whose script was never seen loaded is not held back");
    Expect(!leaving.Update(7, std::nullopt), "nor one whose adapter does not say");
    Expect(!leaving.Update(8, std::nullopt) && !leaving.Update(8, true) && leaving.Update(8, false), "unknown, loaded, gone: left");
    if (!failures)
        std::printf("interface leaving checks passed\n");
    return failures ? 1 : 0;
}
