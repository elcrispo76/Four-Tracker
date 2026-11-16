// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#include "MakerComponent.h"
#include "Slicer.h"
#include "UI/PixelFont.h"
#include <cmath>
#include <limits>

using namespace juce;
using namespace pm;

namespace
{
    const Colour kStepOn { 0xffe4e6eb }, kGhost { 0xff8c9098 }, kBeat { 0xff2b2d32 }, kOffBeat { 0xff1b1c20 }, kPlay { 0xffd9342b };

    void fitText (Graphics& g, const String& t, juce::Rectangle<float> r, float h, Colour c, Justification j = Justification::centred, bool bold = false)
    {
        Theme::text (g, t, r, jmin (h, h * (r.getWidth() - 2.0f) / jmax (1.0f, PixelFont::getWidth (t, h))), c, j, bold);
    }

    String noteName (int key, int semis)
    {
        return keyName (key + semis) + (semis >= 12 ? "'" : "");
    }
}

// ===========================================================================
// SoundLibrary
// ===========================================================================

juce::File SoundLibrary::findPack()
{
    Array<File> candidates {
        File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Four Tracker/Samples/Four Tracker Sample Pack"),
        File::getSpecialLocation (File::currentExecutableFile).getParentDirectory().getChildFile ("Sample Pack"),
        File::getSpecialLocation (File::currentExecutableFile).getParentDirectory().getParentDirectory().getChildFile ("Sample Pack"),
        File ("/Users/Shared/Four Tracker/Four Tracker Sample Pack"),
        File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Four Tracker/Samples"),
    };
    for (auto& f : candidates)
        if (f.getChildFile ("Drums").isDirectory()) return f;
    return {};
}

juce::File SoundLibrary::defaultSound (Style style, LaneKind lane) const
{
    if (! isValid()) return {};
    //                       KICK                     SNARE                    CLAP                  CLOSED HAT                OPEN HAT                    PERC                              BASS                     ARP / KEYS
    if ((int) lane >= kNumGenLanes) return {};          // lanes 9-16 start empty
    static const char* table[(int) Style::NumStyles][kNumGenLanes] = {
        { "Kicks/KICK 909",      "Snares/SNARE 909",  "Claps/CLAP 808",  "Hats/HAT 909 CLOSED", "Hats/HAT 909 OPEN",       "Percussion/SHAKER",     "Bass/BASS 80S C2",      "Synth/Chords/CHORD STAB C MINOR 7" },
        { "Kicks/KICK HARD",     "Snares/SNARE 909",  "Claps/CLAP 808",  "Hats/HAT 909 CLOSED", "Hats/HAT 909 OPEN",       "Snares/RIMSHOT",        "Bass/BASS ROLLING C2",  "Bass/BASS ACID C2" },
        { "Kicks/KICK HARD",     "Snares/SNARE 909",  "Claps/CLAP SNAP", "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Cymbals/RIDE",          "Bass/BASS REESE C2",    "Synth/Plucks/BELL SPACE C5" },
        { "Kicks/KICK SOFT",     "Snares/SNARE 808",  "Claps/CLAP SNAP", "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Percussion/CONGA HIGH", "Bass/BASS SUB C2",      "Synth/Plucks/BELL SPACE C5" },
        { "Kicks/KICK SOFT",     "Snares/SNARE 808",  "Claps/CLAP BIG",  "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Percussion/TAMBOURINE", "Bass/BASS PLUCK C2",    "Synth/Chords/CHORD PLUCK C MINOR" },
        { "Kicks/KICK 808 LONG", "Snares/SNARE 808",  "Claps/CLAP 808",  "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Percussion/COWBELL 808","Bass/BASS SQUARE C2",   "Synth/Plucks/PLUCK PULSE C4" },
        { "Kicks/KICK 808 LONG", "Snares/SNARE 808",  "Claps/CLAP SNAP", "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Percussion/SNAP",       "Bass/BASS SUB C2",      "Synth/Plucks/KEYS SOFT C4" },
        { "Kicks/KICK TRANCE",   "Snares/SNARE TRANCE","Claps/CLAP BIG", "Hats/HAT 909 CLOSED", "Hats/HAT TRANCE OFFBEAT", "Percussion/SHAKER",     "Bass/BASS ROLLING C2",  "Synth/Plucks/PLUCK TRANCE C4" },
        { "Kicks/KICK ITALO",    "Snares/SNARE ITALO", "Claps/CLAP 808",  "Hats/HAT 808 CLOSED", "Hats/HAT 808 OPEN",       "Percussion/TAMBOURINE", "Bass/BASS 80S C2",      "Synth/Plucks/PLUCK 80S C4" },
        { "Kicks/KICK SOFT",     "Snares/SNARE 808",  "Claps/CLAP SNAP", "Hats/HAT PEDAL",      "Hats/HAT 808 OPEN",       "Percussion/SHAKER",     "Bass/BASS SUB C2",      "Synth/Pads/PAD JUNO C3" },
    };
    String rel = table[jlimit (0, (int) Style::NumStyles - 1, (int) style)][(int) lane];
    if (! rel.startsWith ("Bass/") && ! rel.startsWith ("Synth/")) rel = "Drums/" + rel;
    auto f = pack.getChildFile (rel + ".wav");
    if (f.existsAsFile()) return f;
    // the folder is there but not that file: the first sound in the same folder
    auto files = f.getParentDirectory().findChildFiles (File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
    files.sort();
    return files.isEmpty() ? File() : files[0];
}

juce::Array<juce::File> SoundLibrary::foldersFor (LaneKind lane) const
{
    Array<File> out;
    if (! isValid()) return out;
    auto add = [&] (const char* rel) { auto f = pack.getChildFile (rel); if (f.isDirectory()) out.add (f); };
    switch (lane)
    {
        case LaneKind::Kick:      add ("Drums/Kicks"); add ("Drums/Toms"); break;
        case LaneKind::Snare:     add ("Drums/Snares"); add ("Drums/Claps"); break;
        case LaneKind::Clap:      add ("Drums/Claps"); add ("Drums/Snares"); break;
        case LaneKind::ClosedHat: add ("Drums/Hats"); add ("Drums/Percussion"); break;
        case LaneKind::OpenHat:   add ("Drums/Hats"); add ("Drums/Cymbals"); break;
        case LaneKind::Perc:      add ("Drums/Percussion"); add ("Drums/Cymbals"); add ("Drums/Toms"); add ("FX"); break;
        case LaneKind::Bass:      add ("Bass"); add ("Synth/Chip"); break;
        case LaneKind::Arp:       add ("Synth/Plucks"); add ("Synth/Leads"); add ("Synth/Chords"); add ("Synth/Brass"); add ("Synth/Pads"); add ("Synth/Chip"); break;
        case LaneKind::Extra:
            for (auto* f : { "Drums/Kicks", "Drums/Snares", "Drums/Claps", "Drums/Hats", "Drums/Percussion", "Drums/Cymbals", "Drums/Toms",
                             "Bass", "Synth/Plucks", "Synth/Leads", "Synth/Chords", "Synth/Pads", "Synth/Brass", "Synth/Chip", "FX", "Vocals" })
                add (f);
            break;
    }
    add ("My samples");
    return out;
}

// ===========================================================================
// StepGrid
// ===========================================================================

StepGrid::StepGrid (std::function<Pattern&()> getPattern, int laneIndex) : pattern (std::move (getPattern)), lane (laneIndex) {}

int StepGrid::stepAt (int x) const
{
    const int i = jlimit (0, 15, x * 16 / jmax (1, getWidth()));
    return bar * 16 + i;
}

void StepGrid::paint (Graphics& g)
{
    auto& p = pattern();
    const auto& l = p.lanes[(size_t) lane];
    const bool melodic = laneMelodic (l);
    const float cw = getWidth() / 16.0f;
    for (int i = 0; i < 16; ++i)
    {
        const int st = bar * 16 + i;
        auto r = juce::Rectangle<float> (i * cw + 2.0f, 2.0f, cw - 4.0f, (float) getHeight() - 4.0f);
        if (i % 4 == 0) r.removeFromLeft (i == 0 ? 0.0f : 2.0f);
        const auto& s = l.steps[(size_t) st];
        const bool inRange = st < p.steps();
        g.setColour (! inRange ? Colour (0xff3a3d43) : (i % 4 == 0 ? kBeat : kOffBeat));
        g.fillRect (r);
        if (inRange && s.on)
        {
            const bool ghost = s.vel < 64;
            auto pad = r.reduced (3.0f);
            if (ghost && ! melodic) pad = pad.withTrimmedTop (pad.getHeight() * 0.45f);
            g.setColour (! l.on ? kGhost.darker (0.5f) : (ghost ? kGhost : kStepOn));
            g.fillRect (pad);
            if (melodic)
            {
                fitText (g, noteName (p.settings.key, s.note), pad.withTrimmedBottom (pad.getHeight() * 0.35f), 15.0f, Theme::ink, Justification::centred, true);
                // how long it sounds: a bar along the bottom
                const float lenW = jmin ((float) s.len, 16.0f - i) * cw - 6.0f;
                g.setColour (Theme::ink.withAlpha (0.6f));
                g.fillRect (pad.getX() + 2.0f, pad.getBottom() - 6.0f, jmax (4.0f, jmin (lenW, pad.getWidth() - 4.0f)), 3.0f);
            }
        }
        if (st == playStep)
        {
            g.setColour (kPlay);
            g.drawRect (r, 3.0f);
        }
    }
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    auto& p = pattern();
    auto& l = p.lanes[(size_t) lane];
    const int st = stepAt (e.x);
    if (st >= p.steps()) return;
    auto& s = l.steps[(size_t) st];
    if (e.mods.isPopupMenu())
    {
        if (! laneMelodic (l))
        {
            if (! s.on) { s.on = true; s.vel = 24; }
            else s.vel = s.vel < 64 ? 64 : 24;        // right-click: soft (ghost) or full
        }
        else
        {
            // right-click on a bass / arp step: pick the note from the scale
            PopupMenu m;
            m.addSectionHeader ("NOTE (" + keyName (p.settings.key) + " " + scaleName (p.settings.scale) + ")");
            const auto& sc = scaleSteps (p.settings.scale);
            for (int oct = 0; oct < 2; ++oct)
                for (auto semis : sc)
                    m.addItem (1 + oct * 12 + semis, noteName (p.settings.key, semis + 12 * oct), true, s.on && s.note == semis + 12 * oct);
            Component::SafePointer<StepGrid> safe (this);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe, st] (int chosen)
            {
                if (safe == nullptr || chosen <= 0) return;
                auto& stp = safe->pattern().lanes[(size_t) safe->lane].steps[(size_t) st];
                stp.on = true; stp.note = chosen - 1;
                if (stp.len < 1) stp.len = 1;
                if (safe->onEdited) safe->onEdited();
                safe->repaint();
            });
            return;
        }
    }
    else
    {
        if (s.on) s.on = false;
        else
        {
            s.on = true;
            s.vel = 64;
            if (laneMelodic (l))
            {
                // the note of the step before, or the root
                int prev = st - 1;
                while (prev >= 0 && ! l.steps[(size_t) prev].on) --prev;
                s.note = prev >= 0 ? l.steps[(size_t) prev].note : 0;
                s.len = 1;
            }
        }
    }
    if (onEdited) onEdited();
    repaint();
}

void StepGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    auto& p = pattern();
    auto& l = p.lanes[(size_t) lane];
    const int st = stepAt (e.x);
    if (! laneMelodic (l) || st >= p.steps() || ! l.steps[(size_t) st].on) return;
    auto& s = l.steps[(size_t) st];
    const bool up = (w.deltaY > 0) != w.isReversed;
    if (e.mods.isShiftDown()) { s.len = jlimit (1, 16, s.len + (up ? 1 : -1)); }     // Shift + wheel: longer / shorter
    else
    {
        // the next note in the scale up or down
        const auto& sc = scaleSteps (p.settings.scale);
        std::vector<int> all;
        for (int oct = -1; oct <= 2; ++oct) for (auto x : sc) all.push_back (x + 12 * oct);
        int best = 0;
        for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i] == s.note) best = i;
        if (std::find (all.begin(), all.end(), s.note) == all.end())
            for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i] < s.note) best = i;
        s.note = all[(size_t) jlimit (0, (int) all.size() - 1, best + (up ? 1 : -1))];
        s.note = jlimit (-12, 24, s.note);
    }
    if (onEdited) onEdited();
    repaint();
}

// ===========================================================================
// MakerComponent
// ===========================================================================

MakerComponent::MakerComponent (FourTrackerProcessor& processor, SoundLibrary library)
    : proc (processor), lib (std::move (library)), clock (processor.getEngine()), metronomeOut (processor.getEngine())
{
    // Four Tracker's built-in demo sounds, in case the sample pack isn't installed
    for (int i = 1; i < (int) builtIn.size(); ++i) builtIn[(size_t) i] = proc.getInstrument (i);
    proc.newPatternSet();

    for (auto* c : std::initializer_list<Component*> { &stylePrev, &styleNext, &styleLabel, &bpmBox, &swingBox, &keyBox, &busyBox, &scaleBtn,
                                                        &fillBtn, &generateBtn, &playBtn, &saveBtn, &audioBtn, &copyBarBtn })
        addAndMakeVisible (c);
    styleLabel.setScreen (true);
    stylePrev.icon = [] (Graphics& g, juce::Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (12.0f, 20.0f), c, false); };
    styleNext.icon = [] (Graphics& g, juce::Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (12.0f, 20.0f), c, true); };
    stylePrev.onClick = [this] { setStyle ((Style) (((int) pattern.settings.style + (int) Style::NumStyles - 1) % (int) Style::NumStyles), true); };
    styleNext.onClick = [this] { setStyle ((Style) (((int) pattern.settings.style + 1) % (int) Style::NumStyles), true); };

    for (auto* v : { &bpmBox, &swingBox, &keyBox, &busyBox }) { v->setShowSpinner (true); v->setJustification (Justification::centred); }
    keyBox.formatter = [] (int v) { return keyName (v); };
    keyBox.setDragSensitivity (10.0f);
    bpmBox.onChange   = [this] (int v) { pattern.settings.bpm = v; apply(); };
    swingBox.onChange = [this] (int v) { pattern.settings.swing = v; apply(); };
    keyBox.onChange   = [this] (int v) { pattern.settings.key = v; apply(); repaint(); };
    busyBox.onChange  = [this] (int v) { pattern.settings.busy = v; };
    scaleBtn.onClick = [this]
    {
        PopupMenu m;
        for (int i = 0; i < (int) Scale::NumScales; ++i) m.addItem (i + 1, scaleName ((Scale) i), true, (int) pattern.settings.scale == i);
        Component::SafePointer<MakerComponent> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&scaleBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->pattern.settings.scale = (Scale) (r - 1);
            // new notes for the bass and arp in the new scale, the drums stay
            safe->regenerateMelodic();
        });
    };
    for (int i = 0; i < 3; ++i)
    {
        const int bars = i == 0 ? 1 : (i == 1 ? 2 : 4);
        auto b = std::make_unique<PixelButton> (String (bars), PixelButton::Style::Segment, 18.0f);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (8101);
        b->onClick = [this, bars]
        {
            if (pattern.settings.bars == bars) return;
            const int old = pattern.steps();
            pattern.settings.bars = bars;
            for (auto& l : pattern.lanes)                  // more bars: repeat the ones there are
                for (int st = old; st < pattern.steps(); ++st) l.steps[(size_t) st] = l.steps[(size_t) (st % old)];
            viewBar = jmin (viewBar, bars - 1);
            refreshControls();
            refreshRecControls();
            apply();
        };
        addAndMakeVisible (*b);
        barsBtns[(size_t) i] = std::move (b);
    }
    fillBtn.setClickingTogglesState (true);
    fillBtn.onClick = [this] { pattern.settings.fill = fillBtn.getToggleState(); };
    generateBtn.setComponentID ("generate");
    generateBtn.onClick = [this] { generateAll(); };
    playBtn.setComponentID ("play");
    playBtn.onClick = [this] { togglePlay(); };
    saveBtn.setComponentID ("save");
    saveBtn.icon = Theme::iconSave;
    saveBtn.iconWidth = 44.0f;
    saveBtn.onClick = [this]
    {
        auto f = suggestedFile();
        f.getParentDirectory().createDirectory();
        chooser = std::make_unique<FileChooser> ("Save the pattern for Four Tracker (LOAD PATTERN)", f, "*.ftpat");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [this] (const FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == File()) return;
                if (! file.hasFileExtension ("ftpat")) file = file.withFileExtension ("ftpat");
                const auto err = saveTo (file);
                setStatus (err.isEmpty() ? "SAVED " + file.getFileName().toUpperCase() + " - IN FOUR TRACKER: SONG / PATTERN > LOAD PATTERN" : err);
            });
    };
    audioBtn.onClick = [this] { if (onAudioSettings) onAudioSettings(); };

    for (int i = 0; i < 4; ++i)
    {
        auto b = std::make_unique<PixelButton> ("BAR " + String (i + 1), PixelButton::Style::Segment, 16.0f);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (8102);
        b->onClick = [this, i] { viewBar = i; refreshControls(); };
        addAndMakeVisible (*b);
        barTabs[(size_t) i] = std::move (b);
    }
    copyBarBtn.onClick = [this]
    {
        for (int l = 0; l < kNumLanes; ++l) copyFirstBar (pattern, l);
        apply();
        repaint();
        setStatus ("BAR 1 COPIED TO THE OTHER BARS");
    };

    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& r = rows[(size_t) i];
        r.onBtn = std::make_unique<PixelButton> ("", PixelButton::Style::Record, 13.0f);
        r.onBtn->setClickingTogglesState (true);
        r.onBtn->onClick = [this, i] { pattern.lanes[(size_t) i].on = rows[(size_t) i].onBtn->getToggleState(); apply(); repaint(); };
        r.soundBtn = std::make_unique<PixelButton> ("", PixelButton::Style::Raised, 13.0f);
        r.soundBtn->onClick = [this, i] { chooseSound (i); };
        r.diceBtn = std::make_unique<PixelButton> ("NEW", PixelButton::Style::Raised, 11.0f);
        r.diceBtn->setComponentID ("dice" + String (i));
        r.diceBtn->onClick = [this, i]
        {
            regenerateLane (pattern, i, Random::getSystemRandom().nextInt64());
            apply();
            repaint();
            setStatus ("NEW " + laneLabel (i));
        };
        r.vol = std::make_unique<ValueBox> (0, 64, 64, 20.0f);
        r.vol->setJustification (Justification::centred);
        r.vol->onChange = [this, i] (int v) { pattern.lanes[(size_t) i].volume = v; apply(); };
        r.grid = std::make_unique<StepGrid> ([this]() -> Pattern& { return pattern; }, i);
        r.grid->onEdited = [this] { apply(); };
        for (auto* c : std::initializer_list<Component*> { r.onBtn.get(), r.soundBtn.get(), r.diceBtn.get(), r.vol.get(), r.grid.get() })
            addAndMakeVisible (c);
    }

    // ---- BLANK + REC ----
    for (auto* c : std::initializer_list<Component*> { &blankBtn, &recBtn, &countBtn, &metroBtn, &velBtn, &undoBtn, &doubleBtn, &modeBtn, &padsBtn, &keysBtn, &midiBtn })
        addAndMakeVisible (c);
    blankBtn.setComponentID ("blank");
    recBtn.setComponentID ("rec");
    blankBtn.onClick = [this] { blank(); };
    recBtn.onClick   = [this] { toggleRec(); };
    undoBtn.onClick  = [this] { undoTake(); };
    doubleBtn.setComponentID ("double");
    doubleBtn.onClick = [this] { doubleBars(); };
    countBtn.onClick = [this] { setCountIn (! countIn); };
    metroBtn.onClick = [this] { setMetronome (! metronome); };
    velBtn.onClick   = [this] { useVelocity = ! useVelocity; refreshRecControls(); saveMidiSettings(); };
    modeBtn.setComponentID ("mode");
    modeBtn.onClick = [this] { setPadMode (! padMode); };
    keysBtn.onClick  = [this]
    {
        if (padMode) { setStatus ("KEYS IS FOR INPUT: KEYBOARD - IN DRUM PADS MODE PADS 7 AND 8 PLAY BASS AND ARP"); return; }
        keysLane = keysLane < 0 ? 6 : (keysLane == 6 ? 7 : -1);
        refreshRecControls();
        saveMidiSettings();
        setStatus (keysLane < 0 ? String ("KEYS: OFF - ONLY THE PADS PLAY")
                                : "KEYS: A KEYBOARD PLAYS " + laneName ((LaneKind) keysLane) + " (ROOT " + ft::midiNoteName (keysRoot ((LaneKind) keysLane)) + ")");
    };
    padsBtn.onClick = [this]
    {
        PopupMenu m;
        m.addSectionHeader (String (padMode ? "DRUM PADS" : "KEYBOARD") + ": HIT THE PAD OR KEY AFTER CHOOSING");
        m.addItem (100, padMode ? "LEARN ALL 16 PADS, ONE AFTER THE OTHER" : "LEARN THE DRUM KEYS, ONE AFTER THE OTHER");
        m.addSeparator();
        for (int i = 0; i < kNumLanes; ++i)
        {
            const int n = getPads().note[(size_t) i];
            m.addItem (i + 1, "LEARN " + laneLabel (i) + "   (" + (n < 0 ? String ("NONE") : ft::midiNoteName (n)) + ")");
        }
        m.addSeparator();
        m.addItem (201, String ("PADS 1-8 IN A ROW FROM C-2 (BEATSTEP, MPD ...)") + (padMode ? "  - DEFAULT" : ""));
        m.addItem (200, String ("GENERAL MIDI DRUM MAP (DRUM KITS, KEYBOARDS)") + (padMode ? "" : "  - DEFAULT"));
        Component::SafePointer<MakerComponent> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&padsBtn), [safe] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            if (r == 100) safe->learnPad (0, true);
            else if (r == 200) { safe->getPads() = PadMap::generalMidi(); safe->saveMidiSettings(); safe->setStatus ("PADS: GENERAL MIDI DRUM MAP - KICK C-2, SNARE D-2, CLAP D#2, HATS F#2 / A#2, PERC C#2"); }
            else if (r == 201)
            {
                safe->getPads() = PadMap {};
                safe->saveMidiSettings();
                safe->setStatus ("PADS 1-16 FROM C-2: LANES 1-16 (KICK, SNARE, CLAP, HATS, PERC, BASS, ARP, THEN 9-16)");
            }
            else safe->learnPad (r - 1, false);
        });
    };
    midiBtn.onClick = [this]
    {
        PopupMenu m;
        const auto devs = MidiInput::getAvailableDevices();
        m.addItem (1, "ALL MIDI INPUTS", true, midiInName == "ALL");
        for (int i = 0; i < devs.size(); ++i) m.addItem (10 + i, devs[i].name, true, midiInName == devs[i].name);
        if (devs.isEmpty()) m.addItem (2, "(NO MIDI INPUT FOUND - PLUG IT IN, THEN OPEN THIS AGAIN)", false);
        m.addItem (3, "NONE", true, midiInName == "NONE");
        Component::SafePointer<MakerComponent> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&midiBtn), [safe, devs] (int r)
        {
            if (safe == nullptr || r <= 0) return;
            safe->midiInName = r == 1 ? String ("ALL") : (r == 3 ? String ("NONE") : devs[r - 10].name);
            safe->openMidiInputs();
            safe->saveMidiSettings();
            safe->setStatus (safe->openInputNames.isEmpty() ? String ("NO MIDI INPUT OPEN")
                                                            : "MIDI IN: " + safe->openInputNames.joinIntoString (", ").toUpperCase());
        });
    };
    // lanes 1-8 / 9-16, and the SLICER
    for (int i = 0; i < 2; ++i)
    {
        auto b = std::make_unique<PixelButton> (i == 0 ? "LANES 1-8" : "LANES 9-16", PixelButton::Style::Segment, 13.0f);
        b->setClickingTogglesState (true);
        b->setRadioGroupId (8103);
        b->setComponentID (i == 0 ? "lanes1" : "lanes2");
        b->onClick = [this, i] { setLaneView (i); };
        addAndMakeVisible (*b);
        laneTabs[(size_t) i] = std::move (b);
    }
    addAndMakeVisible (slicerBtn);
    slicerBtn.setComponentID ("slicer");
    slicerBtn.onClick = [this] { openSlicer(); };
    selfSafe = this;
    loadMidiSettings();
    openMidiInputs();
    refreshRecControls();
    clock.startTimer (1);

    setWantsKeyboardFocus (true);
    setSize (kWidth, kHeight);
    Settings s;
    s.seed = Random::getSystemRandom().nextInt64();
    pattern.settings = s;
    setStyle (Style::House, true);
    startTimerHz (30);
}

