// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "Pages.h"

using namespace juce;
using namespace ft;

namespace
{
    void captionUnder (Graphics& g, Component& c, const String& text, float h = 16.0f)
    {
        auto b = c.getBoundsInParent().toFloat();
        Theme::text (g, text, { b.getX() - 20.0f, b.getBottom() + 6.0f, b.getWidth() + 40.0f, h + 4.0f }, h, Theme::ink, Justification::centredTop);
    }

    void labelLeft (Graphics& g, const String& text, Rectangle<int> r, float h = 18.0f)
    {
        Theme::text (g, text, r.toFloat(), h, Theme::ink, Justification::centredLeft);
    }

    void infoTable (Graphics& g, Rectangle<float> area, const Instrument& in)
    {
        Theme::drawScreen (g, area);
        const bool empty = in.isEmpty();
        const String rows[5][2] = {
            { "LENGTH",      empty ? "-" : String (in.data->getLength()) },
            { "SAMPLE RATE", empty ? "-" : String ((int) in.data->sampleRate) },
            { "BIT DEPTH",   empty ? "-" : String (in.data->bitDepth) },
            { "CHANNELS",    empty ? "-" : (in.data->getNumChannels() == 1 ? String ("1 (MONO)") : String ("2 (STEREO)")) },
            { "FILE SIZE",   empty ? "-" : String ((in.data->getLength() * in.data->getNumChannels() * jmax (8, in.data->bitDepth) / 8 + 1023) / 1024) + " KB" } };

        const float rowH = (area.getHeight() - 8.0f) / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            auto r = Rectangle<float> (area.getX() + 12.0f, area.getY() + 4.0f + i * rowH, area.getWidth() - 24.0f, rowH);
            if (i > 0) { g.setColour (Colour (0xff2a2c31)); g.fillRect (r.getX() - 8.0f, r.getY(), r.getWidth() + 16.0f, 1.5f); }
            Theme::text (g, rows[i][0], r, 15.0f, Theme::screenText, Justification::centredLeft);
            Theme::text (g, rows[i][1], r, 15.0f, Theme::screenText, Justification::centredRight);
        }
    }

    /** Plays an instrument from a mouse click. A looping instrument (a synth,
        or a sample with loop on) would ring forever, so it is stopped again
        after a moment, unless something else has been previewed since. */
    int previewToken = 0;
    void previewOnce (EditorState& st, int slot, Component* owner)
    {
        const int my = ++previewToken;
        st.engine().requestPreview (slot, kNativeNote);
        const auto& in = st.proc.getInstrument (slot);
        if (in.isEmpty() || ! in.params.loop) return;
        Component::SafePointer<Component> safe (owner);
        auto* engine = &st.engine();
        Timer::callAfterDelay (in.params.synth ? 900 : 1500, [safe, engine, my]
        {
            if (safe != nullptr && my == previewToken) engine->requestStopPreview();
        });
    }

    void drawWaveIcon (Graphics& g, Rectangle<float> r, Colour c, int wave)
    {
        r = r.reduced (r.getWidth() * 0.12f, r.getHeight() * 0.28f);
        Path p;
        const int n = 48;
        Random rnd (7);
        for (int i = 0; i <= n; ++i)
        {
            const float ph = (float) i / n * 2.0f;          // two cycles
            const float f = ph - std::floor (ph);
            float v = 0.0f;
            switch (wave)
            {
                case waveSine:     v = std::sin (f * MathConstants<float>::twoPi); break;
                case waveTriangle: v = f < 0.5f ? 4.0f * f - 1.0f : 3.0f - 4.0f * f; break;
                case waveSaw:      v = 2.0f * f - 1.0f; break;
                case waveSquare:   v = f < 0.5f ? 1.0f : -1.0f; break;
                default:           v = rnd.nextFloat() * 2.0f - 1.0f; break;
            }
            const float x = r.getX() + r.getWidth() * i / n, y = r.getCentreY() - v * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y);
            else if (wave == waveSquare || wave == waveSaw)
            {
                // vertical edges
                const float prevY = p.getCurrentPosition().y;
                if (std::abs (prevY - y) > r.getHeight() * 0.6f) p.lineTo (x, prevY);
                p.lineTo (x, y);
            }
            else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, PathStrokeType (2.5f));
    }

}

// ===========================================================================
// ChannelStrip
// ===========================================================================

ChannelStrip::ChannelStrip (EditorState& s, int channel) : st (s), ch (channel)
{
    addAndMakeVisible (wave);
    addAndMakeVisible (vol);
    addAndMakeVisible (pan);
    addAndMakeVisible (pitch);
    wave.showMarkers = false;
    wave.lineThickness = 1.2f;
    wave.onClicked = [this]
    {
        if (shownInst > 0) { st.proc.selectedInstrument = shownInst; st.changed(); }
    };

    pan.formatter = [] (int v) { return panToString (v); };
    pan.parser = [] (const String& t, bool& ok)
    {
        auto txt = t.trim().toUpperCase();
        ok = true;
        if (txt == "C" || txt == "0") return 0;
        if (txt.startsWith ("L")) return -txt.substring (1).getIntValue();
        if (txt.startsWith ("R")) return txt.substring (1).getIntValue();
        return txt.getIntValue();
    };
    pan.setDragSensitivity (2.0f);
    pitch.formatter = [] (int v) { return signedToString (v); };
    pitch.setDragSensitivity (8.0f);
    vol.setDragSensitivity (2.0f);

    vol.onChange   = [this] (int v) { st.engine().mix[ch].volume = v; };
    pan.onChange   = [this] (int v) { st.engine().mix[ch].pan = v; };
    pitch.onChange = [this] (int v) { st.engine().mix[ch].pitch = v; };
    refresh();
}

void ChannelStrip::setChannel (int c)
{
    if (c == ch) return;
    ch = c;
    shownInst = -1;
    shownData = nullptr;
    level = 0.0f;
    refresh();
    repaint();
}

void ChannelStrip::refresh()
{
    auto& e = st.engine();
    vol.setValue (e.mix[ch].volume.load());
    pan.setValue (e.mix[ch].pan.load());
    pitch.setValue (e.mix[ch].pitch.load());

    int inst = e.lastInstrumentOnChannel[ch].load();
    if (inst <= 0)
    {
        // Before anything has played, show the first instrument used in this channel.
        auto& pat = e.getPattern (e.selectedPattern.load());
        for (int r = 0; r < pat.getNumRows() && inst <= 0; ++r) inst = pat.at (r, ch).inst;
    }

    auto& in = st.proc.getInstrument (jmax (0, inst));
    if (inst != shownInst || in.data.get() != shownData || in.name != name)
    {
        shownInst = inst;
        shownData = in.data.get();
        name = inst > 0 ? in.name : String();
        wave.setSample (inst > 0 ? in.data : nullptr, nullptr);
        repaint();
    }

    const float lvl = (float) e.channelLevel[ch].exchange (0) / 1000.0f;
    const float newLevel = jmax (lvl, level * 0.82f);
    const bool m = e.mix[ch].mute.load();
    if (std::abs (newLevel - level) > 0.004f || m != muted)
    {
        level = newLevel;
        muted = m;
        repaint (wave.getBounds().withY (wave.getBottom()).withHeight (8));
        repaint (0, 0, 120, 50);
    }
}

void ChannelStrip::paint (Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat());
    Theme::text (g, "CH" + String (ch + 1), { 18.0f, 14.0f, 120.0f, 30.0f }, 30.0f, muted ? Theme::panelEdgeLo : Theme::ink, Justification::centredLeft, true);
    if (muted) Theme::text (g, "MUTE", { 100.0f, 22.0f, 80.0f, 16.0f }, 15.0f, Theme::ink, Justification::centredLeft);

    labelLeft (g, "VOL",   { 222, 16, 56, 40 }, 17.0f);
    labelLeft (g, "PAN",   { 222, 64, 56, 40 }, 17.0f);
    labelLeft (g, "PITCH", { 212, 112, 66, 40 }, 17.0f);

    // level meter under the waveform
    auto meter = Rectangle<float> ((float) wave.getX(), (float) wave.getBottom() + 2.0f, (float) wave.getWidth(), 5.0f);
    g.setColour (Colour (0xff3a3d43));
    g.fillRect (meter);
    g.setColour (Theme::ink);
    g.fillRect (meter.withWidth (meter.getWidth() * jmin (1.0f, level * 1.4f)));

    Theme::text (g, name.isEmpty() ? String ("---") : name, { 18.0f, 146.0f, 190.0f, 20.0f }, 18.0f, Theme::ink, Justification::centredLeft);
}

