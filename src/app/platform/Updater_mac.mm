// Sparkle 2 behind app::Updater. Carthage gives Sparkle its own user driver, so an update shows in
// the app's banner instead of Sparkle's windows, and "Install on quit" / "Restart now" map onto
// Sparkle's replies (docs/RELEASING.md, "Update behaviour in the app"):
//
//   update found     -> banner; the user's choice replies "install", which downloads and verifies
//   ready to install -> "Restart now" replies "install" (relaunches now); "Install on quit" keeps
//                       the reply unanswered, and Sparkle installs the update when Carthage quits
//
// The public key and the feeds are compiled in (CMake: CARTHAGE_UPDATE_PUBLIC_KEY and the feed
// URLs); Info.plist carries SUPublicEDKey, which Sparkle itself requires.

#include "../Updater.h"

#import <Sparkle/Sparkle.h>

using namespace carthage::app;

namespace
{
    juce::String toJuce (NSString* s)   { return s != nil ? juce::String::fromUTF8 (s.UTF8String) : juce::String(); }

    /** The appcast's notes are HTML (a list of changes); the banner shows plain lines. */
    juce::String plainNotes (NSString* html)
    {
        auto text = toJuce (html).replace ("</li>", "\n").replace ("<br>", "\n").replace ("</p>", "\n");
        juce::String out;
        bool inTag = false;

        for (auto c : text)
        {
            if (c == '<')       inTag = true;
            else if (c == '>')  inTag = false;
            else if (! inTag)   out << juce::String::charToString (c);
        }

        juce::StringArray lines;
        lines.addLines (out.replace ("&amp;", "&").replace ("&lt;", "<").replace ("&gt;", ">").replace ("&quot;", "\""));
        lines.trim();
        lines.removeEmptyStrings();
        return lines.joinIntoString ("\n");
    }

    class SparkleUpdater;
}

@interface CarthageSparkleBridge : NSObject <SPUUserDriver, SPUUpdaterDelegate>
- (instancetype)initWithOwner:(SparkleUpdater*)owner;
@end

namespace
{
    class SparkleUpdater final : public Updater
    {
    public:
        explicit SparkleUpdater (Hooks h)
            : Updater (std::move (h))
        {
            if (CARTHAGE_BUILD_NUMBER == 0)
                reason = "This is a development build. Released builds update themselves.";
            else if (juce::String (CARTHAGE_UPDATE_PUBLIC_KEY).isEmpty())
                reason = "This build has no update key, so it can not check for updates.";

            if (reason.isNotEmpty())
                return;

            bridge = [[CarthageSparkleBridge alloc] initWithOwner:this];
            NSBundle* bundle = NSBundle.mainBundle;
            updater = [[SPUUpdater alloc] initWithHostBundle:bundle applicationBundle:bundle userDriver:bridge delegate:bridge];
            updater.automaticallyChecksForUpdates = NO;   // Carthage checks once on launch, itself
            updater.automaticallyDownloadsUpdates = NO;   // only after the user chose

            NSError* error = nil;

            if (! [updater startUpdater:&error])
            {
                reason = "The updater could not start: " + toJuce (error.localizedDescription);
                updater = nil;
            }
        }

        bool isAvailable() const override               { return updater != nil; }
        juce::String whyUnavailable() const override     { return reason; }

        void checkInBackground() override
        {
            if (updater != nil && updater.canCheckForUpdates)
            {
                userAsked = false;
                [updater checkForUpdatesInBackground];
            }
        }

        void checkNow() override
        {
            if (updater == nil)
            {
                say (reason, false);
                return;
            }

            if (offer)   // already known: show it again
            {
                show();
                return;
            }

            userAsked = true;
            [updater checkForUpdates];
        }

        void installOnQuit() override
        {
            if (! offer)
                return;

            offer->installOnQuitChosen = true;
            startDownloadIfNeeded();
            show();
        }

        void restartNow() override
        {
            if (! offer)
                return;

            if (hooks.isBusy && hooks.isBusy())
            {
                say ("Stop playback and recording first. The update can also install when you quit.", true);
                return;
            }

            wantsRestart = true;
            startDownloadIfNeeded();

            if (readyReply != nil)
                installNow();
            else
                show();
        }

