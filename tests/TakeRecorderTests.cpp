#include <engine/TakeRecorder.h>

#include <thread>

using carthage::engine::TakeRecorder;

class TakeRecorderTests final : public juce::UnitTest
{
public:
    TakeRecorderTests() : juce::UnitTest ("Take recorder", "engine") {}

    void runTest() override
    {
        const auto folder = juce::File::createTempFile ("carthage-recorder-test");
        folder.createDirectory();

        beginTest ("A take written from another thread reads back sample for sample");
        {
            // 32-bit float is lossless, so the file must equal what the "audio thread" pushed.
            constexpr double rate = 48000.0;
            constexpr int block = 64, blocks = 3000;   // 4 s, many times the writer's poll interval

            TakeRecorder recorder;
            recorder.prepare (2, static_cast<int> (rate));   // 1 s FIFO
            const auto file = folder.getChildFile ("float.wav");
            expect (recorder.start (file, { rate, 2, TakeRecorder::SampleFormat::float32 }).wasOk());

            juce::AudioBuffer<float> expected (2, block * blocks);
            juce::Random rng (42);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < expected.getNumSamples(); ++i)
                    expected.setSample (ch, i, rng.nextFloat() * 2.0f - 1.0f);

            std::thread audio ([&]
            {
                for (int b = 0; b < blocks; ++b)
                {
                    const float* chans[] = { expected.getReadPointer (0, b * block), expected.getReadPointer (1, b * block) };
                    { const carthage::rt::ScopedRealtime realtime; recorder.pushBlock (chans, 2, block, 10.0 + b * block / rate); }   // checked in the rtsan build
                    std::this_thread::sleep_for (std::chrono::microseconds (100));   // faster than real time
                }
            });
            audio.join();

            const auto result = recorder.stop();
            expect (result.succeeded(), result.error);
            expectEquals (static_cast<int> (result.framesWritten), block * blocks);
            expectEquals (static_cast<int> (result.framesDropped), 0);
            expectEquals (result.overflows, 0);
            expectWithinAbsoluteError (result.firstBlockStreamTime, 10.0, 1.0e-12);

            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            expect (reader != nullptr);

            if (reader != nullptr)
            {
                expectEquals (static_cast<int> (reader->lengthInSamples), block * blocks);
                expectEquals (static_cast<int> (reader->numChannels), 2);
                juce::AudioBuffer<float> read (2, block * blocks);
                reader->read (&read, 0, block * blocks, 0, true, true);

                int mismatches = 0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < read.getNumSamples(); ++i)
                        mismatches += read.getSample (ch, i) != expected.getSample (ch, i) ? 1 : 0;

                expectEquals (mismatches, 0);
            }
        }

        beginTest ("A full FIFO drops whole blocks and counts them, never blocks");
        {
            TakeRecorder recorder;
            recorder.prepare (1, 100);
            expect (recorder.start (folder.getChildFile ("overflow.wav"), { 48000.0, 1, TakeRecorder::SampleFormat::int24 }).wasOk());

            // No sleeping: push far faster than the writer drains, so the FIFO must overflow.
            std::vector<float> data (80, 0.25f);
            const float* chans[] = { data.data() };
            int pushes = 0;

            while (recorder.getOverflowCount() == 0 && pushes < 100000)
            {
                { const carthage::rt::ScopedRealtime realtime; recorder.pushBlock (chans, 1, 80, 0.0); }   // checked in the rtsan build
                ++pushes;
            }

            const auto result = recorder.stop();
            expectGreaterThan (result.overflows, 0);
            expectEquals (static_cast<int> (result.framesDropped), result.overflows * 80);
            expectEquals (static_cast<int> (result.framesWritten + result.framesDropped), pushes * 80);
        }

        beginTest ("Stopping before any audio arrives leaves an empty take, not a hang");
        {
            TakeRecorder recorder;
            recorder.prepare (1, 1024);
            expect (recorder.start (folder.getChildFile ("empty.wav"), { 44100.0, 1, TakeRecorder::SampleFormat::int16 }).wasOk());
            const auto result = recorder.stop();
            expect (result.succeeded());
            expectEquals (static_cast<int> (result.framesWritten), 0);
            expect (! recorder.isRecording());
        }

        beginTest ("Bit depths map to the WAV formats the settings offer");
        {
            expectEquals (TakeRecorder::bitsFor (TakeRecorder::SampleFormat::int16), 16);
            expectEquals (TakeRecorder::bitsFor (TakeRecorder::SampleFormat::int24), 24);
            expectEquals (TakeRecorder::bitsFor (TakeRecorder::SampleFormat::float32), 32);
        }

        folder.deleteRecursively();
    }
};

static TakeRecorderTests takeRecorderTests;
