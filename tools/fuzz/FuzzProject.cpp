// Fuzzes the project reader (W38): any bytes, given as a .carthage file, must open cleanly or be
// refused with a message, never crash, hang or read out of bounds. Built by the `fuzz` preset;
// docs/TESTING.md, "Static checks and sanitizers".
#include <engine/Project.h>

#include <cstddef>
#include <cstdint>

namespace
{
    struct Headless final : tracktion::EngineBehaviour
    {
        bool autoInitialiseDeviceManager() override   { return false; }
    };

    struct World
    {
        juce::ScopedJuceInitialiser_GUI gui;
        tracktion::Engine engine { "CarthageFuzz", nullptr, std::make_unique<Headless>() };
        juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("carthage-fuzz-project");
    };

    World& world()
    {
        static World w;   // one engine for the whole run: making one per input would be all the fuzzer measures
        return w;
    }
}

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    auto& w = world();
    w.folder.createDirectory();
    const auto file = w.folder.getChildFile ("Fuzz.carthage");
    file.replaceWithData (data, size);

    auto opened = carthage::project::open (w.engine, file, file);
    juce::ignoreUnused (opened);
    return 0;
}
