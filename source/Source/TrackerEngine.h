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

    void prepare (double sampleRate, int maxBlockSize = 4096);
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
    Pattern& getPattern (int i) noexcept { return (*patterns)[(size_t) juce::jlimit (0, kMaxPatterns - 1, i)]; }

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
    std::atomic<int>  numChannels    { 4 };      // channels in use: 4, 8 or 16

    ChannelMix mix[kNumChannels];

    /** Optional per-channel insert (VST effects in the FX build). When set,
        each channel is rendered into its own stereo buffer, passed through
        this callback on the audio thread, and then mixed. Set it before
        audio starts. */
    std::function<void (int channel, float* left, float* right, int numSamples)> channelInsert;
    /** Optional chaining (with channelInsert): the channel whose effects this channel's sound
        goes on into after its own, or -1 for straight out. Loops are ignored. */
    std::function<int (int channel)> channelRoute;

    /** MIDI out from MIDI instruments (filled by process(), read and cleared by the processor). */
    juce::MidiBuffer midiOut;

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
        int   slot = 0;                      // instrument slot, to follow live slider changes
        float fCut = -1.0f, fRes = 0.0f;     // smoothed filter settings (0..127), -1 = not started
        int   envStage = 0;                  // 0 none, 1 attack, 2 decay, 3 sustain, 4 release
        float env = 1.0f, relStep = 0.0f;
        float accentEnv = 0.0f;              // 1 at the note start, fades out (ACCENT)
        float fL1 = 0, fL2 = 0, fR1 = 0, fR2 = 0;   // filter state
    };
    void startEnvelope (Voice& v);
    struct ChannelState;
    void releaseChannel (ChannelState& cs);                    // note-off: fade the voice, end a MIDI note
    void midiNoteOff (int& note, int chan);
    void midiNoteOn (int note, int chan, int velocity, int program);
    int  blockOffset = 0;                                     // where in the block MIDI events go
    int  lastProgram[17] {};                                  // per MIDI channel, 0 = none sent yet
    int  previewMidiNote = -1, previewMidiChan = 1;

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
        int  fxCutoff = -1, fxReso = -1;     // 0X / 0Z filter override (0..127), -1 = use the instrument's
        int  fxDrive = -1, fxAccent = -1;    // 0Y / 0V override (0..64)
        int  midiNote = -1, midiChan = 1;    // a MIDI note sounding on this channel (MIDI instrument)
        bool midiPending = false;            // note-on waits until the row's effects (0Cxx = velocity) are read
        int  midiProgram = 0;
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

    void renderVoices (float* L, float* R, int offset, int numSamples);
    void renderVoice (Voice& v, float* L, float* R, int numSamples, float& peak, int channel = -1);
    static float readFrame (const InstrumentSnapshot& s, int ch, double pos) noexcept;
    void fadeOutVoice (Voice& v);

    double samplesPerTick() const noexcept;
    static int waveValue (int wave, int pos) noexcept;   // -255..255

    // ~1 MB with 16 channels: kept on the heap, so the engine (and the processor) stay small
    std::unique_ptr<std::array<Pattern, kMaxPatterns>> patterns { std::make_unique<std::array<Pattern, kMaxPatterns>>() };
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
    std::vector<float> chanScratch;     // kNumChannels * 2 * chanScratchSize, for channelInsert
    int chanScratchSize = 0;
    float* chanL (int ch) noexcept { return chanScratch.data() + (size_t) (ch * 2) * (size_t) chanScratchSize; }
    float* chanR (int ch) noexcept { return chanScratch.data() + (size_t) (ch * 2 + 1) * (size_t) chanScratchSize; }
    bool useChannelBuffers = false;     // this block renders channels separately
    float smoothCoeff = 0.01f;
    float fadeStep = 0.01f;
};

} // namespace ft
