// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "FxPage.h"

#if FOURTRACKER_FX_HOST

using namespace juce;

namespace
{
    constexpr int kEmptyId = 1, kFirstEffectId = 2, kMissingId = 100000;

    bool sameEffect (const PluginDescription& a, const PluginDescription& b)
    {
        return a.fileOrIdentifier == b.fileOrIdentifier && a.uniqueId == b.uniqueId && a.pluginFormatName == b.pluginFormatName;
    }
}

namespace fxui
{
    void fillBox (ComboBox& box, FxHost& fx, const Array<PluginDescription>& effects, int c, int i, bool showFormat)
    {
        box.clear (dontSendNotification);
        box.addItem ("--- EMPTY ---", kEmptyId);
        for (int e = 0; e < effects.size(); ++e)
            box.addItem (showFormat ? effects[e].name + "   (" + effects[e].pluginFormatName + ")" : effects[e].name, kFirstEffectId + e);

        int selected = kEmptyId;
        if (fx.hasPlugin (c, i))
        {
            const auto d = fx.getSlotDescription (c, i);
            selected = kMissingId;
            for (int e = 0; e < effects.size(); ++e)
                if (sameEffect (effects[e], d)) { selected = kFirstEffectId + e; break; }
            if (selected == kMissingId) box.addItem (d.name, kMissingId);
        }
        box.setSelectedId (selected, dontSendNotification);
    }

    String applyChoice (FxHost& fx, const Array<PluginDescription>& effects, int c, int i, int id)
    {
        const String slotName = "CH" + String (c + 1) + " FX" + String (i + 1);
        if (id == kEmptyId || id == 0)
        {
            fx.clearSlot (c, i);
            return slotName + " CLEARED";
        }
        if (id >= kFirstEffectId && id - kFirstEffectId < effects.size())
        {
            const auto& d = effects[id - kFirstEffectId];
            const auto err = fx.loadSlot (c, i, d);
            return err.isEmpty() ? slotName + ": " + d.name.toUpperCase()
                                 : "COULD NOT LOAD " + d.name.toUpperCase() + ": " + err.toUpperCase();
        }
        return {};
    }
}

FxPage::FxPage (EditorState& s, FxHost& host) : st (s), fx (host)
{
    for (int c = 0; c < ft::kNumChannels; ++c)
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            auto u = std::make_unique<SlotUI>();
            u->box.setLookAndFeel (&lnf);
            u->box.setWantsKeyboardFocus (false);
            u->box.setMouseClickGrabsKeyboardFocus (false);
            u->edit.setWantsKeyboardFocus (false);
            u->edit.setMouseClickGrabsKeyboardFocus (false);
            u->bypass.setWantsKeyboardFocus (false);
            u->bypass.setMouseClickGrabsKeyboardFocus (false);
            u->box.setTextWhenNothingSelected ("--- EMPTY ---");
            u->box.onChange = [this, c, i] { chosen (c, i); };
            u->edit.onClick = [this, c, i] { fx.showEditor (c, i); };
            u->bypass.onClick = [this, c, i]
            {
                fx.setBypass (c, i, ! fx.isBypassed (c, i));
                updateButtons();
            };
            addAndMakeVisible (u->box);
            addAndMakeVisible (u->edit);
            addAndMakeVisible (u->bypass);
            ui[(size_t) c][(size_t) i] = std::move (u);
        }

    for (auto* b : { &addFolder, &removeFolder, &scan, &closeBtn }) addAndMakeVisible (b);
    addFolder.icon = Theme::iconFolder; addFolder.iconWidth = 44.0f;

    addFolder.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Choose a folder with VST effects", File(), String(), true);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& f)
        {
            const auto dir = f.getResult();
            if (! dir.isDirectory()) return;
            fx.getSearchPath().addIfNotAlreadyThere (dir);
            fx.saveSettings();
            message = "FOLDER ADDED - PRESS SCAN";
            repaint();
        });
    };
    removeFolder.onClick = [this]
    {
        auto& p = fx.getSearchPath();
        if (p.getNumPaths() > 0) p.remove (p.getNumPaths() - 1);
        fx.saveSettings();
        repaint();
    };
    scan.onClick = [this] { message = {}; fx.startScan(); };
    message = {};
    closeBtn.onClick = [this] { close(); };

    fx.addListener (this);
    setWantsKeyboardFocus (true);
    refillLists();
}