void ChannelStrip::resized()
{
    wave.setBounds (16, 52, 188, 82);
    vol.setBounds (280, 12, 74, 46);
    pan.setBounds (280, 60, 74, 46);
    pitch.setBounds (280, 108, 74, 46);
}

void ChannelStrip::mouseDown (const MouseEvent& e)
{
    if (e.y < 50 && e.x < 180)
    {
        auto& m = st.engine().mix[ch].mute;
        m = ! m.load();
        st.setStatus (String ("CH") + String (ch + 1) + (m.load() ? " MUTED" : " UNMUTED"));
        refresh();
        repaint();
    }
}

ChannelStripsPanel::ChannelStripsPanel (EditorState& s) : st (s)
{
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        strips.push_back (std::make_unique<ChannelStrip> (s, i));
        addAndMakeVisible (*strips.back());
    }
}

void ChannelStripsPanel::resized()
{
    const int gap = 12;
    const int w = (getWidth() - gap * 3) / 4;
    for (int i = 0; i < kVisibleChannels; ++i)
        strips[(size_t) i]->setBounds (i * (w + gap), 0, w, getHeight());
}

void ChannelStripsPanel::refresh()
{
    const int first = st.firstVisibleChannel(), n = st.numChannels();
    for (int i = 0; i < kVisibleChannels; ++i)
    {
        auto& s = *strips[(size_t) i];
        s.setVisible (first + i < n);
        s.setChannel (first + i);
        s.refresh();
    }
}

// ===========================================================================
// SongPanel
// ===========================================================================

SongPanel::SongPanel (EditorState& s) : st (s)
{
    for (auto* b : { &start, &stop, &newSet, &load, &save }) { addAndMakeVisible (b); b->iconWidth = 30.0f; }
    newSet.subLabel = "MODULE";
    newSet.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        // blank page with a folded corner
        auto b = r.withSizeKeepingCentre (r.getWidth() * 0.8f, jmin (r.getHeight(), r.getWidth() * 1.1f));
        Path p;
        const float fold = b.getWidth() * 0.35f;
        p.startNewSubPath (b.getX(), b.getY());
        p.lineTo (b.getRight() - fold, b.getY());
        p.lineTo (b.getRight(), b.getY() + fold);
        p.lineTo (b.getRight(), b.getBottom());
        p.lineTo (b.getX(), b.getBottom());
        p.closeSubPath();
        g.setColour (c);
        g.strokePath (p, PathStrokeType (2.5f));
        g.drawLine (b.getRight() - fold, b.getY(), b.getRight() - fold, b.getY() + fold, 2.5f);
        g.drawLine (b.getRight() - fold, b.getY() + fold, b.getRight(), b.getY() + fold, 2.5f);
    };
    newSet.onClick = [this]
    {
        auto opts = MessageBoxOptions::makeOptionsYesNoCancel (MessageBoxIconType::WarningIcon,
                        "New module",
                        "Start with an empty module, or load the demo song?\n"
                        "Both replace ALL patterns and ALL loaded samples.\nAnything not saved will be lost.",
                        "Empty", "Demo song", "Cancel", this);
        Component::SafePointer<SongPanel> safe (this);
        std::function<void (int)> onResult = [safe] (int result)
        {
            if (result == 0) return;   // cancel
            if (auto* panel = safe.getComponent())
                panel->clearEverything (result == 2);
        };
        AlertWindow::showAsync (opts, onResult);
    };
    start.subLabel = "PATTERN"; start.icon = Theme::iconPlay;
    stop.subLabel  = "PATTERN"; stop.icon  = Theme::iconStop;
    load.subLabel  = "MODULE"; load.icon = Theme::iconFolder;
    save.subLabel  = "MODULE"; save.icon = Theme::iconSave;

    addAndMakeVisible (length);
    length.setShowSpinner (true);
    length.setDragSensitivity (3.0f);
    length.setLargeStep (16);

    start.onClick = [this] { st.engine().requestStart(); };
    stop.onClick  = [this] { st.engine().requestStop(); };

    length.onChange = [this] (int v)
    {
        auto& pat = st.engine().getPattern (st.viewPattern());
        pat.numRows = v;
        if (st.cursorRow >= v) st.cursorRow = v - 1;
        st.changed();
    };

    save.onClick = [this]
    {
        auto name = lastModule.existsAsFile() ? lastModule : lastDir.getChildFile ("my song.ftm");
        chooser = std::make_unique<FileChooser> ("Save module (patterns + samples)", name, "*.ftm");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                if (! f.hasFileExtension ("ftm")) f = f.withFileExtension ("ftm");
                const bool ok = st.proc.saveModule (f);
                if (ok) { lastModule = f; lastDir = f.getParentDirectory(); }
                st.setStatus (ok ? "SAVED " + f.getFileName().toUpperCase() : String ("COULD NOT SAVE FILE"));
                st.changed();
            });
    };

    load.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Load module", lastDir, "*.ftm;*.ftps");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                auto err = st.proc.loadModule (f);
                if (err.isEmpty()) { lastModule = f; lastDir = f.getParentDirectory(); }
                st.undoStack.clear(); st.redoStack.clear();
                st.hasSelection = false;
                st.setStatus (err.isEmpty() ? "LOADED " + f.getFileName().toUpperCase() : err.toUpperCase());
                st.changed();
            });
    };
}

void SongPanel::clearEverything (bool loadDemo)
{
    if (loadDemo) st.proc.loadDemoSong();
    else        { st.proc.newPatternSet(); st.proc.clearAllEffects(); }   // Empty: the VST effects go too
    lastModule = File();
    st.undoStack.clear();
    st.redoStack.clear();
    st.hasSelection = false;
    st.cursorRow = 0; st.cursorCh = 0; st.cursorCol = 0;
    st.setStatus (loadDemo ? "DEMO SONG LOADED" : (st.proc.getFxHost() != nullptr ? "NEW MODULE - EVERYTHING CLEARED, ALSO THE EFFECTS"
                                                                                  : "NEW MODULE - EVERYTHING CLEARED"));
    st.changed();
}

void SongPanel::refresh()
{
    length.setValue (st.engine().getPattern (st.viewPattern()).getNumRows());
}

void SongPanel::paint (Graphics& g)
{
    Theme::drawPanel (g, Rectangle<float> (0.0f, 0.0f, 250.0f, (float) getHeight()));
    Theme::text (g, "SONG /",  { 0.0f, 30.0f, 250.0f, 50.0f }, 30.0f, Theme::ink, Justification::centred, true);
    Theme::text (g, "PATTERN", { 0.0f, 92.0f, 250.0f, 50.0f }, 30.0f, Theme::ink, Justification::centred, true);

    auto lenPanel = Rectangle<float> (1284.0f, 0.0f, (float) getWidth() - 1284.0f, (float) getHeight());
    Theme::drawPanel (g, lenPanel);
    Theme::text (g, "PATTERN LENGTH", lenPanel.withTrimmedTop (20.0f).withHeight (18.0f), 15.0f, Theme::ink, Justification::centredTop);

    Theme::drawPanel (g, Rectangle<float> (258.0f, 0.0f, 1018.0f, (float) getHeight()));
}

void SongPanel::resized()
{
    const int x0 = 268, w = 194, gap = 8;
    PixelButton* bs[] = { &start, &stop, &newSet, &load, &save };
    for (int i = 0; i < 5; ++i)
        bs[i]->setBounds (x0 + i * (w + gap), 12, w, 148);
    length.setBounds (1310, 66, 170, 64);
}

// ===========================================================================
// BlockPanel
// ===========================================================================