MakerComponent::~MakerComponent()
{
    if (slicer != nullptr)
    {
        slicer->player.stop();
        if (audioDevices != nullptr) audioDevices->removeAudioCallback (&slicer->player);
    }
    slicerWindow.reset();
    slicer.reset();
    if (audioDevices != nullptr) audioDevices->removeAudioCallback (&metronomeOut);
    clock.stopTimer();
    for (auto& in : inputs) in->stop();
    inputs.clear();
    stopTimer();
    proc.getEngine().requestStop();
}

void MakerComponent::setStyle (Style st, bool newTempo)
{
    pattern.settings.style = st;
    if (newTempo)
    {
        pattern.settings.bpm = defaultBpm (st);
        pattern.settings.swing = defaultSwing (st);
    }
    for (int i = 0; i < kNumGenLanes; ++i)                // the style's own sounds (a slice stays)
    {
        if (pattern.lanes[(size_t) i].slice) continue;
        loadedSound[(size_t) i] = {};
        pattern.lanes[(size_t) i].sound = lib.defaultSound (st, (LaneKind) i).getFullPathName();
    }
    generateAll();
}

void MakerComponent::generateAll()
{
    auto s = pattern.settings;
    s.seed = Random::getSystemRandom().nextInt64();
    const auto old = pattern.lanes;
    pattern = generate (s);
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& l = pattern.lanes[(size_t) i];
        if (i >= kNumGenLanes) { l = old[(size_t) i]; continue; }     // lanes 9-16 are yours: as they were
        l.sound = old[(size_t) i].sound;
        l.volume = old[(size_t) i].volume;
        l.slice = old[(size_t) i].slice;
    }
    if (! volumesSet)
    {
        // a mix that doesn't go into the red: kick and bass up front, the rest under them
        const int v[kNumLanes] = { 60, 48, 44, 34, 34, 30, 50, 36, 48, 48, 48, 48, 48, 48, 48, 48 };
        for (int i = 0; i < kNumLanes; ++i) pattern.lanes[(size_t) i].volume = v[i];
        volumesSet = true;
    }
    refreshControls();
    apply();
    repaint();
    setStatus (styleName (s.style) + " AT " + String (s.bpm) + " BPM - CLICK THE STEPS TO CHANGE THEM, NEW MAKES ONE LANE AGAIN");
}