        void dismiss() override
        {
            // Before the download: "later" (Sparkle may offer it again on the next launch). After
            // it: hide the banner; the update still installs when Carthage quits.
            if (foundReply != nil)
            {
                auto reply = foundReply;
                foundReply = nil;
                reply (SPUUserUpdateChoiceDismiss);
                offer.reset();
            }

            if (hooks.showOffer)
                hooks.showOffer (std::nullopt);
        }

        //==========================================================================================
        // From the bridge (Sparkle calls the user driver on the main thread).
        void found (SUAppcastItem* item, SPUUserUpdateState* state, void (^reply) (SPUUserUpdateChoice))
        {
            if (item.informationOnlyUpdate)
            {
                reply (SPUUserUpdateChoiceDismiss);
                return;
            }

            Offer o;
            o.version = toJuce (item.displayVersionString);
            o.notes = plainNotes (item.itemDescription);
            o.stage = state.stage == SPUUserUpdateStageNotDownloaded ? Stage::found : Stage::ready;
            o.installOnQuitChosen = offer && offer->installOnQuitChosen;
            offer = o;

            if (state.stage == SPUUserUpdateStageNotDownloaded)
            {
                foundReply = reply;

                if (offer->installOnQuitChosen || wantsRestart)
                    startDownloadIfNeeded();
            }
            else
            {
                // Downloaded on an earlier run, or installing: carry on.
                reply (SPUUserUpdateChoiceInstall);
            }

            show();
        }

        void downloading (double fraction)
        {
            if (! offer)
                return;

            offer->stage = Stage::downloading;
            offer->progress = fraction;
            show();
        }

        void ready (void (^reply) (SPUUserUpdateChoice))
        {
            readyReply = reply;

            if (offer)
                offer->stage = Stage::ready;

            if (wantsRestart && ! (hooks.isBusy && hooks.isBusy()))
                installNow();
            else
                show();   // keep the reply unanswered: Sparkle installs on quit
        }

        void installing()
        {
            if (offer)
            {
                offer->stage = Stage::installing;
                show();
            }
        }

        void finished()
        {
            offer.reset();
            foundReply = nil;
            readyReply = nil;
            wantsRestart = false;

            if (hooks.showOffer)
                hooks.showOffer (std::nullopt);
        }

        void notFound (NSError* error)
        {
            if (userAsked)
            {
                const auto why = static_cast<SPUNoUpdateFoundReason> ([error.userInfo[SPUNoUpdateFoundReasonKey] integerValue]);
                say (why == SPUNoUpdateFoundReasonOnNewerThanLatestVersion
                         ? "This build is newer than the latest release."
                         : "Carthage " + juce::String (CARTHAGE_VERSION_STRING) + " is up to date.",
                     false);
            }

            userAsked = false;
        }

        void failed (NSError* error)
        {
            // A failed background check is silent; a check the user asked for, or a failed install,
            // says why.
            if (userAsked || offer)
                say ("Could not update: " + toJuce (error.localizedDescription), true);

            userAsked = false;
        }

        void focus()    { show(); }

        juce::String feed() const    { return getFeedUrl(); }

    private:
        CarthageSparkleBridge* bridge = nil;
        SPUUpdater* updater = nil;
        juce::String reason;
        std::optional<Offer> offer;
        void (^foundReply) (SPUUserUpdateChoice) = nil;
        void (^readyReply) (SPUUserUpdateChoice) = nil;
        bool userAsked = false, wantsRestart = false;

        void startDownloadIfNeeded()
        {
            if (foundReply != nil)
            {
                auto reply = foundReply;
                foundReply = nil;
                offer->stage = Stage::downloading;
                reply (SPUUserUpdateChoiceInstall);
            }
        }

        void installNow()
        {
            if (hooks.prepareToRestart && ! hooks.prepareToRestart())
            {
                wantsRestart = false;
                show();
                return;
            }

            auto reply = readyReply;
            readyReply = nil;
            offer->stage = Stage::installing;
            show();
            reply (SPUUserUpdateChoiceInstall);
        }

