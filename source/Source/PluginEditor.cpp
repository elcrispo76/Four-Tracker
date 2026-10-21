// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "PluginEditor.h"

using namespace juce;
using namespace ft;

FourTrackerEditor::FourTrackerEditor (FourTrackerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), state (p),
      title (state), header (state), patternView (state), strips (state), songPanel (state),
      blockPanel (state, patternView), instrumentsPage (state), sampleEditPage (state), settingsPage (state), sampleBrowser (state)
{
    addAndMakeVisible (root);
    for (auto* c : std::initializer_list<Component*> { &title, &header, &patternView, &strips, &songPanel, &blockPanel,
                                                        &instrumentsPage, &sampleEditPage, &settingsPage,
                                                        &tabInstruments, &tabSample, &tabSong, &tabBlock, &tabSettings, &tabTracks })
        root.addChildComponent (c);

    title.setVisible (true);
    header.setVisible (true);
    for (auto* t : { &tabInstruments, &tabSample, &tabSong, &tabBlock, &tabSettings, &tabTracks }) t->setVisible (true);

    tabInstruments.icon = Theme::iconWave;  tabInstruments.iconWidth = 84.0f;
    tabSample.icon = Theme::iconWave;       tabSample.iconWidth = 84.0f;
    tabSong.icon = Theme::iconList;         tabSong.iconWidth = 70.0f;
    tabBlock.icon = Theme::iconGrid;        tabBlock.iconWidth = 66.0f;
    tabSettings.icon = Theme::iconMidi;     tabSettings.iconWidth = 66.0f;
    tabTracks.icon = Theme::iconTracks;
    tabTracks.setButtonText ({});

    tabInstruments.onClick = [this] { showPage (Page::Instruments); };
    tabSample.onClick      = [this] { showPage (Page::SampleEdit); };
    tabSong.onClick        = [this] { showPage (Page::Song); };
    tabBlock.onClick       = [this] { showPage (Page::Block); };
    tabSettings.onClick    = [this] { showPage (Page::Settings); };
    tabTracks.onClick      = [this] { showPage (Page::Tracks); };
    title.onGridClicked    = [this] { showPage (Page::Tracks); };
    header.onInstrumentSelected = [this] { instrumentsPage.instrumentsChanged(); sampleEditPage.instrumentsChanged(); };

    state.onAnythingChanged = [this] { refreshAll(); };

    root.addChildComponent (sampleBrowser);
    state.openSampleBrowser = [this] { sampleBrowser.open(); };
    sampleBrowser.onClosed  = [this] { grabKeyboardFocus(); refreshAll(); };
    proc.addChangeListener (this);

    showPage (Page::Tracks);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (768, 512, 2304, 1536);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kDesignW / kDesignH);
    setSize (1152, 768);

    startTimerHz (30);

    // first run: put the demo song and demo samples in Documents/Four Tracker/Demo
    MessageManager::callAsync ([] { FourTrackerProcessor::ensureDemoFolder(); });
}

FourTrackerEditor::~FourTrackerEditor()
{
    proc.removeChangeListener (this);
    stopTimer();
}

void FourTrackerEditor::paint (Graphics& g)
{
    g.fillAll (Theme::background);
}

void FourTrackerEditor::resized()
{
    const float scale = jmin (getWidth() / (float) kDesignW, getHeight() / (float) kDesignH);
    root.setBounds (0, 0, kDesignW, kDesignH);
    root.setTransform (AffineTransform::scale (scale));

    title.setBounds (14, 10, 1508, 66);
    header.setBounds (14, 84, 1508, 106);

    const Rectangle<int> content (14, 196, 1508, 732);
    strips.setBounds (14, 196, 1508, 174);
    songPanel.setBounds (14, 196, 1508, 174);
    blockPanel.setBounds (14, 196, 1508, 174);
    patternView.setBounds (18, 376, 1500, 548);
    instrumentsPage.setBounds (content);
    sampleEditPage.setBounds (content);
    settingsPage.setBounds (content);
    sampleBrowser.setBounds (14, 84, 1508, 844);   // covers header + page, leaves the tabs

    // bottom tab bar: six entries
    int x = 14;
    const int y = 936, h = 66, gap = 10;
    auto place = [&] (PixelButton& b, int w) { b.setBounds (x, y, w, h); x += w + gap; };
    place (tabInstruments, 290);
    place (tabSample, 284);
    place (tabSong, 300);
    place (tabBlock, 210);
    place (tabSettings, 260);
    tabTracks.setBounds (x, y, 1522 - x, h);
}

