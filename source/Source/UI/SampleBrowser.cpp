// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "SampleBrowser.h"
#include "../PluginProcessor.h"

using namespace juce;
using namespace ft;

File& SampleBrowser::lastSampleFolder()
{
    static File folder;
    return folder;
}

BrowserLookAndFeel::BrowserLookAndFeel()
    : LookAndFeel_V4 (LookAndFeel_V4::ColourScheme {
          Colour (0xffc4c7cf), Colour (0xff040405), Colour (0xffe4e6eb), Colour (0xff878b94), Colour (0xff0c0d0f),
          Colour (0xffe4e6eb), Colour (0xff0c0d0f), Colour (0xffe4e6eb), Colour (0xff0c0d0f) })
{
    setColour (DirectoryContentsDisplayComponent::textColourId, Theme::screenText);
    setColour (DirectoryContentsDisplayComponent::highlightColourId, Theme::buttonLight);
    setColour (DirectoryContentsDisplayComponent::highlightedTextColourId, Theme::ink);
    setColour (ListBox::backgroundColourId, Theme::screen);
    setColour (ListBox::textColourId, Theme::screenText);
    setColour (TextEditor::backgroundColourId, Colour (0xffe4e6eb));
    setColour (TextEditor::textColourId, Theme::ink);
    setColour (ComboBox::backgroundColourId, Colour (0xffe4e6eb));
    setColour (ComboBox::textColourId, Theme::ink);
    setColour (ComboBox::arrowColourId, Theme::ink);
    setColour (Label::textColourId, Theme::ink);
    setColour (TextButton::buttonColourId, Colour (0xffe4e6eb));
    setColour (TextButton::textColourOffId, Theme::ink);
    setColour (ScrollBar::thumbColourId, Theme::screenDim);
}

void BrowserLookAndFeel::drawFileBrowserRow (Graphics& g, int width, int height, const File&, const String& filename,
                                             Image*, const String& fileSizeDescription, const String& fileTimeDescription,
                                             bool isDirectory, bool isItemSelected, int, DirectoryContentsDisplayComponent&)
{
    const auto bg  = isItemSelected ? Theme::buttonLight : Theme::screen;
    const auto ink = isItemSelected ? Theme::ink : Theme::screenText;
    const auto dim = isItemSelected ? Theme::ink.withAlpha (0.6f) : Theme::screenDim;
    g.fillAll (bg);

    const float h = (float) height, th = jmin (14.0f, h * 0.5f);
    auto r = Rectangle<float> (0.0f, 0.0f, (float) width, h).reduced (6.0f, 0.0f);

    auto iconArea = r.removeFromLeft (h * 0.9f);
    if (isDirectory) Theme::iconFolder (g, iconArea.reduced (2.0f, h * 0.22f), ink);
    r.removeFromLeft (4.0f);

    if (! isDirectory && width > 420)
    {
        Theme::text (g, fileTimeDescription.toUpperCase(), r.removeFromRight (150.0f), th * 0.8f, dim, Justification::centredRight);
        Theme::text (g, fileSizeDescription.toUpperCase(), r.removeFromRight (100.0f), th * 0.8f, dim, Justification::centredRight);
        r.removeFromRight (10.0f);
    }
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());
    Theme::text (g, filename.toUpperCase(), r, th, ink, Justification::centredLeft, isDirectory);
    g.restoreState();
}

SampleBrowser::SampleBrowser (EditorState& s)
    : st (s),
      filter (s.proc.getSupportedAudioWildcard(), "*", "Samples"),
      wave (false)
{
    browser = std::make_unique<FileBrowserComponent> (FileBrowserComponent::openMode
                                                        | FileBrowserComponent::canSelectFiles
                                                        | FileBrowserComponent::canSelectMultipleItems
                                                        | FileBrowserComponent::filenameBoxIsReadOnly,
                                                      FourTrackerProcessor::getUserFolder(), &filter, nullptr);
    browser->setLookAndFeel (&lnf);
    browser->addListener (this);
    browserHolder.addAndMakeVisible (*browser);
    addAndMakeVisible (browserHolder);

    wave.showMarkers = false;
    wave.onClicked = [this] { if (current.existsAsFile()) showFile (current, true); };
    addAndMakeVisible (wave);

    play.icon = Theme::iconPlay;  play.iconWidth = 40.0f;
    stop.icon = Theme::iconStop;  stop.iconWidth = 40.0f;
    load.icon = Theme::iconFolder; load.iconWidth = 44.0f;
    for (auto* c : std::initializer_list<Component*> { &autoPair, &play, &stop, &load, &cancel })
        addAndMakeVisible (c);

    autoPair.setState (true);
    autoPair.onChange = [this] (bool on) { autoPlay = on; if (! on) st.proc.stopAudition(); focusList(); };
    play.onClick   = [this] { if (current.existsAsFile()) showFile (current, true); focusList(); };
    stop.onClick   = [this] { st.proc.stopAudition(); focusList(); };
    load.onClick   = [this] { loadSelected(); };
    cancel.onClick = [this] { close(); };

    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, true);
}