void MakerComponent::regenerateMelodic()
{
    for (int i = 0; i < kNumLanes; ++i)
        if (isMelodic (pattern.lanes[(size_t) i].kind)) regenerateLane (pattern, i, Random::getSystemRandom().nextInt64());
    refreshControls();
    apply();
    repaint();
}

void MakerComponent::refreshControls()
{
    const auto& s = pattern.settings;
    styleLabel.setText (styleName (s.style));
    bpmBox.setValue (s.bpm);
    swingBox.setValue (s.swing);
    keyBox.setValue (s.key);
    busyBox.setValue (s.busy);
    scaleBtn.setButtonText (scaleName (s.scale));
    fillBtn.setToggleState (s.fill, dontSendNotification);
    for (int i = 0; i < 3; ++i) barsBtns[(size_t) i]->setToggleState ((i == 0 ? 1 : (i == 1 ? 2 : 4)) == s.bars, dontSendNotification);
    for (int i = 0; i < 4; ++i)
    {
        barTabs[(size_t) i]->setVisible (i < s.bars);
        barTabs[(size_t) i]->setToggleState (i == viewBar, dontSendNotification);
    }
    copyBarBtn.setVisible (s.bars > 1);
    for (int i = 0; i < 2; ++i) laneTabs[(size_t) i]->setToggleState (i == laneView, dontSendNotification);
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& r = rows[(size_t) i];
        const auto& l = pattern.lanes[(size_t) i];
        const bool inView = i / 8 == laneView;
        for (auto* c : std::initializer_list<Component*> { r.onBtn.get(), r.soundBtn.get(), r.vol.get(), r.grid.get() }) c->setVisible (inView);
        r.diceBtn->setVisible (inView && i < kNumGenLanes && ! l.slice);
        r.onBtn->setButtonText (laneLabel (i));
        r.onBtn->setToggleState (l.on, dontSendNotification);
        const File f (l.sound);
        String name = f.getFileNameWithoutExtension().toUpperCase();
        if (l.slice && name.contains (" SLICE "))          // "BREAK SLICE 03" -> "SLICE 03 BREAK"
            name = name.fromLastOccurrenceOf (" SLICE ", true, false).trim() + " " + name.upToLastOccurrenceOf (" SLICE ", false, false);
        if (name.length() > 19) name = name.substring (0, 18).trimEnd() + ".";
        r.soundBtn->setButtonText (l.sound.isEmpty() ? String (i < kNumGenLanes ? "BUILT-IN SOUND" : "NO SOUND - CHOOSE") : name);
        r.vol->setValue (l.volume);
        r.grid->bar = viewBar;
        r.grid->repaint();
    }
}

void MakerComponent::chooseSound (int lane)
{
    auto& l = pattern.lanes[(size_t) lane];
    PopupMenu m;
    Array<File> files;
    for (auto& folder : lib.foldersFor (l.kind))
    {
        auto list = folder.findChildFiles (File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac");
        list.sort();
        if (list.isEmpty()) continue;
        m.addSectionHeader (folder.getFileName().toUpperCase());
        for (auto& f : list) { files.add (f); m.addItem (files.size(), f.getFileNameWithoutExtension().toUpperCase(), true, f.getFullPathName() == l.sound); }
    }
    m.addSeparator();
    m.addItem (10000, "CHOOSE A FILE ...");
    if (lane >= kNumGenLanes) m.addItem (10002, "EMPTY (NO SOUND)", true, l.sound.isEmpty());
    if (! lib.isValid()) m.addItem (10001, "(THE FOUR TRACKER SAMPLE PACK WAS NOT FOUND)", false);
    Component::SafePointer<MakerComponent> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (rows[(size_t) lane].soundBtn.get()), [safe, files, lane] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& self = *safe;
        if (r == 10000)
        {
            self.chooser = std::make_unique<FileChooser> ("Choose a sound for " + laneLabel (lane),
                                                         self.lib.pack, self.proc.getSupportedAudioWildcard());
            self.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe, lane] (const FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == File()) return;
                safe->setLaneSound (lane, fc.getResult(), false);
            });
            return;
        }
        if (r == 10002)
        {
            auto& l = self.pattern.lanes[(size_t) lane];
            l.sound = {};
            l.slice = false;
            l.on = false;
            self.refreshControls();
            self.apply();
            return;
        }
        if (r - 1 < files.size())
        {
            self.setLaneSound (lane, files[r - 1], false);
            self.proc.getEngine().requestPreview (lane + 1, ft::kNativeNote);   // hear the new sound
        }
    });
}

