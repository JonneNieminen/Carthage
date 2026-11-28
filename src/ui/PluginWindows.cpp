#include "PluginWindows.h"
#include "theme/Theme.h"

#include <engine/plughost/PluginProxy.h>

namespace carthage::ui
{
    using namespace juce;

    //==============================================================================================
    /** One plugin's window: its own editor, or JUCE's generic parameter view for a plugin without
        one. It keeps the plugin alive while open. */
    class PluginWindows::Window final : public DocumentWindow
    {
    public:
        Window (tracktion::Plugin& p, AudioPluginInstance& instance, std::function<void()> closed)
            : DocumentWindow (p.getName(), theme::bg, DocumentWindow::closeButton), plugin (&p), onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            AudioProcessorEditor* editor = instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;

            if (editor == nullptr)
                editor = new GenericAudioProcessorEditor (instance);   // owned by the window below

            // The window takes the editor's size, and follows it when the editor resizes itself
            // (many plugins size their editors only once shown); never forced to a size of ours.
            setContentOwned (editor, true);
            setResizable (editor->isResizable(), false);
            setTopLeftPosition (Desktop::getInstance().getDisplays().getPrimaryDisplay()->userBounds.toNearestInt().getCentre()
                                - Point<int> (getWidth() / 2, getHeight() / 2));
            setVisible (true);
        }

        void closeButtonPressed() override   { if (onClosed) onClosed(); }

        /** At the plugin's own size, whatever Carthage's interface size (W38): Carthage scales its
            windows by a global factor, which native plugin editors do not follow, so at 125 %
            they came out clipped or cramped (found by the computer-use test, 2026-11-20). JUCE's
            own plugin host does the same. */
        float getDesktopScaleFactor() const override   { return 1.0f; }

        tracktion::Plugin::Ptr plugin;

