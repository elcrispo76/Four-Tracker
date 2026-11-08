// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#include "MakerComponent.h"
#include "UI/PixelFont.h"

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
    static const char* table[(int) Style::NumStyles][kNumLanes] = {
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
    const bool melodic = isMelodic (l.kind);
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
        if (! isMelodic (l.kind))
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
            if (isMelodic (l.kind))
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
    if (! isMelodic (l.kind) || st >= p.steps() || ! l.steps[(size_t) st].on) return;
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

MakerComponent::MakerComponent (FourTrackerProcessor& processor, SoundLibrary library) : proc (processor), lib (std::move (library))
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
        r.onBtn = std::make_unique<PixelButton> ("", PixelButton::Style::Record, 16.0f);
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
            setStatus ("NEW " + laneName (pattern.lanes[(size_t) i].kind));
        };
        r.vol = std::make_unique<ValueBox> (0, 64, 64, 20.0f);
        r.vol->setJustification (Justification::centred);
        r.vol->onChange = [this, i] (int v) { pattern.lanes[(size_t) i].volume = v; apply(); };
        r.grid = std::make_unique<StepGrid> ([this]() -> Pattern& { return pattern; }, i);
        r.grid->onEdited = [this] { apply(); };
        for (auto* c : std::initializer_list<Component*> { r.onBtn.get(), r.soundBtn.get(), r.diceBtn.get(), r.vol.get(), r.grid.get() })
            addAndMakeVisible (c);
    }

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
    for (int i = 0; i < kNumLanes; ++i) loadedSound[(size_t) i] = {};   // the style's own sounds
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto f = lib.defaultSound (st, (LaneKind) i);
        pattern.lanes[(size_t) i].sound = f.getFullPathName();
    }
    generateAll();
}

void MakerComponent::generateAll()
{
    auto s = pattern.settings;
    s.seed = Random::getSystemRandom().nextInt64();
    std::array<String, kNumLanes> sounds;
    std::array<int, kNumLanes> vols;
    for (int i = 0; i < kNumLanes; ++i) { sounds[(size_t) i] = pattern.lanes[(size_t) i].sound; vols[(size_t) i] = pattern.lanes[(size_t) i].volume; }
    pattern = generate (s);
    for (int i = 0; i < kNumLanes; ++i) { pattern.lanes[(size_t) i].sound = sounds[(size_t) i]; pattern.lanes[(size_t) i].volume = vols[(size_t) i]; }
    if (! volumesSet)
    {
        // a mix that doesn't go into the red: kick and bass up front, the rest under them
        const int v[kNumLanes] = { 60, 48, 44, 34, 34, 30, 50, 36 };
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
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& r = rows[(size_t) i];
        const auto& l = pattern.lanes[(size_t) i];
        r.onBtn->setButtonText (laneName (l.kind));
        r.onBtn->setToggleState (l.on, dontSendNotification);
        const File f (l.sound);
        r.soundBtn->setButtonText (l.sound.isEmpty() ? String ("BUILT-IN SOUND") : f.getFileNameWithoutExtension().toUpperCase());
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
    if (! lib.isValid()) m.addItem (10001, "(THE FOUR TRACKER SAMPLE PACK WAS NOT FOUND)", false);
    Component::SafePointer<MakerComponent> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (rows[(size_t) lane].soundBtn.get()), [safe, files, lane] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& self = *safe;
        if (r == 10000)
        {
            self.chooser = std::make_unique<FileChooser> ("Choose a sound for " + laneName (self.pattern.lanes[(size_t) lane].kind),
                                                         self.lib.pack, self.proc.getSupportedAudioWildcard());
            self.chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe, lane] (const FileChooser& fc)
            {
                if (safe == nullptr || fc.getResult() == File()) return;
                safe->pattern.lanes[(size_t) lane].sound = fc.getResult().getFullPathName();
                safe->refreshControls();
                safe->apply();
            });
            return;
        }
        if (r - 1 < files.size())
        {
            self.pattern.lanes[(size_t) lane].sound = files[r - 1].getFullPathName();
            self.refreshControls();
            self.apply();
            self.proc.getEngine().requestPreview (lane + 1, ft::kNativeNote);   // hear the new sound
        }
    });
}