void MakerComponent::apply (bool forSave)
{
    auto& e = proc.getEngine();
    // each lane's sound in slot 01..08 (lane order); loaded again only when it changed
    std::array<int, kNumLanes> slots {};
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& l = pattern.lanes[(size_t) i];
        const int slot = i + 1;
        if (i >= kNumGenLanes && l.sound.isEmpty()) { slots[(size_t) i] = 0; continue; }   // an empty lane 9-16: not played
        slots[(size_t) i] = slot;
        if (loadedSound[(size_t) i] != l.sound || proc.getInstrument (slot).isEmpty())
        {
            String err = "?";
            if (l.sound.isNotEmpty() && File (l.sound).existsAsFile()) err = proc.loadInstrument (slot, File (l.sound));
            if (err.isNotEmpty())
            {
                if (i < kNumGenLanes)
                {
                    // no pack: Four Tracker's own demo sounds
                    const int b[] = { 1, 2, 2, 3, 3, 4, 5, 6 };
                    proc.getInstrument (slot) = builtIn[(size_t) b[i]];
                }
                else slots[(size_t) i] = 0;
                if (l.sound.isNotEmpty()) { l.sound = {}; l.slice = false; }
            }
            loadedSound[(size_t) i] = l.sound;
        }
        auto& in = proc.getInstrument (slot);
        if (! in.isEmpty())
        {
            in.params.volume = l.volume;
            proc.instrumentChanged (slot);
        }
    }

    int lanesOn = 0;
    for (int i = 0; i < kNumLanes; ++i) if (pattern.lanes[(size_t) i].on && slots[(size_t) i] > 0) ++lanesOn;
    const auto cells = toCells (pattern, slots);
    {
        const ScopedLock sl (e.getDataLock());
        auto& pat = e.getPattern (0);
        pat.clear();
        pat.numRows = pattern.steps();
        for (auto& c : cells) pat.at (c.row, c.channel) = c.cell;
        // the count-in: an empty bar in pattern 01, before pattern 00 - the metronome clicks it
        // (its own sound, not a lane), and the engine goes on to the pattern right at its end
        if (! forSave)
        {
            auto& ci = e.getPattern (1);
            ci.clear();
            ci.numRows = 16;
        }
    }
    e.numChannels = lanesOn <= 4 ? 4 : (lanesOn <= 8 ? 8 : 16);
    e.bpm = pattern.settings.bpm;
    e.speed = 6;
    e.loopPattern = true;
    e.selectedPattern = 0;
    for (int c = 0; c < ft::kNumChannels; ++c) { e.mix[c].volume = 64; e.mix[c].mute = false; e.mix[c].solo = false; }
}

void MakerComponent::togglePlay()
{
    auto& e = proc.getEngine();
    if (e.playing.load())
    {
        e.requestStop();
        if (armed) { armed = false; setStatus ("RECORDED - REC AGAIN TO PLAY MORE IN, UNDO TO TAKE IT BACK"); }
        pendingApplyRow = -1.0;
        apply();
        refreshRecControls();
        return;
    }
    apply();
    countInRun = false;
    e.selectedPattern = 0;
    e.queuedPattern = -1;
    clock.running = false;        // the clock starts again with this PLAY
    e.requestStart();
}

// ===========================================================================
// BLANK + REC
// ===========================================================================

void MakerComponent::blank()
{
    undoPattern = pattern;
    hasUndo = true;
    clearSteps (pattern);
    apply();
    refreshControls();
    refreshRecControls();
    repaint();
    setStatus ("BLANK - PRESS REC (R): ONE BAR COUNT-IN, THEN PLAY THE PADS OR KEYS. KEYS 1-8 PLAY THE LANES TOO");
}

void MakerComponent::setMetronome (bool on)
{
    metronome = on;
    metronomeOut.metroOn = on;
    refreshRecControls();
}

void MakerComponent::toggleRec()
{
    auto& e = proc.getEngine();
    if (armed)
    {
        armed = false;                                  // punch out: it keeps playing
        refreshRecControls();
        setStatus ("REC OFF - UNDO TAKES THE LAST TAKE BACK");
        return;
    }
    undoPattern = pattern;
    hasUndo = true;
    armed = true;
    if (! e.playing.load())
    {
        apply();
        e.switchMode = (int) ft::SwitchMode::EndOfPattern;
        e.queuedPattern = -1;
        if (countIn)
        {
            countInRun = true;
            e.selectedPattern = 1;                      // one empty bar, the metronome counting ...
            clock.running = false;        // the clock starts again with this PLAY
            e.requestStart();
            e.requestPattern (0);                       // ... then the pattern
            setStatus ("COUNT-IN: 1 2 3 4 - THEN EVERY HIT IS RECORDED ON THE NEAREST 16TH. IT GOES ROUND: PLAY MORE IN EACH TIME");
        }
        else
        {
            countInRun = false;
            e.selectedPattern = 0;
            clock.running = false;        // the clock starts again with this PLAY
            e.requestStart();
            setStatus ("RECORDING - EVERY HIT GOES ON THE NEAREST 16TH");
        }
    }
    else
        setStatus ("RECORDING (PUNCHED IN)");
    refreshRecControls();
}

void MakerComponent::undoTake()
{
    if (! hasUndo) { setStatus ("NOTHING TO UNDO"); return; }
    std::swap (pattern, undoPattern);
    apply();
    refreshControls();
    repaint();
    setStatus ("UNDONE (UNDO AGAIN TO GET IT BACK)");
}

void MakerComponent::doubleBars()
{
    if (pattern.settings.bars >= 4) { setStatus ("ALREADY 4 BARS - THE MOST THERE IS"); return; }
    undoPattern = pattern;
    hasUndo = true;
    const int old = pattern.steps();
    pattern.settings.bars *= 2;
    for (auto& l : pattern.lanes)
        for (int st = old; st < pattern.steps(); ++st) l.steps[(size_t) st] = l.steps[(size_t) (st % old)];
    refreshControls();
    refreshRecControls();
    apply();
    repaint();
    setStatus ("DOUBLED: " + String (old / 16) + (old == 16 ? " BAR" : " BARS") + " IS NOW " + String (pattern.settings.bars)
               + " BARS - CHANGE THE COPY, OR PLAY MORE IN WITH REC");
}

// ===========================================================================
// 16 lanes, the SLICER
// ===========================================================================

void MakerComponent::setLaneSound (int lane, const File& f, bool isSlice)
{
    if (! isPositiveAndBelow (lane, kNumLanes)) return;
    auto& l = pattern.lanes[(size_t) lane];
    l.sound = f.getFullPathName();
    l.slice = isSlice;
    l.on = true;
    loadedSound[(size_t) lane] = "<new>";          // load it again even if the name is the same (a new slice)
    if (lane / 8 != laneView && ! isSlice) setLaneView (lane / 8);
    refreshControls();
    apply();
    repaint();
}

void MakerComponent::setBpm (int bpm)
{
    bpm = jlimit (40, 255, bpm);
    bpmBox.setValue (bpm);
    pattern.settings.bpm = bpm;
    apply();
}

