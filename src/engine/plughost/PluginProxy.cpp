#include "PluginProxy.h"

#include "Protocol.h"

#include <engine/Log.h>

namespace carthage::engine::plughost
{
    /** One of the plugin's parameters, whichever side the plugin is on. Its fixed facts come from
        the plugin's description; its value from the plugin here, or from the last value sent to
        or reported by the host. */
    class PluginProxy::Parameter final : public juce::AudioPluginInstance::HostedParameter
    {
    public:
        Parameter (PluginProxy& o, int i, const juce::ValueTree& info)
            : owner (o), index (i), id (info[msg::paramID].toString()), name (info[msg::name].toString()),
              label (info[msg::label].toString()), defaultValue (info[msg::defaultValue]), steps (info[msg::steps]),
              discrete (info[msg::discrete]), boolean (info[msg::boolean]), automatable (info[msg::automatable]),
              cached (static_cast<float> (info[msg::value]))
        {}

        float getValue() const override              { return owner.valueOf (index); }
        void setValue (float v) override             { owner.setValueOf (index, v); }
        float getDefaultValue() const override       { return defaultValue; }
        juce::String getName (int maxLength) const override   { return name.substring (0, maxLength); }
        juce::String getLabel() const override       { return label; }
        int getNumSteps() const override             { return steps > 0 ? steps : AudioProcessorParameter::getNumSteps(); }
        bool isDiscrete() const override             { return discrete; }
        bool isBoolean() const override              { return boolean; }
        bool isAutomatable() const override          { return automatable; }
        juce::String getText (float v, int maxLength) const override   { return owner.textOf (index, v, maxLength); }
        float getValueForText (const juce::String& text) const override   { return owner.valueForText (index, text); }
        juce::String getParameterID() const override { return id; }

        std::atomic<float>& value() noexcept   { return cached; }

    private:
        PluginProxy& owner;
        const int index;
        const juce::String id, name, label;
        const float defaultValue;
        const int steps;
        const bool discrete, boolean, automatable;
        std::atomic<float> cached;
    };

    namespace
    {
        juce::AudioChannelSet setFor (int channels)
        {
            const auto canonical = juce::AudioChannelSet::canonicalChannelSet (channels);
            return canonical.size() == channels ? canonical : juce::AudioChannelSet::discreteChannels (channels);
        }

        /** Counts a parameter call or block as using the plugin, for the length of a scope. */
        struct InUse
        {
            explicit InUse (std::atomic<int>& c) noexcept : count (c)   { count.fetch_add (1); }
            ~InUse() noexcept                                           { count.fetch_sub (1); }
            InUse (const InUse&) = delete;
            InUse& operator= (const InUse&) = delete;
            std::atomic<int>& count;
        };
    }

    juce::AudioProcessor::BusesProperties PluginProxy::busesFrom (const juce::ValueTree& info)
    {
        BusesProperties buses;

        for (const auto& b : info)
            if (b.hasType (msg::bus))
                buses.addBus (static_cast<bool> (b[msg::input]), b[msg::name].toString(),
                              setFor (static_cast<int> (b[msg::channels])), static_cast<bool> (b[msg::enabled]));

        return buses;
    }

    std::unique_ptr<PluginProxy> PluginProxy::create (const juce::PluginDescription& d, double sampleRate, int samplesPerBlock,
                                                      HostProcess* host, MakeLocal makeLocal, juce::String& error)
    {
        std::unique_ptr<RemotePlugin> remote;
        std::unique_ptr<juce::AudioPluginInstance> local;
        juce::ValueTree info;

        if (host != nullptr && (host->isRunning() || host->start().wasOk()))
        {
            remote = host->load (d, sampleRate, samplesPerBlock, error);

            if (remote != nullptr)
                info = remote->getInfo();
        }

        if (remote == nullptr)
        {
            juce::String localError;
            local = makeLocal != nullptr ? makeLocal (d, sampleRate, samplesPerBlock, localError) : nullptr;

            if (local == nullptr)
            {
                error = error.isNotEmpty() ? error : localError;
                return {};
            }

            local->enableAllBuses();   // as Tracktion does, and as the host does
            info = juce::ValueTree (msg::reply);
            describe (*local, info);
        }

        auto proxy = std::make_unique<PluginProxy> (d, busesFrom (info), info, host, std::move (makeLocal));
        proxy->rate = sampleRate;
        proxy->block = samplesPerBlock;

        if (remote != nullptr)
        {
            proxy->remote = std::move (remote);
            proxy->inHost.store (true);
        }
        else
        {
            proxy->adopt (std::move (local));
        }

        proxy->updateLatency();
        return proxy;
    }