void FxPage::fxChanged()
{
    refillLists();
    updateButtons();
    repaint();
}

FxPage::~FxPage()
{
    fx.removeListener (this);
    for (auto& chan : ui)
        for (auto& u : chan)
            u->box.setLookAndFeel (nullptr);
}

void FxPage::open()
{
    refillLists();
    updateButtons();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void FxPage::close()
{
    setVisible (false);
    if (onClosed) onClosed();
}

void FxPage::refillLists()
{
    effects = fx.getEffects();
    const ScopedValueSetter<bool> svs (refilling, true);
    for (int c = 0; c < ft::kNumChannels; ++c)
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
            fxui::fillBox (ui[(size_t) c][(size_t) i]->box, fx, effects, c, i, true);
}

void FxPage::chosen (int c, int i)
{
    if (refilling) return;
    message = fxui::applyChoice (fx, effects, c, i, ui[(size_t) c][(size_t) i]->box.getSelectedId());
    refillLists();
    updateButtons();
    repaint();
}

void FxPage::updateButtons()
{
    for (int c = 0; c < ft::kNumChannels; ++c)
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            auto& u = *ui[(size_t) c][(size_t) i];
            const bool has = fx.hasPlugin (c, i);
            u.edit.setEnabled (has);
            u.bypass.setEnabled (has);
            u.edit.setAlpha (has ? 1.0f : 0.4f);
            u.bypass.setAlpha (has ? 1.0f : 0.4f);
            // lit = the effect is ON
            u.bypass.setButtonText (has && fx.isBypassed (c, i) ? "BYPASS" : "ON");
            u.bypass.setToggleState (has && ! fx.isBypassed (c, i), dontSendNotification);
        }
    scan.setEnabled (! fx.isScanning());
    scan.setButtonText (fx.isScanning() ? "SCANNING" : "SCAN");
}

bool FxPage::keyPressed (const KeyPress& k)
{
    if (k.getKeyCode() == KeyPress::escapeKey || k.getKeyCode() == KeyPress::F8Key) { close(); return true; }
    return true;   // keep keys from reaching the tracker behind this page
}

