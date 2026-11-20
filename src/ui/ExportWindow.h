#pragma once

#include <engine/Export.h>
#include <engine/Session.h>

namespace carthage::ui
{
    /** Export (Cmd+R, W43): a small window that says what will be exported (the loop region, or
        the project from its first sound to its last), with the format and sample rate (kept in the
        settings, also under Settings > Recording), where the file goes, and Export. While it
        renders it shows the progress and offers Cancel; afterwards, Show in Finder. The render
        runs on a thread of its own. */
    class ExportWindow final : public juce::DocumentWindow
    {
    public:
        ExportWindow (engine::Session&, juce::ApplicationCommandManager&);
        ~ExportWindow() override;

        /** Shows the window for the front project, reading its range afresh. */
        void show();

        bool isExporting() const;
        void closeButtonPressed() override;

        /** The settings' keys: the format (its index in exporting::formats) and the rate (0: the interface's). */
        static constexpr const char* formatKey = "exportFormat";
        static constexpr const char* rateKey = "exportRate";

        /** Fills a format or rate picker and reads the choice back: shared with Settings. */
        static void fillFormats (juce::ComboBox&, juce::PropertiesFile&);
        static void fillRates (juce::ComboBox&, juce::PropertiesFile&, double interfaceRate);
        static exporting::Settings settingsFrom (juce::PropertiesFile&, double interfaceRate);

    private:
        class Panel;
        std::unique_ptr<Panel> panel;
        juce::ApplicationCommandManager& commands;
    };
}