SampleBrowser::~SampleBrowser()
{
    stopTimer();
    browser->removeListener (this);
    browser->setLookAndFeel (nullptr);
}

void SampleBrowser::open()
{
    auto start = lastSampleFolder();
    if (! start.isDirectory()) start = FourTrackerProcessor::getUserFolder();
    if (! start.isDirectory()) start = File::getSpecialLocation (File::userDocumentsDirectory);

    if (browser->getRoot() != start) browser->setRoot (start);
    else browser->refresh();

    current = File();
    info.clear();
    wave.setSample (nullptr, nullptr);
    setVisible (true);
    toFront (false);
    startTimerHz (30);
    focusList();
    repaint();
}

void SampleBrowser::close()
{
    st.proc.stopAudition();
    stopTimer();
    setVisible (false);
    if (onClosed) onClosed();
}

void SampleBrowser::focusList()
{
    // the file list is a ListBox somewhere inside the browser
    std::function<Component* (Component&)> find = [&] (Component& c) -> Component*
    {
        if (dynamic_cast<ListBox*> (&c) != nullptr) return &c;
        for (auto* child : c.getChildren())
            if (auto* r = find (*child)) return r;
        return nullptr;
    };
    if (auto* list = find (*browser)) list->grabKeyboardFocus();
    else grabKeyboardFocus();
}

// ---------------------------------------------------------------------------

void SampleBrowser::selectionChanged()
{
    const auto f = browser->getHighlightedFile();
    if (f.existsAsFile() && f != current)
        showFile (f, autoPlay);
}

void SampleBrowser::fileClicked (const File& f, const MouseEvent&)
{
    // clicking the same file again plays it again
    if (f.existsAsFile() && f == current && autoPlay)
        showFile (f, true);
}

void SampleBrowser::fileDoubleClicked (const File& f)
{
    if (f.existsAsFile()) loadSelected();
}

void SampleBrowser::showFile (const File& f, bool playIt)
{
    current = f;
    info.clear();
    if (st.proc.auditionFile (f, playIt))
    {
        const auto& in = st.proc.getAuditionSample();
        wave.setSample (in.data, &in.params);
        const auto& d = *in.data;
        const double secs = d.getLength() / jmax (1.0, d.sampleRate);
        info.add (f.getFileName().toUpperCase());
        info.add (String (secs, secs < 10.0 ? 2 : 1) + " SEC  " + String (d.getLength()) + " FRAMES");
        info.add (String (roundToInt (d.sampleRate)) + " HZ  " + String (d.bitDepth) + " BIT  "
                  + (d.getNumChannels() > 1 ? "STEREO" : "MONO"));
    }
    else
    {
        wave.setSample (nullptr, nullptr);
        info.add (f.getFileName().toUpperCase());
        info.add ("NOT A SAMPLE THIS PROGRAM CAN READ");
    }
    repaint();
}

