#include "CrashBanner.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int pad = theme::space4;

        /** "A", "A and B", "A, B and C". */
        String listOf (const StringArray& names)
        {
            if (names.size() <= 1)
                return names.joinIntoString ({});

            StringArray firsts (names);
            const auto last = firsts[firsts.size() - 1];
            firsts.remove (firsts.size() - 1);
            return firsts.joinIntoString (", ") + " and " + last;
        }
    }

    CrashBanner::CrashBanner()
    {
        styleLabel (title, theme::textMd, theme::text, true);
        styleLabel (body, theme::textSm, theme::muted);
        body.setJustificationType (Justification::topLeft);
        close.setTooltip ("Hide this. Carthage's log has each crash too (Help > Export log).");
        close.onClick = [this] { hide(); };

        for (auto* c : std::initializer_list<Component*> { &title, &body, &close })
            addAndMakeVisible (c);

        setVisible (false);
    }

    void CrashBanner::add (const StringArray& crashed, const StringArray& stoppedForGood, int others)
    {
        for (const auto& n : crashed)
            crashedNames.addIfNotAlreadyThere (n);

        for (const auto& n : stoppedForGood)
            stoppedNames.addIfNotAlreadyThere (n);

        otherPlugins += others;
        refresh();
        setVisible (true);
    }

    void CrashBanner::hide()
    {
        crashedNames.clear();
        stoppedNames.clear();
        otherPlugins = 0;
        setVisible (false);
    }

    void CrashBanner::refresh()
    {
        const auto names = crashedNames.isEmpty() ? String ("A plugin") : listOf (crashedNames);
        title.setText (names + " stopped working", dontSendNotification);

        StringArray running (crashedNames);

        for (const auto& n : stoppedNames)
            running.removeString (n);

        String text;

        if (crashedNames.isEmpty() || running == crashedNames)
        {
            text << (crashedNames.size() <= 1
                         ? "It crashed its process. Carthage started it again with its settings, in a process of its own. "
                           "Its channel was quiet for a moment."
                         : "They crashed their processes. Carthage started them again with their settings, each in a "
                           "process of its own. Their channels were quiet for a moment.");
        }
        else if (! running.isEmpty())
        {
            text << listOf (running) << (running.size() == 1 ? " started again with its settings, in a process of its own."
                                                              : " started again with their settings, each in a process of its own.");
        }

        if (otherPlugins > 0)
            text << (text.isEmpty() ? "" : " ") << (otherPlugins == 1 ? "The other plugin in that process started again too."
                                                                       : "The " + String (otherPlugins) + " other plugins in that process started again too.");

        if (! stoppedNames.isEmpty())
            text << (text.isEmpty() ? "" : "\n") << listOf (stoppedNames)
                 << (stoppedNames.size() == 1 ? " crashed 3 times, so it stays off until you open the project again."
                                              : " crashed 3 times each, so they stay off until you open the project again.");

        body.setText (text, dontSendNotification);
        resized();
        repaint();
    }

    int CrashBanner::getPreferredHeight (int width) const
    {
        const auto textWidth = width - 2 * theme::space2 - 2 * pad;
        AttributedString a;
        a.append (body.getText(), body.getFont(), theme::muted);
        a.setWordWrap (AttributedString::byWord);
        TextLayout layout;
        layout.createLayout (a, static_cast<float> (juce::jmax (100, textWidth)));
        return 2 * theme::space2 + pad + 20 + theme::space1 + static_cast<int> (std::ceil (layout.getHeight())) + theme::space2 + pad;
    }

    void CrashBanner::paint (Graphics& g)
    {
        // The update banner's card: card surface, 10 px radius, hairline, the floating shadow.
        const auto r = getLocalBounds().toFloat().reduced (theme::space2);
        DropShadow (Colours::black.withAlpha (0.35f), theme::space4, { 0, theme::space1 }).drawForPath (g, [&]
        {
            Path p;
            p.addRoundedRectangle (r, theme::radiusMd);
            return p;
        }());

        g.setColour (theme::card);
        g.fillRoundedRectangle (r, theme::radiusMd);
        g.setColour (theme::lineStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusMd, theme::stroke);
    }

    void CrashBanner::resized()
    {
        auto r = getLocalBounds().reduced (theme::space2).reduced (pad);
        auto top = r.removeFromTop (20);
        close.setBounds (top.removeFromRight (20).expanded (2));
        title.setBounds (top);
        r.removeFromTop (theme::space1);
        body.setBounds (r);
    }
}
