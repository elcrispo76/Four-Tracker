// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "TrackerTypes.h"
#include "Instrument.h"

namespace ft
{

/** Mixer settings for one channel, written by the UI, read by audio. */
struct ChannelMix
{
    std::atomic<int>  volume { 64 };   // 0..64
    std::atomic<int>  pan    { 0 };    // -64..64
    std::atomic<int>  pitch  { 0 };    // semitones -24..24
    std::atomic<bool> mute   { false };
};

/** The playback engine: 4 sample channels, FastTracker 2 effects,
    tick timing, and live pattern switching. Everything in process()
    runs on the audio thread; the "request" functions are safe to call
    from the message thread. */
class TrackerEngine
{
public:
    TrackerEngine();

    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, double hostBpm);

    // ---- control (any thread) ---------------------------------------------
    void requestStartStop()                   { push ({ Cmd::StartStop }); }
    void requestStart()                       { push ({ Cmd::Start }); }
    void requestStop()                        { push ({ Cmd::Stop }); }
    void requestPattern (int p)               { push ({ Cmd::SelectPattern, p }); }
    void requestPreview (int inst, int note, bool loopOnly = false) { push ({ Cmd::Preview, inst, note, loopOnly ? 1 : 0 }); }
    void requestStopPreview()                 { push ({ Cmd::StopPreview }); }

    /** Sample browser: plays a file that is not loaded into any slot.
        publishAudition() hands the sound over (message thread), then
        requestAudition() plays it once at its own rate. */
    void publishAudition (InstrumentSnapshot::Ptr snap) { publishSnapshot (kAuditionSlot, snap); }
    void requestAudition()                    { push ({ Cmd::Preview, kAuditionSlot, kNativeNote, 0 }); }

    // ---- instruments -------------------------------------------------------
    /** Message thread: replace the playback view of an instrument slot. */
    void publishInstrument (int slot, InstrumentSnapshot::Ptr snap);
    /** Message thread: frees snapshots no longer used by the audio thread. */
    void collectGarbage();

    // ---- song data -------------------------------------------------------
    Pattern& getPattern (int i) noexcept { return patterns[(size_t) juce::jlimit (0, kMaxPatterns - 1, i)]; }

    /** Take this while doing bulk changes to pattern data from the UI.
        The audio thread skips a block rather than wait for it. */
    juce::CriticalSection& getDataLock() noexcept { return dataLock; }

    // ---- live state for the UI (read-only from the UI) ---------------------
    std::atomic<bool> playing        { false };
    std::atomic<int>  playPattern    { 0 };
    std::atomic<int>  playRow        { 0 };
    std::atomic<int>  selectedPattern{ 0 };
    std::atomic<int>  queuedPattern  { -1 };
    std::atomic<int>  lastInstrumentOnChannel[kNumChannels] {};
    std::atomic<int>  channelLevel[kNumChannels] {};   // 0..1000, for meters
    std::atomic<int>  lastMidiNote   { -1 };
    std::atomic<int>  previewPosition{ -1 };           // physical frame of the preview voice, -1 when idle

    // ---- settings (UI writes, audio reads) ---------------------------------
    std::atomic<int>  speed          { 6 };
    std::atomic<int>  bpm            { 125 };
    std::atomic<bool> loopPattern    { true };
    std::atomic<int>  midiBaseNote   { 36 };     // C-2 = start/stop, next keys = patterns
    std::atomic<int>  midiChannel    { 0 };      // 0 = omni, 1..16
    std::atomic<int>  switchMode     { (int) SwitchMode::LegatoNextRow };
    std::atomic<bool> followHostTempo{ false };
    std::atomic<bool> midiLearn      { false };
    std::atomic<bool> respondToMidiStartStop { true };   // MIDI clock Start/Stop messages
    std::atomic<int>  masterVolume   { 80 };     // 0..100

    ChannelMix mix[kNumChannels];

private:
    // extra snapshot slot after the 16 instruments, never reachable from patterns
    static constexpr int kAuditionSlot = kNumInstruments;
    static constexpr size_t kSnapSlots = (size_t) kNumInstruments + 1;
    void publishSnapshot (int slot, InstrumentSnapshot::Ptr snap);

    // ---- command queue ------------------------------------------------------
    struct Cmd
    {
        enum Type { StartStop, Start, Stop, SelectPattern, Preview, StopPreview } type;
        int a = 0, b = 0, c = 0;
    };
    void push (Cmd c);
    void handleCommand (const Cmd& c);
    void handleMidi (const juce::MidiMessage& m);

