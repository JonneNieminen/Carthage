#pragma once

#include "Controls.h"

#include <engine/Session.h>

namespace carthage::ui
{
    /** The start screen (F20): recent projects and templates, each with its arrangement in
        miniature (its lanes as thin rows of clip marks in the lane colours, read from the file),
        and New and Open. On by default at launch, switchable in Settings; File > Start screen
        shows it again. It covers the window below the tabs; choosing anything closes it. */
    class StartScreen final : public juce::Component
    {
    public:
        explicit StartScreen (engine::Session&);

        /** Where templates live (app/Paths: <settings>/Templates). */
        void setTemplatesFolder (const juce::File& f)   { templates = f; }
        ~StartScreen() override;

        /** Reads the recent projects and templates again (when shown). */
        void refresh();

        /** `template` is a template's project file, or none for an empty project. */
        std::function<void (const juce::File& templateFile)> onNew;
        std::function<void (const juce::File& projectFile)> onOpen;
        std::function<void()> onOpenOther, onClose;
        std::function<void (bool)> onShowAtStartChanged;

        void setShowAtStart (bool);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Row;

        engine::Session& session;
        juce::File templates;
        juce::Label title, recentHeading, templatesHeading, recentEmpty, templatesHint;
        juce::TextButton openButton { juce::String::fromUTF8 ("Open\xe2\x80\xa6") }, newButton { "New project" };
        juce::ToggleButton showAtStart { "Show this when Carthage opens" };
        IconButton closeButton { IconButton::Glyph::close };
        juce::OwnedArray<Row> recentRows, templateRows;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);

        void loadMiniatures();
    };
}
