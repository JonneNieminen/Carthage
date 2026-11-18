#include "StartScreen.h"

#include <thread>

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int rowHeight = 64, miniWidth = 148, miniHeight = 44;

        const std::array<Colour, 8>& laneColours()
        {
            static const std::array<Colour, 8> c { theme::laneSky, theme::laneTide, theme::laneKelp, theme::laneSand,
                                                   theme::laneCoral, theme::laneMurex, theme::laneRose, theme::laneSlate };
            return c;
        }

        /** The arrangement in miniature: a well with one thin row per lane, clips as marks. */
        void drawMiniature (Graphics& g, Rectangle<float> r, const std::optional<project::Miniature>& m)
        {
            g.setColour (theme::well);
            g.fillRoundedRectangle (r, theme::radiusXs);

            const auto lanes = m ? std::max<size_t> (m->lanes.size(), 8) : 8;
            const auto inner = r.reduced (4.0f, 4.0f);
            const auto laneH = inner.getHeight() / static_cast<float> (lanes);
            const auto span = m && m->seconds > 0.0 ? static_cast<float> (m->seconds) : 1.0f;

            for (size_t i = 0; i < lanes; ++i)
            {
                const auto y = inner.getY() + laneH * static_cast<float> (i);
                const auto colour = laneColours()[i % 8];

                // An empty lane is a faint line, so an empty project still reads as lanes.
                g.setColour (theme::line);
                g.fillRect (inner.getX(), y + laneH * 0.5f, inner.getWidth(), 0.5f);

                if (! m || i >= m->lanes.size())
                    continue;

                g.setColour (colour.withAlpha (0.85f));

                for (const auto& [start, length] : m->lanes[i])
                {
                    const auto x0 = inner.getX() + inner.getWidth() * start / span;
                    const auto w = std::max (1.0f, inner.getWidth() * length / span - 0.5f);
                    g.fillRect (x0, y + laneH * 0.15f, w, std::max (1.0f, laneH * 0.7f));
                }
            }
        }
    }

    //==============================================================================================
    /** One project or template: its miniature, name and where it lives. */
    class StartScreen::Row final : public Component, public SettableTooltipClient
    {
    public:
        Row (File f, String n, String d) : file (std::move (f)), name (std::move (n)), detail (std::move (d))
        {
            setMouseCursor (MouseCursor::PointingHandCursor);
        }

        File file;
        String name, detail, when;
        std::optional<project::Miniature> miniature;
        bool loaded = false;
        std::function<void()> onChoose;
        std::function<void (const MouseEvent&)> onMenu;

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds();

            if (isMouseOver())
            {
                g.setColour (theme::bg2);
                g.fillRoundedRectangle (r.toFloat(), theme::radiusSm);
            }

            r.reduce (theme::space2, (rowHeight - miniHeight) / 2);
            drawMiniature (g, r.removeFromLeft (miniWidth).toFloat(), miniature);
            r.removeFromLeft (theme::space4);

            if (when.isNotEmpty())
            {
                g.setColour (theme::faint);
                g.setFont (theme::uiFont (theme::textSm));
                g.drawText (when, r.removeFromRight (96), Justification::centredRight, false);
            }

            g.setColour (theme::text);
            g.setFont (theme::uiFont (theme::textLg, true));
            g.drawText (name, r.removeFromTop (r.getHeight() / 2 + 2), Justification::bottomLeft, true);
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (theme::textSm));
            g.drawText (detail, r.withTrimmedTop (2), Justification::topLeft, true);
        }

        void mouseEnter (const MouseEvent&) override   { repaint(); }
        void mouseExit (const MouseEvent&) override    { repaint(); }

        void mouseUp (const MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onMenu) onMenu (e);
            }
            else if (e.mouseWasClicked() && onChoose)
            {
                onChoose();
            }
        }
    };

    //==============================================================================================
    StartScreen::StartScreen (engine::Session& s) : session (s)
    {
        styleLabel (title, theme::text2xl, theme::text, true);
        title.setText ("Start", dontSendNotification);   // the wordmark is artwork, never set in type

        for (auto [l, text] : { std::pair { &recentHeading, "Recent projects" }, { &templatesHeading, "Start from" } })
        {
            styleLabel (*l, theme::textSm, theme::muted, true);
            l->setText (text, dontSendNotification);
        }

        styleLabel (recentEmpty, theme::textMd, theme::muted);
        recentEmpty.setJustificationType (Justification::topLeft);
        recentEmpty.setText ("Projects you open or save appear here.", dontSendNotification);
        styleLabel (templatesHint, theme::textSm, theme::faint);
        templatesHint.setJustificationType (Justification::topLeft);
        templatesHint.setText (String::fromUTF8 ("Make your own from a project with File > Save as template\xe2\x80\xa6"), dontSendNotification);

        openButton.setTooltip ("Open a saved project");
        openButton.onClick = [this] { if (onOpenOther) onOpenOther(); };
        newButton.setTooltip ("Start an empty project");
        newButton.onClick = [this] { if (onNew) onNew ({}); };
        closeButton.setTooltip ("Close the start screen");
        closeButton.onClick = [this] { if (onClose) onClose(); };
        showAtStart.setTooltip ("Settings > Projects has the same switch");
        showAtStart.onClick = [this] { if (onShowAtStartChanged) onShowAtStartChanged (showAtStart.getToggleState()); };

        for (auto* c : std::initializer_list<Component*> { &title, &recentHeading, &templatesHeading, &recentEmpty, &templatesHint,
                                                          &openButton, &newButton, &showAtStart, &closeButton })
            addAndMakeVisible (c);
    }

    StartScreen::~StartScreen()
    {
        *alive = false;
    }

    void StartScreen::setShowAtStart (bool on)   { showAtStart.setToggleState (on, dontSendNotification); }

    void StartScreen::refresh()
    {
        recentRows.clear();
        templateRows.clear();

        for (const auto& file : session.getRecentProjects())
        {
            // Where it lives, from the home folder: "~/Music/Carthage".
            const auto home = File::getSpecialLocation (File::userHomeDirectory).getFullPathName();
            auto where = file.getParentDirectory().getParentDirectory().getFullPathName();

            if (where.startsWith (home))
                where = "~" + where.substring (home.length());

            auto* row = recentRows.add (new Row (file, file.getParentDirectory().getFileName(), where));
            row->when = file.getLastModificationTime().formatted ("%e %b %Y").trim();
            row->setTooltip (file.getParentDirectory().getFullPathName());
            row->onChoose = [this, file] { if (onOpen) onOpen (file); };
            row->onMenu = [this, file] (const MouseEvent&)
            {
                PopupMenu m;
                m.addItem ("Show in Finder", [file] { file.revealToUser(); });
                m.addItem ("Remove from the list", [this, file] { session.forgetRecentProject (file); refresh(); });
                m.showMenuAsync (PopupMenu::Options().withMousePosition());
            };
            addAndMakeVisible (row);
        }

        // An empty project first, then the user's templates by name.
        auto* empty = templateRows.add (new Row ({}, "Empty project", "Eight empty lanes"));
        empty->loaded = true;
        empty->onChoose = [this] { if (onNew) onNew ({}); };
        addAndMakeVisible (empty);

        auto folders = templates.findChildFiles (File::findDirectories, false);
        std::sort (folders.begin(), folders.end(), [] (const File& a, const File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });

        for (const auto& folder : folders)
        {
            const auto file = project::fileIn (folder);

            if (! file.existsAsFile())
                continue;

            auto* row = templateRows.add (new Row (file, folder.getFileName(), "Your template"));
            row->onChoose = [this, file] { if (onNew) onNew (file); };
            row->onMenu = [file] (const MouseEvent&)
            {
                PopupMenu m;
                m.addItem ("Show in Finder", [file] { file.revealToUser(); });
                m.showMenuAsync (PopupMenu::Options().withMousePosition());
            };
            addAndMakeVisible (row);
        }

        recentEmpty.setVisible (recentRows.isEmpty());
        resized();
        loadMiniatures();
    }

    void StartScreen::loadMiniatures()
    {
        // Read on a worker: a project file can be large; the rows draw when each is ready.
        std::vector<File> files;

        for (auto* rows : { &recentRows, &templateRows })
            for (auto* r : *rows)
                if (! r->loaded)
                    files.push_back (r->file);

        std::thread ([this, files, token = std::weak_ptr<bool> (alive)]
        {
            for (const auto& f : files)
            {
                auto m = project::miniatureOf (f);

                MessageManager::callAsync ([this, f, m, token]
                {
                    if (token.expired())
                        return;

                    for (auto* rows : { &recentRows, &templateRows })
                        for (auto* r : *rows)
                            if (r->file == f)
                            {
                                r->miniature = m;
                                r->loaded = true;
                                r->repaint();
                            }
                });
            }
        }).detach();
    }

    void StartScreen::paint (Graphics& g)
    {
        g.fillAll (theme::bg);
    }

    void StartScreen::resized()
    {
        closeButton.setBounds (getWidth() - theme::space4 - 32, theme::space4, 32, 32);

        // A centred column, two lists side by side.
        auto r = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 2 * theme::space8, 1040), getHeight())
                     .withTrimmedTop (theme::space8 + theme::space4).withTrimmedBottom (theme::space6);

        auto top = r.removeFromTop (40);
        newButton.setBounds (top.removeFromRight (128).withSizeKeepingCentre (128, 32));
        top.removeFromRight (theme::space2);
        openButton.setBounds (top.removeFromRight (96).withSizeKeepingCentre (96, 32));
        title.setBounds (top);
        r.removeFromTop (theme::space6);

        showAtStart.setBounds (r.removeFromBottom (28).removeFromLeft (320));
        r.removeFromBottom (theme::space2);

        auto left = r.removeFromLeft (r.getWidth() * 3 / 5 - theme::space4);
        r.removeFromLeft (theme::space8);
        auto right = r;

        recentHeading.setBounds (left.removeFromTop (24));
        left.removeFromTop (theme::space1);
        recentEmpty.setBounds (left.withHeight (40));

        for (auto* row : recentRows)
        {
            row->setVisible (left.getHeight() >= rowHeight);
            row->setBounds (left.removeFromTop (rowHeight));
        }

        templatesHeading.setBounds (right.removeFromTop (24));
        right.removeFromTop (theme::space1);

        for (auto* row : templateRows)
        {
            row->setVisible (right.getHeight() >= rowHeight);
            row->setBounds (right.removeFromTop (rowHeight));
        }

        right.removeFromTop (theme::space2);
        templatesHint.setBounds (right.removeFromTop (40).reduced (theme::space2, 0));
    }
}