void MakerComponent::setLaneView (int v)
{
    laneView = jlimit (0, 1, v);
    refreshControls();
    repaint();
}

void MakerComponent::setAudioDevices (AudioDeviceManager* d)
{
    if (audioDevices != nullptr) audioDevices->removeAudioCallback (&metronomeOut);
    audioDevices = d;
    // after the engine's player: it clicks on the rows the engine has just begun in the same block
    if (audioDevices != nullptr) audioDevices->addAudioCallback (&metronomeOut);
}

namespace
{
    class SlicerWindow : public DocumentWindow
    {
    public:
        explicit SlicerWindow (std::function<void()> close)
            : DocumentWindow ("Four Tracker Pattern Maker - SLICER", Theme::titleBar, DocumentWindow::closeButton), onCloseWin (std::move (close)) {}
        void closeButtonPressed() override { if (onCloseWin) onCloseWin(); }
        std::function<void()> onCloseWin;
    };
}

void MakerComponent::openSlicer()
{
    if (slicer == nullptr)
    {
        slicer = std::make_unique<SlicerComponent> (*this);
        slicer->onClose = [this] { MessageManager::callAsync ([safe = selfSafe] { if (safe != nullptr) safe->closeSlicer(); }); };
        if (audioDevices != nullptr) audioDevices->addAudioCallback (&slicer->player);
    }
    if (slicerWindow == nullptr)
    {
        if (Desktop::getInstance().getDisplays().getPrimaryDisplay() == nullptr) return;    // no screen (the test)
        auto w = std::make_unique<SlicerWindow> ([this] { closeSlicer(); });
        w->setUsingNativeTitleBar (true);
        w->setContentNonOwned (slicer.get(), true);
        w->setResizable (false, false);
        w->centreWithSize (SlicerComponent::kWidth, SlicerComponent::kHeight);
        w->setAlwaysOnTop (true);
        slicerWindow = std::move (w);
    }
    slicerWindow->setVisible (true);
    slicerWindow->toFront (true);
    slicer->grabKeyboardFocus();
    setStatus ("SLICER: LOAD A LONG SAMPLE, PLAY THE SLICES ON THE PADS, GENERATE SAMPLE LIST PUTS THEM ON THE LANES");
}

void MakerComponent::closeSlicer()
{
    if (slicer != nullptr) slicer->player.stop();
    if (slicerWindow != nullptr) slicerWindow->setVisible (false);
    grabKeyboardFocus();
}

void MakerComponent::refreshRecControls()
{
    recBtn.setToggleState (armed, dontSendNotification);
    countBtn.setToggleState (countIn, dontSendNotification);
    metroBtn.setToggleState (metronome, dontSendNotification);
    velBtn.setToggleState (useVelocity, dontSendNotification);
    modeBtn.setButtonText (padMode ? "INPUT: DRUM PADS" : "INPUT: KEYBOARD");
    keysBtn.setButtonText (padMode ? String ("KEYS: -") : (keysLane < 0 ? String ("KEYS: OFF") : String (keysLane == 7 ? "KEYS: ARP" : "KEYS: BASS")));
    keysBtn.setEnabled (! padMode);
    midiBtn.setButtonText ("MIDI IN");
    midiBtn.setTooltip (midiInName);
    undoBtn.setEnabled (hasUndo);
    doubleBtn.setEnabled (pattern.settings.bars < 4);
}

void MakerComponent::setPadMode (bool drumPads)
{
    padMode = drumPads;
    learnLane = -1;
    refreshRecControls();
    saveMidiSettings();
    if (padMode)
        setStatus ("INPUT: DRUM PADS - PADS 1-8: KICK, SNARE, CLAP, CLOSED HAT, OPEN HAT, PERC, BASS, ARP. OTHER PADS ARE QUIET");
    else
        setStatus ("INPUT: KEYBOARD - DRUMS ON THE GENERAL MIDI KEYS (KICK C-2, SNARE D-2, HAT F#2), THE REST PLAYS "
                   + String (keysLane == 7 ? "THE ARP" : (keysLane < 0 ? "NOTHING (KEYS: OFF)" : "THE BASS")));
}

int MakerComponent::laneForMidi (int midiNote) const
{
    const int lane = (padMode ? padPads : keyPads).laneFor (midiNote);
    if (lane >= 0 || padMode) return lane;
    return keysLane;
}

void MakerComponent::learnPad (int lane, bool thenTheNext)
{
    learnLane = lane;
    learnChain = thenTheNext;
    setStatus ("LEARN: HIT THE PAD OR KEY FOR " + laneLabel (lane) + (thenTheNext ? "   (ESC: STOP)" : ""));
}

void MakerComponent::RowClock::hiResTimerCallback()
{
    const double now = Time::getMillisecondCounterHiRes();
    if (! engine.playing.load())
    {
        running = false;
        lastPattern = lastRow = -1;
        return;
    }
    const int p = engine.playPattern.load(), r = engine.playRow.load();
    if (! running.load())
    {
        absRow = (double) r;
        changeMs = now;
        lastPattern = p; lastRow = r;
        running = true;
        return;
    }
    if (p == lastPattern && r == lastRow) return;
    int delta = 1;
    if (p == lastPattern)
    {
        const int len = jmax (1, engine.getPattern (p).getNumRows());
        delta = ((r - lastRow) % len + len) % len;
        if (delta == 0) delta = len;
    }
    else
    {
        const int len = jmax (1, engine.getPattern (lastPattern).getNumRows());
        delta = jmax (1, len - lastRow + r);
    }
    absRow = absRow.load() + delta;
    changeMs = now;
    lastPattern = p; lastRow = r;
}

double MakerComponent::positionAt (double timeMs) const
{
    const double rowMs = 15000.0 / jmax (1, pattern.settings.bpm);     // SPEED 6: 4 rows to a beat
    if (! clock.running.load())
    {
        // just started (the clock hasn't seen it yet): the engine's row is near enough
        auto& e = proc.getEngine();
        if (! e.playing.load()) return std::numeric_limits<double>::quiet_NaN();
        return (e.playPattern.load() == 1 && countInRun ? e.playRow.load() - 16.0 : (double) e.playRow.load()) + 0.5;
    }
    const double lat = outputLatencyMs ? outputLatencyMs() : 0.0;
    const double abs = clock.absRow.load() + (timeMs - lat - clock.changeMs.load()) / rowMs;
    return abs - (countInRun ? 16.0 : 0.0);
}

void MakerComponent::scheduleApply (double absRow)
{
    pendingApplyRow = jmax (pendingApplyRow, absRow);
}

void MakerComponent::recordAt (int lane, int velocity127, int midiNote, double pos)
{
    if (! isPositiveAndBelow (lane, kNumLanes)) return;
    const int step = quantizeStep (pos, pattern.steps());
    if (step < 0) return;
    const auto kind = pattern.lanes[(size_t) lane].kind;
    const bool melodic = laneMelodic (pattern.lanes[(size_t) lane]);
    const int note = melodic && midiNote >= 0 ? noteForKey (pattern.settings, kind, midiNote) : 0;
    const bool wasOn = pattern.lanes[(size_t) lane].on;
    recordHit (pattern, lane, step, velocity127, note, useVelocity);
    if (melodic && midiNote >= 0 && midiNote < 128) held[(size_t) midiNote] = { lane, step, pos };
    if (! wasOn) refreshControls();
    rows[(size_t) lane].grid->repaint();
}

