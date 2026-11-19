#include "SettingsTest.h"
#include "Log.h"
#include "Session.h"

namespace carthage::engine
{
    SettingsTest::SettingsTest (Session& s) : session (s) {}

    SettingsTest::~SettingsTest()
    {
        if (running)
            finish (true);
    }

    SettingsTest::Recommendation SettingsTest::recommend (std::vector<Step> measured, double currentRate, int cores)
    {
        Recommendation r;
        r.steps = std::move (measured);
        r.sampleRate = currentRate;
        r.parallelChecks = std::clamp (cores / 2, 1, 8);   // half the cores: the computer stays usable meanwhile

        for (const auto& step : r.steps)
        {
            if (step.clean)
            {
                // The smallest clean size, but not below what the processor can carry once plugins
                // run: an empty project runs clean almost anywhere, an amp simulator does not.
                const auto floor = cores <= 4 ? 256 : cores <= 8 ? 128 : 64;
                r.bufferSize = std::max (floor, step.bufferSize);
                r.valid = true;
                break;
            }
        }

        if (! r.valid)
        {
            r.reason = "No buffer size ran without dropouts. Check the interface's connection and driver.";
            return r;
        }

        // Above 48 kHz with a large buffer the computer is working hard for little gain: 48 kHz
        // halves the work of 96 kHz and is what most music is made at.
        if (currentRate > 48000.0 && r.bufferSize >= 256)
            r.sampleRate = 48000.0;

        r.reason = "Each size ran for " + juce::String (secondsPerStep, 0) + " s; clean means no dropouts and the audio thread's worst block under "
                   + juce::String (juce::roundToInt (headroomLoad * 100.0f)) + " % of its time.";
        return r;
    }

    void SettingsTest::start (std::function<void (const juce::String&)> progress, std::function<void (Recommendation)> done)
    {
        onProgress = std::move (progress);
        onDone = std::move (done);
        steps.clear();

        auto& adm = session.getDeviceManager();
        auto* device = adm.getCurrentAudioDevice();

        if (device == nullptr || session.isRecording())
        {
            Recommendation none;
            none.reason = device == nullptr ? "No audio interface is open." : "Stop recording first.";
            if (onDone) onDone (none);
            return;
        }

        original = adm.getAudioDeviceSetup();
        rate = device->getCurrentSampleRate();
        sizes.clearQuick();

        for (auto size : device->getAvailableBufferSizes())
            if (size >= 32 && size <= 1024)
                sizes.add (size);

        if (sizes.isEmpty())
            sizes.add (device->getCurrentBufferSizeSamples());

        // Silent: the input is not passed to the outputs while the test runs.
        wasMonitoring = session.isMonitoring();
        session.setMonitoring (false);

        running = true;
        index = 0;
        log::info ("Settings test: " + juce::String (sizes.size()) + " buffer sizes at " + juce::String (rate, 0) + " Hz");
        beginStep();
    }

    void SettingsTest::beginStep()
    {
        if (session.isRecording())
        {
            finish (true);   // a take started: the device is left alone from here
            return;
        }

        auto& adm = session.getDeviceManager();
        auto setup = adm.getAudioDeviceSetup();
        setup.bufferSize = sizes[index];

        if (auto err = adm.setAudioDeviceSetup (setup, true); err.isNotEmpty())
        {
            steps.push_back ({ sizes[index], -1, 0.0f, false });
            ++index;

            if (index < sizes.size()) beginStep(); else finish (false);
            return;
        }

        auto* device = adm.getCurrentAudioDevice();
        auto& callback = session.getCallback();
        callback.takeTestPeak();
        xrunsAtStart = device != nullptr ? device->getXRunCount() : 0;
        lateAtStart = callback.getLateCallbackCount();
        stepStarted = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        if (onProgress)
            onProgress ("Trying " + juce::String (sizes[index]) + " samples (" + juce::String (index + 1) + " of " + juce::String (sizes.size()) + ")");

        startTimer (100);
    }

    void SettingsTest::timerCallback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;

        if (session.isRecording())
        {
            finish (true);
            return;
        }

        if (now - stepStarted < secondsPerStep)
            return;

        stopTimer();
        auto* device = session.getDeviceManager().getCurrentAudioDevice();
        auto& callback = session.getCallback();

        Step step;
        step.bufferSize = sizes[index];
        const auto xruns = device != nullptr ? std::max (0, device->getXRunCount() - xrunsAtStart) : 0;
        step.dropouts = xruns + std::max (0, callback.getLateCallbackCount() - lateAtStart);
        step.worstLoad = callback.takeTestPeak();
        step.clean = step.dropouts == 0 && step.worstLoad < headroomLoad;
        steps.push_back (step);
        log::info ("Settings test: " + juce::String (step.bufferSize) + " samples, " + juce::String (step.dropouts) + " dropouts, worst block "
                   + juce::String (juce::roundToInt (step.worstLoad * 100.0f)) + " %");

        ++index;

        // Once a size ran clean, the larger ones would too: stop there.
        if (step.clean || index >= sizes.size())
            finish (false);
        else
            beginStep();
    }

    void SettingsTest::cancel()
    {
        if (running)
            finish (true);
    }

    void SettingsTest::finish (bool cancelled)
    {
        stopTimer();
        running = false;

        // The setup as it was before the test; the person chooses what to change. Never under a
        // take: then the device stays as it is (log says so).
        if (session.isRecording())
            log::warning ("Settings test stopped by a recording; the buffer size stays at " + juce::String (sizes[std::min (index, sizes.size() - 1)]));
        else
            session.getDeviceManager().setAudioDeviceSetup (original, true);

        session.setMonitoring (wasMonitoring);

        auto r = recommend (steps, rate, juce::SystemStats::getNumCpus());

        if (cancelled)
        {
            r.valid = false;
            r.reason = "The test was stopped.";
        }

        if (onDone)
            onDone (r);
    }
}
