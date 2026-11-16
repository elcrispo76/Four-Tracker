// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Manager - Copyright (C) 2026 elcrispo76

#include "ManagerComponent.h"

using namespace juce;
using namespace pmgr;

namespace
{
    constexpr int kRowH = 38;
    const Colour kPlayRed { 0xffd9342b };

    String hex2 (int v) { return String::toHexString (v).paddedLeft ('0', 2).toUpperCase(); }
    String mmss (double s) { const int t = (int) s; return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2); }

    juce::File settingsFile()
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Four Tracker").getChildFile ("pattern-manager.txt");
    }
}

// ===========================================================================
// SongPlayer
// ===========================================================================

void SongPlayer::play (const Song& s, int from)
{
    {
        const ScopedLock sl (lock);
        song = s;
        const int n = (int) song.entries.size();
        from = jlimit (0, jmax (0, n - 1), from);
        const int loops = song.loop ? 200 : 1;
        events = schedule (song, from, loops);
        order.clear(); starts.clear();
        double t = 0.0;
        for (int i = from; i < n; ++i) { order.push_back (i); starts.push_back (t); t += song.entrySeconds (i); }
        for (int k = 1; k < loops; ++k)
            for (int i = 0; i < n; ++i) { order.push_back (i); starts.push_back (t); t += song.entrySeconds (i); }
        starts.push_back (t);                        // the end
        next = 0;
        noteOffs.clear();
        t0 = Time::getMillisecondCounterHiRes() * 0.001;
        pos = from;
        playing = true;
    }
    if (! isTimerRunning()) startTimer (1);
}

void SongPlayer::stop()
{
    const ScopedLock sl (lock);
    if (playing.exchange (false)) sendKey (song.startKey);    // the start/stop key stops Four Tracker
    pos = -1;
    events.clear();
}

double SongPlayer::elapsed() const
{
    return playing.load() ? Time::getMillisecondCounterHiRes() * 0.001 - t0 : 0.0;
}

void SongPlayer::sendKey (int note)
{
    if (! send) return;
    const int ch = jlimit (1, 16, song.channel);
    send (MidiMessage::noteOn (ch, note, (uint8) 100));
    noteOffs.push_back ({ Time::getMillisecondCounterHiRes() * 0.001 + 0.04, note });
}

void SongPlayer::hiResTimerCallback()
{
    const ScopedLock sl (lock);
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    for (auto it = noteOffs.begin(); it != noteOffs.end();)
    {
        if (it->first <= now) { if (send) send (MidiMessage::noteOff (jlimit (1, 16, song.channel), it->second)); it = noteOffs.erase (it); }
        else ++it;
    }
    if (! playing.load()) return;
    const double el = now - t0;
    while (next < events.size() && events[next].time <= el)
    {
        const auto& e = events[next++];
        sendKey (e.note);
        if (e.stop) { playing = false; pos = -1; return; }
    }
    // which position plays now
    for (size_t k = 0; k + 1 < starts.size(); ++k)
        if (el >= starts[k] && el < starts[k + 1]) { pos = order[k]; break; }
    if (! starts.empty() && el >= starts.back()) { playing = false; pos = -1; }
}

// ===========================================================================
// ManagerComponent
// ===========================================================================

