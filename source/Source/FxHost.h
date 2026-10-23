// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

// VST effect hosting: 2 effect slots per channel. Only compiled in the
// personal "FX" build (cmake -DFOURTRACKER_FX_HOST=ON, build-windows-fx.bat)
// and only active in the standalone app.

#if FOURTRACKER_FX_HOST

#include <juce_audio_processors/juce_audio_processors.h>
#include "TrackerTypes.h"

class FxHost : private juce::Timer
{
public:
    static constexpr int kSlotsPerChannel = 2;

    FxHost();
    ~FxHost() override;

    // ---- audio ------------------------------------------------------------------
    void prepare (double sampleRate, int blockSize);
    /** Audio thread: runs the channel's effects over its stereo buffer. */
    void process (int channel, float* left, float* right, int numSamples) noexcept;

    // ---- folders and scanning (message thread) --------------------------------
    juce::FileSearchPath& getSearchPath() noexcept { return searchPath; }
    void startScan();
    bool isScanning() const noexcept { return scanning; }
    float getScanProgress() const noexcept { return scanProgress; }
    juce::String getScanningName() const { return scanningName; }
    /** Plugin formats this build can load, e.g. "VST3, VST2". */
    juce::String getFormatNames() const;
    /** What the last scan found and skipped (32-bit files, failed files, crash). */
    juce::String getScanReport() const { return scanReport; }

    /** All scanned effects (instruments are left out), sorted by name. */
    juce::Array<juce::PluginDescription> getEffects() const;
    /** Told about scan progress and every slot change (FX page, FX bar on the tracks page). */
    struct Listener { virtual ~Listener() = default; virtual void fxChanged() = 0; };
    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // ---- slots (message thread) ---------------------------------------------------
    juce::String loadSlot (int channel, int slot, const juce::PluginDescription&);   // returns error or empty
    void clearSlot (int channel, int slot);
    void clearAll();   // every slot on every channel
    bool hasPlugin (int channel, int slot) const;
    juce::String getSlotName (int channel, int slot) const;
    juce::PluginDescription getSlotDescription (int channel, int slot) const;
    void setBypass (int channel, int slot, bool b);
    bool isBypassed (int channel, int slot) const;
    void showEditor (int channel, int slot);

    /** Chaining: a channel can send its sound (after its own effects) on into another
        channel's effects instead of straight out. -1 = straight out (master). */
    void setRoute (int channel, int target);
    int  getRoute (int channel) const noexcept { return route[(size_t) channel].load(); }
    void clearRoutes();
    void closeAllEditors();

    /** Settings file: folders, scanned list, and each slot's plugin + its settings. */
    void saveSettings();

private:
    struct Slot
    {
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::PluginDescription desc;
        juce::AudioBuffer<float> scratch;
        std::atomic<bool> bypass { false };
        int ins = 2, outs = 2;
    };

    void timerCallback() override;
    void notify() { listeners.call ([] (Listener& l) { l.fxChanged(); }); }
    juce::ListenerList<Listener> listeners;
    void loadSettings();
    void restorePendingSlots();
    void prepareSlot (Slot&);
    static juce::File settingsFolder();

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList known;
    juce::FileSearchPath searchPath;

    std::array<std::array<Slot, kSlotsPerChannel>, ft::kNumChannels> slots;
    std::array<std::atomic<int>, ft::kNumChannels> route;
    juce::CriticalSection audioLock;          // the audio thread only tries it
    juce::MidiBuffer emptyMidi;
    double sampleRate = 0.0;
    int blockSize = 512;

    // scanning, one file per timer tick on the message thread (plugins expect that)
    std::vector<std::unique_ptr<juce::PluginDirectoryScanner>> scanners;
    size_t scannerIndex = 0;
    bool scanning = false;
    float scanProgress = 0.0f;
    juce::String scanningName;
    juce::StringArray failedFiles;
    juce::String scanReport;
    juce::uint32 lastProgressSave = 0;
    void makeScanReport();

    std::unique_ptr<juce::XmlElement> pendingSlots;   // restored once audio is prepared
    bool restoring = false;
    juce::uint32 restoreDoneAt = 0;

    struct EditorWindow;
    std::array<std::array<std::unique_ptr<juce::DocumentWindow>, kSlotsPerChannel>, ft::kNumChannels> windows;
};

#endif
