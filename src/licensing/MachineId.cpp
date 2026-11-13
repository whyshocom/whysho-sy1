#include "MachineId.h"
#include <juce_cryptography/juce_cryptography.h>

namespace wsho::lic
{
juce::String getMachineId()
{
    juce::String seed;

   #if WSHO_LIC_TESTING   // test builds only: pretend to be another computer
    seed << juce::SystemStats::getEnvironmentVariable ("WHYSHO_TEST_MACHINE", {}) << "|";
   #endif

    // JUCE's stable per-device id (OS-level, survives reinstalls where possible).
    // (Deprecated in JUCE 8, but dropping it would change every machine id.)
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wdeprecated-declarations")
    JUCE_BEGIN_IGNORE_WARNINGS_MSVC (4996)
    auto ids = juce::SystemStats::getDeviceIdentifiers();
    JUCE_END_IGNORE_WARNINGS_MSVC
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
    for (auto& s : ids) seed << s << "|";

    seed << juce::SystemStats::getUniqueDeviceID() << "|";
    seed << juce::SystemStats::getOperatingSystemName() << "|";
    // A product-specific salt so the same machine yields different ids per app
    // (prevents cross-product license correlation / reuse).
    seed << "whysho-sy1-salt-v1";

    return juce::SHA256 (seed.toRawUTF8(), (size_t) seed.getNumBytesAsUTF8()).toHexString();
}
}