BlockPanel::BlockPanel (EditorState& s, PatternView& pv) : st (s)
{
    for (auto* b : { &copyTrack, &cutTrack, &pasteTrack, &copyBlock, &cutBlock, &pasteBlock })
    {
        addAndMakeVisible (b);
        b->iconWidth = 34.0f;
    }
    copyTrack.subLabel = cutTrack.subLabel = pasteTrack.subLabel = "TRACK";
    copyBlock.subLabel = cutBlock.subLabel = pasteBlock.subLabel = "BLOCK";
    copyTrack.icon = copyBlock.icon = Theme::iconClip;
    pasteTrack.icon = pasteBlock.icon = Theme::iconClip;
    cutTrack.icon = cutBlock.icon = Theme::iconScissors;

    copyTrack.onClick  = [&pv] { pv.copyTrack(); };
    cutTrack.onClick   = [&pv] { pv.cutTrack(); };
    pasteTrack.onClick = [&pv] { pv.pasteTrack(); };
    copyBlock.onClick  = [&pv] { pv.copyWholeBlock(); };
    cutBlock.onClick   = [&pv] { pv.cutWholeBlock(); };
    pasteBlock.onClick = [&pv] { pv.pasteWholeBlock(); };
}

void BlockPanel::paint (Graphics& g)
{
    Theme::drawPanel (g, Rectangle<float> (0.0f, 0.0f, 250.0f, (float) getHeight()));
    Theme::text (g, "BLOCK", { 0.0f, 0.0f, 250.0f, (float) getHeight() }, 46.0f, Theme::ink, Justification::centred, true);

    Theme::drawPanel (g, trackArea.toFloat());
    Theme::drawPanel (g, blockArea.toFloat());
    Theme::text (g, "TRACK OPERATIONS", trackArea.toFloat().translated (18.0f, 12.0f).withHeight (20.0f), 19.0f, Theme::ink, Justification::topLeft);
    Theme::text (g, "BLOCK OPERATIONS", blockArea.toFloat().translated (18.0f, 12.0f).withHeight (20.0f), 19.0f, Theme::ink, Justification::topLeft);

    captionUnder (g, cutTrack, "SHIFT+F3", 17.0f);
    captionUnder (g, copyTrack, "SHIFT+F4", 17.0f);
    captionUnder (g, pasteTrack, "SHIFT+F5", 17.0f);
    captionUnder (g, cutBlock, "CTRL+F3", 17.0f);
    captionUnder (g, copyBlock, "CTRL+F4", 17.0f);
    captionUnder (g, pasteBlock, "CTRL+F5", 17.0f);
}

void BlockPanel::resized()
{
    trackArea = { 258, 0, 590, getHeight() };
    blockArea = { 856, 0, getWidth() - 856, getHeight() };

    auto layout = [] (Rectangle<int> area, PixelButton& a, PixelButton& b, PixelButton& c)
    {
        auto r = area.reduced (16, 0).withTrimmedTop (46).withHeight (76);
        const int w = (r.getWidth() - 16) / 3;
        a.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
        b.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
        c.setBounds (r);
    };
    layout (trackArea, cutTrack, copyTrack, pasteTrack);
    layout (blockArea, cutBlock, copyBlock, pasteBlock);
}

// ===========================================================================
// InstrumentList
// ===========================================================================

InstrumentList::InstrumentList (EditorState& s) : st (s)
{
    addAndMakeVisible (load);
    addAndMakeVisible (discard);
    load.icon = Theme::iconFolder;
    discard.icon = Theme::iconTrash;
    load.iconWidth = discard.iconWidth = 40.0f;

    load.onClick = [this]
    {
        // in-app browser that plays each sample as you select it
        if (st.openSampleBrowser) st.openSampleBrowser();
    };

    discard.onClick = [this]
    {
        st.proc.discardInstrument (st.proc.selectedInstrument);
        st.setStatus ("DISCARDED INSTRUMENT " + hex2 (st.proc.selectedInstrument));
        st.changed();
    };
}

void InstrumentList::paint (Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat());
    Theme::text (g, "INSTRUMENTS", { 18.0f, 14.0f, 300.0f, 32.0f }, 30.0f, Theme::ink, Justification::centredLeft, true);

    Theme::drawScreen (g, listArea);
    const float rowH = (listArea.getHeight() - 4.0f) / kNumSlots;
    for (int i = 1; i <= kNumSlots; ++i)
    {
        auto r = Rectangle<float> (listArea.getX() + 2.0f, listArea.getY() + 2.0f + (i - 1) * rowH, listArea.getWidth() - 4.0f, rowH);
        const bool sel = i == st.proc.selectedInstrument;
        if (sel) { g.setColour (Theme::buttonLight); g.fillRect (r); }
        g.setColour (Colour (0xff2a2c31));
        if (i > 1) g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.5f);
        g.fillRect (r.getX() + 66.0f, r.getY(), 1.5f, r.getHeight());

        const auto col = sel ? Theme::ink : Theme::screenText;
        const auto& in = st.proc.getInstrument (i);
        Theme::text (g, formatNumber (i, 2, st.hex()), r.withWidth (66.0f), 20.0f, col, Justification::centred);
        Theme::text (g, in.isEmpty() ? String ("---") : in.name, r.withTrimmedLeft (86.0f), 20.0f, col, Justification::centredLeft);
    }
}

void InstrumentList::resized()
{
    listArea = { 12.0f, 62.0f, (float) getWidth() - 24.0f, 566.0f };
    const int w = (getWidth() - 24 - 10) / 2;
    load.setBounds (12, 640, w, 66);
    discard.setBounds (12 + w + 10, 640, w, 66);
}

int InstrumentList::rowAt (float y) const
{
    if (y < listArea.getY() || y > listArea.getBottom()) return -1;
    const float rowH = (listArea.getHeight() - 4.0f) / kNumSlots;
    return jlimit (1, kNumSlots, 1 + (int) ((y - listArea.getY() - 2.0f) / rowH));
}

void InstrumentList::mouseDown (const MouseEvent& e)
{
    const int r = rowAt (e.position.y);
    if (r <= 0) return;
    st.proc.selectedInstrument = r;
    if (onSelectionChanged) onSelectionChanged();
    st.changed();
}

void InstrumentList::mouseDoubleClick (const MouseEvent& e)
{
    const int r = rowAt (e.position.y);
    if (r > 0) previewOnce (st, r, this);
}

// ===========================================================================
// InstrumentsPage
// ===========================================================================