void MakerComponent::apply()
{
    auto& e = proc.getEngine();
    // each lane's sound in slot 01..08 (lane order); loaded again only when it changed
    std::array<int, kNumLanes> slots {};
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& l = pattern.lanes[(size_t) i];
        const int slot = i + 1;
        slots[(size_t) i] = slot;
        if (loadedSound[(size_t) i] != l.sound || proc.getInstrument (slot).isEmpty())
        {
            String err = "?";
            if (l.sound.isNotEmpty() && File (l.sound).existsAsFile()) err = proc.loadInstrument (slot, File (l.sound));
            if (err.isNotEmpty())
            {
                // no pack: Four Tracker's own demo sounds
                const int b[] = { 1, 2, 2, 3, 3, 4, 5, 6 };
                proc.getInstrument (slot) = builtIn[(size_t) b[i]];
                if (l.sound.isNotEmpty()) l.sound = {};
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
    for (auto& l : pattern.lanes) if (l.on) ++lanesOn;
    const auto cells = toCells (pattern, slots);
    {
        const ScopedLock sl (e.getDataLock());
        auto& pat = e.getPattern (0);
        pat.clear();
        pat.numRows = pattern.steps();
        for (auto& c : cells) pat.at (c.row, c.channel) = c.cell;
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
    apply();
    if (e.playing.load()) e.requestStop();
    else                  e.requestStart();
}

juce::File MakerComponent::suggestedFile() const
{
    const auto& s = pattern.settings;
    const auto name = styleName (s.style) + " " + String (s.bpm) + " " + keyName (s.key) + (s.scale == Scale::Major ? " MAJ" : " MIN");
    return FourTrackerProcessor::getUserFolder().getChildFile ("Patterns").getChildFile (File::createLegalFileName (name) + ".ftpat");
}

juce::String MakerComponent::saveTo (const juce::File& file)
{
    apply();
    file.getParentDirectory().createDirectory();
    return proc.savePattern (0, file);
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
    if (k.getKeyCode() == KeyPress::escapeKey) { proc.getEngine().requestStop(); return true; }
    return false;
}

void MakerComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    Theme::drawPanel (g, topPanel.toFloat());
    Theme::text (g, "PATTERN MAKER", { 24.0f, 16.0f, 440.0f, 42.0f }, 32.0f, Theme::ink, Justification::centredLeft, true);
    Theme::text (g, "FOR FOUR TRACKER", { 28.0f, 58.0f, 400.0f, 20.0f }, 15.0f, Theme::ink.withAlpha (0.65f), Justification::centredLeft);

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
    if (auto* grid = rows[0].grid.get())
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

    Theme::drawScreen (g, statusArea.toFloat());
    Theme::text (g, status, statusArea.toFloat().reduced (14.0f, 0.0f), 16.0f, Theme::screenText, Justification::centredLeft);
    fitText (g, "CLICK: STEP ON / OFF    RIGHT-CLICK: SOFT HIT, OR PICK THE NOTE    WHEEL: NOTE UP / DOWN    SHIFT+WHEEL: LONGER / SHORTER    SPACE: PLAY    G: GENERATE",
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
    const int top = 262, rh = 58;
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& r = rows[(size_t) i];
        const int ry = top + i * (rh + 4);
        r.onBtn->setBounds (24, ry, 150, rh);
        r.soundBtn->setBounds (180, ry, 190, rh);
        r.vol->setBounds (376, ry + 10, 52, rh - 20);
        r.diceBtn->setBounds (gx - 38, ry + 10, 32, rh - 20);
        r.grid->setBounds (gx, ry, gw, rh);
    }
    for (auto& r : rows) r.diceBtn->setBounds (432, r.diceBtn->getY(), 34, r.diceBtn->getHeight());
    statusArea = { 12, getHeight() - 84, getWidth() - 24, 44 };
    refreshControls();
}