void FxPage::paint (Graphics& g)
{
    Theme::drawPanel (g, getLocalBounds().toFloat());
    Theme::text (g, "VST EFFECTS", { 22.0f, 14.0f, 600.0f, 34.0f }, 30.0f, Theme::ink, Justification::centredLeft, true);
    Theme::text (g, "TWO EFFECTS PER CHANNEL - LOADS " + fx.getFormatNames().toUpperCase() + " - "
                    + String ((int) sizeof (void*) * 8) + "-BIT", { 420.0f, 18.0f, 1060.0f, 28.0f }, 16.0f,
                 Theme::ink.withAlpha (0.7f), Justification::centredLeft);

    // folders
    Theme::text (g, "PLUGIN FOLDERS (SCANNED WITH SUB-FOLDERS)", { (float) folderBox.getX(), (float) folderBox.getY() - 30.0f, 900.0f, 26.0f },
                 17.0f, Theme::ink, Justification::centredLeft, true);
    Theme::drawScreen (g, folderBox.toFloat());
    {
        auto r = folderBox.toFloat().reduced (14.0f, 10.0f);
        const auto& p = fx.getSearchPath();
        if (p.getNumPaths() == 0)
            Theme::text (g, "NO FOLDERS - PRESS ADD FOLDER", r.removeFromTop (28.0f), 16.0f, Theme::screenDim);
        for (int i = 0; i < p.getNumPaths() && r.getHeight() >= 26.0f; ++i)
        {
            const bool last = i == 3 && p.getNumPaths() > 4;
            Theme::text (g, last ? "... AND " + String (p.getNumPaths() - 3) + " MORE" : p.getRawString (i).toUpperCase(),
                         r.removeFromTop (28.0f), 16.0f, Theme::screenText);
            if (last) break;
        }
    }

    // status
    Theme::drawScreen (g, statusBox.toFloat());
    {
        auto r = statusBox.toFloat().reduced (14.0f, 0.0f);
        String txt;
        if (fx.isScanning())
        {
            const auto bar = r.removeFromRight (260.0f).reduced (0.0f, 12.0f);
            g.setColour (Theme::screenFaint); g.fillRect (bar);
            g.setColour (Theme::screenText);  g.fillRect (bar.withWidth (bar.getWidth() * fx.getScanProgress()));
            txt = "SCANNING " + fx.getScanningName().toUpperCase();
        }
        else if (message.isNotEmpty())           txt = message;
        else if (fx.getScanReport().isNotEmpty()) txt = fx.getScanReport();
        else                                      txt = String (effects.size()) + " EFFECTS FOUND";
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        Theme::text (g, txt, r, 16.0f, Theme::screenText, Justification::centredLeft);
        g.restoreState();
    }

    // channel rows
    const int rowH = rowsArea.getHeight() / ft::kVisibleChannels;
    for (int v = 0; v < ft::kVisibleChannels; ++v)
    {
        const int c = firstCh + v;
        if (c >= usedCh) break;
        auto row = Rectangle<int> (rowsArea.getX(), rowsArea.getY() + v * rowH, rowsArea.getWidth(), rowH - 10);
        Theme::drawRaised (g, row.toFloat(), Theme::panel.brighter (0.06f));
        Theme::text (g, "CH" + String (c + 1), row.toFloat().withWidth (110.0f).withTrimmedLeft (18.0f), 28.0f, Theme::ink,
                     Justification::centredLeft, true);
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            const auto& b = ui[(size_t) c][(size_t) i]->box.getBounds();
            Theme::text (g, "FX" + String (i + 1), { (float) b.getX() - 54.0f, (float) b.getY(), 50.0f, (float) b.getHeight() },
                         17.0f, Theme::ink, Justification::centredLeft);
        }
    }

    Theme::text (g, "A 64-BIT FOUR TRACKER LOADS 64-BIT PLUGINS ONLY. EFFECTS AND THEIR SETTINGS ARE REMEMBERED.   F8 / ESC = CLOSE",
                 { 22.0f, (float) getHeight() - 44.0f, 1200.0f, 26.0f }, 15.0f, Theme::ink.withAlpha (0.75f));
}

void FxPage::setView (int first, int used)
{
    if (first == firstCh && used == usedCh) return;
    firstCh = first; usedCh = used;
    resized();
    repaint();
}

void FxPage::resized()
{
    folderBox = { 22, 92, 1000, 150 };
    const int bx = folderBox.getRight() + 18, bw = getWidth() - 22 - bx;
    addFolder.setBounds (bx, folderBox.getY(), bw, 46);
    removeFolder.setBounds (bx, folderBox.getY() + 52, bw, 46);
    scan.setBounds (bx, folderBox.getY() + 104, bw, 46);
    statusBox = { 22, folderBox.getBottom() + 12, getWidth() - 44, 48 };

    rowsArea = { 22, statusBox.getBottom() + 18, getWidth() - 44, getHeight() - statusBox.getBottom() - 18 - 140 };
    // the 4 channels in view (the same ones as on the tracks page)
    const int rowH = rowsArea.getHeight() / ft::kVisibleChannels;
    const int groupW = (rowsArea.getWidth() - 120) / FxHost::kSlotsPerChannel;
    for (int c = 0; c < ft::kNumChannels; ++c)
    {
        const int v = c - firstCh;
        const bool shown = v >= 0 && v < ft::kVisibleChannels && c < usedCh;
        const int y = rowsArea.getY() + v * rowH + (rowH - 10 - 52) / 2;
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            auto& u = *ui[(size_t) c][(size_t) i];
            for (Component* comp : { (Component*) &u.box, (Component*) &u.edit, (Component*) &u.bypass }) comp->setVisible (shown);
            if (! shown) continue;
            const int x = rowsArea.getX() + 120 + i * groupW + 54;
            const int w = groupW - 54 - 20;
            u.box.setBounds (x, y, w - 2 * 112, 52);
            u.edit.setBounds (x + w - 2 * 112 + 8, y, 100, 52);
            u.bypass.setBounds (x + w - 112 + 8, y, 100, 52);
        }
    }
    closeBtn.setBounds (getWidth() - 22 - 220, getHeight() - 122, 220, 66);
}