ManagerComponent::ManagerComponent()
{
    for (auto* c : std::initializer_list<Component*> { &midiBtn, &bpmBox, &speedBox, &chanBox, &keyBox, &loopBtn, &up, &down, &patDown, &patUp,
                                                        &rowsDown, &rowsUp, &rowsBox, &insHere, &insAfter, &delBtn, &playBtn, &stopBtn, &playHereBtn,
                                                        &newBtn, &loadBtn, &saveBtn })
        addAndMakeVisible (c);

    auto arrow = [] (bool vertical, bool positive)
    {
        return [vertical, positive] (Graphics& g, juce::Rectangle<float> r, Colour c)
        {
            auto b = r.withSizeKeepingCentre (22.0f, 22.0f);
            Path p;
            if (vertical)
            {
                if (positive) p.addTriangle (b.getX(), b.getBottom() - 4.0f, b.getRight(), b.getBottom() - 4.0f, b.getCentreX(), b.getY() + 2.0f);
                else          p.addTriangle (b.getX(), b.getY() + 4.0f, b.getRight(), b.getY() + 4.0f, b.getCentreX(), b.getBottom() - 2.0f);
                g.setColour (c);
                g.fillPath (p);
            }
            else Theme::iconArrow (g, r.withSizeKeepingCentre (14.0f, 24.0f), c, positive);
        };
    };
    up.icon = arrow (true, true);
    down.icon = arrow (true, false);
    patDown.icon = arrow (false, false);
    patUp.icon = arrow (false, true);
    up.setComponentID ("up"); down.setComponentID ("down"); patDown.setComponentID ("patDown"); patUp.setComponentID ("patUp");
    insHere.setComponentID ("insHere"); insAfter.setComponentID ("insAfter");
    up.onClick       = [this] { moveSelection (-1); };
    down.onClick     = [this] { moveSelection (1); };
    patDown.onClick  = [this] { changePattern (-1); };
    patUp.onClick    = [this] { changePattern (1); };
    rowsDown.onClick = [this] { changeRows (-1); };
    rowsUp.onClick   = [this] { changeRows (1); };
    insHere.onClick  = [this] { insertHere(); };
    insAfter.onClick = [this] { insertAfter(); };
    delBtn.onClick   = [this] { deleteSelected(); };
    playBtn.onClick  = [this] { play(); };
    stopBtn.onClick  = [this] { stop(); };
    playHereBtn.onClick = [this]
    {
        if (midiOut == nullptr) { setStatus ("CHOOSE MIDI OUT FIRST (LOOPBE1 OR LOOPMIDI)"); return; }
        player.stop();
        player.play (song, selected);
        setStatus ("PLAYING FROM POSITION " + hex2 (selected));
    };
    newBtn.onClick = [this]
    {
        stop();
        const auto keep = song;
        song = Song();
        song.bpm = keep.bpm; song.speed = keep.speed; song.startKey = keep.startKey; song.channel = keep.channel;
        selected = 0;
        refreshControls();
        setStatus ("NEW SONG");
    };
    saveBtn.onClick = [this]
    {
        auto dir = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Four Tracker/Songs");
        dir.createDirectory();
        chooser = std::make_unique<FileChooser> ("Save the song list", dir.getChildFile ("My song.ftsong"), "*.ftsong");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                if (! f.hasFileExtension ("ftsong")) f = f.withFileExtension ("ftsong");
                const auto err = saveTo (f);
                setStatus (err.isEmpty() ? "SAVED " + f.getFileName().toUpperCase() : err);
            });
    };
    loadBtn.onClick = [this]
    {
        auto dir = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Four Tracker/Songs");
        chooser = std::make_unique<FileChooser> ("Load a song list", dir, "*.ftsong");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult() == File()) return;
            const auto err = loadFrom (fc.getResult());
            setStatus (err.isEmpty() ? "LOADED " + fc.getResult().getFileName().toUpperCase() : err);
        });
    };
    midiBtn.onClick = [this] { chooseMidiOut(); };
    loopBtn.setClickingTogglesState (true);
    loopBtn.onClick = [this] { song.loop = loopBtn.getToggleState(); };

    for (auto* v : { &bpmBox, &speedBox, &chanBox, &keyBox }) { v->setShowSpinner (true); v->setJustification (Justification::centred); }
    keyBox.formatter = [] (int v) { return midiNoteName (v); };
    rowsBox.setJustification (Justification::centred);
    rowsBox.setDragSensitivity (4.0f);
    rowsBox.setLargeStep (16);
    rowsBox.onChange = [this] (int v) { setRows (v); };
    keyBox.setDragSensitivity (8.0f);
    bpmBox.onChange   = [this] (int v) { song.bpm = v; repaint(); };
    speedBox.onChange = [this] (int v) { song.speed = v; repaint(); };
    chanBox.onChange  = [this] (int v) { song.channel = v; };
    keyBox.onChange   = [this] (int v) { song.startKey = v; repaint(); };

    for (auto* b : std::initializer_list<Button*> { &midiBtn, &loopBtn, &up, &down, &patDown, &patUp, &rowsDown, &rowsUp, &insHere, &insAfter,
                                                     &delBtn, &playBtn, &stopBtn, &playHereBtn, &newBtn, &loadBtn, &saveBtn })
        b->setWantsKeyboardFocus (false);

    player.send = [this] (const MidiMessage& m) { if (midiOut != nullptr) midiOut->sendMessageNow (m); };

    // the MIDI output used last time (or the first LoopBe / loopMIDI port there is)
    String want = settingsFile().loadFileAsString().trim();
    if (want.isEmpty())
        for (auto& d : MidiOutput::getAvailableDevices())
            if (d.name.containsIgnoreCase ("loopbe") || d.name.containsIgnoreCase ("loopmidi")) { want = d.identifier; break; }
   #if JUCE_MAC
    if (want.isEmpty()) want = "virtual";
   #endif
    if (want.isNotEmpty()) setMidiOutput (want);

    setWantsKeyboardFocus (true);
    setSize (kWidth, kHeight);
    refreshControls();
    setStatus ("IN FOUR TRACKER: MIDI / AUDIO > TICK THE LOOPBE / LOOPMIDI INPUT, AND SET PATTERN SWITCH TO END OF PATTERN");
    startTimerHz (30);
}