void FourTrackerEditor::showPage (Page p)
{
    if (sampleBrowser.isVisible()) sampleBrowser.close();
    page = p;
    const bool patternPage = (p == Page::Tracks || p == Page::Song || p == Page::Block);
    patternView.setVisible (patternPage);
    strips.setVisible (p == Page::Tracks);
    songPanel.setVisible (p == Page::Song);
    blockPanel.setVisible (p == Page::Block);
    instrumentsPage.setVisible (p == Page::Instruments);
    sampleEditPage.setVisible (p == Page::SampleEdit);
    settingsPage.setVisible (p == Page::Settings);

    tabInstruments.setToggleState (p == Page::Instruments, dontSendNotification);
    tabSample.setToggleState (p == Page::SampleEdit, dontSendNotification);
    tabSong.setToggleState (p == Page::Song, dontSendNotification);
    tabBlock.setToggleState (p == Page::Block, dontSendNotification);
    tabSettings.setToggleState (p == Page::Settings, dontSendNotification);
    tabTracks.setToggleState (p == Page::Tracks, dontSendNotification);

    if (p == Page::Instruments) instrumentsPage.instrumentsChanged();
    if (p == Page::SampleEdit)  sampleEditPage.instrumentsChanged();
    grabKeyboardFocus();
}

void FourTrackerEditor::refreshAll()
{
    header.refresh();
    title.refresh();
    songPanel.refresh();
    strips.refresh();
    settingsPage.refresh();
    if (instrumentsPage.isVisible()) instrumentsPage.instrumentsChanged();
    if (sampleEditPage.isVisible())  sampleEditPage.instrumentsChanged();
    patternView.repaint();
    root.repaint();
}

void FourTrackerEditor::changeListenerCallback (ChangeBroadcaster*)
{
    refreshAll();
}

void FourTrackerEditor::timerCallback()
{
    header.refresh();
    title.refresh();
    if (patternView.isVisible()) patternView.refreshFromEngine();
    if (strips.isVisible())      strips.refresh();
    if (songPanel.isVisible())   songPanel.refresh();
    if (sampleEditPage.isVisible()) sampleEditPage.refresh();
    if (settingsPage.isVisible()) settingsPage.refresh();
}

// ===========================================================================
// keyboard
// ===========================================================================

