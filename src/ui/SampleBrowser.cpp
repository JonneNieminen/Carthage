#include "SampleBrowser.h"

namespace carthage::ui
{
    using namespace juce;

    namespace
    {
        constexpr int pad = theme::space3, indent = 12;

        Path star (Rectangle<float> r)
        {
            Path p;
            p.addStar (r.getCentre(), 5, r.getWidth() * 0.22f, r.getWidth() * 0.5f, 0.0f);
            return p;
        }

        /** "A minor" -> "Am", "F# major" -> "F#": short enough for a row. */
        String shortKey (const String& key)
        {
            if (key.isEmpty())
                return {};

            const auto note = key.upToFirstOccurrenceOf (" ", false, false);
            return key.endsWith ("minor") ? note + "m" : note;
        }

        PropertiesFile& settingsOf (engine::Session& s)   { return s.getEngine().getPropertyStorage().getPropertiesFile(); }
    }

    SampleBrowser::SampleBrowser (engine::Session& s) : session (s), playWhileHeld(settingsOf (session).getBoolValue ("playWhileHeld", false))
    {
        styleLabel (status, theme::textSm, theme::muted);

        add.setTooltip ("Add a folder of samples: every sound under it is listed, folders and all");
        add.onClick = [this] { addFolder(); };

        search.setTextToShowWhenEmpty ("Search samples", theme::faint);
        search.setFont (theme::uiFont());
        search.setIndents (theme::space2, 6);
        search.setTooltip ("Type part of a name or folder; letters can be spread out");
        search.onTextChange = [this] { refresh(); };
        search.onEscapeKey = [this] { search.clear(); refresh(); };
        search.onReturnKey = [this]
        {
            for (int r = 0; r < getNumRows(); ++r)
                if (sampleAt (r) != nullptr) { list.selectRow (r); play (r); return; }
        };

        for (auto [b, fav, tip] : { std::tuple { &viewFolders, false, "Your folders" },
                                    { &viewFavourites, true, "Sounds you starred" } })
        {
            b->setClickingTogglesState (false);
            b->setTooltip (tip);
            b->onClick = [this, f = fav] { setView (f); };
            addAndMakeVisible (*b);
        }

        list.setModel (this);
        list.setColour (ListBox::backgroundColourId, theme::bg2);
        list.setOutlineThickness (0);
        list.addMouseListener (this, true);   // presses and releases on rows: play, hold, star, open

        styleLabel (volumeLabel, theme::textSm, theme::muted);
        volumeLabel.setText ("Preview", dontSendNotification);
        volume.setSliderStyle (Slider::LinearHorizontal);
        volume.setTextBoxStyle (Slider::NoTextBox, true, 0, 0);
        volume.setRange (-60.0, 6.0, 0.5);
        volume.setPopupDisplayEnabled (true, true, this);
        volume.setTextValueSuffix (" dB");
        volume.setTooltip ("How loud sounds play while you browse; the project's own level is not changed. "
                           "Double-click for the default; scroll to adjust, Shift for fine steps.");
        volume.setValue (settingsOf (session).getDoubleValue ("previewVolumeDb", -6.0), dontSendNotification);
        session.getPreview().setVolumeDb (static_cast<float> (volume.getValue()));
        volume.onValueChange = [this]
        {
            session.getPreview().setVolumeDb (static_cast<float> (volume.getValue()));
            settingsOf (session).setValue ("previewVolumeDb", volume.getValue());
        };

        for (auto* c : std::initializer_list<Component*> { &status, &add, &search, &list, &volumeLabel, &volume })
            addAndMakeVisible (c);

        setTextSize (static_cast<float> (settingsOf (session).getDoubleValue ("browserTextSize", 13.0)));
        

        for (const auto& root : session.getSampleLibrary().getRoots())
            open.insert (root.getFullPathName());

        setView (false);
    }

    SampleBrowser::~SampleBrowser()
    {
        list.removeMouseListener (this);
        list.setModel (nullptr);
    }

    void SampleBrowser::setTextSize (float px)
    {
        textSize = jlimit (12.0f, 16.0f, px);
        rowHeight = roundToInt (textSize * 2.15f);
        list.setRowHeight (rowHeight);
        list.repaint();
    }

    void SampleBrowser::focusSearch()
    {
        search.grabKeyboardFocus();
        search.selectAll();
    }

