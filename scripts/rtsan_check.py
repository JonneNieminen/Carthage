#!/usr/bin/env python3
"""Checks a RealTimeSanitizer log against the third-party baseline (docs/TESTING.md).

    RTSAN_OPTIONS=halt_on_error=false <carthage-bench or Carthage built with the rtsan preset> 2> run.log
    python3 -I scripts/rtsan_check.py run.log tests/rtsan/baseline.txt

Each unique report is reduced to a signature: the unsafe call RTSan intercepted, and the innermost
frame of its stack (inline frames included) that belongs to Carthage, Tracktion Engine or JUCE,
skipping JUCE's generic containers and locks. The check fails if any signature is owned by
Carthage (namespace carthage::), or is not in the baseline. Baseline entries that no longer occur
are listed, so the file can shrink when upstream fixes them.
"""
import re
import sys

OWNERS = ("carthage::", "tracktion::", "juce::")
GENERIC = ("juce::CriticalSection", "juce::GenericScopedLock", "juce::GenericScopedUnlock", "juce::HeapBlock",
           "juce::Array<", "juce::ArrayBase<", "juce::AudioBuffer<", "juce::BigInteger", "juce::String",
           "juce::MemoryBlock", "juce::OwnedArray<", "juce::ReferenceCountedObjectPtr<", "juce::SpinLock",
           "juce::DeviceSetup", "juce::MidiBuffer")


def signatures(log_text):
    found = {}
    for block in log_text.split("ERROR: RealtimeSanitizer")[1:]:
        call = re.search(r"function `([^`]+)`", block)
        call = call.group(1) if call else "unknown"
        frames = [re.sub(r"\[abi:[^\]]+\]", "", f).strip()
                  for f in re.findall(r"#\d+ 0x[0-9a-f]+ in (.*)", block.split("SUMMARY:")[0])]
        owner = "unknown"
        for frame in frames[1:]:
            name = frame.split("(")[0]
            if name.startswith(OWNERS) and not name.startswith(GENERIC):
                owner = name
                break
        found.setdefault((call, owner), frames)
    return found


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    log_text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    baseline = set()
    for line in open(sys.argv[2], encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            call, owner = (part.strip() for part in line.split("|", 1))
            baseline.add((call, owner))

    if "ERROR: RealtimeSanitizer" not in log_text and "Total error count" not in log_text:
        print("No RealTimeSanitizer output in the log: was the build made with the rtsan preset?")

    found = signatures(log_text)
    failures = 0

    for (call, owner), frames in sorted(found.items(), key=lambda kv: kv[0][1]):
        if owner.startswith("carthage::"):
            verdict, failures = "FAIL (Carthage's code)", failures + 1
        elif (call, owner) in baseline:
            verdict = "baseline"
        else:
            verdict, failures = "FAIL (new)", failures + 1
        print(f"{verdict:24} {call:22} {owner}")
        if verdict.startswith("FAIL"):
            for frame in frames[:14]:
                print("        " + frame[:160])

    for call, owner in sorted(baseline - set(found)):
        print(f"{'not seen':24} {call:22} {owner}")

    print(f"\n{len(found)} distinct report(s); {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
