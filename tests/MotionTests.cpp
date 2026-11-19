#include <ui/Controls.h>
#include <ui/Timeline.h>

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

/** The wheel over a value control (W37), with what macOS sends: Shift turns a mouse wheel's
    turn into a sideways scroll, so the delta arrives in deltaX with deltaY at zero. */
class WheelTests final : public juce::UnitTest
{
public:
    WheelTests() : juce::UnitTest ("The wheel over a value control", "ui") {}

    void runTest() override
    {
        const auto wheel = [] (ui::ValueSlider& slider, juce::ModifierKeys mods, float dx, float dy)
        {
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), {}, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      &slider, &slider, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 0, false);
            juce::MouseWheelDetails details {};
            details.deltaX = dx;
            details.deltaY = dy;
            slider.mouseWheelMove (e, details);
        };

        beginTest ("Shift with a mouse wheel still adjusts, in finer steps");
        {
            ui::ValueSlider slider (0.0);
            slider.setRange (-48.0, 12.0, 0.01);
            slider.setValue (0.0);
            slider.setSize (200, 24);
            wheel (slider, {}, 0.0f, 0.1f);
            const auto plain = slider.getValue();
            expect (plain > 0.0, "the wheel raises the value");

            slider.setValue (0.0);
            wheel (slider, juce::ModifierKeys::shiftModifier, 0.1f, 0.0f);   // as macOS sends it
            const auto fine = slider.getValue();
            expect (fine > 0.0, "Shift + wheel raises it too");
            expect (fine < plain, "and by less");
        }

        beginTest ("Shift + wheel over the lanes scrolls in time");
        {
            juce::MouseWheelDetails macShift {};
            macShift.deltaX = 0.2f;   // macOS: the turn arrives sideways, deltaY 0
            const auto mac = ui::Timeline::wheelScroll (true, macShift);
            expect (mac.sideways && mac.delta > 0.0f, "the turn as macOS sends it moves in time");

            juce::MouseWheelDetails winShift {};
            winShift.deltaY = 0.2f;   // elsewhere it stays in deltaY
            const auto win = ui::Timeline::wheelScroll (true, winShift);
            expect (win.sideways && win.delta > 0.0f);

            juce::MouseWheelDetails plain {};
            plain.deltaY = 0.2f;
            expect (! ui::Timeline::wheelScroll (false, plain).sideways, "without Shift it scrolls the lanes down");
        }
    }
};

static WheelTests wheelTests;

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