InstrumentsPage::InstrumentsPage (EditorState& s) : st (s), list (s)
{
    for (auto* c : std::initializer_list<Component*> { &list, &numberLabel, &name, &prev, &next, &volume, &transpose, &finetune,
                                                        &loop, &pingPong, &reverse, &wave })
        addAndMakeVisible (c);

    numberLabel.setScreen (false);
    prev.icon = [] (Graphics& g, Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (14.0f, 22.0f), c, false); };
    next.icon = [] (Graphics& g, Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (14.0f, 22.0f), c, true); };
    prev.onClick = [this] { st.proc.selectedInstrument = jmax (1, slot() - 1); st.changed(); };
    next.onClick = [this] { st.proc.selectedInstrument = jmin (kNumInstruments - 1, slot() + 1); st.changed(); };

    name.onChange = [this] (const String& t) { st.proc.getInstrument (slot()).name = t; st.proc.instrumentChanged (slot()); st.changed(); };

    volume.setDragSensitivity (2.0f);
    transpose.formatter = [] (int v) { return signedToString (v); };
    finetune.formatter = [] (int v) { return signedToString (v); };
    finetune.setDragSensitivity (1.5f);
    transpose.setDragSensitivity (6.0f);

    for (auto* v : { &volume, &transpose, &finetune }) v->onChange = [this] (int) { push(); };
    for (auto* sp : { &loop, &pingPong, &reverse }) sp->onChange = [this] (bool) { push(); };

    // synth controls
    for (auto* b : { &srcSample, &srcSynth, &srcMidi }) addAndMakeVisible (b);
    srcSample.onClick = [this] { setSource (srcIsSample); };
    srcSynth.onClick  = [this] { setSource (srcIsSynth); };
    srcMidi.onClick   = [this] { setSource (srcIsMidi); };

    // MIDI instrument: channel and program
    addChildComponent (midiChannelBox);
    addChildComponent (midiProgramBox);
    midiChannelBox.setShowSpinner (true);
    midiProgramBox.setShowSpinner (true);
    midiProgramBox.formatter = [] (int v) { return v == 0 ? String ("OFF") : String (v); };
    midiChannelBox.setDragSensitivity (8.0f);
    midiProgramBox.setDragSensitivity (3.0f);
    midiChannelBox.onChange = [this] (int v)
    {
        auto& in = st.proc.getInstrument (slot());
        if (! in.isMidi()) return;
        if (in.name == "MIDI CH " + String (in.params.midiChannel)) in.name = "MIDI CH " + String (v);
        in.params.midiChannel = v;
        st.proc.instrumentChanged (slot());
        st.changed();
    };
    midiProgramBox.onChange = [this] (int v)
    {
        auto& in = st.proc.getInstrument (slot());
        if (! in.isMidi()) return;
        in.params.midiProgram = v;
        st.proc.instrumentChanged (slot());
    };
    for (int w = 0; w < numSynthWaves; ++w)
    {
        auto b = std::make_unique<PixelButton> (synthWaveName (w), PixelButton::Style::Segment, 17.0f);
        b->icon = [w] (Graphics& g, Rectangle<float> r, Colour c) { drawWaveIcon (g, r, c, w); };
        b->iconWidth = 70.0f;
        b->onClick = [this, w]
        {
            auto& in = st.proc.getInstrument (slot());
            in.makeSynth (w);
            st.proc.instrumentChanged (slot());
            previewOnce (st, slot(), this);
            st.changed();
        };
        addChildComponent (*b);
        waveButtons[(size_t) w] = std::move (b);
    }
    for (auto* sl : { &attack, &decay, &release })
    {
        sl->skew = 3.0f;
        sl->formatter = [] (int v) { return v >= 1000 ? String (v / 1000.0, 2) + " S" : String (v) + " MS"; };
    }
    cutoff.formatter = [] (int v) { const double hz = 30.0 * std::pow (2.0, v / 127.0 * 9.4);
                                    return v >= 127 ? String ("OPEN") : hz >= 1000.0 ? String (hz / 1000.0, 1) + " KHZ" : String (roundToInt (hz)) + " HZ"; };
    for (auto* sl : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &distortion, &accent })
    {
        sl->onChange = [this] (int) { pushSynth(); };
        addChildComponent (sl);
    }

    wave.onClicked = [this] { previewOnce (st, slot(), this); };
    list.onSelectionChanged = [this] { instrumentsChanged(); };
    instrumentsChanged();
}

void InstrumentsPage::push()
{
    auto& in = st.proc.getInstrument (slot());
    if (in.isEmpty()) return;
    in.params.volume = volume.getValue();
    in.params.transpose = transpose.getValue();
    in.params.fineTune = finetune.getValue();
    in.params.loop = loop.getState();
    in.params.pingPong = pingPong.getState();
    in.params.reverse = reverse.getState();
    st.proc.instrumentChanged (slot());
}

void InstrumentsPage::setSource (int source)
{
    auto& in = st.proc.getInstrument (slot());
    const int now = in.isMidi() ? srcIsMidi : in.isSynth() ? srcIsSynth : srcIsSample;
    if (source == now) { instrumentsChanged(); return; }

    if (source == srcIsSynth)
    {
        in.makeSynth (waveSine);
        st.setStatus ("SLOT " + hex2 (slot()) + " IS NOW A SYNTH");
    }
    else if (source == srcIsMidi)
    {
        in.makeMidi();
        st.setStatus ("SLOT " + hex2 (slot()) + " IS NOW A MIDI INSTRUMENT - CHOOSE THE MIDI OUTPUT ON THE MIDI / AUDIO PAGE");
    }
    else
    {
        in.clear();
        st.setStatus ("SLOT " + hex2 (slot()) + " CLEARED - LOAD OR DROP A SAMPLE");
    }
    st.proc.instrumentChanged (slot());
    st.changed();
}

void InstrumentsPage::pushSynth()
{
    auto& in = st.proc.getInstrument (slot());
    if (! in.isSynth()) return;
    in.params.attack = attack.getValue();
    in.params.decay = decay.getValue();
    in.params.sustain = sustain.getValue();
    in.params.release = release.getValue();
    in.params.cutoff = cutoff.getValue();
    in.params.resonance = resonance.getValue();
    in.params.distortion = distortion.getValue();
    in.params.accent = accent.getValue();
    st.proc.instrumentChanged (slot());
}

void InstrumentsPage::updateSynthVisibility()
{
    for (auto& b : waveButtons) b->setVisible (synthMode);
    for (auto* sl : { &attack, &decay, &sustain, &release, &cutoff, &resonance, &distortion, &accent }) sl->setVisible (synthMode);
    midiChannelBox.setVisible (midiMode);
    midiProgramBox.setVisible (midiMode);
    wave.setVisible (! synthMode && ! midiMode);   // the waveform buttons show the synth's shape
    for (auto* sp : { &loop, &pingPong, &reverse })
    {
        sp->setEnabled (! synthMode && ! midiMode);
        sp->setAlpha (synthMode || midiMode ? 0.35f : 1.0f);
    }
    wave.showMarkers = ! synthMode;     // a synth's loop is the whole cycle: no need to shade it
    resized();
}

void InstrumentsPage::instrumentsChanged()
{
    auto& in = st.proc.getInstrument (slot());
    const bool nowSynth = in.isSynth(), nowMidi = in.isMidi();
    if (nowSynth != synthMode || nowMidi != midiMode) { synthMode = nowSynth; midiMode = nowMidi; updateSynthVisibility(); }
    srcSample.setToggleState (! nowSynth && ! nowMidi, dontSendNotification);
    srcSynth.setToggleState (nowSynth, dontSendNotification);
    srcMidi.setToggleState (nowMidi, dontSendNotification);
    midiChannelBox.setValue (in.params.midiChannel);
    midiProgramBox.setValue (in.params.midiProgram);
    for (int w = 0; w < numSynthWaves; ++w) waveButtons[(size_t) w]->setToggleState (nowSynth && in.params.synthWave == w, dontSendNotification);
    attack.setValue (in.params.attack);
    decay.setValue (in.params.decay);
    sustain.setValue (in.params.sustain);
    release.setValue (in.params.release);
    cutoff.setValue (in.params.cutoff);
    resonance.setValue (in.params.resonance);
    distortion.setValue (in.params.distortion);
    accent.setValue (in.params.accent);
    numberLabel.setText (formatNumber (slot(), 2, st.hex()));
    name.setText (in.name);
    volume.setValue (in.params.volume);
    transpose.setValue (in.params.transpose);
    finetune.setValue (in.params.fineTune);
    loop.setState (in.params.loop);
    pingPong.setState (in.params.pingPong);
    reverse.setState (in.params.reverse);
    wave.setSample (in.data, &in.params);
    repaint();
}

void InstrumentsPage::refresh() {}

