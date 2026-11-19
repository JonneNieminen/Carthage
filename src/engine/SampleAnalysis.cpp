#include "SampleAnalysis.h"
#include "Trace.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <numeric>
#include <cmath>

namespace carthage::engine
{
    namespace
    {
        const std::array<const char*, 12> noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

        // Krumhansl-Kessler key profiles, from the tonic upwards.
        constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        double correlation (const std::array<double, 12>& a, const std::array<double, 12>& profile, int tonic)
        {
            double ma = 0.0, mb = 0.0;

            for (int i = 0; i < 12; ++i)
            {
                ma += a[static_cast<size_t> (i)];
                mb += profile[static_cast<size_t> (i)];
            }

            ma /= 12.0;
            mb /= 12.0;
            double num = 0.0, da = 0.0, db = 0.0;

            for (int i = 0; i < 12; ++i)
            {
                const auto x = a[static_cast<size_t> ((i + tonic) % 12)] - ma;
                const auto y = profile[static_cast<size_t> (i)] - mb;
                num += x * y;
                da += x * x;
                db += y * y;
            }

            return da > 0.0 && db > 0.0 ? num / std::sqrt (da * db) : 0.0;
        }

        /** Magnitude spectra of successive Hann-windowed frames. */
        template <typename Visit>
        void spectra (const float* samples, int numSamples, int order, int hop, Visit&& visit)
        {
            const int size = 1 << order;
            juce::dsp::FFT fft (order);
            juce::dsp::WindowingFunction<float> window (static_cast<size_t> (size), juce::dsp::WindowingFunction<float>::hann, false);
            std::vector<float> frame (static_cast<size_t> (size) * 2);

            for (int start = 0; start + size <= numSamples; start += hop)
            {
                std::fill (frame.begin(), frame.end(), 0.0f);
                std::copy (samples + start, samples + start + size, frame.begin());
                window.multiplyWithWindowingTable (frame.data(), static_cast<size_t> (size));
                fft.performFrequencyOnlyForwardTransform (frame.data(), true);
                visit (frame.data(), size / 2);
            }
        }

        constexpr double beatClarity = 0.35;

        std::optional<double> tempoOf (const float* samples, int numSamples, double rate)
        {
            const auto seconds = numSamples / rate;

            if (seconds < 1.5)
                return std::nullopt;   // a one-shot

            // Onset strength: how much the (compressed) spectrum rises from one frame to the next.
            const int hop = std::max (64, static_cast<int> (std::round (rate * 0.0058)));   // ~172 frames a second
            const auto fps = rate / hop;
            const auto topBin = static_cast<int> (8000.0 / rate * 1024.0);
            std::vector<float> previous (512, 0.0f);
            std::vector<double> envelope;

            spectra (samples, numSamples, 10, hop, [&] (const float* mag, int bins)
            {
                double flux = 0.0;

                for (int b = 1; b < std::min (bins, topBin); ++b)
                {
                    const auto v = std::log1p (1000.0f * mag[b]);
                    flux += std::max (0.0f, v - previous[static_cast<size_t> (b)]);
                    previous[static_cast<size_t> (b)] = v;
                }

                envelope.push_back (flux);
            });

            if (envelope.size() < 64)
                return std::nullopt;

            // Keep the peaks: subtract a half-second moving average, drop what is below it.
            const auto span = static_cast<int> (fps * 0.25);
            std::vector<double> onset (envelope.size(), 0.0);

            for (int i = 0; i < static_cast<int> (envelope.size()); ++i)
            {
                double sum = 0.0;
                int count = 0;

                for (int j = std::max (0, i - span); j <= std::min (static_cast<int> (envelope.size()) - 1, i + span); ++j, ++count)
                    sum += envelope[static_cast<size_t> (j)];

                onset[static_cast<size_t> (i)] = std::max (0.0, envelope[static_cast<size_t> (i)] - sum / count);
            }

            const auto acf = [&] (int lag)
            {
                double s = 0.0;

                for (size_t i = 0; i + static_cast<size_t> (lag) < onset.size(); ++i)
                    s += onset[i] * onset[i + static_cast<size_t> (lag)];

                return s / static_cast<double> (onset.size() - static_cast<size_t> (lag));
            };

            const auto zero = acf (0);

            if (zero <= 0.0)
                return std::nullopt;

            const auto minLag = static_cast<int> (fps * 60.0 / 200.0);
            const auto maxLag = std::min (static_cast<int> (fps * 60.0 / 60.0), static_cast<int> (onset.size()) / 2);
            int best = -1;
            double bestScore = 0.0, bestValue = 0.0;

            for (int lag = minLag; lag <= maxLag; ++lag)
            {
                const auto value = acf (lag);
                const auto bpm = fps * 60.0 / lag;
                const auto prefer = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0), 2.0));   // around 120, gently