// ===========================================================================
// FxBar: the two effect lists under each channel strip on the tracks page
// ===========================================================================

FxBar::FxBar (FxHost& host) : fx (host)
{
    for (int c = 0; c < ft::kNumChannels; ++c)
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            auto& b = boxes[(size_t) c][(size_t) i];
            b = std::make_unique<ComboBox>();
            b->setLookAndFeel (&lnf);
            b->setTextWhenNothingSelected ("FX" + String (i + 1));
            b->setTooltip ("CH" + String (c + 1) + " effect " + String (i + 1));
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);      // Enter must keep starting / stopping the pattern
            b->onChange = [this, c, i]
            {
                if (refilling) return;
                fxui::applyChoice (fx, effects, c, i, boxes[(size_t) c][(size_t) i]->getSelectedId());
                refill();
                done();
            };
            addAndMakeVisible (*b);

            auto& e = edits[(size_t) c][(size_t) i];
            e = std::make_unique<PixelButton> ("E", PixelButton::Style::Raised, 15.0f);
            e->setWantsKeyboardFocus (false);
            e->setMouseClickGrabsKeyboardFocus (false);
            e->onClick = [this, c, i] { fx.showEditor (c, i); };
            addAndMakeVisible (*e);

            // ON (lit) / OFF (dark) = bypass
            auto& by = bypasses[(size_t) c][(size_t) i];
            by = std::make_unique<PixelButton> ("ON", PixelButton::Style::Segment, 13.0f);
            by->setWantsKeyboardFocus (false);
            by->setMouseClickGrabsKeyboardFocus (false);
            by->onClick = [this, c, i] { fx.setBypass (c, i, ! fx.isBypassed (c, i)); refill(); done(); };
            addAndMakeVisible (*by);
        }
    fx.addListener (this);
    refill();
}

FxBar::~FxBar()
{
    fx.removeListener (this);
    for (auto& chan : boxes)
        for (auto& b : chan)
            b->setLookAndFeel (nullptr);
}

void FxBar::fxChanged()
{
    if (! fx.isScanning()) refill();
}

void FxBar::refill()
{
    effects = fx.getEffects();
    const ScopedValueSetter<bool> svs (refilling, true);
    for (int c = 0; c < ft::kNumChannels; ++c)
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            fxui::fillBox (*boxes[(size_t) c][(size_t) i], fx, effects, c, i, false);
            auto& e = *edits[(size_t) c][(size_t) i];
            auto& by = *bypasses[(size_t) c][(size_t) i];
            const bool has = fx.hasPlugin (c, i), on = has && ! fx.isBypassed (c, i);
            for (auto* b : { &e, &by })
            {
                b->setEnabled (has);
                b->setAlpha (has ? 1.0f : 0.35f);
            }
            by.setButtonText (has && ! on ? "OFF" : "ON");
            by.setToggleState (on, dontSendNotification);
            if (! has) boxes[(size_t) c][(size_t) i]->setSelectedId (0, dontSendNotification);   // shows "FX1" / "FX2"
        }
}

void FxBar::setView (int first, int used)
{
    if (first == firstCh && used == usedCh) return;
    firstCh = first; usedCh = used;
    resized();
    repaint();
}

void FxBar::paint (Graphics& g)
{
    const int gap = 12, w = (getWidth() - gap * 3) / ft::kVisibleChannels;
    for (int v = 0; v < ft::kVisibleChannels && firstCh + v < usedCh; ++v)
        Theme::drawPanel (g, Rectangle<float> ((float) (v * (w + gap)), 0.0f, (float) w, (float) getHeight()));
}

