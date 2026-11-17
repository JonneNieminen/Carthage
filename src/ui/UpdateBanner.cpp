#include "UpdateBanner.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int pad = theme::space4, buttonHeight = 28, maxNoteLines = 4;

        /** The notes, cut to a few lines so the card stays small; the rest is in the release. */
        String shortNotes (const String& text)
        {
            StringArray lines;
            lines.addLines (text);
            lines.removeEmptyStrings();

            const auto more = lines.size() > maxNoteLines;
            lines.removeRange (maxNoteLines, lines.size());

            for (auto& l : lines)
                l = String::fromUTF8 ("\xe2\x80\xa2 ") + l.trimCharactersAtStart ("-*\xe2\x80\xa2 ");   // "• change"

            if (more)
                lines.add ("and more");

            return lines.joinIntoString ("\n");
        }
    }

    UpdateBanner::UpdateBanner()
    {
        styleLabel (title, theme::textMd, theme::text, true);
        styleLabel (notes, theme::textSm, theme::muted);
        styleLabel (detail, theme::textSm, theme::muted);
        notes.setJustificationType (Justification::topLeft);
        detail.setJustificationType (Justification::centredLeft);

        // The default choice is filled with the strongest neutral; sky is kept for live things.
        primary.setColour (TextButton::buttonColourId, theme::mist);
        primary.setColour (TextButton::textColourOffId, theme::graphite);
        secondary.setColour (TextButton::buttonColourId, theme::hover);

        close.setTooltip ("Hide. An update you chose still installs when you quit.");

        primary.onClick = [this]
        {
            if (! model)
                return;

            const auto installOnQuit = model->canInstallOnQuit && model->stage == Stage::found && ! model->installOnQuitChosen;

            if (installOnQuit) { if (onInstallOnQuit) onInstallOnQuit(); }
            else               { if (onRestartNow) onRestartNow(); }
        };

        secondary.onClick = [this] { if (onRestartNow) onRestartNow(); };
        close.onClick = [this] { if (onDismiss) onDismiss(); };

        for (auto* c : std::initializer_list<Component*> { &title, &notes, &detail, &primary, &secondary, &close })
            addAndMakeVisible (c);

        setVisible (false);
    }

    void UpdateBanner::show (std::optional<Model> m)
    {
        model = std::move (m);
        setVisible (model.has_value());
        refresh();
    }

    void UpdateBanner::setBusy (bool b)
    {
        if (busy != b)
        {
            busy = b;
            refresh();
        }
    }

    void UpdateBanner::refresh()
    {
        if (! model)
            return;

        const auto& m = *model;
        const auto name = "Carthage " + m.version;
        const String waitTip = "Available when playback and recording stop";

        notes.setText (shortNotes (m.notes), dontSendNotification);
        primary.setVisible (true);
        secondary.setVisible (false);
        primary.setEnabled (true);
        primary.setTooltip ({});

        switch (m.stage)
        {
            case Stage::found:
                if (! m.canInstallOnQuit)
                {
                    title.setText (name + " is available", dontSendNotification);
                    detail.setText ({}, dontSendNotification);
                    primary.setButtonText ("Install now");
                    primary.setEnabled (! busy);
                    primary.setTooltip (busy ? waitTip : "Download and install it; Carthage closes and reopens");
                }
                else if (m.installOnQuitChosen)
                {
                    title.setText (name + " installs when you quit", dontSendNotification);
                    detail.setText ("Getting it ready", dontSendNotification);
                    primary.setButtonText ("Restart now");
                    primary.setEnabled (! busy);
                    primary.setTooltip (busy ? waitTip : "Install it now and reopen Carthage");
                }
                else
                {
                    title.setText (name + " is available", dontSendNotification);
                    detail.setText ({}, dontSendNotification);
                    primary.setButtonText ("Install on quit");
                    primary.setTooltip ("Download it now and install it the next time you quit Carthage");
                    secondary.setVisible (true);
                    secondary.setButtonText ("Restart now");
                    secondary.setEnabled (! busy);
                    secondary.setTooltip (busy ? waitTip : "Download it, install it and reopen Carthage");
                }
                break;

            case Stage::downloading:
                title.setText (m.installOnQuitChosen ? name + " installs when you quit" : "Getting " + name, dontSendNotification);
                detail.setText ("Downloading, " + String (roundToInt (m.progress * 100.0)) + "%", dontSendNotification);
                primary.setButtonText ("Restart now");
                primary.setEnabled (false);
                primary.setTooltip ("Available once the download is checked");
                break;

            case Stage::ready:
                title.setText (name + " installs when you quit", dontSendNotification);
                detail.setText ("Ready", dontSendNotification);
                primary.setButtonText ("Restart now");
                primary.setEnabled (! busy);
                primary.setTooltip (busy ? waitTip : "Install it now and reopen Carthage");
                break;

            case Stage::installing:
                title.setText ("Installing " + name, dontSendNotification);
                detail.setText ("Carthage reopens in a moment", dontSendNotification);
                primary.setVisible (false);
                break;
        }

        resized();
        repaint();
    }

    int UpdateBanner::getPreferredHeight (int width) const
    {
        const auto lines = notes.getText().isEmpty() ? 0 : StringArray::fromLines (notes.getText()).size();
        const auto noteHeight = lines * 17;
        juce::ignoreUnused (width);
        return 2 * theme::space2 + pad + 20 + (noteHeight > 0 ? theme::space1 + noteHeight : 0) + theme::space3 + buttonHeight + pad;
    }

    void UpdateBanner::paint (Graphics& g)
    {
        // A floating card: card surface, 10 px radius, hairline, and the one soft shadow the theme
        // allows for things that float.
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

        if (model && model->stage == Stage::downloading)
        {
            // Download progress as a hairline along the card's foot.
            auto bar = r.reduced (theme::radiusMd, 0).removeFromBottom (2.0f).translated (0.0f, -1.0f);
            g.setColour (theme::lineStrong);
            g.fillRect (bar);
            g.setColour (theme::muted);
            g.fillRect (bar.withWidth (bar.getWidth() * static_cast<float> (model->progress)));
        }
    }

    void UpdateBanner::resized()
    {
        auto r = getLocalBounds().reduced (theme::space2).reduced (pad);

        auto top = r.removeFromTop (20);
        close.setBounds (top.removeFromRight (20).expanded (2));
        title.setBounds (top);

        if (notes.getText().isNotEmpty())
        {
            r.removeFromTop (theme::space1);
            notes.setBounds (r.removeFromTop (StringArray::fromLines (notes.getText()).size() * 17));
        }
        else
        {
            notes.setBounds ({});
        }

        r.removeFromTop (theme::space3);
        auto buttons = r.removeFromTop (buttonHeight);

        if (primary.isVisible())
        {
            primary.setBounds (buttons.removeFromLeft (primary.getBestWidthForHeight (buttonHeight) + theme::space4));
            buttons.removeFromLeft (theme::space2);
        }

        if (secondary.isVisible())
        {
            secondary.setBounds (buttons.removeFromLeft (secondary.getBestWidthForHeight (buttonHeight) + theme::space4));
            buttons.removeFromLeft (theme::space2);
        }

        detail.setBounds (buttons);
    }
}