void InstrumentsPage::paint (Graphics& g)
{
    auto& in = st.proc.getInstrument (slot());

    Theme::drawPanel (g, header.toFloat());
    Theme::text (g, "INSTRUMENT", header.toFloat().withTrimmedLeft (16.0f), 30.0f, Theme::ink, Justification::centredLeft, true);
    {
        auto nb = numberLabel.getBounds().toFloat().expanded (2.0f);
        Theme::drawRaised (g, nb, Theme::buttonLight);
    }

    Theme::drawPanel (g, paramsPanel.toFloat());
    labelLeft (g, "VOLUME",    { paramsPanel.getX() + 26, volume.getY(), 180, volume.getHeight() }, 21.0f);
    labelLeft (g, "TRANSPOSE", { paramsPanel.getX() + 26, transpose.getY(), 180, transpose.getHeight() }, 21.0f);
    labelLeft (g, "FINE TUNE", { paramsPanel.getX() + 26, finetune.getY(), 180, finetune.getHeight() }, 21.0f);

    Theme::drawPanel (g, togglesPanel.toFloat());
    labelLeft (g, "LOOP",      { togglesPanel.getX() + 18, loop.getY(), 160, loop.getHeight() }, 18.0f);
    labelLeft (g, "PING PONG", { togglesPanel.getX() + 18, pingPong.getY(), 160, pingPong.getHeight() }, 18.0f);
    labelLeft (g, "REVERSE",   { togglesPanel.getX() + 18, reverse.getY(), 160, reverse.getHeight() }, 18.0f);

    Theme::drawPanel (g, infoPanel.toFloat());
    Theme::text (g, "SAMPLE INFO", infoPanel.toFloat().translated (10.0f, 12.0f).withHeight (22.0f), 21.0f, Theme::ink, Justification::topLeft);
    infoTable (g, infoPanel.toFloat().withTrimmedTop (42.0f).reduced (8.0f, 6.0f), in);

    Theme::drawPanel (g, dropBar.toFloat());
    labelLeft (g, "SOURCE", { dropBar.getX() + 18, dropBar.getY(), 120, dropBar.getHeight() }, 18.0f);
    {
        g.saveState();
        auto tr = dropBar.toFloat().withTrimmedLeft ((float) (srcMidi.getRight() - dropBar.getX()) + 22.0f).withTrimmedRight (12.0f);
        g.reduceClipRegion (tr.toNearestInt());
        Theme::text (g, in.isMidi() ? String ("NOTES GO TO MIDI OUT")
                       : in.isSynth() ? String ("BUILT-IN SYNTH - WAVEFORM, ENVELOPE AND FILTER")
                       : in.isEmpty() ? String ("DROP AUDIO FILES HERE OR PRESS LOAD   (WAV AIFF FLAC OGG MP3)")
                       : (in.sourcePath.isNotEmpty() ? in.sourcePath.toUpperCase() : String ("BUILT-IN DEMO SOUND - DROP A FILE TO REPLACE IT")),
                     tr, 16.0f, Theme::ink.withAlpha (0.75f), Justification::centredLeft);
        g.restoreState();
    }

    Theme::drawPanel (g, slotPanel.toFloat());

    if (midiMode)
    {
        Theme::text (g, "MIDI OUT", slotPanel.toFloat().translated (16.0f, 14.0f).withHeight (26.0f), 25.0f, Theme::ink, Justification::topLeft, true);
        labelLeft (g, "MIDI CHANNEL", { midiChannelBox.getX() - 250, midiChannelBox.getY(), 240, midiChannelBox.getHeight() }, 19.0f);
        labelLeft (g, "PROGRAM",      { midiProgramBox.getX() - 250, midiProgramBox.getY(), 240, midiProgramBox.getHeight() }, 19.0f);
        auto info = Rectangle<float> ((float) midiChannelBox.getRight() + 40.0f, (float) midiChannelBox.getY(),
                                      (float) slotPanel.getRight() - midiChannelBox.getRight() - 56.0f, 220.0f);
        Theme::drawScreen (g, info);
        const char* lines[] = { "NOTES ARE SENT AS MIDI:",
                                "STANDALONE: MIDI OUT ON THE MIDI / AUDIO PAGE",
                                "PLUGIN: TO YOUR DAW",
                                "C-4 = NOTE 60, VOLUME / 0C = VELOCITY",
                                "=== OR A NEW NOTE = NOTE OFF",
                                "PROGRAM: OFF = NO PROGRAM CHANGE" };
        auto r = info.reduced (16.0f, 12.0f);
        g.saveState();
        g.reduceClipRegion (info.reduced (4.0f).toNearestInt());
        for (auto* l : lines) Theme::text (g, l, r.removeFromTop (32.0f), 15.0f, Theme::screenText, Justification::centredLeft);
        g.restoreState();
        return;
    }

    if (synthMode)
    {
        Theme::text (g, "SYNTH", slotPanel.toFloat().translated (16.0f, 14.0f).withHeight (26.0f), 25.0f, Theme::ink, Justification::topLeft, true);
        auto lab = [&] (const String& t, const Component& c)
        {
            labelLeft (g, t, { c.getX() - 168, c.getY(), 164, c.getHeight() }, 17.0f);
        };
        lab ("ATTACK", attack); lab ("DECAY", decay); lab ("SUSTAIN", sustain); lab ("RELEASE", release);
        lab ("CUTOFF", cutoff); lab ("RESONANCE", resonance);
        lab ("DISTORTION", distortion); lab ("ACCENT", accent);
        return;
    }

    Theme::text (g, "INSTRUMENT SLOT", slotPanel.toFloat().translated (16.0f, 14.0f).withHeight (26.0f), 25.0f, Theme::ink, Justification::topLeft, true);

    // slot grid: 4 columns x 4 rows
    Theme::drawScreen (g, slotGrid.toFloat());
    const float cw = (slotGrid.getWidth() - 8.0f) / 4.0f, rh = (slotGrid.getHeight() - 8.0f) / 4.0f;
    for (int i = 1; i <= kNumSlots; ++i)
    {
        const int col = (i - 1) / 4, row = (i - 1) % 4;
        auto cell = Rectangle<float> (slotGrid.getX() + 4.0f + col * cw, slotGrid.getY() + 4.0f + row * rh, cw, rh).reduced (6.0f, 2.0f);
        const bool sel = i == slot();
        g.setColour (sel ? Theme::buttonLight : Colour (0xff111215));
        g.fillRect (cell);
        g.setColour (Colour (0xff2a2c31));
        g.fillRect (cell.getX() + 62.0f, cell.getY(), 1.5f, cell.getHeight());
        const auto colr = sel ? Theme::ink : Theme::screenText;
        const auto& si = st.proc.getInstrument (i);
        Theme::text (g, formatNumber (i, 2, st.hex()), cell.withWidth (62.0f), 20.0f, colr, Justification::centred);
        Theme::text (g, si.isEmpty() ? String ("---") : si.name.substring (0, 11), cell.withTrimmedLeft (80.0f), 20.0f, colr, Justification::centredLeft);
    }
}

void InstrumentsPage::mouseDown (const MouseEvent& e)
{
    if (synthMode || midiMode || ! slotGrid.contains (e.getPosition())) return;
    const float cw = (slotGrid.getWidth() - 8.0f) / 4.0f, rh = (slotGrid.getHeight() - 8.0f) / 4.0f;
    const int col = jlimit (0, 3, (int) ((e.position.x - slotGrid.getX() - 4.0f) / cw));
    const int row = jlimit (0, 3, (int) ((e.position.y - slotGrid.getY() - 4.0f) / rh));
    const int i = col * 4 + row + 1;
    st.proc.selectedInstrument = i;
    if (e.getNumberOfClicks() > 1) previewOnce (st, i, this);
    st.changed();
}