ManagerComponent::~ManagerComponent()
{
    stopTimer();
    player.stop();
    player.send = nullptr;
}

void ManagerComponent::setMidiOutput (const String& identifier)
{
    player.stop();
    midiOut.reset();
    midiOutName = {};
   #if JUCE_MAC || JUCE_LINUX
    if (identifier == "virtual")
    {
        // a MIDI port of its own that Four Tracker sees as an input (macOS and Linux can do this)
        midiOut = MidiOutput::createNewDevice ("Four Tracker Pattern Manager");
        if (midiOut != nullptr) midiOutName = "Four Tracker Pattern Manager";
    }
   #endif
    if (midiOut == nullptr)
        for (auto& d : MidiOutput::getAvailableDevices())
            if (d.identifier == identifier || d.name == identifier)
            {
                midiOut = MidiOutput::openDevice (d.identifier);
                if (midiOut != nullptr) midiOutName = d.name;
                break;
            }
    if (midiOut != nullptr)
    {
        settingsFile().getParentDirectory().createDirectory();
        settingsFile().replaceWithText (identifier);
    }
    refreshControls();
}

void ManagerComponent::chooseMidiOut()
{
    PopupMenu m;
    m.addSectionHeader ("SEND THE KEYS TO (LOOPBE1 / LOOPMIDI)");
    const auto devs = MidiOutput::getAvailableDevices();
    for (int i = 0; i < devs.size(); ++i) m.addItem (i + 1, devs[i].name, true, devs[i].name == midiOutName);
   #if JUCE_MAC || JUCE_LINUX
    m.addSeparator();
    m.addItem (999, "ITS OWN VIRTUAL PORT: FOUR TRACKER PATTERN MANAGER (NO EXTRA PROGRAM NEEDED)", true, midiOutName == "Four Tracker Pattern Manager");
   #else
    if (devs.isEmpty()) m.addItem (1000, "NO MIDI OUTPUTS - INSTALL LOOPBE1 OR LOOPMIDI", false);
   #endif
    Component::SafePointer<ManagerComponent> safe (this);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&midiBtn), [safe, devs] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        if (r == 999) { safe->setMidiOutput ("virtual"); safe->setStatus ("MIDI OUT: ITS OWN PORT - TICK FOUR TRACKER PATTERN MANAGER ON FOUR TRACKER'S MIDI / AUDIO PAGE"); return; }
        if (r > devs.size()) return;
        safe->setMidiOutput (devs[r - 1].identifier);
        safe->setStatus ("MIDI OUT: " + devs[r - 1].name.toUpperCase());
    });
}

void ManagerComponent::refreshControls()
{
    bpmBox.setValue (song.bpm);
    speedBox.setValue (song.speed);
    chanBox.setValue (song.channel);
    keyBox.setValue (song.startKey);
    loopBtn.setToggleState (song.loop, dontSendNotification);
    midiBtn.setButtonText (midiOutName.isEmpty() ? String ("MIDI OUT: CHOOSE ...") : "MIDI OUT: " + midiOutName.toUpperCase());
    selected = jlimit (0, (int) song.entries.size() - 1, selected);
    repaint();
}