void FxBar::resized()
{
    // same columns as the channel strips above
    const int gap = 12, w = (getWidth() - gap * 3) / ft::kVisibleChannels;
    const int ew = 30, bw = 40, inner = 6;
    for (int c = 0; c < ft::kNumChannels; ++c)
    {
        const int v = c - firstCh;
        const bool shown = v >= 0 && v < ft::kVisibleChannels && c < usedCh;
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
            for (Component* comp : { (Component*) boxes[(size_t) c][(size_t) i].get(), (Component*) edits[(size_t) c][(size_t) i].get(),
                                     (Component*) bypasses[(size_t) c][(size_t) i].get() })
                comp->setVisible (shown);
        if (! shown) continue;
        auto r = Rectangle<int> (v * (w + gap), 0, w, getHeight()).reduced (inner, 5);
        const int half = (r.getWidth() - inner) / 2;
        for (int i = 0; i < FxHost::kSlotsPerChannel; ++i)
        {
            auto cell = r.removeFromLeft (half);
            if (i == 0) r.removeFromLeft (inner);
            bypasses[(size_t) c][(size_t) i]->setBounds (cell.removeFromRight (bw));
            cell.removeFromRight (2);
            edits[(size_t) c][(size_t) i]->setBounds (cell.removeFromRight (ew));
            cell.removeFromRight (2);
            boxes[(size_t) c][(size_t) i]->setBounds (cell);
        }
    }
}

// ---------------------------------------------------------------------------
// RouteBar: chaining one channel into another channel's effects
// ---------------------------------------------------------------------------

RouteBar::RouteBar (FxHost& host) : fx (host)
{
    for (int c = 0; c < ft::kNumChannels; ++c)
    {
        auto& b = boxes[(size_t) c];
        b = std::make_unique<ComboBox>();
        b->setLookAndFeel (&lnf);
        b->setTooltip ("Where CH" + String (c + 1) + " goes after its effects: straight out, or on into another channel's effects");
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onChange = [this, c]
        {
            if (refilling) return;
            const int id = boxes[(size_t) c]->getSelectedId();
            fx.setRoute (c, id >= 2 ? id - 2 : -1);
            refill();
            if (onDone) onDone();
        };
        addChildComponent (*b);
    }
    fx.addListener (this);
    refill();
}

RouteBar::~RouteBar()
{
    fx.removeListener (this);
    for (auto& b : boxes) b->setLookAndFeel (nullptr);
}

void RouteBar::refill()
{
    const ScopedValueSetter<bool> svs (refilling, true);
    for (int c = 0; c < ft::kNumChannels; ++c)
    {
        auto& b = *boxes[(size_t) c];
        b.clear (dontSendNotification);
        b.addItem ("CH" + String (c + 1) + " > OUT", 1);
        for (int t = 0; t < usedCh; ++t)
            if (t != c) b.addItem ("CH" + String (c + 1) + " > INTO CH" + String (t + 1) + " FX", t + 2);
        const int r = fx.getRoute (c);
        b.setSelectedId (r >= 0 && r < usedCh ? r + 2 : 1, dontSendNotification);
    }
    repaint();
}

void RouteBar::setView (int first, int used)
{
    if (first == firstCh && used == usedCh) return;
    const bool countChanged = used != usedCh;
    firstCh = first; usedCh = used;
    if (countChanged) refill();
    resized();
    repaint();
}

void RouteBar::paint (Graphics& g)
{
    // a small arrow on the left of each list that is chained into another channel
    for (int v = 0; v < ft::kVisibleChannels && firstCh + v < usedCh; ++v)
    {
        auto col = columnBounds != nullptr ? columnBounds (v) : Rectangle<int>();
        Theme::drawPanel (g, col.toFloat());
        if (fx.getRoute (firstCh + v) >= 0)
            { g.setColour (Theme::ink); g.fillRect (col.reduced (4).removeFromLeft (4)); }
    }
}

void RouteBar::resized()
{
    for (int c = 0; c < ft::kNumChannels; ++c)
    {
        const int v = c - firstCh;
        const bool shown = v >= 0 && v < ft::kVisibleChannels && c < usedCh && columnBounds != nullptr;
        boxes[(size_t) c]->setVisible (shown);
        if (shown) boxes[(size_t) c]->setBounds (columnBounds (v).reduced (10, 4));
    }
}

#endif