    juce::AbstractFifo cmdFifo { 256 };
    std::array<Cmd, 256> cmdBuffer {};
    juce::SpinLock pushLock;

    // ---- voices -------------------------------------------------------------
    struct Voice
    {
        InstrumentSnapshot::Ptr inst;
        double pos  = 0.0;     // virtual frame
        double step = 0.0;
        int  dir    = 1;
        bool active = false;
        bool inLoop = false;
        float gainL = 0, gainR = 0;          // smoothed
        float targetL = 0, targetR = 0;
        float fade = 1.0f;                   // used while fading out
        bool  fading = false;
        bool  loopOnly = false;              // preview of the loop region only

        // synth instruments: ADSR envelope and resonant low-pass filter
        int   envStage = 0;                  // 0 none, 1 attack, 2 decay, 3 sustain, 4 release
        float env = 1.0f, relStep = 0.0f;
        float fL1 = 0, fL2 = 0, fR1 = 0, fR2 = 0;   // filter state
    };
    void startEnvelope (Voice& v);

    struct ChannelState
    {
        Voice voice, ghost;
        int  instNum = 0;
        InstrumentSnapshot::Ptr inst;
        int  note = 0;
        double pitch = 0;          // 1/64 semitone units, higher = higher pitch
        double targetPitch = 0;
        int  volume = 64;
        int  pan = 128;            // 0..255
        int  fineTune = 0;
        bool keyedOff = false;

        // effect state for the current row
        Cell cell;
        int  delayTick = -1;
        int  cutTick = -1;
        int  keyOffTick = -1;

        // effect memory
        uint8_t mPortaUp = 0, mPortaDown = 0, mTonePorta = 0, mVolSlide = 0, mOffset = 0;
        uint8_t mFinePortaUp = 0, mFinePortaDown = 0, mFineVolUp = 0, mFineVolDown = 0;
        uint8_t mPanSlide = 0, mGlobalSlide = 0, mRetrig = 0, mTremor = 0, mXFineUp = 0, mXFineDown = 0;
        int  vibSpeed = 0, vibDepth = 0, vibPos = 0, vibWave = 0;
        int  tremSpeed = 0, tremDepth = 0, tremPos = 0, tremWave = 0;
        int  retrigCount = 0, tremorPos = 0;
        bool tremorOn = true;
        int  loopRow = 0, loopCount = 0;

        // per tick outputs
        double pitchOffset = 0;
        int    volOffset = 0;
    };

    void resetChannels (bool keepVoices);
    void startPlayback();
    void stopPlayback();
    void selectPattern (int p);

    void doTick();
    void processRow();
    void processChannelRowStart (int ch);
    void processChannelTick (int ch);
    void triggerCell (int ch, const Cell& c);
    void triggerVoice (ChannelState& cs, int startOffset);
    void advanceRow();
    void switchToQueued (bool keepPosition);
    void updateVoiceTargets (int ch);

    void renderVoices (float* L, float* R, int numSamples);
    void renderVoice (Voice& v, float* L, float* R, int numSamples, float& peak);
    static float readFrame (const InstrumentSnapshot& s, int ch, double pos) noexcept;
    void fadeOutVoice (Voice& v);

    double samplesPerTick() const noexcept;
    static int waveValue (int wave, int pos) noexcept;   // -255..255

    std::array<Pattern, kMaxPatterns> patterns;
    juce::CriticalSection dataLock;

    juce::SpinLock snapLock;
    std::array<InstrumentSnapshot::Ptr, kSnapSlots> snapshots;       // message side
    std::array<InstrumentSnapshot::Ptr, kSnapSlots> audioSnapshots;  // audio side copy
    juce::ReferenceCountedArray<InstrumentSnapshot> garbage;

    ChannelState chans[kNumChannels];
    Voice previewVoice;

    double sr = 44100.0;
    double samplesToNextTick = 0.0;
    double currentHostBpm = -1.0;
    int tick = 0;
    int row = 0;
    int pattern = 0;
    int patternDelay = 0;          // EEx
    int globalVolume = 64;
    bool breakPending = false;
    int  breakRow = 0;
    bool loopJumpPending = false;
    int  loopJumpRow = 0;
    std::vector<float> monoScratch;
    float smoothCoeff = 0.01f;
    float fadeStep = 0.01f;
};

} // namespace ft