void ManagerComponent::moveSelection (int delta)
{
    selected = jlimit (0, (int) song.entries.size() - 1, selected + delta);
    repaint();
}

void ManagerComponent::changePattern (int delta)
{
    auto& e = song.entries[(size_t) selected];
    e.pattern = jlimit (0, kMaxPatterns - 1, e.pattern + delta);
    repaint();
}

void ManagerComponent::changeRows (int delta)
{
    auto& e = song.entries[(size_t) selected];
    // the usual lengths first: 16, 32, 48, 64, 96, 128 ... then one at a time with Shift (keyboard)
    const int steps[] = { 1, 2, 4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256 };
    int best = 0;
    for (int i = 0; i < 14; ++i) if (steps[i] <= e.rows) best = i;
    if (steps[best] != e.rows && delta < 0) ++best;
    e.rows = steps[jlimit (0, 13, best + delta)];
    repaint();
}

void ManagerComponent::setRows (int rows)
{
    song.entries[(size_t) selected].rows = jlimit (1, 256, rows);
    repaint();
}

void ManagerComponent::insertHere()
{
    const auto e = song.entries[(size_t) selected];
    song.entries.insert (song.entries.begin() + selected, e);       // the new one takes this place, the rest move down
    repaint();
    setStatus ("INSERTED AT POSITION " + hex2 (selected));
}

void ManagerComponent::insertAfter()
{
    const auto e = song.entries[(size_t) selected];
    song.entries.insert (song.entries.begin() + selected + 1, e);
    ++selected;
    repaint();
    setStatus ("INSERTED AFTER: POSITION " + hex2 (selected));
}

void ManagerComponent::deleteSelected()
{
    if (song.entries.size() <= 1) { setStatus ("THE SONG NEEDS AT LEAST ONE POSITION"); return; }
    song.entries.erase (song.entries.begin() + selected);
    selected = jmin (selected, (int) song.entries.size() - 1);
    repaint();
}

void ManagerComponent::play()
{
    if (midiOut == nullptr) { setStatus ("CHOOSE MIDI OUT FIRST (LOOPBE1 OR LOOPMIDI)"); return; }
    if (player.isPlaying()) { setStatus ("ALREADY PLAYING - STOP FIRST"); return; }
    player.play (song, 0);
    setStatus ("PLAYING THE SONG - " + mmss (song.totalSeconds()) + " AT " + String (song.bpm) + " BPM");
}

void ManagerComponent::stop()
{
    player.stop();
    setStatus ("STOPPED");
}

String ManagerComponent::saveTo (const File& f)
{
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (JSON::toString (song.toVar())) ? String() : "COULD NOT SAVE " + f.getFileName().toUpperCase();
}

String ManagerComponent::loadFrom (const File& f)
{
    Song s;
    if (! s.fromVar (JSON::parse (f.loadFileAsString()))) return f.getFileName().toUpperCase() + " IS NOT A PATTERN MANAGER SONG";
    stop();
    song = s;
    selected = 0;
    refreshControls();
    return {};
}

void ManagerComponent::timerCallback()
{
    if (! song.entries.empty())
    {
        const int r = song.entries[(size_t) jlimit (0, (int) song.entries.size() - 1, selected)].rows;
        if (rowsBox.getValue() != r) rowsBox.setValue (r);       // follows the selected position
    }
    const int p = player.position();
    const bool pl = player.isPlaying();
    playBtn.setToggleState (pl, dontSendNotification);
    if (p != lastShownPos) { lastShownPos = p; repaint(); }
    else if (pl) repaint (statusArea);
}

int ManagerComponent::firstShown() const
{
    const int visible = jmax (1, listArea.getHeight() / kRowH - 1);
    const int focus = player.position() >= 0 ? player.position() : selected;
    return jmax (0, jmin (focus - visible / 2, (int) song.entries.size() - visible));
}

juce::Rectangle<int> ManagerComponent::rowBounds (int index) const
{
    return { listArea.getX() + 6, listArea.getY() + kRowH + (index - firstShown()) * kRowH, listArea.getWidth() - 12, kRowH - 4 };
}

void ManagerComponent::mouseDown (const MouseEvent& e)
{
    for (int i = firstShown(); i < (int) song.entries.size(); ++i)
        if (rowBounds (i).contains (e.getPosition())) { selected = i; repaint(); return; }
}