void InstrumentsPage::resized()
{
    list.setBounds (0, 0, 390, getHeight());
    header = { 398, 0, getWidth() - 398, 66 };
    numberLabel.setBounds (header.getX() + 296, 9, 70, 48);
    name.setBounds (header.getX() + 380, 9, header.getWidth() - 380 - 166, 48);
    prev.setBounds (header.getRight() - 160, 9, 74, 48);
    next.setBounds (header.getRight() - 80, 9, 74, 48);

    paramsPanel  = { 398, 74, 440, 210 };
    togglesPanel = { 844, 74, 346, 210 };
    infoPanel    = { 1196, 74, getWidth() - 1196, 210 };

    volume.setBounds (paramsPanel.getX() + 206, 92, 130, 50);
    transpose.setBounds (paramsPanel.getX() + 206, 152, 130, 50);
    finetune.setBounds (paramsPanel.getX() + 206, 214, 130, 50);

    loop.setBounds (togglesPanel.getX() + 182, 92, 152, 52);
    pingPong.setBounds (togglesPanel.getX() + 182, 152, 152, 52);
    reverse.setBounds (togglesPanel.getX() + 182, 214, 152, 52);

    dropBar  = { 398, 292, getWidth() - 398, 56 };
    slotPanel = { 398, 356, getWidth() - 398, getHeight() - 356 };
    slotGrid = { slotPanel.getX() + 12, slotPanel.getY() + 52, slotPanel.getWidth() - 24, 196 };
    {
        const int bx = dropBar.getX() + 130, by = dropBar.getY() + 7, bh = dropBar.getHeight() - 14, bw = 118;
        srcSample.setBounds (bx, by, bw, bh);
        srcSynth.setBounds (bx + bw + 6, by, bw, bh);
        srcMidi.setBounds (bx + 2 * (bw + 6), by, bw, bh);
    }

    if (midiMode)
    {
        midiChannelBox.setBounds (slotPanel.getX() + 280, slotPanel.getY() + 70, 150, 56);
        midiProgramBox.setBounds (slotPanel.getX() + 280, slotPanel.getY() + 140, 150, 56);
        return;
    }

    if (! synthMode)
    {
        wave.setBounds (slotPanel.getX() + 6, slotGrid.getBottom() + 10, slotPanel.getWidth() - 12, slotPanel.getBottom() - slotGrid.getBottom() - 16);
        return;
    }

    // synth: waveform buttons, then envelope (left) and filter + waveform view (right)
    const int x0 = slotPanel.getX() + 12, w = slotPanel.getWidth() - 24;
    const int bw = (w - 4 * 8) / 5;
    for (int i = 0; i < numSynthWaves; ++i)
        waveButtons[(size_t) i]->setBounds (x0 + i * (bw + 8), slotPanel.getY() + 50, bw, 62);

    const int rowY = slotPanel.getY() + 126, rowH = 46, step = 54;
    const int colW = (w - 20) / 2;
    const int lx = x0 + 168, lw = colW - 168;
    attack.setBounds  (lx, rowY,            lw, rowH);
    decay.setBounds   (lx, rowY + step,     lw, rowH);
    sustain.setBounds (lx, rowY + step * 2, lw, rowH);
    release.setBounds (lx, rowY + step * 3, lw, rowH);

    const int rx = x0 + colW + 20 + 168, rw = colW - 168;
    cutoff.setBounds     (rx, rowY,            rw, rowH);
    resonance.setBounds  (rx, rowY + step,     rw, rowH);
    distortion.setBounds (rx, rowY + step * 2, rw, rowH);
    accent.setBounds     (rx, rowY + step * 3, rw, rowH);
}

// ===========================================================================
// SampleEditPage
// ===========================================================================

