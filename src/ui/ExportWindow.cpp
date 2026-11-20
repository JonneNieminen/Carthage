#include "ExportWindow.h"

#include "Controls.h"

#include <app/Commands.h>
#include <engine/Project.h>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 28, labelWidth = 84, width = 460;

        String clock (double seconds)
        {
            const auto minutes = static_cast<int> (seconds / 60.0);
            return String (minutes) + ":" + String (seconds - minutes * 60.0, 1).paddedLeft ('0', 4);
        }

        String rateText (double r)
        {
            return String (r / 1000.0, std::fmod (r, 1000.0) != 0.0 ? 1 : 0) + " kHz";
        }

        double interfaceRate (engine::Session& s)
        {
            if (auto* device = s.getDeviceManager().getCurrentAudioDevice())
                return device->getCurrentSampleRate();

            return 48000.0;
        }
    }

    //==============================================================================================
    void ExportWindow::fillFormats (ComboBox& box, PropertiesFile& props)
    {
        box.clear (dontSendNotification);

        for (size_t i = 0; i < std::size (exporting::formats); ++i)
            box.addItem (exporting::describe (exporting::formats[i]), static_cast<int> (i) + 1);

        box.setSelectedId (jlimit (1, static_cast<int> (std::size (exporting::formats)), props.getIntValue (formatKey, 1) + 1), dontSendNotification);
        box.setTooltip ("WAV for anything; FLAC is the same sound in a smaller file. 24-bit is plenty for a mix; 32-bit float can not clip.");
    }

    void ExportWindow::fillRates (ComboBox& box, PropertiesFile& props, double rateOfInterface)
    {
        box.clear (dontSendNotification);
        box.addItem ("As the interface (" + rateText (rateOfInterface) + ")", 1);

        for (const auto r : exporting::sampleRates)
            box.addItem (rateText (r), roundToInt (r));

        const auto chosen = props.getIntValue (rateKey, 0);
        box.setSelectedId (chosen > 0 ? chosen : 1, dontSendNotification);
        box.setTooltip ("The export's sample rate. As the interface is what you hear while mixing.");
    }

    exporting::Settings ExportWindow::settingsFrom (PropertiesFile& props, double rateOfInterface)
    {
        exporting::Settings s;
        const auto index = jlimit (0, static_cast<int> (std::size (exporting::formats)) - 1, props.getIntValue (formatKey, 1));
        s.format = exporting::formats[static_cast<size_t> (index)];
        const auto rate = props.getIntValue (rateKey, 0);
        s.sampleRate = rate > 0 ? static_cast<double> (rate) : rateOfInterface;
        return s;
    }

    //==============================================================================================
    class ExportWindow::Panel final : public Component, private Timer
    {
    public:
        explicit Panel (engine::Session& s) : session (s)
        {
            styleLabel (title, theme::textLg, theme::text, true);
            title.setText ("Export the mix", dontSendNotification);

            for (auto [l, text] : { std::pair { &whatLabel, "What" }, { &formatLabel, "Format" }, { &rateLabel, "Sample rate" }, { &whereLabel, "Save to" } })
            {
                styleLabel (*l, theme::textMd, theme::muted);
                l->setText (text, dontSendNotification);
            }

            styleLabel (whatValue, theme::textMd, theme::text);
            whatValue.setJustificationType (Justification::topLeft);
            styleLabel (whereValue, theme::textSm, theme::text);
            whereValue.setMinimumHorizontalScale (0.6f);
            styleLabel (status, theme::textSm, theme::muted);
            status.setJustificationType (Justification::topLeft);

            auto& props = this->props();
            fillFormats (format, props);
            fillRates (rate, props, interfaceRate (session));
            format.onChange = [this]
            {
                this->props().setValue (formatKey, format.getSelectedId() - 1);
                destination = destination.withFileExtension (exporting::extensionOf (settings().format));
                showDestination();
            };
            rate.onChange = [this] { this->props().setValue (rateKey, rate.getSelectedId() == 1 ? 0 : rate.getSelectedId()); };

            change.setTooltip ("Choose the file's name and folder");
            change.onClick = [this] { chooseDestination(); };
            exportButton.setTooltip ("Render the mix to the file (" + String (app::commands::commandName) + "+R opens this window)");
            exportButton.onClick = [this] { begin(); };
            cancel.onClick = [this] { if (job != nullptr) job->cancel(); };
            reveal.setTooltip ("Show the exported file in " + String (JUCE_MAC ? "Finder" : "Explorer"));
            reveal.onClick = [this] { if (lastExport.existsAsFile()) lastExport.revealToUser(); };

            for (auto* c : std::initializer_list<Component*> { &title, &whatLabel, &whatValue, &formatLabel, &format, &rateLabel, &rate,
                                                               &whereLabel, &whereValue, &change, &exportButton, &status })
                addAndMakeVisible (c);

            addChildComponent (cancel);
            addChildComponent (reveal);
            setSize (width, 300);
        }

        ~Panel() override
        {
            stopTimer();
            job.reset();   // cancels a render in progress
        }

        PropertiesFile& props()   { return session.getEngine().getPropertyStorage().getPropertiesFile(); }

        exporting::Settings settings()   { return settingsFrom (props(), interfaceRate (session)); }

        /** The front project's range, and a file name for it, afresh. */
        void refresh()
        {
            if (isExporting())
                return;

            fillFormats (format, props());                          // Settings may have changed them
            fillRates (rate, props(), interfaceRate (session));     // and the interface its rate
            range = exporting::rangeOf (session.getEdit());

            if (! range)
                whatValue.setText ("Nothing to export yet: put a sound on a lane first.", dontSendNotification);
            else
            {
                const auto from = range->time.getStart().inSeconds(), to = range->time.getEnd().inSeconds();
                whatValue.setText ((range->isLoopRegion ? "The loop region, " : "The whole project, ") + clock (from) + " to " + clock (to)
                                       + (range->isLoopRegion ? String (". Clear the loop region to export everything.")
                                                              : String (", and as long after as reverbs and delays ring on.")),
                                   dontSendNotification);
            }

            // A file in the project's Exports folder, named after the project, not over an older one.
            if (destination == File() || ! destination.getParentDirectory().isAChildOf (session.getProjectFolder()))
            {
                const auto folder = project::exportsIn (session.getProjectFolder());
                destination = folder.getNonexistentChildFile (session.getProjectName(), "." + exporting::extensionOf (settings().format), false);
            }

            exportButton.setEnabled (range.has_value());
            showDestination();
            status.setText ({}, dontSendNotification);
            reveal.setVisible (false);
            repaint();
        }

        bool isExporting() const   { return job != nullptr; }

        void paint (Graphics& g) override
        {
            g.fillAll (theme::bg2);

            if (! isExporting() && progress <= 0.0f)
                return;

            // The progress: a thin bar in the accent (it is live) over a well.
            g.setColour (theme::well);
            g.fillRoundedRectangle (bar.toFloat(), 2.0f);
            g.setColour (theme::accent);
            g.fillRoundedRectangle (bar.toFloat().withWidth (static_cast<float> (bar.getWidth()) * std::clamp (progress, 0.0f, 1.0f)), 2.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (theme::space6, theme::space4);
            title.setBounds (r.removeFromTop (rowHeight));
            r.removeFromTop (theme::space2);

            auto what = r.removeFromTop (44);
            whatLabel.setBounds (what.removeFromLeft (labelWidth).withHeight (rowHeight));
            whatValue.setBounds (what.withTrimmedTop (5));
            r.removeFromTop (theme::space2);

            for (auto [l, c] : { std::pair<Component*, Component*> { &formatLabel, &format }, { &rateLabel, &rate } })
            {
                auto line = r.removeFromTop (rowHeight);
                l->setBounds (line.removeFromLeft (labelWidth));
                c->setBounds (line);
                r.removeFromTop (theme::space1);
            }

            auto where = r.removeFromTop (rowHeight);
            whereLabel.setBounds (where.removeFromLeft (labelWidth));
            change.setBounds (where.removeFromRight (84));
            where.removeFromRight (theme::space2);
            whereValue.setBounds (where);
            r.removeFromTop (theme::space4);

            bar = r.removeFromTop (6);
            r.removeFromTop (theme::space3);

            auto buttons = r.removeFromBottom (rowHeight + 2);
            exportButton.setBounds (buttons.removeFromRight (96));
            cancel.setBounds (exportButton.getBounds());
            buttons.removeFromRight (theme::space2);
            reveal.setBounds (buttons.removeFromRight (128));
            status.setBounds (r);
        }

    private:
        engine::Session& session;
        Label title, whatLabel, whatValue, formatLabel, rateLabel, whereLabel, whereValue, status;
        ComboBox format, rate;
        TextButton change { String::fromUTF8 ("Change\xe2\x80\xa6") }, exportButton { "Export" }, cancel { "Cancel" },
                   reveal { JUCE_MAC ? "Show in Finder" : "Show in Explorer" };
        Rectangle<int> bar;
        std::optional<exporting::Range> range;
        File destination, lastExport;
        std::unique_ptr<FileChooser> chooser;
        std::unique_ptr<exporting::Job> job;
        float progress = 0.0f;

        void showDestination()
        {
            const auto inProject = destination.getParentDirectory() == project::exportsIn (session.getProjectFolder());
            whereValue.setText (inProject ? destination.getFileName() + ", in this project's Exports folder"
                                          : destination.getFullPathName().replace (File::getSpecialLocation (File::userHomeDirectory).getFullPathName(), "~"),
                                dontSendNotification);
            whereValue.setTooltip (destination.getFullPathName());
        }

        void chooseDestination()
        {
            chooser = std::make_unique<FileChooser> ("Export the mix as", destination, "*." + exporting::extensionOf (settings().format));
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                                  [safe = SafePointer<Panel> (this)] (const FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == File())
                    return;

                safe->destination = fc.getResult().withFileExtension (exporting::extensionOf (safe->settings().format));
                safe->showDestination();
            });
        }

        void begin()
        {
            if (! range.has_value() || isExporting())
                return;

            const auto what = *range;   // read once: the calls below can not move it, but say so

            if (session.isRecording())
            {
                status.setText ("Stop recording first.", dontSendNotification);
                return;
            }

            if (const auto made = destination.getParentDirectory().createDirectory(); made.failed())
            {
                status.setText ("Could not make the folder: " + made.getErrorMessage(), dontSendNotification);
                return;
            }

            const auto chosen = settings();
            const auto file = destination;
            status.setText ("Exporting " + exporting::describe (chosen.format) + " at " + rateText (chosen.sampleRate) + String::fromUTF8 ("\xe2\x80\xa6"),
                            dontSendNotification);
            progress = 0.0f;
            setBusy (true);

            job = exporting::start (session.getEdit(), what, chosen, file, [safe = SafePointer<Panel> (this), file, chosen] (const Result& r)
            {
                if (safe != nullptr)
                    safe->finished (r, file, chosen);
            });

            startTimerHz (30);
        }

        void finished (const Result& r, const File& file, const exporting::Settings& chosen)
        {
            stopTimer();
            job.reset();
            setBusy (false);

            if (r.wasOk())
            {
                lastExport = file;
                progress = 1.0f;
                status.setText ("Exported " + file.getFileName() + ": " + exporting::describe (chosen.format) + ", " + rateText (chosen.sampleRate)
                                    + ", " + File::descriptionOfSizeInBytes (file.getSize()) + ".",
                                dontSendNotification);
                reveal.setVisible (true);

                // The next export goes beside it, not over it.
                destination = file.getParentDirectory().getNonexistentChildFile (session.getProjectName(), file.getFileExtension(), false);
                showDestination();
            }
            else
            {
                progress = 0.0f;
                status.setText (r.getErrorMessage() == "Cancelled" ? String ("Cancelled. Nothing was written.")
                                                                   : "The export did not finish: " + r.getErrorMessage(),
                                dontSendNotification);
            }

            repaint();
        }

        void setBusy (bool busy)
        {
            exportButton.setVisible (! busy);
            cancel.setVisible (busy);
            reveal.setVisible (false);

            for (auto* c : std::initializer_list<Component*> { &format, &rate, &change })
                c->setEnabled (! busy);
        }

        void timerCallback() override
        {
            if (job != nullptr)
            {
                progress = job->getProgress();
                repaint (bar.expanded (2));
            }
        }
    };

    //==============================================================================================
    ExportWindow::ExportWindow (engine::Session& s, ApplicationCommandManager& c)
        : DocumentWindow ("Export", theme::bg2, DocumentWindow::closeButton), commands (c)
    {
        setUsingNativeTitleBar (true);
        panel = std::make_unique<Panel> (s);
        setContentNonOwned (panel.get(), true);
        setResizable (false, false);
        addKeyListener (commands.getKeyMappings());
    }

    ExportWindow::~ExportWindow()
    {
        removeKeyListener (commands.getKeyMappings());
        clearContentComponent();
    }

    void ExportWindow::show()
    {
        panel->refresh();
        setVisible (true);
        toFront (true);
    }

    bool ExportWindow::isExporting() const
    {
        return panel->isExporting();
    }

    void ExportWindow::closeButtonPressed()
    {
        setVisible (false);   // an export in progress carries on; the window comes back with Cmd+R
    }
}