    void SampleBrowser::addFolder()
    {
        chooser = std::make_unique<FileChooser> ("Add a folder of samples", File::getSpecialLocation (File::userMusicDirectory));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc)
                              {
                                  if (const auto folder = fc.getResult(); folder.isDirectory())
                                  {
                                      open.insert (folder.getFullPathName());
                                      session.getSampleLibrary().addRoot (folder);
                                      refresh();
                                  }
                              });
    }

    void SampleBrowser::openAllFolders()
    {
        for (const auto& f : session.getSampleLibrary().getFolders())
            open.insert (f.file.getFullPathName());

        refresh();
    }

    void SampleBrowser::setView (bool favourites)
    {
        favouritesOnly = favourites;

        for (auto [b, f] : { std::pair { &viewFolders, false }, { &viewFavourites, true } })
        {
            b->setColour (TextButton::buttonColourId, f == favourites ? theme::hover : theme::bg2);
            b->setColour (TextButton::textColourOffId, f == favourites ? theme::text : theme::muted);
        }

        refresh();
    }

    void SampleBrowser::refresh()
    {
        auto& library = session.getSampleLibrary();
        const auto& folders = library.getFolders();
        const auto query = search.getText().trim();
        rows.clear();

        if (query.isNotEmpty() || favouritesOnly)
        {
            const auto found = library.find (query, favouritesOnly);
            const auto title = query.isNotEmpty() ? (found.empty() ? String ("No sound matches") : String ("Best matches"))
                                                  : (found.empty() ? String ("No favourites yet: star a sound to keep it here")
                                                                   : "Favourites (" + String (static_cast<int> (found.size())) + ")");
            rows.push_back ({ Row::Type::header, 0, 0, title });

            for (auto i : found)
                rows.push_back ({ Row::Type::sample, static_cast<int> (i), 0, {} });
        }
        else
        {
            // The tree: each open folder shows its folders, then its sounds.
            const auto addFolder = [&] (auto& self, int index) -> void
            {
                const auto& f = folders[static_cast<size_t> (index)];
                rows.push_back ({ Row::Type::folder, index, f.depth, {} });

                if (! open.contains (f.file.getFullPathName()))
                    return;

                for (auto child : f.folders)
                    self (self, child);

                for (auto sample : f.samples)
                    rows.push_back ({ Row::Type::sample, sample, f.depth + 1, {} });
            };

            for (size_t i = 0; i < folders.size(); ++i)
                if (folders[i].parent < 0)
                    addFolder (addFolder, static_cast<int> (i));
        }

        const auto count = static_cast<int> (library.getSamples().size());
        const auto roots = library.getRoots().size();
        status.setText (library.isIndexing() ? String ("Reading your folders")
                        : roots == 0 ? String ("No folders yet")
                        : String (count) + (count == 1 ? " sound in " : " sounds in ") + String (roots) + (roots == 1 ? " folder" : " folders"),
                        dontSendNotification);

        list.setVisible (roots > 0);
        list.updateContent();
        list.repaint();
        repaint();   // the empty state
    }

    void SampleBrowser::frame()
    {
        auto& preview = session.getPreview();
        const auto now = preview.isPlaying() ? preview.getFile() : File();
        const auto progress = now == File() ? -1.0 : preview.getProgress();

        if (now != playing || std::abs (progress - lastProgress) > 0.002)
        {
            playing = now;
            lastProgress = progress;
            list.repaint();
        }
    }

    const engine::SampleLibrary::Sample* SampleBrowser::sampleAt (int row) const
    {
        if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<size_t> (row)].type != Row::Type::sample)
            return nullptr;

        return &session.getSampleLibrary().getSamples()[static_cast<size_t> (rows[static_cast<size_t> (row)].index)];
    }

    void SampleBrowser::play (int row)
    {
        if (const auto* s = sampleAt (row))
        {
            session.getPreview().play (s->file);
            playing = s->file;
            lastProgress = 0.0;
            list.repaint();
        }
    }

    void SampleBrowser::toggleFolder (int row)
    {
        const auto& f = session.getSampleLibrary().getFolders()[static_cast<size_t> (rows[static_cast<size_t> (row)].index)];
        const auto path = f.file.getFullPathName();

        if (! open.erase (path))
            open.insert (path);

        refresh();
    }

    void SampleBrowser::showFolderMenu (int row)
    {
        const auto& f = session.getSampleLibrary().getFolders()[static_cast<size_t> (rows[static_cast<size_t> (row)].index)];
        const auto file = f.file;
        PopupMenu m;
        m.addItem ("Show in Finder", [file] { file.revealToUser(); });

        if (f.parent < 0)
            m.addItem ("Remove from the browser", [this, file] { session.getSampleLibrary().removeRoot (file); refresh(); });

        m.showMenuAsync (PopupMenu::Options().withMousePosition());
    }

    //==============================================================================================
    void SampleBrowser::mouseDown (const MouseEvent& e)
    {
        if (e.eventComponent != &list && ! list.isParentOf (e.eventComponent))
            return;

        const auto p = e.getEventRelativeTo (&list).getPosition();
        const auto row = list.getRowContainingPosition (p.x, p.y);

        if (row < 0 || row >= getNumRows())
            return;

        const auto& r = rows[static_cast<size_t> (row)];

        if (r.type == Row::Type::folder)
        {
            if (e.mods.isPopupMenu())
                showFolderMenu (row);
            else
                toggleFolder (row);

            return;
        }

        if (const auto* s = sampleAt (row))
        {
            const auto rowTop = list.getRowPosition (row, true).getY();

            if (starArea (list.getVisibleRowWidth()).contains (p.x, p.y - rowTop))
            {
                auto& library = session.getSampleLibrary();
                library.setFavourite (s->file, ! library.isFavourite (s->file));
                return;
            }

            selectingWithMouse = true;
            list.selectRow (row);
            selectingWithMouse = false;

            // Every click plays from the top, cutting whatever is playing: clicking again
            // retriggers the sound, as fast as you click.
            play (row);
        }
    }

    void SampleBrowser::mouseUp (const MouseEvent& e)
    {
        if (playWhileHeld && (e.eventComponent == &list || list.isParentOf (e.eventComponent)))
            session.getPreview().stop();
    }

    bool SampleBrowser::keyPressed (const KeyPress& k)
    {
        if (k == KeyPress::spaceKey && sampleAt (list.getSelectedRow()) != nullptr)
        {
            play (list.getSelectedRow());
            heldByKey = playWhileHeld;
            return true;   // over a sound, Space plays it rather than the project
        }

        return false;
    }

    bool SampleBrowser::keyStateChanged (bool isKeyDown)
    {
        if (heldByKey && ! isKeyDown && ! KeyPress::isKeyCurrentlyDown (KeyPress::spaceKey)
             && ! KeyPress::isKeyCurrentlyDown (KeyPress::returnKey))
        {
            heldByKey = false;
            session.getPreview().stop();
        }

        return false;
    }

    void SampleBrowser::returnKeyPressed (int row)
    {
        play (row);
        heldByKey = playWhileHeld;
    }

    void SampleBrowser::selectedRowsChanged (int row)
    {
        // The arrow keys move and play; a click plays on its own (mouseDown).
        if (! selectingWithMouse && ! playWhileHeld && sampleAt (row) != nullptr && list.hasKeyboardFocus (true))
            play (row);
    }

    //==============================================================================================
    int SampleBrowser::getNumRows()   { return static_cast<int> (rows.size()); }

    void SampleBrowser::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= getNumRows())
            return;

        const auto& r = rows[static_cast<size_t> (row)];
        auto& library = session.getSampleLibrary();
        auto area = Rectangle<int> (0, 0, width, height).reduced (pad, 0);

        if (r.type == Row::Type::header)
        {
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (textSize - 1.0f, true));
            g.drawText (r.text, area.withTrimmedTop (height / 4), Justification::centredLeft, true);
            return;
        }

        area.removeFromLeft (r.depth * indent);

        if (r.type == Row::Type::folder)
        {
            const auto& f = library.getFolders()[static_cast<size_t> (r.index)];
            const auto isOpen = open.contains (f.file.getFullPathName());

            // The chevron: right when closed, down when open.
            const auto c = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (8.0f, 8.0f);
            Path chevron;
            if (isOpen)
                chevron.addTriangle (c.getX(), c.getY() + 2.0f, c.getRight(), c.getY() + 2.0f, c.getCentreX(), c.getBottom() - 1.0f);
            else
                chevron.addTriangle (c.getX() + 2.0f, c.getY(), c.getX() + 2.0f, c.getBottom(), c.getRight() - 1.0f, c.getCentreY());
            g.setColour (theme::muted);
            g.fillPath (chevron);

            area.removeFromLeft (theme::space1);
            const auto count = static_cast<int> (f.samples.size());

            if (count > 0)
            {
                g.setColour (theme::faint);
                g.setFont (theme::timeFont (textSize - 2.0f));
                g.drawText (String (count), area.removeFromRight (40), Justification::centredRight, false);
            }

            g.setColour (theme::text);
            g.setFont (theme::uiFont (textSize, f.parent < 0));
            g.drawText (f.name, area, Justification::centredLeft, true);
            return;
        }

        const auto& s = library.getSamples()[static_cast<size_t> (r.index)];
        const auto isPlaying = s.file == playing;

        if (selected || isPlaying)
        {
            // Selection and playback are live: the accent, faint; the part heard so far, a little more.
            const auto box = Rectangle<int> (theme::space1, 1, width - 2 * theme::space1, height - 2).toFloat();
            g.setColour (theme::accent.withAlpha (0.10f));
            g.fillRoundedRectangle (box, theme::radiusSm);

            if (isPlaying && lastProgress > 0.0)
            {
                g.setColour (theme::accent.withAlpha (0.14f));
                g.fillRoundedRectangle (box.withWidth (box.getWidth() * static_cast<float> (lastProgress)), theme::radiusSm);
            }
        }

        area.removeFromLeft (14 + theme::space1);   // under the folder's name, past its chevron
        area.removeFromRight (24);                   // the star

        if (const auto a = library.getAnalysis (s.file))
        {
            String facts;
            facts << shortKey (a->key);
            if (a->bpm) facts << (facts.isEmpty() ? "" : "  ") << String (roundToInt (*a->bpm));

            g.setColour (theme::faint);
            g.setFont (theme::timeFont (textSize - 2.0f));
            g.drawText (facts, area.removeFromRight (64), Justification::centredRight, false);
        }

        g.setColour (isPlaying ? theme::accentText : theme::text);
        g.setFont (theme::uiFont (textSize));
        g.drawText (s.name, area, Justification::centredLeft, true);

        const auto st = starArea (width).toFloat().withSizeKeepingCentre (13.0f, 13.0f);
        const auto p = star (st);

        if (library.isFavourite (s.file))
        {
            g.setColour (theme::text);
            g.fillPath (p);
        }
        else
        {
            g.setColour (theme::faint);
            g.strokePath (p, PathStrokeType (theme::stroke, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    var SampleBrowser::getDragSourceDescription (const SparseSet<int>& selected)
    {
        if (selected.size() == 1)
            if (const auto* s = sampleAt (selected[0]))
                return String (dragPrefix) + s->file.getFullPathName();

        return {};
    }

    String SampleBrowser::getTooltipForRow (int row)
    {
        if (const auto* s = sampleAt (row))
        {
            String tip = s->where + "/" + s->file.getFileName();

            if (const auto a = session.getSampleLibrary().getAnalysis (s->file))
            {
                if (a->key.isNotEmpty()) tip << ". Key " << a->key;
                if (a->bpm) tip << ", " << String (*a->bpm, 1) << " bpm";
            }

            return tip + ". Drag onto a lane to use it.";
        }

        return {};
    }

    void SampleBrowser::paint (Graphics& g)
    {
        g.fillAll (theme::bg2);
        g.setColour (theme::line);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());

        if (session.getSampleLibrary().getRoots().isEmpty())
        {
            // The empty state: one direction, and the button that does it is right above.
            g.setColour (theme::muted);
            g.setFont (theme::uiFont (textSize));
            g.drawFittedText ("Add the folders you keep your samples in. Carthage lists every sound in them, "
                              "folders and all, and leaves the files where they are.",
                              list.getBounds().reduced (pad, pad * 2).withHeight (80), Justification::topLeft, 4);
        }
    }

    void SampleBrowser::resized()
    {
        auto r = getLocalBounds().reduced (pad, 0).withTrimmedRight (1);
        r.removeFromTop (theme::space2);

        auto top = r.removeFromTop (26);
        add.setBounds (top.removeFromRight (add.getBestWidthForHeight (24) + theme::space4).withSizeKeepingCentre (add.getBestWidthForHeight (24) + theme::space4, 24));
        status.setBounds (top);
        r.removeFromTop (theme::space2);

        search.setBounds (r.removeFromTop (30));
        r.removeFromTop (theme::space2);

        auto views = r.removeFromTop (26);
        viewFolders.setBounds (views.removeFromLeft (views.getWidth() / 2 - 2));
        views.removeFromLeft (4);
        viewFavourites.setBounds (views);
        r.removeFromTop (theme::space2);

        auto bottom = getLocalBounds().withTrimmedRight (1).removeFromBottom (36).reduced (pad, 0);
        volumeLabel.setBounds (bottom.removeFromLeft (56));
        volume.setBounds (bottom);

        list.setBounds (getLocalBounds().withTrimmedTop (r.getY()).withTrimmedRight (1).withTrimmedBottom (36));
        list.setVisible (! session.getSampleLibrary().getRoots().isEmpty());
    }
}