SampleEditPage::SampleEditPage (EditorState& s) : st (s), list (s)
{
    for (auto* c : std::initializer_list<Component*> { &list, &numberLabel, &name, &prev, &next, &lengthVal, &rateVal, &bitsVal, &chansVal,
                                                        &curPos, &wave, &startBox, &endBox, &loopStartBox, &loopEndBox, &setStart, &setEnd,
                                                        &setLoop, &playLoop, &loopMode, &cut, &copy, &paste, &fadeIn, &fadeOut, &normalize,
                                                        &reverseBtn, &mix, &zoomIn, &zoomOut, &fit, &left, &right })
        addAndMakeVisible (c);

    for (auto* l : { &lengthVal, &rateVal, &bitsVal, &chansVal }) l->setScreen (true);
    numberLabel.setScreen (true);

    prev.icon = [] (Graphics& g, Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (14.0f, 22.0f), c, false); };
    next.icon = [] (Graphics& g, Rectangle<float> r, Colour c) { Theme::iconArrow (g, r.withSizeKeepingCentre (14.0f, 22.0f), c, true); };
    left.icon = prev.icon; right.icon = next.icon;
    prev.onClick = [this] { st.proc.selectedInstrument = jmax (1, slot() - 1); st.changed(); };
    next.onClick = [this] { st.proc.selectedInstrument = jmin (kNumInstruments - 1, slot() + 1); st.changed(); };
    name.onChange = [this] (const String& t) { st.proc.getInstrument (slot()).name = t; st.proc.instrumentChanged (slot()); st.changed(); };
    list.onSelectionChanged = [this] { wave.clearSelection(); instrumentsChanged(); };

    auto five = [] (int v) { return String (v).paddedLeft ('0', 5); };
    for (auto* v : { &curPos, &startBox, &endBox, &loopStartBox, &loopEndBox })
    {
        v->formatter = five;
        v->setShowSpinner (true);
        v->setDragSensitivity (0.5f);
        v->setLargeStep (1000);
    }

    curPos.onChange = [this] (int v) { wave.setCursor (v); };
    wave.onCursorChanged = [this] { curPos.setValue (wave.getCursor()); };
    wave.onActivated = [this]
    {
        if (! wave.areKeysActive()) { st.setStatus ("WAVEFORM: CTRL+A SELECT ALL, CTRL+C / X / V, DEL"); st.changed(); }
        wave.setKeysActive (true);
    };
    wave.onPointsChanged = [this] (const InstrumentParams& p) { pushPoints (p); };

    auto pointsFromBoxes = [this]
    {
        auto p = st.proc.getInstrument (slot()).params;
        p.start = startBox.getValue();
        p.end = endBox.getValue();
        p.loopStart = loopStartBox.getValue();
        p.loopEnd = loopEndBox.getValue();
        pushPoints (p);
    };
    for (auto* v : { &startBox, &endBox, &loopStartBox, &loopEndBox }) v->onChange = [pointsFromBoxes] (int) { pointsFromBoxes(); };

    setStart.onClick = [this] { auto p = st.proc.getInstrument (slot()).params; p.start = wave.getCursor(); if (p.end < p.start) p.end = p.start; pushPoints (p); };
    setEnd.onClick   = [this] { auto p = st.proc.getInstrument (slot()).params; p.end = wave.getCursor(); if (p.start > p.end) p.start = p.end; pushPoints (p); };
    setLoop.onClick  = [this]
    {
        auto p = st.proc.getInstrument (slot()).params;
        if (wave.hasSelection()) { p.loopStart = wave.getSelectionStart(); p.loopEnd = wave.getSelectionEnd(); }
        else                     { p.loopStart = wave.getCursor(); p.loopEnd = p.end; }
        p.loop = true;
        pushPoints (p);
        st.setStatus ("LOOP SET");
    };
    playLoop.onClick = [this]
    {
        if (loopPreviewing) { st.engine().requestStopPreview(); loopPreviewing = false; }
        else
        {
            auto& in = st.proc.getInstrument (slot());
            if (! in.params.loop) { st.setStatus ("LOOP IS OFF - USE SET LOOP FIRST"); st.changed(); return; }
            st.engine().requestPreview (slot(), kNativeNote, true);
            loopPreviewing = true;
        }
        playLoop.setButtonText (loopPreviewing ? "STOP LOOP" : "PLAY LOOP");
    };
    loopMode.onChange = [this] (bool forward)
    {
        auto p = st.proc.getInstrument (slot()).params;
        p.loopBackward = ! forward;
        p.pingPong = false;
        pushPoints (p);
    };

    auto range = [this] (int& from, int& to)
    {
        if (wave.hasSelection()) { from = wave.getSelectionStart(); to = wave.getSelectionEnd(); return true; }
        auto& in = st.proc.getInstrument (slot());
        from = 0; to = in.isEmpty() ? 0 : in.data->getLength() - 1;
        return false;
    };
    ignoreUnused (range);

    cut.onClick       = [this] { edit ([this] (Instrument& in, int a, int b) { return in.cut (a, b, clip); }, true, "CUT"); wave.clearSelection(); };
    copy.onClick      = [this]
    {
        if (! wave.hasSelection()) { st.setStatus ("SELECT PART OF THE SAMPLE FIRST"); st.changed(); return; }
        st.proc.getInstrument (slot()).copy (wave.getSelectionStart(), wave.getSelectionEnd(), clip);
        st.setStatus ("COPIED " + String (clip.getNumSamples()) + " FRAMES");
        st.changed();
    };
    paste.onClick     = [this]
    {
        if (clip.getNumSamples() == 0) { st.setStatus ("SAMPLE CLIPBOARD IS EMPTY"); st.changed(); return; }
        auto& in = st.proc.getInstrument (slot());
        if (in.paste (wave.getCursor(), clip))
        {
            st.proc.instrumentChanged (slot());
            wave.setSelection (wave.getCursor(), wave.getCursor() + clip.getNumSamples() - 1);
            st.setStatus ("PASTED");
            st.changed();
        }
    };
    fadeIn.onClick    = [this] { edit ([] (Instrument& in, int a, int b) { return in.fadeIn (a, b); }, false, "FADE IN"); };
    fadeOut.onClick   = [this] { edit ([] (Instrument& in, int a, int b) { return in.fadeOut (a, b); }, false, "FADE OUT"); };
    normalize.onClick = [this] { edit ([] (Instrument& in, int a, int b) { return in.normalise (a, b); }, false, "NORMALIZE"); };
    reverseBtn.onClick= [this] { edit ([] (Instrument& in, int a, int b) { return in.reverseRange (a, b); }, false, "REVERSE"); };

    // MIX (like OctaMED): pick another slot, its sample is mixed in from the cursor (or the selection start)
    mix.onClick = [this]
    {
        auto& in = st.proc.getInstrument (slot());
        if (in.isEmpty()) { st.setStatus ("NO SAMPLE IN THIS SLOT"); st.changed(); return; }
        PopupMenu m;
        m.addSectionHeader ("MIX INTO " + hex2 (slot()) + " FROM:");
        for (int i = 1; i < kNumInstruments; ++i)
        {
            const auto& o = st.proc.getInstrument (i);
            if (i != slot() && ! o.isEmpty())
                m.addItem (i, formatNumber (i, 2, st.hex()) + "  " + o.name);
        }
        if (m.getNumItems() <= 1) { st.setStatus ("LOAD ANOTHER SAMPLE TO MIX IN"); st.changed(); return; }
        Component::SafePointer<SampleEditPage> safe (this);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&mix), [safe] (int chosen)
        {
            if (safe == nullptr || chosen <= 0) return;
            auto& self = *safe;
            auto& dst = self.st.proc.getInstrument (self.slot());
            const int at = self.wave.hasSelection() ? self.wave.getSelectionStart() : self.wave.getCursor();
            if (dst.mixIn (self.st.proc.getInstrument (chosen), at))
            {
                self.st.proc.instrumentChanged (self.slot());
                self.st.setStatus ("MIXED " + hex2 (chosen) + " " + self.st.proc.getInstrument (chosen).name.toUpperCase()
                                   + " IN AT " + String (at));
            }
            self.st.changed();
        });
    };
    mix.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        r = r.reduced (r.getWidth() * 0.18f, r.getHeight() * 0.25f);
        for (int w = 0; w < 2; ++w)
        {
            Path p;
            for (int i = 0; i <= 32; ++i)
            {
                const float x = r.getX() + r.getWidth() * i / 32.0f;
                const float y = r.getCentreY() - std::sin (i / 32.0f * MathConstants<float>::twoPi * (w == 0 ? 1.0f : 2.0f))
                                                 * r.getHeight() * (w == 0 ? 0.45f : 0.25f);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (c);
            g.strokePath (p, PathStrokeType (2.2f));
        }
    };

    cut.icon = Theme::iconScissors;
    copy.icon = Theme::iconClip;
    paste.icon = Theme::iconClip;
    fadeIn.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        g.setColour (c);
        for (int i = 0; i < 9; ++i) { const float h = r.getHeight() * (i + 1) / 9.0f; g.fillRect (r.getX() + i * r.getWidth() / 9.0f, r.getBottom() - h, r.getWidth() / 9.0f - 1.5f, h); }
    };
    fadeOut.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        g.setColour (c);
        for (int i = 0; i < 9; ++i) { const float h = r.getHeight() * (9 - i) / 9.0f; g.fillRect (r.getX() + i * r.getWidth() / 9.0f, r.getBottom() - h, r.getWidth() / 9.0f - 1.5f, h); }
    };
    normalize.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        g.setColour (c);
        const float hs[] = { 0.35f, 0.55f, 0.8f, 1.0f };
        for (int i = 0; i < 4; ++i) { const float h = r.getHeight() * hs[i]; g.fillRect (r.getX() + r.getWidth() * (0.15f + i * 0.18f), r.getBottom() - h, r.getWidth() * 0.13f, h); }
    };
    reverseBtn.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        Path p;
        auto t = r.withSizeKeepingCentre (r.getHeight() * 0.9f, r.getHeight() * 0.75f);
        p.addTriangle (t.getX(), t.getBottom(), t.getRight(), t.getBottom(), t.getCentreX(), t.getY());
        g.setColour (c);
        g.fillPath (p);
    };
    zoomIn.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        auto b = r.withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (c);
        g.drawEllipse (b.withSize (20.0f, 20.0f), 2.5f);
        g.drawLine (b.getX() + 17.0f, b.getY() + 17.0f, b.getRight(), b.getBottom(), 3.0f);
        g.fillRect (b.getX() + 5.0f, b.getY() + 9.0f, 10.0f, 2.5f);
        g.fillRect (b.getX() + 8.75f, b.getY() + 5.0f, 2.5f, 10.0f);
    };
    zoomOut.icon = [] (Graphics& g, Rectangle<float> r, Colour c)
    {
        auto b = r.withSizeKeepingCentre (26.0f, 26.0f);
        g.setColour (c);
        g.drawEllipse (b.withSize (20.0f, 20.0f), 2.5f);
        g.drawLine (b.getX() + 17.0f, b.getY() + 17.0f, b.getRight(), b.getBottom(), 3.0f);
        g.fillRect (b.getX() + 5.0f, b.getY() + 9.0f, 10.0f, 2.5f);
    };

    zoomIn.onClick  = [this] { wave.zoomIn(); };
    zoomOut.onClick = [this] { wave.zoomOut(); };
    fit.onClick     = [this] { wave.zoomFit(); };
    left.onClick    = [this] { wave.scroll (-1); };
    right.onClick   = [this] { wave.scroll (1); };

    captions = { { &cut, "CUT" }, { &copy, "COPY" }, { &paste, "PASTE" }, { &fadeIn, "FADE IN" }, { &fadeOut, "FADE OUT" },
                 { &normalize, "NORMALIZE" }, { &reverseBtn, "REVERSE" }, { &mix, "MIX" } };
    for (auto& [b, t] : captions) b->setButtonText ({});

    instrumentsChanged();
}

void SampleEditPage::mouseDown (const MouseEvent&)
{
    wave.setKeysActive (false);     // clicked outside the waveform
}

void SampleEditPage::visibilityChanged()
{
    if (! isVisible()) wave.setKeysActive (false);
}

bool SampleEditPage::handleKey (const KeyPress& k)
{
    if (! wave.areKeysActive()) return false;
    const auto m = k.getModifiers();
    const int code = k.getKeyCode();
    const bool ctrl = m.isCommandDown() && ! m.isAltDown();
    auto is = [&] (char ch) { return ctrl && (code == ch || code == ch + 32); };

    if (is ('A'))
    {
        auto& in = st.proc.getInstrument (slot());
        if (in.isEmpty()) return true;
        wave.setSelection (0, in.data->getLength() - 1);
        st.setStatus ("WHOLE SAMPLE SELECTED");
        st.changed();
        return true;
    }
    // run the buttons' actions right away (triggerClick would run them later)
    if (is ('C')) { if (copy.onClick)  copy.onClick();  return true; }
    if (is ('X')) { if (cut.onClick)   cut.onClick();   return true; }
    if (is ('V')) { if (paste.onClick) paste.onClick(); return true; }
    if (code == KeyPress::deleteKey && ! m.isAnyModifierKeyDown() && wave.hasSelection())
    {
        // Delete removes the selection without touching the clipboard
        edit ([] (Instrument& in, int a, int b) { AudioBuffer<float> scratch; return in.cut (a, b, scratch); }, true, "DELETED");
        wave.clearSelection();
        return true;
    }
    if (code == KeyPress::escapeKey && wave.hasSelection()) { wave.clearSelection(); return true; }
    return false;
}