    PluginProxy::PluginProxy (const juce::PluginDescription& d, const BusesProperties& buses, const juce::ValueTree& info,
                              HostProcess* h, MakeLocal m)
        : juce::AudioPluginInstance (buses), description (d), host (h), makeLocal (std::move (m)),
          midiIn (info[msg::acceptsMidi]), midiOut (info[msg::producesMidi]), tail (info[msg::tail])
    {
        int index = 0;

        for (const auto& c : info)
            if (c.hasType (msg::parameter) && index < maxLinkParameters)
            {
                auto p = std::make_unique<Parameter> (*this, index++, c);
                parameters.add (p.get());
                addHostedParameter (std::move (p));
            }
    }

    PluginProxy::~PluginProxy()
    {
        alive->store (false);

        if (local != nullptr)
            local->removeListener (this);
    }

    void PluginProxy::adopt (std::unique_ptr<juce::AudioPluginInstance> instance)
    {
        local = std::move (instance);
        local->addListener (this);
        local->setNonRealtime (isNonRealtime());
    }

    void PluginProxy::waitForIdle() const noexcept
    {
        while (inFlight.load() != 0)
            juce::Thread::yield();
    }

    void PluginProxy::updateLatency()
    {
        const auto latency = inHost.load() ? (remote != nullptr ? remote->latency() : 0)
                                           : (local != nullptr ? local->getLatencySamples() : 0);

        if (latency != getLatencySamples())
            setLatencySamples (latency);
    }

    void PluginProxy::refreshValues (const juce::Array<float>& values)
    {
        for (int i = 0; i < parameters.size() && i < values.size(); ++i)
        {
            auto& cached = parameters.getUnchecked (i)->value();

            if (cached.exchange (values.getUnchecked (i)) != values.getUnchecked (i))
                parameters.getUnchecked (i)->sendValueChangedMessageToListeners (values.getUnchecked (i));
        }
    }

    //==============================================================================================
    bool PluginProxy::moveToHost (bool toHost, juce::String& error)
    {
        if (toHost == inHost.load())
            return true;

        if (toHost)
        {
            if (host == nullptr || (! host->isRunning() && host->start().failed()))
            {
                error = "the plugin's process could not start";
                return false;
            }

            juce::MemoryBlock state;
            local->getStateInformation (state);
            auto moved = host->load (description, rate, block, error);

            if (moved == nullptr || ! moved->setState (state))
            {
                error = error.isNotEmpty() ? error : description.name + " did not take its settings in its process";
                return false;
            }

            // Values the state did not carry go over one by one.
            const auto values = moved->lastValues();

            for (int i = 0; i < parameters.size() && i < values.size(); ++i)
                if (const auto v = local->getParameters()[i]->getValue(); v != values.getUnchecked (i))
                    moved->setParameter (i, v);

            for (int i = 0; i < parameters.size(); ++i)
                parameters.getUnchecked (i)->value().store (local->getParameters()[i]->getValue());

            remote = std::move (moved);
            lastState = std::move (state);
            inHost.store (true);
            waitForIdle();   // no block or parameter call is still on the plugin here
            local->removeListener (this);
            local.reset();
        }
        else
        {
            if (makeLocal == nullptr)
            {
                error = "this plugin runs only in its own process";
                return false;
            }

            auto state = remote != nullptr && remote->isAlive() ? remote->getState() : std::nullopt;
            auto made = makeLocal (description, rate, block, error);

            if (made == nullptr)
                return false;

            made->enableAllBuses();
            made->setRateAndBufferSizeDetails (rate, block);

            if (prepared)
                made->prepareToPlay (rate, block);

            const auto& settings = state.has_value() ? *state : lastState;

            if (settings.getSize() > 0)
                made->setStateInformation (settings.getData(), static_cast<int> (settings.getSize()));

            // Values the state did not carry: a VST3 takes a changed setting into its state only
            // while it processes audio (W64), so a host that has not played since lacks it.
            for (int i = 0; i < parameters.size() && i < made->getParameters().size(); ++i)
                if (const auto v = parameters.getUnchecked (i)->value().load(); made->getParameters()[i]->getValue() != v)
                    made->getParameters()[i]->setValue (v);

            adopt (std::move (made));
            inHost.store (false);
            waitForIdle();
            remote.reset();

            juce::Array<float> values;

            for (auto* p : local->getParameters())
                values.add (p->getValue());

            refreshValues (values);
        }

        updateLatency();
        return true;
    }

