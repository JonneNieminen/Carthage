#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>
#include <vector>

namespace carthage::ui::routing
{
    /** The mixer's Routing section (owner, W61): its own section under the strips, drawn in one
        of five ways chosen in Settings > Mixer. The selected channel's story is the default. */
    enum class Style { story, lines, flow, grid, tree };
    inline constexpr Style styles[] = { Style::story, Style::lines, Style::flow, Style::grid, Style::tree };

    juce::String nameOf (Style);        ///< "The selected channel", "Lines per destination"…
    juce::String describe (Style);      ///< one sentence for Settings
    juce::String idOf (Style);          ///< kept in the settings file: "story", "lines"…
    Style styleFromId (const juce::String&);   ///< the default for anything unknown

    struct Send
    {
        int to = -1;        ///< the node it goes to
        float db = 0.0f;
    };

    /** A channel as the section sees it: what it is, where its sound goes, where its strip is. */
    struct Node
    {
        enum class Kind { channel, bus, preMaster, master };

        juce::String name;
        juce::Colour colour;
        Kind kind = Kind::channel;
        bool muted = false;
        juce::String input;              ///< "Analogue 1"; empty when it takes none
        int out = -1;                    ///< the node its main output plays into; -1 when that is off
        std::vector<Send> sends;
        float levelDb = 0.0f;
        int plugins = 0;
        float x = 0.0f;                  ///< its strip's centre in the section (lines)
        int beyond = 0;                  ///< -1 / 1: its strip is scrolled out of view to the left / right

        bool pinned() const noexcept     { return kind == Kind::preMaster || kind == Kind::master; }
    };

    struct Model
    {
        std::vector<Node> nodes;         ///< master first, then the Pre-Master (if on), then the channels in order
        int selected = -1;
        juce::String masterOutput;       ///< the interface outputs master plays to
    };

    /** The project the previews show: Kick and Snare into a Drum bus and sending to Reverb, the
        Drum bus selected; strips laid across `width`. */
    Model example (float width);

    /** Plain words for the selected channel: what plays into it and where it goes. */
    juce::String sentence (const Model&);

    /** How tall a style wants to be for this model, without the heading. */
    int heightFor (const Model&, Style);

    /** Draws the model in `area` (no heading). */
    void paint (juce::Graphics&, juce::Rectangle<float> area, const Model&, Style);

    /** The patch grid's cell under `p`: (the channel of its row, the destination of its column). */
    std::optional<std::pair<int, int>> gridCellAt (juce::Rectangle<float> area, const Model&, juce::Point<float> p);

    /** The section itself: a heading, then the model drawn in the chosen style. Its tooltip says
        in words what the selected channel's routing is. */
    class Section final : public juce::Component, public juce::TooltipClient
    {
    public:
        static constexpr int headingHeight = 30;

        void setModel (Model);
        void setStyle (Style);
        Style getStyle() const noexcept   { return style; }

        /** How tall the section wants to be: its heading and its drawing. */
        int idealHeight() const           { return headingHeight + heightFor (model, style); }

        /** The patch grid: a click in the selected channel's row routes it to that column's node
            (the routing never changes which channel is selected, owner). */
        std::function<void (int destination)> onRoute;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void resized() override   { scrollBy (0.0f); }
        juce::String getTooltip() override;

    private:
        Model model;
        Style style = Style::story;
        float scroll = 0.0f;   // how far a drawing taller than the section is scrolled
        juce::Rectangle<float> drawing() const;
        void scrollBy (float pixels);
    };

    /** A picture of one style, drawn from the example project, to choose it by (Settings). */
    class Preview final : public juce::Button
    {
    public:
        explicit Preview (Style);
        Style getStyleShown() const noexcept   { return shown; }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Style shown;
        static constexpr float logicalWidth = 640.0f;   // drawn this wide, then scaled into the card
        Model sample = example (logicalWidth);
    };
}