void SampleEditPage::edit (std::function<bool (Instrument&, int, int)> op, bool needsSelection, const String& what)
{
    auto& in = st.proc.getInstrument (slot());
    if (in.isEmpty()) { st.setStatus ("NO SAMPLE IN THIS SLOT"); st.changed(); return; }
    int a = 0, b = in.data->getLength() - 1;
    if (wave.hasSelection()) { a = wave.getSelectionStart(); b = wave.getSelectionEnd(); }
    else if (needsSelection) { st.setStatus ("SELECT PART OF THE SAMPLE FIRST"); st.changed(); return; }

    if (op (in, a, b))
    {
        st.proc.instrumentChanged (slot());
        st.setStatus (what + (wave.hasSelection() ? " (SELECTION)" : " (WHOLE SAMPLE)"));
    }
    st.changed();
}

void SampleEditPage::pushPoints (const InstrumentParams& p)
{
    auto& in = st.proc.getInstrument (slot());
    if (in.isEmpty()) return;
    in.params = p;
    st.proc.instrumentChanged (slot());
    instrumentsChanged();
}

void SampleEditPage::instrumentsChanged()
{
    auto& in = st.proc.getInstrument (slot());
    numberLabel.setText (formatNumber (slot(), 2, st.hex()));
    name.setText (in.name);

    const bool empty = in.isEmpty();
    const int len = empty ? 0 : in.data->getLength();
    lengthVal.setText (empty ? "-" : String (len));
    rateVal.setText (empty ? "-" : String ((int) in.data->sampleRate));
    bitsVal.setText (empty ? "-" : String (in.data->bitDepth));
    chansVal.setText (empty ? "-" : (in.data->getNumChannels() == 1 ? String ("1 (MONO)") : String ("2 (STEREO)")));

    const int last = jmax (0, len - 1);
    curPos.setRange (0, last);
    for (auto* v : { &startBox, &endBox, &loopStartBox, &loopEndBox }) v->setRange (0, last);
    startBox.setValue (in.params.start);
    endBox.setValue (in.params.end);
    loopStartBox.setValue (in.params.loopStart);
    loopEndBox.setValue (in.params.loopEnd);
    loopMode.setState (! in.params.loopBackward);

    wave.setSample (in.data, &in.params);
    curPos.setValue (wave.getCursor());
    repaint();
}

void SampleEditPage::refresh()
{
    const int pp = st.engine().previewPosition.load();
    wave.setPlayhead (pp);
    if (loopPreviewing && pp < 0)
    {
        loopPreviewing = false;
        playLoop.setButtonText ("PLAY LOOP");
    }
}

void SampleEditPage::paint (Graphics& g)
{
    Theme::drawPanel (g, header.toFloat());
    Theme::text (g, "SAMPLE EDIT", header.toFloat().withTrimmedLeft (8.0f), 28.0f, Theme::ink, Justification::centredLeft, true);

    Theme::drawPanel (g, infoRow.toFloat());
    const char* titles[] = { "LENGTH", "SAMPLE RATE", "BIT DEPTH", "CHANNELS", "CURRENT POS" };
    Component* fields[] = { &lengthVal, &rateVal, &bitsVal, &chansVal, &curPos };
    for (int i = 0; i < 5; ++i)
    {
        auto b = fields[i]->getBounds();
        Theme::text (g, titles[i], { (float) b.getX(), (float) infoRow.getY() + 10.0f, 260.0f, 20.0f }, 19.0f, Theme::ink, Justification::topLeft);
        if (i > 0) { g.setColour (Theme::panelEdgeLo); g.fillRect ((float) b.getX() - 10.0f, (float) infoRow.getY() + 4.0f, 2.0f, (float) infoRow.getHeight() - 8.0f); }
    }

    Theme::drawPanel (g, pointsRow.toFloat());
    labelLeft (g, "START",      { startBox.getX() - 84, startBox.getY(), 80, startBox.getHeight() }, 16.0f);
    labelLeft (g, "END",        { endBox.getX() - 56, endBox.getY(), 56, endBox.getHeight() }, 16.0f);
    labelLeft (g, "LOOP START", { loopStartBox.getX() - 146, loopStartBox.getY(), 146, loopStartBox.getHeight() }, 16.0f);
    labelLeft (g, "LOOP END",   { loopEndBox.getX() - 114, loopEndBox.getY(), 114, loopEndBox.getHeight() }, 16.0f);

    Theme::drawPanel (g, buttonsRow.toFloat());
    labelLeft (g, "LOOP MODE", { loopMode.getX() - 136, loopMode.getY(), 132, loopMode.getHeight() }, 16.0f);

    Theme::drawPanel (g, editPanel.toFloat());
    Theme::text (g, "EDIT", editPanel.toFloat().translated (12.0f, 10.0f).withHeight (22.0f), 21.0f, Theme::ink, Justification::topLeft);
    for (auto& [b, t] : captions)   // shrink long captions so they stay under their own button
        captionUnder (g, *b, t, jmin (13.0f, 13.0f * (float) (b->getWidth() + 4) / jmax (1.0f, PixelFont::getWidth (t, 13.0f))));

    Theme::drawPanel (g, zoomPanel.toFloat());
    Theme::text (g, "ZOOM", zoomPanel.toFloat().translated (12.0f, 10.0f).withHeight (22.0f), 21.0f, Theme::ink, Justification::topLeft);
    Theme::text (g, "SCROLL", Rectangle<float> ((float) left.getX(), (float) left.getBottom() + 8.0f, (float) (right.getRight() - left.getX()), 18.0f),
                 16.0f, Theme::ink, Justification::centredTop);
}

void SampleEditPage::resized()
{
    list.setBounds (0, 0, 390, getHeight());
    const int x0 = 396, w = getWidth() - x0;

    header = { x0, 0, w, 62 };
    numberLabel.setBounds (x0 + 290, 7, 70, 48);
    name.setBounds (x0 + 372, 7, w - 372 - 170, 48);
    prev.setBounds (header.getRight() - 162, 7, 74, 48);
    next.setBounds (header.getRight() - 82, 7, 74, 48);

    infoRow = { x0, 68, w, 96 };
    const int fw = (w - 20) / 5;
    Component* fields[] = { &lengthVal, &rateVal, &bitsVal, &chansVal, &curPos };
    for (int i = 0; i < 5; ++i) fields[i]->setBounds (x0 + 12 + i * fw, 104, fw - 22, 48);

    wave.setBounds (x0, 172, w, 236);

    pointsRow = { x0, 412, w, 66 };
    const int pw = w / 4;
    startBox.setBounds (x0 + 90, 421, pw - 104, 48);
    endBox.setBounds (x0 + pw + 62, 421, pw - 76, 48);
    loopStartBox.setBounds (x0 + 2 * pw + 146, 421, pw - 160, 48);
    loopEndBox.setBounds (x0 + 3 * pw + 114, 421, pw - 124, 48);

    buttonsRow = { x0, 482, w, 64 };
    setStart.setBounds (x0 + 8, 490, 142, 48);
    setEnd.setBounds (x0 + 158, 490, 126, 48);
    setLoop.setBounds (x0 + 292, 490, 134, 48);
    playLoop.setBounds (x0 + 434, 490, 152, 48);
    loopMode.setBounds (x0 + w - 324, 490, 316, 48);

    editPanel = { x0, 552, 760, getHeight() - 552 };
    zoomPanel = { x0 + 768, 552, w - 768, getHeight() - 552 };

    const int bw = 86, bh = 80;
    PixelButton* eb[] = { &cut, &copy, &paste, &fadeIn, &fadeOut, &normalize, &reverseBtn, &mix };
    for (int i = 0; i < 8; ++i)
        eb[i]->setBounds (editPanel.getX() + 12 + i * (bw + 8), editPanel.getY() + 38, bw, bh);

    const int zx = zoomPanel.getX() + 12, zy = zoomPanel.getY() + 38;
    zoomIn.setBounds (zx, zy, 60, 80);
    zoomOut.setBounds (zx + 66, zy, 60, 80);
    fit.setBounds (zx + 132, zy, 60, 80);
    left.setBounds (zx + 198, zy, 60, 66);
    right.setBounds (zx + 264, zy, 60, 66);
}