void MakerComponent::padDown (int lane, int velocity127, int midiNote, double timeMs)
{
    if (! isPositiveAndBelow (lane, kNumLanes)) return;
    auto& e = proc.getEngine();
    const auto& ln = pattern.lanes[(size_t) lane];
    const auto kind = ln.kind;
    if (lane >= kNumGenLanes && ln.sound.isEmpty())
    {
        setStatus (laneLabel (lane) + " HAS NO SOUND YET - CHOOSE ONE, OR PUT A SLICE THERE WITH THE SLICER");
        return;
    }
    // hear it now (a jam voice: the pattern's own notes don't cut it)
    int jamNote = ft::kNativeNote;
    if (laneMelodic (ln))
    {
        const int key = pattern.settings.key > 6 ? pattern.settings.key - 12 : pattern.settings.key;
        jamNote = midiNote >= 0 ? ft::kNativeNote + midiNote - keysRoot (kind) : ft::kNativeNote + key;
    }
    e.requestJam (lane + 1, jlimit (1, 96, jamNote), jamChannel (lane));
    flash[(size_t) lane] = Time::getMillisecondCounterHiRes();

    if (! armed || ! e.playing.load()) return;
    const double pos = positionAt (timeMs);
    if (! std::isfinite (pos)) return;
    recordAt (lane, velocity127, midiNote, pos);
    // into the engine once it has passed the row (a hit just before its step would sound twice)
    scheduleApply (clock.absRow.load() + 2.0);
}

void MakerComponent::padUp (int midiNote, double timeMs)
{
    if (! isPositiveAndBelow (midiNote, 128)) return;
    auto& h = held[(size_t) midiNote];
    if (h.lane < 0) return;
    proc.getEngine().requestJamOff (jamChannel (h.lane));
    const double pos = positionAt (timeMs);
    if (armed && std::isfinite (pos) && h.step >= 0)
    {
        auto& st = pattern.lanes[(size_t) h.lane].steps[(size_t) h.step];
        st.len = jlimit (1, 16, roundToInt (pos - h.pos));
        rows[(size_t) h.lane].grid->repaint();
        scheduleApply (clock.absRow.load() + 2.0);
    }
    h = {};
}

void MakerComponent::midiNoteOn (int midiNote, int velocity127, double timeMs)
{
    if (learnLane >= 0)
    {
        getPads().assign (learnLane, midiNote);
        const int was = learnLane;
        padDown (was, velocity127, -1, timeMs);
        if (learnChain && was < (padMode ? kNumLanes - 1 : 5)) learnPad (was + 1, true);   // 16 pads, or the six drum keys
        else
        {
            learnLane = -1;
            setStatus (laneLabel (was) + " = " + ft::midiNoteName (midiNote) + (learnChain ? "  - ALL PADS LEARNT" : ""));
        }
        saveMidiSettings();
        return;
    }
    if (slicerWindow != nullptr && slicerWindow->isVisible() && slicer != nullptr)
    {
        // the SLICER is open: the pads play its slices (pad 1 = C-2 ...)
        int pad = getPads().laneFor (midiNote);
        if (pad < 0 && ! padMode) pad = midiNote - 36;
        if (isPositiveAndBelow (pad, 16)) slicer->padHit (pad);
        return;
    }
    const int lane = laneForMidi (midiNote);
    if (lane < 0) return;
    const bool isPad = getPads().laneFor (midiNote) >= 0;
    padDown (lane, velocity127, isPad ? -1 : midiNote, timeMs);      // a key plays the bass / arp at its pitch
}

void MakerComponent::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    // the time now, on the MIDI thread; the rest on the message thread
    const double now = Time::getMillisecondCounterHiRes();
    if (! (m.isNoteOn() || m.isNoteOff())) return;
    const bool on = m.isNoteOn();
    const int note = m.getNoteNumber(), vel = m.getVelocity();
    auto safe = selfSafe;             // made on the message thread (a copy here is safe)
    MessageManager::callAsync ([safe, on, note, vel, now]
    {
        if (safe == nullptr) return;
        if (on) safe->midiNoteOn (note, vel, now);
        else    safe->padUp (note, now);
    });
}

void MakerComponent::openMidiInputs()
{
    for (auto& in : inputs) in->stop();
    inputs.clear();
    openInputNames.clear();
    if (midiInName == "NONE") return;
    for (auto& d : MidiInput::getAvailableDevices())
    {
        if (midiInName != "ALL" && d.name != midiInName) continue;
        if (auto in = MidiInput::openDevice (d.identifier, this))
        {
            in->start();
            openInputNames.add (d.name);
            inputs.push_back (std::move (in));
        }
    }
}

static File midiSettingsFile() { return FourTrackerProcessor::getUserFolder().getChildFile ("Pattern Maker MIDI.json"); }

void MakerComponent::loadMidiSettings()
{
    const auto v = JSON::parse (midiSettingsFile());
    if (! v.isObject()) return;
    if (v.hasProperty ("in")) midiInName = v["in"].toString();
    if (v.hasProperty ("keys")) keysLane = jlimit (-1, 7, (int) v["keys"]);
    if (keysLane >= 0 && keysLane < 6) keysLane = 6;
    if (v.hasProperty ("velocity")) useVelocity = (bool) v["velocity"];
    if ((int) v["padsVersion"] >= 2)          // version 1 had only the General MIDI map
    {
        auto read = [] (const var& arr, PadMap& m)
        {
            if (auto* a = arr.getArray())
                for (int i = 0; i < jmin (kNumLanes, a->size()); ++i) m.note[(size_t) i] = jlimit (-1, 127, (int) (*a)[i]);
        };
        read (v["pads"], padPads);
        read (v["keyPads"], keyPads);
        if (v.hasProperty ("mode")) padMode = v["mode"].toString() != "keyboard";
    }
}

void MakerComponent::saveMidiSettings() const
{
    auto* o = new DynamicObject();
    o->setProperty ("in", midiInName);
    o->setProperty ("keys", keysLane);
    o->setProperty ("velocity", useVelocity);
    Array<var> a, k;
    for (auto n : padPads.note) a.add (n);
    for (auto n : keyPads.note) k.add (n);
    o->setProperty ("pads", a);
    o->setProperty ("keyPads", k);
    o->setProperty ("mode", padMode ? "pads" : "keyboard");
    o->setProperty ("padsVersion", 2);
    const auto f = midiSettingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (JSON::toString (var (o)));
}

void MakerComponent::paintOverChildren (Graphics& g)
{
    // a lane lights up when its pad is hit
    const double now = Time::getMillisecondCounterHiRes();
    for (int i = 0; i < kNumLanes; ++i)
        if (now - flash[(size_t) i] < 140.0 && rows[(size_t) i].onBtn->isVisible())
        {
            g.setColour (Colour (0xffd9342b));
            g.drawRect (rows[(size_t) i].onBtn->getBounds().expanded (3), 3);
        }
    if (learnLane >= 0 && rows[(size_t) learnLane].onBtn->isVisible())
    {
        g.setColour (Colour (0xffffc23a));
        g.drawRect (rows[(size_t) learnLane].onBtn->getBounds().expanded (3), 3);
    }
}

juce::File MakerComponent::suggestedFile() const
{
    const auto& s = pattern.settings;
    const auto name = styleName (s.style) + " " + String (s.bpm) + " " + keyName (s.key) + (s.scale == Scale::Major ? " MAJ" : " MIN");
    return FourTrackerProcessor::getUserFolder().getChildFile ("Patterns").getChildFile (File::createLegalFileName (name) + ".ftpat");
}

juce::String MakerComponent::saveTo (const juce::File& file)
{
    apply (true);
    file.getParentDirectory().createDirectory();
    const auto err = proc.savePattern (0, file);
    apply (false);
    return err;
}