    private:
        std::function<void()> onClosed;
    };

    //==============================================================================================
    PluginWindows::PluginWindows() = default;
    PluginWindows::~PluginWindows() = default;

    void PluginWindows::open (tracktion::Plugin& plugin)
    {
        if (const auto found = windows.find (plugin.itemID); found != windows.end())
        {
            found->second->toFront (true);
            return;
        }

        if (openElsewhere (plugin))
            return;

        auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (&plugin);
        auto* instance = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;

        if (instance == nullptr)
            return;

        const auto id = plugin.itemID;
        auto window = std::make_unique<Window> (plugin, *instance, [this, id, token = std::weak_ptr<int> (alive)]
        {
            // After the click that asked for it: the window can not delete itself mid-callback.
            MessageManager::callAsync ([this, id, token]
            {
                if (token.lock())
                {
                    windows.erase (id);
                    closed();
                }
            });
        });

        window->setAlwaysOnTop (keepOnTop == nullptr || keepOnTop());
        windows[id] = std::move (window);
    }

    namespace
    {
        engine::plughost::PluginProxy* proxyOf (tracktion::Plugin& plugin)
        {
            auto* ext = dynamic_cast<tracktion::ExternalPlugin*> (&plugin);
            return ext != nullptr ? dynamic_cast<engine::plughost::PluginProxy*> (ext->getAudioPluginInstance()) : nullptr;
        }
    }

    bool PluginWindows::openElsewhere (tracktion::Plugin& plugin)
    {
        auto* proxy = proxyOf (plugin);

        if (proxy == nullptr || ! proxy->isInHost())
            return false;

        const auto id = plugin.itemID;
        proxy->onWindowClosed = [this, id, token = std::weak_ptr<int> (alive)] (bool reopenHere)
        {
            if (! token.lock())
                return;

            const auto found = elsewhere.find (id);

            if (found == elsewhere.end())
                return;

            const tracktion::Plugin::Ptr p = found->second;
            elsewhere.erase (found);

            if (reopenHere)
                open (*p);   // it moved into Carthage: its window opens here
            else
                closed();
        };
        proxy->onWindowKey = [this] (const KeyPress& key) { if (onKey) onKey (key); };

        if (! proxy->openWindowInHost (plugin.getName(), theme::bg, keepOnTop == nullptr || keepOnTop()))
            return false;   // the process could not show it: Carthage shows what it can

        elsewhere[id] = &plugin;
        return true;
    }

    void PluginWindows::closeElsewhere (const tracktion::EditItemID& id)
    {
        if (const auto found = elsewhere.find (id); found != elsewhere.end())
        {
            if (auto* proxy = proxyOf (*found->second))
            {
                proxy->onWindowClosed = nullptr;
                proxy->closeWindowInHost();
            }

            elsewhere.erase (found);
        }
    }

    void PluginWindows::closed()
    {
        if (onClosed)
            onClosed();
    }

    void PluginWindows::close (const tracktion::Plugin& plugin)
    {
        windows.erase (plugin.itemID);
        closeElsewhere (plugin.itemID);
    }

    bool PluginWindows::isOpen (const tracktion::Plugin& plugin) const
    {
        return windows.contains (plugin.itemID) || elsewhere.contains (plugin.itemID);
    }

    void PluginWindows::closeAll()
    {
        windows.clear();

        while (! elsewhere.empty())
            closeElsewhere (elsewhere.begin()->first);
    }

    void PluginWindows::closeMissing()
    {
        std::erase_if (windows, [] (const auto& entry) { return entry.second->plugin->getOwnerList() == nullptr; });

        std::vector<tracktion::EditItemID> gone;

        for (const auto& [id, p] : elsewhere)
            if (p->getOwnerList() == nullptr)
                gone.push_back (id);

        for (const auto& id : gone)
            closeElsewhere (id);
    }

    bool PluginWindows::closeIfFront (const Component* front)
    {
        for (auto it = windows.begin(); it != windows.end(); ++it)
            if (static_cast<const Component*> (it->second.get()) == front)
            {
                windows.erase (it);
                closed();
                return true;
            }

        return false;
    }

    void PluginWindows::setAlwaysOnTop (bool on)
    {
        for (auto& [id, window] : windows)
            window->setAlwaysOnTop (on);
    }

    //==============================================================================================
    PluginChooser::PluginChooser (engine::PluginLibrary& lib, std::function<void (const PluginDescription&)> chosen)
        : library (lib), onChosen (std::move (chosen))
    {
        search.setTextToShowWhenEmpty ("Search effects", theme::muted);
        search.setFont (theme::uiFont());
        search.onTextChange = [this] { refresh(); };
        search.onReturnKey = [this] { choose (std::max (0, list.getSelectedRow())); };
        addAndMakeVisible (search);

        list.setRowHeight (26);
        list.setColour (ListBox::backgroundColourId, theme::bg2);
        addAndMakeVisible (list);

        setSize (320, 380);
        refresh();
    }

    void PluginChooser::refresh()
    {
        engine::PluginLibrary::Filter f;
        f.query = search.getText();
        f.kind = engine::PluginLibrary::Kind::effects;   // instruments come with the piano roll (phase 6)
        shown = library.find (f);
        list.updateContent();
        list.selectRow (0);
        list.repaint();
    }

    void PluginChooser::choose (int row)
    {
        if (row < 0 || row >= static_cast<int> (shown.size()))
            return;

        const auto description = library.getEntries()[shown[static_cast<size_t> (row)]].description;

        if (auto* box = findParentComponentOfClass<CallOutBox>())
            box->dismiss();

        if (onChosen)
            onChosen (description);
    }

    void PluginChooser::paintListBoxItem (int row, Graphics& g, int width, int height, bool selected)
    {
        if (row < 0 || row >= static_cast<int> (shown.size()))
            return;

        const auto& d = library.getEntries()[shown[static_cast<size_t> (row)]].description;

        if (selected)
        {
            g.setColour (theme::hover);
            g.fillRect (0, 0, width, height);
        }

        g.setColour (theme::text);
        g.setFont (theme::uiFont());
        g.drawText (d.name, theme::space2, 0, width - 160, height, Justification::centredLeft, true);
        g.setColour (theme::muted);
        g.setFont (theme::uiFont (theme::textSm));
        const auto format = d.pluginFormatName == "AudioUnit" ? String ("AU") : d.pluginFormatName;   // two of one plugin: which is which
        g.drawText (d.manufacturerName + String::fromUTF8 (" \xc2\xb7 ") + format, width - 156, 0, 152, height, Justification::centredRight, true);
    }

    void PluginChooser::resized()
    {
        auto r = getLocalBounds().reduced (theme::space2);
        search.setBounds (r.removeFromTop (28));
        r.removeFromTop (theme::space2);
        list.setBounds (r);
    }
}