    //==============================================================================================
    void PluginProxy::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        rate = sampleRate;
        block = samplesPerBlock;
        prepared = true;

        if (local != nullptr)
        {
            local->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
            local->prepareToPlay (sampleRate, samplesPerBlock);
        }

        // The hop's latency is the block size the link was made for: a new one for another.
        if (remote != nullptr && (remote->sampleRate() != sampleRate || remote->linkLatency() != samplesPerBlock))
        {
            juce::String error;

            if (! remote->relink (sampleRate, samplesPerBlock, error))
                log::warning (description.name + " kept its old link to its process: " + error);
        }

        updateLatency();
    }

    void PluginProxy::releaseResources()
    {
        prepared = false;

        if (local != nullptr)
            local->releaseResources();
    }

    void PluginProxy::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        const InUse using_ (inFlight);

        if (inHost.load())
        {
            if (remote == nullptr)
                buffer.clear();
            else if (isNonRealtime())
                remote->processWaiting (buffer, midi, 10000);
            else
                remote->process (buffer, midi);

            midi.clear();   // a plugin in its own process sends no MIDI back yet
            return;
        }

        if (local != nullptr)
            local->processBlock (buffer, midi);
        else
            buffer.clear();
    }

    void PluginProxy::setNonRealtime (bool offline) noexcept
    {
        AudioPluginInstance::setNonRealtime (offline);

        if (local != nullptr)
            local->setNonRealtime (offline);
    }

    double PluginProxy::getTailLengthSeconds() const
    {
        return ! inHost.load() && local != nullptr ? local->getTailLengthSeconds() : tail;
    }

    bool PluginProxy::isBusesLayoutSupported (const BusesLayout& layout) const
    {
        return layout == getBusesLayout();   // the plugin's own layout, as it loaded
    }

    juce::AudioProcessorEditor* PluginProxy::createEditor()
    {
        // A plugin in its own process shows its window from there (5f-3).
        return ! inHost.load() && local != nullptr ? local->createEditorAndMakeActive() : nullptr;
    }

    bool PluginProxy::hasEditor() const
    {
        return ! inHost.load() && local != nullptr && local->hasEditor();
    }

    int PluginProxy::getNumPrograms()                          { return ! inHost.load() && local != nullptr ? local->getNumPrograms() : 1; }
    int PluginProxy::getCurrentProgram()                       { return ! inHost.load() && local != nullptr ? local->getCurrentProgram() : 0; }
    const juce::String PluginProxy::getProgramName (int i)     { return ! inHost.load() && local != nullptr ? local->getProgramName (i) : juce::String(); }

    void PluginProxy::setCurrentProgram (int i)
    {
        if (! inHost.load() && local != nullptr)
            local->setCurrentProgram (i);
    }

    void PluginProxy::getStateInformation (juce::MemoryBlock& data)
    {
        if (! inHost.load() && local != nullptr)
            local->getStateInformation (data);
        else if (auto state = remote != nullptr ? remote->getState() : std::nullopt)
            data = std::move (*state);
        else
            data = lastState;   // the host has gone: what it last had

        lastState = data;
    }

    void PluginProxy::setStateInformation (const void* data, int size)
    {
        lastState.replaceAll (data, static_cast<size_t> (juce::jmax (0, size)));

        if (! inHost.load() && local != nullptr)
        {
            local->setStateInformation (data, size);
        }
        else if (remote != nullptr && remote->setState (lastState))
        {
            refreshValues (remote->lastValues());
            updateLatency();
        }
    }

    juce::AudioPluginExtensions::VST3Client* PluginProxy::getVST3Client()
    {
        return ! inHost.load() && local != nullptr ? local->getVST3Client() : nullptr;
    }

    juce::AudioPluginExtensions::AudioUnitClient* PluginProxy::getAudioUnitClient()
    {
        return ! inHost.load() && local != nullptr ? local->getAudioUnitClient() : nullptr;
    }

    //==============================================================================================
    float PluginProxy::valueOf (int index) const noexcept
    {
        if (! inHost.load())
        {
            const InUse using_ (const_cast<std::atomic<int>&> (inFlight));

            if (local != nullptr && juce::isPositiveAndBelow (index, local->getParameters().size()))
                return local->getParameters().getUnchecked (index)->getValue();
        }

        return parameters.getUnchecked (index)->value().load();
    }

    void PluginProxy::setValueOf (int index, float value) noexcept
    {
        const InUse using_ (inFlight);
        parameters.getUnchecked (index)->value().store (value);

        if (inHost.load())
        {
            if (remote != nullptr)
                remote->setParameter (index, value);
        }
        else if (local != nullptr && juce::isPositiveAndBelow (index, local->getParameters().size()))
        {
            local->getParameters().getUnchecked (index)->setValue (value);
        }
    }

    juce::String PluginProxy::textOf (int index, float value, int maxLength) const
    {
        if (! inHost.load() && local != nullptr && juce::isPositiveAndBelow (index, local->getParameters().size()))
            return local->getParameters().getUnchecked (index)->getText (value, maxLength);

        return juce::String (value, 3).substring (0, maxLength);   // the plugin's own words come with 5f-3
    }

    float PluginProxy::valueForText (int index, const juce::String& text) const
    {
        if (! inHost.load() && local != nullptr && juce::isPositiveAndBelow (index, local->getParameters().size()))
            return local->getParameters().getUnchecked (index)->getValueForText (text);

        return juce::jlimit (0.0f, 1.0f, text.getFloatValue());
    }

    //==============================================================================================
    // The plugin here changed something itself (its window, its own automation): pass it on to
    // whoever listens to the proxy (Tracktion).
    void PluginProxy::audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value)
    {
        if (juce::isPositiveAndBelow (index, parameters.size()))
        {
            parameters.getUnchecked (index)->value().store (value);
            parameters.getUnchecked (index)->sendValueChangedMessageToListeners (value);
        }
    }

    void PluginProxy::audioProcessorChanged (juce::AudioProcessor*, const juce::AudioProcessorListener::ChangeDetails& details)
    {
        juce::MessageManager::callAsync ([this, stillHere = alive]
        {
            if (stillHere->load())
                updateLatency();
        });

        updateHostDisplay (details);
    }

    void PluginProxy::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index)
    {
        if (juce::isPositiveAndBelow (index, parameters.size()))
            parameters.getUnchecked (index)->beginChangeGesture();
    }

    void PluginProxy::audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index)
    {
        if (juce::isPositiveAndBelow (index, parameters.size()))
            parameters.getUnchecked (index)->endChangeGesture();
    }
}