bool FourTrackerEditor::keyPressed (const KeyPress& k)
{
    const int code = k.getKeyCode();
    const auto mods = k.getModifiers();
    auto& e = proc.getEngine();

    if (sampleBrowser.isVisible()) return sampleBrowser.keyPressed (k);

    // transport and global keys
    if (code == KeyPress::returnKey && ! mods.isAnyModifierKeyDown()) { e.requestStartStop(); return true; }
    if (code == KeyPress::spaceKey)
    {
        state.setEditMode (! state.editMode);
        return true;
    }
    if (code == KeyPress::escapeKey) { e.requestStop(); return true; }
    if (code == KeyPress::F1Key) { state.octave = jmax (0, state.octave - 1); title.refresh(); return true; }
    if (code == KeyPress::F2Key) { state.octave = jmin (7, state.octave + 1); title.refresh(); return true; }
    if (code == KeyPress::F6Key) { state.editStep = jmax (0, state.editStep - 1); title.refresh(); return true; }
    if (code == KeyPress::F7Key) { state.editStep = jmin (16, state.editStep + 1); title.refresh(); return true; }
    if (code == KeyPress::F9Key)  { showPage (Page::Tracks); return true; }
    if (code == KeyPress::F10Key) { showPage (Page::Instruments); return true; }
    if (code == KeyPress::F11Key) { showPage (Page::SampleEdit); return true; }
    if (code == KeyPress::F12Key) { showPage (Page::Settings); return true; }

    // numpad +/- picks the instrument, like most trackers
    if (code == KeyPress::numberPadAdd || code == KeyPress::numberPadSubtract)
    {
        proc.selectedInstrument = jlimit (1, kNumInstruments - 1, proc.selectedInstrument + (code == KeyPress::numberPadAdd ? 1 : -1));
        refreshAll();
        return true;
    }

    // A held key is auto-repeated by Windows. A note key that is already held
    // must not start the sample again (that made it stutter on the first part).
    const int noteKey = patternView.noteForKey (k);
    if (noteKey > 0)
    {
        const int kc = (int) CharacterFunctions::toUpperCase ((juce_wchar) code);
        if (heldNoteKeys.count (kc) > 0) return true;     // repeat: ignore
        heldNoteKeys.insert (kc);
        jamNoteDown = true;
        jamInstrument = proc.selectedInstrument;
    }

    if (patternView.isVisible())
    {
        if (patternView.handleKey (k)) { title.refresh(); header.refresh(); return true; }

        // not in edit mode: computer keys jam the selected instrument
        const int n = patternView.noteForKey (k);
        if (n > 0)
        {
            e.requestPreview (proc.selectedInstrument, n);
            jamNoteDown = true;
            return true;
        }
    }
    else if (page == Page::Instruments || page == Page::SampleEdit)
    {
        const int n = patternView.noteForKey (k);
        if (n > 0 && ! mods.isAnyModifierKeyDown())
        {
            e.requestPreview (proc.selectedInstrument, n);
            jamNoteDown = true;
            return true;
        }
    }

    return false;
}

bool FourTrackerEditor::keyStateChanged (bool isKeyDown)
{
    if (isKeyDown) return false;

    // forget note keys that have been released
    for (auto it = heldNoteKeys.begin(); it != heldNoteKeys.end();)
    {
        const int kc = *it;
        const bool down = KeyPress::isKeyCurrentlyDown (kc)
                       || (kc >= 'A' && kc <= 'Z' && KeyPress::isKeyCurrentlyDown (kc + 32));
        it = down ? std::next (it) : heldNoteKeys.erase (it);
    }

    if (heldNoteKeys.empty() && jamNoteDown)
    {
        jamNoteDown = false;
        // A sample without a loop always plays to its end, like in ProTracker.
        // A looped sample would ring forever, so it stops when the key is released.
        const auto& in = proc.getInstrument (jamInstrument);
        if (! in.isEmpty() && in.params.loop)
            proc.getEngine().requestStopPreview();
    }
    return false;
}

void FourTrackerEditor::focusLost (FocusChangeType)
{
    heldNoteKeys.clear();
    if (jamNoteDown)
    {
        jamNoteDown = false;
        const auto& in = proc.getInstrument (jamInstrument);
        if (! in.isEmpty() && in.params.loop)
            proc.getEngine().requestStopPreview();
    }
}

// ===========================================================================
// drag and drop
// ===========================================================================

bool FourTrackerEditor::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension ("ftm;ftps")) return true;
        if (proc.getFormatManager().findFormatForFileExtension (file.getFileExtension()) != nullptr) return true;
        if (file.hasFileExtension ("iff;8svx;8sv;raw;smp;sam;snd;sample") || file.getFileExtension().isEmpty()) return true;
    }
    return false;
}

void FourTrackerEditor::filesDropped (const StringArray& files, int, int)
{
    Array<File> audio;
    for (auto& f : files)
    {
        const File file (f);
        if (file.hasFileExtension ("ftm;ftps"))
        {
            auto err = proc.loadModule (file);
            state.setStatus (err.isEmpty() ? "LOADED " + file.getFileName().toUpperCase() : err.toUpperCase());
        }
        else audio.add (file);
    }

    int slot = proc.selectedInstrument;
    for (auto& f : audio)
    {
        if (slot >= kNumInstruments) break;
        auto err = proc.loadInstrument (slot, f);
        if (err.isNotEmpty()) state.setStatus (err.toUpperCase());
        else { state.setStatus ("LOADED " + f.getFileName().toUpperCase() + " INTO " + hex2 (slot)); ++slot; }
    }
    refreshAll();
}
