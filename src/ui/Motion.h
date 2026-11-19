#pragma once

#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace carthage::ui::motion
{
    /** True when the system asks for less motion (macOS: Accessibility > Display > Reduce motion;
        Windows: "Animation effects" off). Motion then jumps to its end (W45). Read each time, so a
        change in System Settings applies at once. */
    [[nodiscard]] bool reduced();

    /** What a moving control asks before it moves: the window's main component, which knows
        whether the project plays (nothing animates during playback but the playhead and meters). */
    struct Host
    {
        virtual ~Host() = default;
        [[nodiscard]] virtual bool isPlaying() const = 0;
    };

    /** Whether `c` may move now: inside a Host that is not playing, and the system has not asked
        for less motion. A control outside any Host (a view in its own window) does not move. */
    [[nodiscard]] inline bool allowedFor (const juce::Component& c)
    {
        const auto* host = c.findParentComponentOfClass<Host>();
        return host != nullptr && ! host->isPlaying() && ! reduced();
    }

    /** One short eased run from 0 to 1, synced to the display, for a component that answers an
        action: `apply` gets each step. When motion is not allowed it gets 1 at once. Starting
        again finishes the run before. */
    class Tween
    {
    public:
        explicit Tween (juce::Component& owner) : updater (&owner) {}

        void start (bool animate, int durationMs, std::function<void (float)> apply)
        {
            stop();

            if (! animate)
            {
                apply (1.0f);
                return;
            }

            animator.emplace (juce::ValueAnimatorBuilder {}
                                  .withDurationMs (static_cast<double> (durationMs))
                                  .withEasing (juce::Easings::createEaseOut())
                                  .withValueChangedCallback (std::move (apply))
                                  .build());
            updater.addAnimator (*animator);
            animator->start();
        }

        /** Jumps the run in progress to its end. */
        void stop()
        {
            if (animator)
            {
                animator->complete();
                updater.removeAnimator (*animator);
                animator.reset();
            }
        }

    private:
        juce::VBlankAnimatorUpdater updater;
        std::optional<juce::Animator> animator;
    };
}
