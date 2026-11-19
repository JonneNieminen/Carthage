#include "Log.h"

#include <memory>
#include <mutex>

namespace carthage::log
{
    namespace
    {
        constexpr int maxLines = 4000;

        class FileLog final : public juce::Logger
        {
        public:
            FileLog (juce::File f) : file (std::move (f))
            {
                file.getParentDirectory().createDirectory();
                out = std::make_unique<juce::FileOutputStream> (file);

                if (out->openedOk())
                {
                    out->setPosition (0);
                    out->truncate();
                }
            }

            void logMessage (const juce::String& message) override   { add ("", message); }

            void add (const juce::String& level, const juce::String& message)
            {
                const auto line = juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + level + message.trimEnd();
                const std::scoped_lock lock (mutex);
                lines.add (line);

                if (lines.size() > maxLines)
                    lines.removeRange (0, lines.size() - maxLines);

                if (out != nullptr && out->openedOk())
                {
                    out->writeText (line + "\n", false, false, nullptr);
                    out->flush();   // a crash must not lose the lines before it
                }
            }

            juce::String text() const
            {
                const std::scoped_lock lock (mutex);
                return lines.joinIntoString ("\n");
            }

            const juce::File file;

        private:
            mutable std::mutex mutex;
            juce::StringArray lines;
            std::unique_ptr<juce::FileOutputStream> out;
        };

        std::unique_ptr<FileLog> current;
        juce::File logFolder;

        juce::File crashNoteFile()   { return logFolder.getChildFile ("Carthage crash.txt"); }
    }

    void start (const juce::File& f, const juce::String& firstLine)
    {
        logFolder = f;
        const auto file = f.getChildFile ("Carthage.log");

        if (file.existsAsFile())
            file.copyFileTo (f.getChildFile ("Carthage previous.log"));

        current = std::make_unique<FileLog> (file);
        juce::Logger::setCurrentLogger (current.get());
        info (firstLine + ", started " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S"));
        info (systemSummary());
    }

    void stop()
    {
        if (current == nullptr)
            return;

        info ("Closed normally");
        juce::Logger::setCurrentLogger (nullptr);
        current.reset();
    }

    void write (Level level, const juce::String& s)
    {
        if (current != nullptr)
            current->add (level == Level::error ? "Error: " : level == Level::warning ? "Warning: " : "", s);
    }

    juce::String text()            { return current != nullptr ? current->text() : juce::String(); }
    juce::File folder()            { return logFolder; }

    juce::String previousRun()
    {
        return logFolder.getChildFile ("Carthage previous.log").loadFileAsString();
    }

    juce::String systemSummary()
    {
        using S = juce::SystemStats;
        return S::getOperatingSystemName() + ", " + S::getCpuModel() + " (" + juce::String (S::getNumCpus()) + " cores), "
               + juce::String (S::getMemorySizeInMegabytes() / 1024) + " GB memory";
    }

    juce::String withoutHome (const juce::String& s)
    {
        const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
        return home.isEmpty() ? s : s.replace (home, "~");
    }

    void installCrashHandler()
    {
        // Runs as the process dies, so it does the least it can: one file, then out.
        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            if (logFolder == juce::File())
                return;

            juce::FileOutputStream note (crashNoteFile());

            if (note.openedOk())
            {
                note.setPosition (0);
                note.truncate();
                note.writeText ("Carthage stopped unexpectedly at " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S")
                                    + "\n\n" + juce::SystemStats::getStackBacktrace(), false, false, nullptr);
                note.flush();
            }
        });
    }

    juce::String takeCrashNote()
    {
        const auto file = crashNoteFile();

        if (! file.existsAsFile())
            return {};

        const auto note = file.loadFileAsString();
        file.deleteFile();
        return note;
    }
}