void SampleBrowser::loadSelected()
{
    Array<File> files;
    for (int i = 0; i < browser->getNumSelectedFiles(); ++i)
    {
        const auto f = browser->getSelectedFile (i);
        if (f.existsAsFile()) files.add (f);
    }
    if (files.isEmpty() && current.existsAsFile()) files.add (current);
    if (files.isEmpty()) return;

    lastSampleFolder() = files.getFirst().getParentDirectory();
    st.proc.stopAudition();

    int slot = st.proc.selectedInstrument;
    StringArray errors;
    for (auto& f : files)
    {
        if (slot >= kNumInstruments) break;
        auto err = st.proc.loadInstrument (slot, f);
        if (err.isNotEmpty()) errors.add (err); else ++slot;
    }
    if (errors.isEmpty())
        st.setStatus (files.size() == 1 ? "LOADED " + files.getFirst().getFileName().toUpperCase() + " INTO " + hex2 (st.proc.selectedInstrument)
                                        : "LOADED " + String (files.size()) + " FILES");
    else
        st.setStatus (errors[0].toUpperCase());

    close();
    st.changed();
}

void SampleBrowser::timerCallback()
{
    wave.setPlayhead (st.engine().previewPosition.load());
}

bool SampleBrowser::keyPressed (const KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == KeyPress::escapeKey) { close(); return true; }
    if (code == KeyPress::returnKey) { loadSelected(); return true; }
    if (code == KeyPress::spaceKey)
    {
        if (st.engine().previewPosition.load() >= 0) st.proc.stopAudition();
        else if (current.existsAsFile()) showFile (current, true);
        return true;
    }
    if (code == KeyPress::backspaceKey)
    {
        if (browser->getRoot().getParentDirectory() != browser->getRoot())
            browser->goUp();
        return true;
    }
    return true;   // the browser is modal: don't let keys reach the tracker behind it
}

// ---------------------------------------------------------------------------

void SampleBrowser::paint (Graphics& g)
{
    g.fillAll (Theme::background.withAlpha (0.6f));
    auto r = getLocalBounds().toFloat();
    Theme::drawPanel (g, r);

    Theme::text (g, "LOAD SAMPLE INTO SLOT " + hex2 (st.proc.selectedInstrument), { 22.0f, 14.0f, 900.0f, 34.0f },
                 30.0f, Theme::ink, Justification::centredLeft, true);
    Theme::text (g, "CLICK OR USE THE ARROW KEYS TO HEAR A SAMPLE   ENTER = LOAD   SPACE = PLAY/STOP   ESC = CANCEL",
                 { 22.0f, (float) getHeight() - 40.0f, 1200.0f, 26.0f }, 15.0f, Theme::ink.withAlpha (0.75f));

    Theme::drawScreen (g, waveFrame);
    Theme::drawScreen (g, infoFrame);
    auto ir = infoFrame.reduced (16.0f, 12.0f);
    for (int i = 0; i < info.size(); ++i)
        Theme::text (g, info[i], ir.removeFromTop (34.0f), i == 0 ? 20.0f : 17.0f,
                     i == 0 ? Theme::screenText : Theme::screenDim);
    if (info.isEmpty())
        Theme::text (g, "NO FILE SELECTED", ir.removeFromTop (34.0f), 17.0f, Theme::screenDim);

    Theme::text (g, "AUTO PLAY", { autoPair.getX() - 0.0f, autoPair.getY() - 32.0f, 300.0f, 28.0f }, 18.0f, Theme::ink, Justification::centredLeft, true);
}

void SampleBrowser::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (46);
    r.removeFromBottom (36);

    auto right = r.removeFromRight (520);
    r.removeFromRight (16);

    listFrame = r.toFloat();
    browserHolder.setBounds (r);
    browser->setTransform (AffineTransform::scale (kBrowserScale));
    browser->setBounds (0, 0, roundToInt (r.getWidth() / kBrowserScale), roundToInt (r.getHeight() / kBrowserScale));

    waveFrame = right.removeFromTop (220).toFloat();
    wave.setBounds (waveFrame.reduced (8.0f).toNearestInt());
    right.removeFromTop (12);
    infoFrame = right.removeFromTop (128).toFloat();
    right.removeFromTop (46);

    auto autoRow = right.removeFromTop (60);
    autoPair.setBounds (autoRow.removeFromLeft (250));

    right.removeFromTop (16);
    auto pr = right.removeFromTop (70);
    play.setBounds (pr.removeFromLeft ((pr.getWidth() - 12) / 2));
    pr.removeFromLeft (12);
    stop.setBounds (pr);

    auto br = right.removeFromBottom (86);
    load.setBounds (br.removeFromLeft ((br.getWidth() - 12) / 2));
    br.removeFromLeft (12);
    cancel.setBounds (br);
}