void MakerComponent::timerCallback()
{
    auto& e = proc.getEngine();
    const bool playing = e.playing.load();
    playBtn.setToggleState (playing, dontSendNotification);
    playBtn.setButtonText (playing ? "STOP" : "PLAY");
    const int ps = playing && e.playPattern.load() == 0 ? e.playRow.load() : -1;
    for (auto& r : rows)
        if (r.grid->playStep != ps) { r.grid->playStep = ps; r.grid->repaint(); }
    // a recorded hit goes into the engine once the engine has passed its row
    if (pendingApplyRow >= 0.0 && (! playing || clock.absRow.load() >= pendingApplyRow))
    {
        pendingApplyRow = -1.0;
        apply();
    }
    if (armed) recBtn.setButtonText (playing && countInRun && e.playPattern.load() == 1 ? "COUNT " + String (e.playRow.load() / 4 + 1) : String ("REC"));
    else       recBtn.setButtonText ("REC");
    repaint();      // the pads' lights (cheap: 30 times a second)
    // the bar in view follows the play position
    if (playing && ps >= 0 && pattern.settings.bars > 1 && ps / 16 != viewBar && followPlay)
    {
        viewBar = ps / 16;
        refreshControls();
    }
}

bool MakerComponent::keyPressed (const KeyPress& k)
{
    if (k.getKeyCode() == KeyPress::spaceKey || k.getKeyCode() == KeyPress::returnKey) { togglePlay(); return true; }
    if (k.getKeyCode() == 'G' || k.getKeyCode() == 'g') { generateAll(); return true; }
    if (k.getKeyCode() == KeyPress::escapeKey)
    {
        if (learnLane >= 0) { learnLane = -1; setStatus ("LEARN STOPPED"); return true; }
        if (proc.getEngine().playing.load()) togglePlay();
        return true;
    }
    if (k.getKeyCode() == 'R' || k.getKeyCode() == 'r') { toggleRec(); return true; }
    if (k.getKeyCode() == 'M' || k.getKeyCode() == 'm') { setMetronome (! metronome); return true; }
    if (k.getKeyCode() == 'D' || k.getKeyCode() == 'd') { doubleBars(); return true; }
    if (k.getKeyCode() >= '1' && k.getKeyCode() <= '8' && ! k.getModifiers().isAnyModifierKeyDown())
    {
        padDown (laneView * 8 + k.getKeyCode() - '1', 110, -1, Time::getMillisecondCounterHiRes());
        return true;
    }
    return false;
}

void MakerComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    Theme::drawPanel (g, topPanel.toFloat());
    Theme::text (g, "PATTERN MAKER", { 24.0f, 16.0f, 440.0f, 42.0f }, 32.0f, Theme::ink, Justification::centredLeft, true);
    Theme::text (g, "FOR FOUR TRACKER  -  V1.4.2", { 28.0f, 58.0f, 440.0f, 20.0f }, 15.0f, Theme::ink.withAlpha (0.65f), Justification::centredLeft);

    auto label = [&] (const String& t, Component& c)
    {
        Theme::text (g, t, { (float) c.getX(), (float) c.getY() - 22.0f, (float) jmax (90, c.getWidth()), 20.0f }, 15.0f, Theme::ink, Justification::bottomLeft);
    };
    label ("STYLE", stylePrev);
    label ("BPM", bpmBox);
    label ("BARS", *barsBtns[0]);
    label ("SWING", swingBox);
    label ("KEY", keyBox);
    label ("SCALE", scaleBtn);
    label ("BUSY", busyBox);

    Theme::drawPanel (g, lanesPanel.toFloat());
    // the beat numbers over the grid
    if (auto* grid = rows[(size_t) laneView * 8].grid.get())
    {
        const float cw = grid->getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            const int stepNo = viewBar * 16 + i;
            Theme::text (g, i % 4 == 0 ? String (viewBar * 4 + i / 4 + 1) : String ("."),
                         { (float) grid->getX() + i * cw, (float) grid->getY() - 26.0f, cw, 20.0f }, 15.0f,
                         i % 4 == 0 ? Theme::ink : Theme::ink.withAlpha (0.45f), Justification::centred, i % 4 == 0);
            ignoreUnused (stepNo);
        }
    }
    Theme::text (g, "VOL", { (float) rows[0].vol->getX(), (float) rows[0].vol->getY() - 26.0f, (float) rows[0].vol->getWidth(), 20.0f }, 15.0f, Theme::ink, Justification::centred);
    ignoreUnused (kNumGenLanes);

    Theme::drawScreen (g, statusArea.toFloat());
    Theme::text (g, status, statusArea.toFloat().reduced (14.0f, 0.0f), 16.0f, Theme::screenText, Justification::centredLeft);
    fitText (g, "CLICK: STEP ON / OFF    RIGHT-CLICK: SOFT HIT / NOTE    WHEEL: NOTE    SHIFT+WHEEL: LONGER    SPACE: PLAY    G: GENERATE    R: REC    M: METRO    D: DOUBLE    1-8: PLAY A LANE    SLICER: CUT A LONG SAMPLE",
             { 24.0f, (float) getHeight() - 34.0f, (float) getWidth() - 48.0f, 24.0f }, 13.0f, Theme::ink.withAlpha (0.65f), Justification::centredLeft);
}

void MakerComponent::resized()
{
    topPanel = { 12, 12, getWidth() - 24, 160 };
    int y = 104, h = 48;
    stylePrev.setBounds (24, y, 52, h);
    styleLabel.setBounds (80, y, 250, h);
    styleNext.setBounds (334, y, 52, h);
    bpmBox.setBounds (410, y, 120, h);
    for (int i = 0; i < 3; ++i) barsBtns[(size_t) i]->setBounds (552 + i * 52, y, 48, h);
    swingBox.setBounds (726, y, 100, h);
    keyBox.setBounds (848, y, 100, h);
    scaleBtn.setBounds (970, y, 200, h);
    busyBox.setBounds (1192, y, 112, h);
    fillBtn.setBounds (1316, y, 72, h);

    generateBtn.setBounds (500, 24, 230, 58);
    playBtn.setBounds (742, 24, 170, 58);
    saveBtn.setBounds (924, 24, 270, 58);
    audioBtn.setBounds (getWidth() - 24 - 110, 24, 110, 58);

    lanesPanel = { 12, 182, getWidth() - 24, getHeight() - 182 - 96 };
    const int gx = 470, gw = getWidth() - gx - 30;
    for (int i = 0; i < 4; ++i) barTabs[(size_t) i]->setBounds (gx + i * 130, 192, 122, 36);
    copyBarBtn.setBounds (gx + gw - 170, 192, 170, 36);
    const int top = 262, rh = 54;
    laneTabs[0]->setBounds (24, 192, 132, 36);
    laneTabs[1]->setBounds (162, 192, 132, 36);
    slicerBtn.setBounds (314, 192, 140, 36);
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& r = rows[(size_t) i];
        const int ry = top + (i % 8) * (rh + 4);
        r.onBtn->setBounds (24, ry, 150, rh);
        r.soundBtn->setBounds (180, ry, 190, rh);
        r.vol->setBounds (376, ry + 10, 52, rh - 20);
        r.diceBtn->setBounds (gx - 38, ry + 10, 32, rh - 20);
        r.grid->setBounds (gx, ry, gw, rh);
    }
    for (auto& r : rows) r.diceBtn->setBounds (432, r.diceBtn->getY(), 34, r.diceBtn->getHeight());
    // BLANK + REC, under the lanes
    recStrip = { 24, top + 8 * (rh + 4) + 18, getWidth() - 48, 46 };
    {
        int x = recStrip.getX();
        const int y = recStrip.getY(), h = recStrip.getHeight();
        auto put = [&] (Component& c, int w) { c.setBounds (x, y, w, h); x += w + 10; };
        put (blankBtn, 100);
        put (recBtn, 120);
        put (countBtn, 120);
        put (metroBtn, 100);
        put (velBtn, 110);
        put (undoBtn, 90);
        put (doubleBtn, 100);
        x += 12;
        put (modeBtn, 190);
        put (padsBtn, 80);
        put (keysBtn, 140);
        midiBtn.setBounds (x, y, recStrip.getRight() - x, h);
    }
    statusArea = { 12, getHeight() - 84, getWidth() - 24, 44 };
    refreshControls();
}
