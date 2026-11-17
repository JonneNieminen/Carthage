# Draft upstream reports (not posted)

Drafts for the owner to approve before anything is posted (DECISIONS F18). They describe what
RealTimeSanitizer (LLVM 23, `-fsanitize=realtime`) reported on the audio thread while Carthage ran
Tracktion Engine `develop` at c6293127 on JUCE 9.0.5, macOS 27, Apple Silicon. Evidence and method:
`tests/rtsan/baseline.txt`, `scripts/rtsan_check.py`, `docs/TESTING.md`.

---

## 1. Tracktion Engine: allocation and locks on the audio thread every block

**Title:** Per-block allocation and locking on the audio thread (RTSan report)

Running an Edit with one audio track monitoring a physical input (`MonitorMode::on`) and one external
plugin, under Clang's RealTimeSanitizer with the whole device callback marked real-time, these sites
report on every block:

- `WaveInputDeviceInstance::acceptInputBuffer` → `RetrospectiveRecordBuffer::updateSizeIfNeeded (…,
  engine.getDeviceManager().getSampleRate())`. On macOS `DeviceManager::getSampleRate()` calls
  `CoreAudioIODevice::getCurrentSampleRate()`, which in JUCE 9 copies a `DeviceSetup` (containing
  `BigInteger`s): one `malloc` and one `free` per call, per block. `getCurrentBufferSizeSamples()` does
  the same. Caching the rate in `DeviceManager` (it already has `currentSampleRate`) would remove it.
  The retrospective buffer is created for every physical input even when retrospective record is not
  used; a way to turn it off would remove the rest of this path.
- `WaveInputDevice::consumeNextAudioBlock` / `acceptInputBuffer`: `consumerLock` (`CriticalSection`).
- `DeviceManager::audioDeviceIOCallbackInternal`: `std::shared_lock` on `contextLock`.
- `DeviceManager::broadcastStreamTimeToMidiDevices`: `std::shared_lock` on `midiInputsMutex`, and a
  condition-variable signal, even with no MIDI devices open.
- `MidiNoteDispatcher::dispatchPendingMessagesForDevices` and `masterTimeUpdate`: locks every block.
- `Edit::updateModifierTimers`: locks `modifierTimers` every block.
- `LiveMidiInjectingNode::process`: `liveMidiLock`.
- `ExternalPlugin::applyToBuffer`: `processMutex`.
- `LevelMeasurer::Client::ensureNumChannels`: `std::vector<bool>::resize` from the audio thread on
  first use.

Also, for context: `WaveInputRecordingThread::addBlockToRecord` takes a `std::mutex` and a
`CriticalSection` and can `new` a block from the audio thread. We record through our own lock-free
FIFO for that reason.

The locks are uncontended almost always, and we measured no dropouts, but they make it impossible to
hold an application to "no locks on the audio thread" with a tool. Would you accept patches that
replace these with lock-free handoffs or move the sizing to `prepareToPlay`?

---

## 2. JUCE: VST3 host wrapper locks per block; CoreAudio getters allocate

**Title:** VST3PluginInstance::processBlock locks a CriticalSection every block

`VST3PluginInstanceHeadless::processAudio` → `associateWith (data, midiMessages)` →
`MidiEventList::clear()` → `Array<Vst::Event, CriticalSection>::clearQuick()` takes a lock every
block, and `MidiEventList::toMidiBuffer` → `getEventCount()` takes it again. The list is only touched
from the processing thread; a `DummyCriticalSection` would remove the lock.

Separately, `CoreAudioIODevice::getCurrentSampleRate()` and `getCurrentBufferSizeSamples()` copy a
`DeviceSetup` (with `BigInteger` members) through `DeviceConfigurator::getCurrentSetup()`, so calling
them from the audio thread allocates. Hosts (Tracktion Engine among them) do call them from the audio
callback. Returning cached values would make them safe there.
