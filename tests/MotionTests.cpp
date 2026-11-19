#include <ui/Controls.h>

using namespace carthage;

namespace
{
    /** Stands in for the main component: says whether the project plays. */
    struct Host final : juce::Component, ui::motion::Host
    {
        bool playing = false;
        bool isPlaying() const override   { return playing; }
    };
}

class MotionTests final : public juce::UnitTest
{
public:
    MotionTests() : juce::UnitTest ("Motion that answers an action", "ui") {}

    void runTest() override
    {
        beginTest ("A control outside a host never moves");
        {
            ui::Switch s ("Hear the input");
            expect (! ui::motion::allowedFor (s), "a view in its own window has no host");
        }

        beginTest ("Nothing moves while the project plays");
        {
            Host host;
            ui::Switch s ("Reverse");
            host.addAndMakeVisible (s);
            host.playing = true;
            expect (! ui::motion::allowedFor (s));
            host.playing = false;
            expect (ui::motion::allowedFor (s) == ! ui::motion::reduced(), "stopped, it moves unless the system asks for less motion");
        }

        beginTest ("A tween that may not move lands at its end at once");
        {
            juce::Component owner;
            ui::motion::Tween tween (owner);
            float last = -1.0f;
            tween.start (false, 120, [&last] (float v) { last = v; });
            expectEquals (last, 1.0f);
        }
    }
};

static MotionTests motionTests;