                if (value * prefer > bestScore)
                {
                    bestScore = value * prefer;
                    bestValue = value;
                    best = lag;
                }
            }

            // A beat that is barely there is not reported (held chords that change now and then
            // repeat weakly at many lags).
            if (best < 0 || bestValue / zero < beatClarity)
                return std::nullopt;

            // Clicks on every beat also repeat every two beats: when the faster tempo's repeat is
            // nearly as strong, it is the beat (and the slower one its bar or half bar).
            while (best / 2 >= minLag)
            {
                int halfBest = best / 2;
                double halfValue = acf (halfBest);

                for (int lag = best / 2 - 1; lag <= best / 2 + 1; ++lag)
                    if (lag >= minLag && acf (lag) > halfValue)
                    {
                        halfValue = acf (lag);
                        halfBest = lag;
                    }

                if (halfValue < 0.8 * bestValue)
                    break;

                best = halfBest;
                bestValue = halfValue;
            }

            // Between frames: a parabola through the peak and its neighbours.
            const auto a = acf (best - 1), b = acf (best), c = acf (best + 1);
            const auto shift = (a - 2.0 * b + c) != 0.0 ? 0.5 * (a - c) / (a - 2.0 * b + c) : 0.0;
            auto bpm = fps * 60.0 / (best + std::clamp (shift, -0.5, 0.5));

            // A loop: the tempo that makes it a whole number of bars, if one is that close.
            const auto beats = seconds * bpm / 60.0;

            for (const auto grain : { 4.0, 2.0 })
            {
                const auto whole = std::round (beats / grain) * grain;

                if (whole >= 2.0 && std::abs (whole - beats) / beats < 0.02)
                {
                    bpm = whole * 60.0 / seconds;
                    break;
                }
            }

            return std::round (bpm * 10.0) / 10.0;
        }

        juce::String keyOf (const float* samples, int numSamples, double rate)
        {
            std::array<double, 12> chroma {};
            const int order = rate > 60000.0 ? 14 : 13;
            const int size = 1 << order;

            if (numSamples < size)
                return {};

            spectra (samples, numSamples, order, size / 2, [&] (const float* mag, int bins)
            {
                for (int b = 1; b < bins; ++b)
                {
                    const auto freq = b * rate / size;

                    if (freq < 55.0 || freq > 5000.0)
                        continue;

                    const auto pitch = 69.0 + 12.0 * std::log2 (freq / 440.0);
                    const auto nearest = std::round (pitch);

                    if (std::abs (pitch - nearest) > 0.4)
                        continue;   // between notes: leakage, not a note

                    const auto pc = static_cast<int> (nearest) % 12;
                    chroma[static_cast<size_t> ((pc + 12) % 12)] += static_cast<double> (mag[b]);
                }
            });

            const auto total = std::accumulate (chroma.begin(), chroma.end(), 0.0);
            const auto peak = *std::max_element (chroma.begin(), chroma.end());

            if (total <= 0.0 || peak / (total / 12.0) < 1.6)
                return {};   // flat: noise or drums, not a key

            // A one-shot has a pitch, not a key: its strongest note, when that note clearly leads.
            if (numSamples / rate < 1.5)
            {
                const auto top = static_cast<size_t> (std::distance (chroma.begin(), std::max_element (chroma.begin(), chroma.end())));
                return peak / (total / 12.0) >= 4.0 ? juce::String (noteNames[top]) : juce::String();
            }

            double best = -2.0, second = -2.0;
            juce::String name;

            for (int tonic = 0; tonic < 12; ++tonic)
            {
                for (const auto minor : { false, true })
                {
                    const auto r = correlation (chroma, minor ? minorProfile : majorProfile, tonic);

                    if (r > best)
                    {
                        second = best;
                        best = r;
                        name = juce::String (noteNames[static_cast<size_t> (tonic)]) + (minor ? " minor" : " major");
                    }
                    else if (r > second)
                    {
                        second = r;
                    }
                }
            }

            return best >= 0.6 && best - second >= 0.01 ? name : juce::String();
        }
    }

    namespace analysis
    {
        SampleLibrary::Analysis analyse (const float* samples, int numSamples, double rate)
        {
            SampleLibrary::Analysis a;
            a.seconds = rate > 0.0 ? numSamples / rate : 0.0;

            if (rate <= 0.0 || numSamples <= 0)
                return a;

            a.bpm = tempoOf (samples, numSamples, rate);
            a.key = keyOf (samples, numSamples, rate);
            return a;
        }

        std::optional<SampleLibrary::Analysis> analyseFile (const juce::File& file)
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            const std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

            if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
                return std::nullopt;

            const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (reader->sampleRate * 60.0)));
            const auto channels = static_cast<int> (std::max (1u, reader->numChannels));
            juce::AudioBuffer<float> buffer (channels, length);
            reader->read (&buffer, 0, length, 0, true, true);

            // Mono: the channels averaged.
            for (int ch = 1; ch < channels; ++ch)
                buffer.addFrom (0, 0, buffer, ch, 0, length);

            buffer.applyGain (0, 0, length, 1.0f / static_cast<float> (channels));

            const trace::Scope traced ("Analyse sample");
            auto a = analyse (buffer.getReadPointer (0), length, reader->sampleRate);
            a.seconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;   // the whole file
            return a;
        }
    }

    //==============================================================================================
    SampleAnalyser::SampleAnalyser (SampleLibrary& l) : juce::Thread ("Carthage sample analysis"), library (l)
    {
        startThread (juce::Thread::Priority::background);
    }

    SampleAnalyser::~SampleAnalyser()
    {
        *alive = false;
        signalThreadShouldExit();
        notify();
        stopThread (5000);
    }

    void SampleAnalyser::update()
    {
        juce::Array<juce::File> missing;

        for (const auto& s : library.getSamples())
            if (! library.getAnalysis (s.file).has_value())
                missing.add (s.file);

        {
            const juce::ScopedLock lock (queueLock);
            queue = std::move (missing);
        }

        notify();
    }

    void SampleAnalyser::run()
    {
        while (! threadShouldExit())
        {
            juce::File next;

            {
                const juce::ScopedLock lock (queueLock);

                if (! queue.isEmpty())
                    next = queue.removeAndReturn (0);
            }

            if (next == juce::File())
            {
                wait (-1);
                continue;
            }

            auto result = analysis::analyseFile (next);

            if (! result)
                result = SampleLibrary::Analysis {};   // unreadable: stored as nothing found, so it is not tried again

            juce::MessageManager::callAsync ([this, next, a = *result, token = std::weak_ptr<bool> (alive)]
            {
                if (! token.expired())
                    library.setAnalysis (next, a);
            });
        }
    }
}