        void show()
        {
            if (hooks.showOffer && offer)
                hooks.showOffer (offer);
        }

        void say (const juce::String& text, bool warning)
        {
            if (hooks.showMessage)
                hooks.showMessage (text, warning);
        }
    };
}

//==================================================================================================
@implementation CarthageSparkleBridge
{
    SparkleUpdater* owner;
    uint64_t expectedLength;
    uint64_t receivedLength;
}

- (instancetype)initWithOwner:(SparkleUpdater*)o
{
    if ((self = [super init]))
        owner = o;

    return self;
}

// SPUUpdaterDelegate ---------------------------------------------------------------------------
- (nullable NSString*)feedURLStringForUpdater:(SPUUpdater*)updater
{
    juce::ignoreUnused (updater);
    return [NSString stringWithUTF8String:owner->feed().toRawUTF8()];
}

// SPUUserDriver --------------------------------------------------------------------------------
- (void)showUpdatePermissionRequest:(SPUUpdatePermissionRequest*)request reply:(void (^)(SUUpdatePermissionResponse*))reply
{
    juce::ignoreUnused (request);
    // Carthage checks on launch itself; never ask, never schedule Sparkle's own checks.
    reply ([[SUUpdatePermissionResponse alloc] initWithAutomaticUpdateChecks:NO sendSystemProfile:NO]);
}

- (void)showUserInitiatedUpdateCheckWithCancellation:(void (^)(void))cancellation
{
    juce::ignoreUnused (cancellation);
}

- (void)showUpdateFoundWithAppcastItem:(SUAppcastItem*)appcastItem state:(SPUUserUpdateState*)state reply:(void (^)(SPUUserUpdateChoice))reply
{
    owner->found (appcastItem, state, reply);
}

- (void)showUpdateReleaseNotesWithDownloadData:(SPUDownloadData*)downloadData
{
    juce::ignoreUnused (downloadData);   // notes are embedded in the appcast
}

- (void)showUpdateReleaseNotesFailedToDownloadWithError:(NSError*)error
{
    juce::ignoreUnused (error);
}

- (void)showUpdateNotFoundWithError:(NSError*)error acknowledgement:(void (^)(void))acknowledgement
{
    owner->notFound (error);
    acknowledgement();
}

- (void)showUpdaterError:(NSError*)error acknowledgement:(void (^)(void))acknowledgement
{
    owner->failed (error);
    acknowledgement();
}

- (void)showDownloadInitiatedWithCancellation:(void (^)(void))cancellation
{
    juce::ignoreUnused (cancellation);
    expectedLength = receivedLength = 0;
    owner->downloading (0.0);
}

- (void)showDownloadDidReceiveExpectedContentLength:(uint64_t)expectedContentLength
{
    expectedLength = expectedContentLength;
}

- (void)showDownloadDidReceiveDataOfLength:(uint64_t)length
{
    receivedLength += length;

    if (expectedLength > 0)
        owner->downloading (static_cast<double> (receivedLength) / static_cast<double> (expectedLength));
}

- (void)showDownloadDidStartExtractingUpdate
{
    owner->downloading (1.0);
}

- (void)showExtractionReceivedProgress:(double)progress
{
    juce::ignoreUnused (progress);
}

- (void)showReadyToInstallAndRelaunch:(void (^)(SPUUserUpdateChoice))reply
{
    owner->ready (reply);
}

- (void)showInstallingUpdateWithApplicationTerminated:(BOOL)applicationTerminated retryTerminatingApplication:(void (^)(void))retryTerminatingApplication
{
    juce::ignoreUnused (applicationTerminated, retryTerminatingApplication);
    owner->installing();
}

- (void)showUpdateInstalledAndRelaunched:(BOOL)relaunched acknowledgement:(void (^)(void))acknowledgement
{
    juce::ignoreUnused (relaunched);
    owner->finished();
    acknowledgement();
}

- (void)dismissUpdateInstallation
{
    owner->finished();
}

- (void)showUpdateInFocus
{
    owner->focus();
}
@end

//==================================================================================================
std::unique_ptr<Updater> Updater::create (Hooks hooks)
{
    return std::make_unique<SparkleUpdater> (std::move (hooks));
}