void ManagerComponent::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (listArea.contains (e.getPosition())) moveSelection ((w.deltaY > 0) != w.isReversed ? -1 : 1);
}

bool ManagerComponent::keyPressed (const KeyPress& k)
{
    const int c = k.getKeyCode();
    if (c == KeyPress::upKey)    { moveSelection (-1); return true; }
    if (c == KeyPress::downKey)  { moveSelection (1); return true; }
    if (c == KeyPress::leftKey)  { changePattern (-1); return true; }
    if (c == KeyPress::rightKey) { changePattern (1); return true; }
    if (c == KeyPress::insertKey) { k.getModifiers().isShiftDown() ? insertHere() : insertAfter(); return true; }
    if (c == KeyPress::deleteKey) { deleteSelected(); return true; }
    if (c == KeyPress::spaceKey || c == KeyPress::returnKey) { player.isPlaying() ? stop() : play(); return true; }
    if (c == KeyPress::escapeKey) { stop(); return true; }
    return false;
}

void ManagerComponent::paint (Graphics& g)
{
    g.fillAll (Theme::background);
    Theme::drawPanel (g, topPanel.toFloat());
    Theme::text (g, "PATTERN MANAGER", { 24.0f, 18.0f, 520.0f, 40.0f }, 32.0f, Theme::ink, Justification::centredLeft, true);
    Theme::text (g, "A WHOLE SONG IN FOUR TRACKER, OVER MIDI", { 28.0f, 58.0f, 420.0f, 20.0f }, 12.0f, Theme::ink.withAlpha (0.65f), Justification::centredLeft);
    auto label = [&] (const String& t, Component& c)
    {
        Theme::text (g, t, { (float) c.getX(), (float) c.getY() - 22.0f, 200.0f, 20.0f }, 15.0f, Theme::ink, Justification::bottomLeft);
    };
    label ("BPM", bpmBox);
    label ("SPEED", speedBox);
    label ("CHANNEL", chanBox);
    label ("START KEY", keyBox);

    // the song list
    Theme::drawScreen (g, listArea.toFloat());
    const auto head = juce::Rectangle<float> ((float) listArea.getX() + 16.0f, (float) listArea.getY() + 6.0f, (float) listArea.getWidth() - 32.0f, (float) kRowH - 10.0f);
    const float cols[] = { 0.0f, 0.15f, 0.40f, 0.60f, 0.80f };
    const char* heads[] = { "POS", "PATTERN", "MIDI KEY", "ROWS", "TIME" };
    for (int c = 0; c < 5; ++c)
        Theme::text (g, heads[c], head.withTrimmedLeft (head.getWidth() * cols[c]).withWidth (head.getWidth() * 0.2f), 14.0f, Theme::screenDim, Justification::centredLeft);
    const int playPos = player.position();
    double t = 0.0;
    std::vector<double> at;
    for (int i = 0; i < (int) song.entries.size(); ++i) { at.push_back (t); t += song.entrySeconds (i); }
    for (int i = firstShown(); i < (int) song.entries.size(); ++i)
    {
        auto r = rowBounds (i);
        if (r.getBottom() > listArea.getBottom() - 2) break;
        const auto& e = song.entries[(size_t) i];
        const bool sel = i == selected, pl = i == playPos;
        if (sel) { g.setColour (Theme::screenText); g.fillRect (r); }
        if (pl)  { g.setColour (kPlayRed); g.fillRect (r.withWidth (8)); }
        const Colour ink = sel ? Theme::ink : Theme::screenText;
        auto rf = r.toFloat().withTrimmedLeft (10.0f);
        const String cells[] = { hex2 (i), hex2 (e.pattern), midiNoteName (song.noteFor (e.pattern)), String (e.rows), mmss (at[(size_t) i]) };
        for (int c = 0; c < 5; ++c)
            Theme::text (g, cells[c], rf.withTrimmedLeft (head.getWidth() * cols[c]).withWidth (head.getWidth() * 0.2f), 20.0f,
                         c == 1 ? (sel ? Theme::ink : Colours::white) : ink.withAlpha (c == 1 ? 1.0f : 0.85f), Justification::centredLeft, c == 1);
    }

    Theme::drawPanel (g, sidePanel.toFloat());
    auto sideLabel = [&] (const String& t, Component& c)
    {
        Theme::text (g, t, { (float) sidePanel.getX() + 14.0f, (float) c.getY() - 24.0f, (float) sidePanel.getWidth() - 28.0f, 20.0f }, 15.0f, Theme::ink, Justification::bottomLeft);
    };
    sideLabel ("POSITION", up);
    sideLabel ("PATTERN NUMBER", patDown);
    sideLabel ("ROWS (PATTERN LENGTH)", rowsDown);
    {
        const auto& e = song.entries[(size_t) jlimit (0, (int) song.entries.size() - 1, selected)];
        Theme::text (g, hex2 (e.pattern), { (float) patDown.getRight(), (float) patDown.getY(), (float) (patUp.getX() - patDown.getRight()), (float) patDown.getHeight() },
                     30.0f, Theme::ink, Justification::centred, true);
        Theme::text (g, hex2 (selected) + " / " + hex2 ((int) song.entries.size() - 1), { (float) up.getRight(), (float) up.getY(), (float) (down.getX() - up.getRight()), (float) up.getHeight() },
                     22.0f, Theme::ink, Justification::centred, true);
    }

    Theme::drawScreen (g, statusArea.toFloat());
    String s = status;
    if (player.isPlaying()) s = "PLAYING POSITION " + hex2 (playPos) + "   " + mmss (player.elapsed()) + " / " + mmss (song.totalSeconds()) + (song.loop ? "   LOOP" : "");
    Theme::text (g, s, statusArea.toFloat().reduced (14.0f, 0.0f),
                 jmin (16.0f, 16.0f * (statusArea.getWidth() - 30.0f) / jmax (1.0f, PixelFont::getWidth (s, 16.0f))), Theme::screenText, Justification::centredLeft);
}

void ManagerComponent::resized()
{
    topPanel = { 12, 12, getWidth() - 24, 160 };
    playBtn.setBounds (470, 24, 150, 58);
    stopBtn.setBounds (628, 24, 140, 58);
    newBtn.setBounds (getWidth() - 24 - 3 * 92 + 6, 24, 86, 58);
    loadBtn.setBounds (newBtn.getRight() + 6, 24, 86, 58);
    saveBtn.setBounds (loadBtn.getRight() + 6, 24, 86, 58);
    const int y = 108, h = 48;
    bpmBox.setBounds (24, y, 130, h);
    speedBox.setBounds (170, y, 110, h);
    keyBox.setBounds (300, y, 140, h);
    chanBox.setBounds (484, y, 100, h);
    loopBtn.setBounds (600, y, 150, h);
    midiBtn.setBounds (762, y, getWidth() - 24 - 762, h);

    const int sideW = 330;
    listArea = { 12, 184, getWidth() - 24 - sideW - 10, getHeight() - 184 - 66 };
    sidePanel = { listArea.getRight() + 10, 184, sideW, listArea.getHeight() };
    const int sx = sidePanel.getX() + 14, sw = sideW - 28;
    int yy = sidePanel.getY() + 40;
    up.setBounds (sx, yy, 70, 52);
    down.setBounds (sx + sw - 70, yy, 70, 52);
    yy += 92;
    patDown.setBounds (sx, yy, 70, 52);
    patUp.setBounds (sx + sw - 70, yy, 70, 52);
    yy += 92;
    rowsDown.setBounds (sx, yy, 70, 46);
    rowsUp.setBounds (sx + sw - 70, yy, 70, 46);
    rowsBox.setBounds (rowsDown.getRight() + 8, yy, rowsUp.getX() - rowsDown.getRight() - 16, 46);
    yy += 66;
    insHere.setBounds (sx, yy, (sw - 8) / 2, 52);
    insAfter.setBounds (sx + (sw + 8) / 2, yy, (sw - 8) / 2, 52);
    yy += 60;
    delBtn.setBounds (sx, yy, (sw - 8) / 2, 46);
    playHereBtn.setBounds (sx + (sw + 8) / 2, yy, (sw - 8) / 2, 46);
    statusArea = { 12, getHeight() - 56, getWidth() - 24, 44 };
}
