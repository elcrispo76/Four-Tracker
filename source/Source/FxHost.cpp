// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "FxHost.h"

#if FOURTRACKER_FX_HOST

using namespace juce;

namespace
{
    /** Prepares a plugin for a stereo channel and sizes its scratch buffer. */
    void preparePlugin (AudioPluginInstance& p, AudioBuffer<float>& scratch, int& ins, int& outs, double sr, int block)
    {
        // ask for stereo in / stereo out on the main buses when the plugin allows it
        if (p.getBusCount (true) > 0 && p.getBusCount (false) > 0)
        {
            auto layout = p.getBusesLayout();
            layout.getChannelSet (true, 0) = AudioChannelSet::stereo();
            layout.getChannelSet (false, 0) = AudioChannelSet::stereo();
            if (p.checkBusesLayoutSupported (layout))
                p.setBusesLayout (layout);
        }
        p.setRateAndBufferSizeDetails (sr, block);
        ins = p.getTotalNumInputChannels();
        outs = p.getTotalNumOutputChannels();
        scratch.setSize (jmax (2, ins, outs), block, false, true, false);
        p.prepareToPlay (sr, block);
    }

    /** The CPU a Windows .dll / .vst3 file is built for: 32 = x86, 64 = x64, 0 = not a Windows binary. */
    int binaryBits (const File& f)
    {
        FileInputStream in (f);
        if (! in.openedOk() || in.readShort() != 0x5a4d) return 0;           // "MZ"
        in.setPosition (0x3c);
        const int peOffset = in.readInt();
        if (peOffset <= 0 || peOffset > 4096) return 0;
        in.setPosition (peOffset);
        if (in.readInt() != 0x00004550) return 0;                            // "PE\0\0"
        const int machine = (unsigned short) in.readShort();
        return machine == 0x14c ? 32 : (machine == 0x8664 || machine == 0xaa64) ? 64 : 0;
    }

    File deadMansPedal() { return File::getSpecialLocation (File::userApplicationDataDirectory)
                                       .getChildFile ("Four Tracker").getChildFile ("fx-scan-in-progress.txt"); }
}

/** A plugin's own editor in a separate window. */
struct FxHost::EditorWindow : public DocumentWindow
{
    EditorWindow (const String& title, AudioProcessorEditor* editor, std::function<void()> closed)
        : DocumentWindow (title, Colour (0xff1a1b1e), DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // delete the window after this call has returned
        MessageManager::callAsync ([cb = onClosed] { if (cb) cb(); });
    }

    std::function<void()> onClosed;
};

File FxHost::settingsFolder()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Application Support/Four Tracker");
   #elif JUCE_WINDOWS && ! JUCE_64BIT
    // the 32-bit program keeps its own plugin list and effects (it loads 32-bit plugins only)
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Four Tracker 32-bit");
   #else
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Four Tracker");
   #endif
}

FxHost::FxHost()
{
    formatManager.addDefaultFormats();     // VST3, and VST2 when built with the VST2 SDK
    clearRoutes();
    loadSettings();
    startTimer (40);
}

FxHost::~FxHost()
{
    stopTimer();
    settingsFolder().getChildFile ("fx-restore-in-progress.txt").deleteFile();   // a normal exit: nothing crashed
    closeAllEditors();
    const ScopedLock l (audioLock);
    for (auto& chan : slots)
        for (auto& s : chan)
            if (s.plugin != nullptr) { s.plugin->releaseResources(); s.plugin.reset(); }
}

// ---------------------------------------------------------------------------
// audio
// ---------------------------------------------------------------------------

void FxHost::prepare (double sr, int block)
{
    const ScopedLock l (audioLock);
    sampleRate = sr;
    blockSize = jmax (32, block);
    for (auto& chan : slots)
        for (auto& s : chan)
            if (s.plugin != nullptr)
            {
                s.plugin->releaseResources();
                preparePlugin (*s.plugin, s.scratch, s.ins, s.outs, sampleRate, blockSize);
            }
}

void FxHost::process (int channel, float* left, float* right, int n) noexcept
{
    const ScopedTryLock l (audioLock);
    if (! l.isLocked()) return;              // a plugin is being swapped: play dry for one block

    for (auto& s : slots[(size_t) channel])
    {
        if (s.plugin == nullptr || s.bypass.load() || s.outs <= 0 || n > s.scratch.getNumSamples())
            continue;

        const int nch = s.scratch.getNumChannels();
        for (int c = 0; c < nch; ++c) FloatVectorOperations::clear (s.scratch.getWritePointer (c), n);
        if (s.ins >= 2)
        {
            FloatVectorOperations::copy (s.scratch.getWritePointer (0), left, n);
            FloatVectorOperations::copy (s.scratch.getWritePointer (1), right, n);
        }
        else if (s.ins == 1)
        {
            auto* d = s.scratch.getWritePointer (0);
            for (int i = 0; i < n; ++i) d[i] = 0.5f * (left[i] + right[i]);
        }

        AudioBuffer<float> view (s.scratch.getArrayOfWritePointers(), nch, n);
        emptyMidi.clear();
        s.plugin->processBlock (view, emptyMidi);

        FloatVectorOperations::copy (left, s.scratch.getReadPointer (0), n);
        FloatVectorOperations::copy (right, s.scratch.getReadPointer (s.outs >= 2 ? 1 : 0), n);
    }
}

// ---------------------------------------------------------------------------
// slots
// ---------------------------------------------------------------------------

String FxHost::loadSlot (int channel, int index, const PluginDescription& desc)
{
    if (! isPositiveAndBelow (channel, kNumFxChannels) || ! isPositiveAndBelow (index, kSlotsPerChannel))
        return "No such slot";

    const double sr = sampleRate > 0 ? sampleRate : 44100.0;
    String error;
    auto inst = formatManager.createPluginInstance (desc, sr, blockSize, error);
    if (inst == nullptr)
        return error.isNotEmpty() ? error : String ("Could not load ") + desc.name;

    AudioBuffer<float> scratch;
    int ins = 2, outs = 2;
    preparePlugin (*inst, scratch, ins, outs, sr, blockSize);
    inst->setPlayHead (&head);

    windows[(size_t) channel][(size_t) index].reset();
    auto& s = slots[(size_t) channel][(size_t) index];
    {
        const ScopedLock l (audioLock);
        std::swap (s.plugin, inst);           // the old plugin ends up in inst
        s.scratch = std::move (scratch);
        s.ins = ins;
        s.outs = outs;
        s.desc = desc;
        s.bypass = false;
    }
    if (inst != nullptr) inst->releaseResources();   // old plugin is deleted here, outside the lock
    if (! restoring) { saveSettings(); notify(); }
    return {};
}

void FxHost::clearSlot (int channel, int index)
{
    windows[(size_t) channel][(size_t) index].reset();
    std::unique_ptr<AudioPluginInstance> old;
    auto& s = slots[(size_t) channel][(size_t) index];
    {
        const ScopedLock l (audioLock);
        std::swap (s.plugin, old);
        s.desc = {};
        s.bypass = false;
    }
    if (old != nullptr) old->releaseResources();
    saveSettings();
    notify();
}

void FxHost::setRoute (int channel, int target)
{
    if (channel < 0 || channel >= ft::kNumChannels) return;
    if (target == channel || target < -1 || target >= ft::kNumChannels) target = -1;
    // no loops: following the chain from the target must not come back to this channel
    for (int t = target, n = 0; t >= 0 && n <= ft::kNumChannels; t = route[(size_t) t].load(), ++n)
        if (t == channel) { target = -1; break; }
    route[(size_t) channel] = target;
    saveSettings();
    notify();
}

void FxHost::clearRoutes()
{
    for (auto& r : route) r = -1;
}

void FxHost::clearAll()
{
    clearRoutes();
    for (int c = 0; c < kNumFxChannels; ++c)
        for (int i = 0; i < kSlotsPerChannel; ++i)
            if (hasPlugin (c, i)) clearSlot (c, i);
    pendingSlots.reset();
    saveSettings();
    notify();
}

bool FxHost::hasPlugin (int channel, int index) const
{
    return slots[(size_t) channel][(size_t) index].plugin != nullptr;
}

String FxHost::getSlotName (int channel, int index) const
{
    const auto& s = slots[(size_t) channel][(size_t) index];
    return s.plugin != nullptr ? s.desc.name : String();
}

PluginDescription FxHost::getSlotDescription (int channel, int index) const
{
    return slots[(size_t) channel][(size_t) index].desc;
}

void FxHost::setBypass (int channel, int index, bool b)
{
    slots[(size_t) channel][(size_t) index].bypass = b;
    saveSettings();
    notify();
}

bool FxHost::isBypassed (int channel, int index) const
{
    return slots[(size_t) channel][(size_t) index].bypass.load();
}

void FxHost::showEditor (int channel, int index)
{
    auto& w = windows[(size_t) channel][(size_t) index];
    if (w != nullptr) { w->toFront (true); return; }

    auto& s = slots[(size_t) channel][(size_t) index];
    if (s.plugin == nullptr) return;

    AudioProcessorEditor* ed = s.plugin->hasEditor() ? s.plugin->createEditorIfNeeded() : nullptr;
    if (ed == nullptr) ed = new GenericAudioProcessorEditor (*s.plugin);

    const String title = channelName (channel) + " FX" + String (index + 1) + " - " + s.desc.name;
    w = std::make_unique<EditorWindow> (title, ed, [this, channel, index] { windows[(size_t) channel][(size_t) index].reset(); });
}

void FxHost::closeAllEditors()
{
    for (auto& chan : windows)
        for (auto& w : chan)
            w.reset();
}

// ---------------------------------------------------------------------------
// scanning
// ---------------------------------------------------------------------------

void FxHost::startScan()
{
    if (scanning) return;
    settingsFolder().createDirectory();
    scanners.clear();
    for (auto* format : formatManager.getFormats())
        scanners.push_back (std::make_unique<PluginDirectoryScanner> (known, *format, searchPath, true, deadMansPedal(), false));
    scannerIndex = 0;
    scanProgress = 0.0f;
    failedFiles.clear();
    scanReport = {};
    scanning = ! scanners.empty();
    notify();
}

String FxHost::getFormatNames() const
{
    StringArray names;
    for (auto* f : formatManager.getFormats())
        names.add (f->getName() == "VST" ? String ("VST2") : f->getName() == "AudioUnit" ? String ("AU") : f->getName());
    return names.joinIntoString (", ");
}

void FxHost::makeScanReport()
{
    const int myBits = (int) sizeof (void*) * 8;
    bool hasVst2 = false;
    for (auto* f : formatManager.getFormats()) hasVst2 = hasVst2 || f->getName() == "VST";

    // look at every plugin file in the folders that did not turn into a plugin
    StringArray knownFiles;
    for (auto& d : known.getTypes()) knownFiles.add (d.fileOrIdentifier);

    int wrongBits = 0, dllsWithoutVst2 = 0;
    for (int i = 0; i < searchPath.getNumPaths(); ++i)
        for (const auto& e : RangedDirectoryIterator (searchPath[i], true, "*.dll;*.vst3", File::findFiles))
        {
            const auto& f = e.getFile();
            if (knownFiles.contains (f.getFullPathName(), true)) continue;
            if (f.getParentDirectory().getParentDirectory().getFileName().equalsIgnoreCase ("Contents"))
                if (knownFiles.contains (f.getParentDirectory().getParentDirectory().getParentDirectory().getFullPathName(), true))
                    continue;   // the binary inside a known .vst3 bundle
            const int bits = binaryBits (f);
            if (bits != 0 && bits != myBits) ++wrongBits;
            else if (! hasVst2 && f.hasFileExtension ("dll") && bits == myBits) ++dllsWithoutVst2;
        }

    StringArray parts;
    parts.add (String (getEffects().size()) + " EFFECTS FOUND");
    if (wrongBits > 0)
        parts.add (String (wrongBits) + " ARE " + String (myBits == 64 ? 32 : 64) + "-BIT: USE "
                   + String (myBits == 64 ? 32 : 64) + "-BIT FOUR TRACKER");
    if (dllsWithoutVst2 > 0)
        parts.add (String (dllsWithoutVst2) + " .DLL SKIPPED: NO VST2 IN THIS BUILD");
    if (failedFiles.size() > 0)
        parts.add (String (failedFiles.size()) + " FAILED");
    scanReport = parts.joinIntoString (" - ");

    // a full list in a text file next to the settings, for the curious
    String log;
    log << "Four Tracker FX scan, " << Time::getCurrentTime().toString (true, true) << "\n"
        << "Formats: " << getFormatNames() << ", program is " << myBits << "-bit\n\n"
        << scanReport << "\n\nCould not be opened:\n" << failedFiles.joinIntoString ("\n") << "\n";
    settingsFolder().getChildFile ("fx-scan-report.txt").replaceWithText (log);
}

Array<PluginDescription> FxHost::getEffects() const
{
    Array<PluginDescription> out;
    for (auto& d : known.getTypes())
        if (! d.isInstrument)
            out.add (d);
    std::sort (out.begin(), out.end(), [] (const PluginDescription& a, const PluginDescription& b)
    {
        const int c = a.name.compareIgnoreCase (b.name);
        return c != 0 ? c < 0 : a.pluginFormatName < b.pluginFormatName;
    });
    return out;
}

void FxHost::timerCallback()
{
    if (restoreDoneAt != 0 && Time::getMillisecondCounter() - restoreDoneAt > 5000)
    {
        restoreDoneAt = 0;
        settingsFolder().getChildFile ("fx-restore-in-progress.txt").deleteFile();
    }

    if (pendingSlots != nullptr && sampleRate > 0.0)
        restorePendingSlots();

    if (! scanning) return;

    // scan for up to ~30 ms per tick, one plugin file at a time, so the window stays responsive
    const auto start = Time::getMillisecondCounter();
    while (scannerIndex < scanners.size() && Time::getMillisecondCounter() - start < 30)
    {
        auto& sc = *scanners[scannerIndex];
        String name;
        const bool more = sc.scanNextFile (true, name);
        scanningName = name;
        scanProgress = ((float) scannerIndex + jlimit (0.0f, 1.0f, sc.getProgress())) / (float) scanners.size();
        if (! more)
        {
            failedFiles.addArray (sc.getFailedFiles());
            ++scannerIndex;
        }
    }

    // keep what has been found so far, in case a plugin crashes the program mid-scan
    if (Time::getMillisecondCounter() - lastProgressSave > 2000)
    {
        lastProgressSave = Time::getMillisecondCounter();
        saveSettings();
    }

    if (scannerIndex >= scanners.size())
    {
        scanning = false;
        scanners.clear();
        scanningName = {};
        scanProgress = 1.0f;
        makeScanReport();
        saveSettings();
    }
    notify();
}

// ---------------------------------------------------------------------------
// settings file
// ---------------------------------------------------------------------------

void FxHost::saveSettings()
{
    XmlElement root ("FOURTRACKER_FX");
    root.setAttribute ("paths", searchPath.toString());
    {
        StringArray r;
        for (auto& x : route) r.add (String (x.load()));
        root.setAttribute ("routes", r.joinIntoString (","));
    }
    if (auto k = known.createXml())
        root.createNewChildElement ("KNOWN")->addChildElement (k.release());

    for (int c = 0; c < kNumFxChannels; ++c)
        for (int i = 0; i < kSlotsPerChannel; ++i)
        {
            auto& s = slots[(size_t) c][(size_t) i];
            if (s.plugin == nullptr) continue;
            auto* e = root.createNewChildElement ("SLOT");
            e->setAttribute ("channel", c);
            e->setAttribute ("slot", i);
            e->setAttribute ("bypass", s.bypass.load());
            e->addChildElement (s.desc.createXml().release());
            MemoryBlock state;
            s.plugin->getStateInformation (state);
            e->createNewChildElement ("STATE")->addTextElement (state.toBase64Encoding());
        }

    // a slot still waiting to be restored is kept as it was
    if (pendingSlots != nullptr)
        for (auto* e : pendingSlots->getChildWithTagNameIterator ("SLOT"))
            root.addChildElement (new XmlElement (*e));

    settingsFolder().createDirectory();
    root.writeTo (settingsFolder().getChildFile ("fx-settings.xml"));
}

void FxHost::loadSettings()
{
    auto xml = parseXML (settingsFolder().getChildFile ("fx-settings.xml"));
    if (xml != nullptr)
    {
        searchPath = FileSearchPath (xml->getStringAttribute ("paths"));
        {
            StringArray r; r.addTokens (xml->getStringAttribute ("routes"), ",", "");
            for (int c = 0; c < ft::kNumChannels; ++c)
            {
                const int t = c < r.size() ? r[c].getIntValue() : -1;
                route[(size_t) c] = (t >= 0 && t < ft::kNumChannels && t != c) ? t : -1;
            }
        }
        if (auto* k = xml->getChildByName ("KNOWN"))
            if (auto* list = k->getFirstChildElement())
                known.recreateFromXml (*list);
        // The program closed while effects were being opened at start-up (or just after):
        // leave the one that was loading out, or all of them if they had all loaded.
        const auto restorePedal = settingsFolder().getChildFile ("fx-restore-in-progress.txt");
        StringArray pedal;
        if (restorePedal.existsAsFile()) { pedal.addLines (restorePedal.loadFileAsString()); pedal.removeEmptyStrings(); }
        restorePedal.deleteFile();
        StringArray skip, skippedNames;
        if (pedal.size() > 0)
        {
            if (pedal[pedal.size() - 1].endsWith (" LOADING")) skip.add (pedal[pedal.size() - 1]);
            else skip = pedal;
        }
        // hold Shift while the program starts to open it without any effects
        const bool skipAll = ModifierKeys::getCurrentModifiersRealtime().isShiftDown();

        pendingSlots = std::make_unique<XmlElement> ("SLOTS");
        for (auto* e : xml->getChildWithTagNameIterator ("SLOT"))
        {
            const String key = e->getStringAttribute ("channel") + "/" + e->getStringAttribute ("slot") + " ";
            if (skipAll) continue;
            bool bad = false;
            for (auto& line : skip)
                if (line.startsWith (key)) { bad = true; skippedNames.add (line.fromFirstOccurrenceOf (" ", false, false).upToLastOccurrenceOf (" ", false, false)); }
            if (! bad) pendingSlots->addChildElement (new XmlElement (*e));
        }
        if (skippedNames.size() > 0)
            scanReport = "THE PROGRAM CLOSED AT START WITH " + skippedNames.joinIntoString (", ").toUpperCase() + " - TAKEN OUT OF ITS SLOT";
        if (skipAll) scanReport = "STARTED WITH SHIFT HELD - NO EFFECTS LOADED";
        if (pendingSlots->getNumChildElements() == 0) pendingSlots.reset();
    }

    if (searchPath.getNumPaths() == 0)
        for (auto* format : formatManager.getFormats())
            searchPath.addPath (format->getDefaultLocationsToSearch());
    searchPath.removeRedundantPaths();
    searchPath.removeNonExistentPaths();

    // the program closed in the middle of a scan: a plugin crashed it
    const auto pedal = deadMansPedal();
    if (pedal.existsAsFile())
    {
        StringArray crashed;
        crashed.addLines (pedal.loadFileAsString());
        crashed.removeEmptyStrings();
        if (crashed.size() > 0)
            scanReport = "LAST SCAN STOPPED AT " + File (crashed[0]).getFileName().toUpperCase()
                         + " - IT WILL BE SKIPPED. PRESS SCAN TO CONTINUE";
    }
}

void FxHost::restorePendingSlots()
{
    auto pending = std::move (pendingSlots);
    restoring = true;
    for (auto* e : pending->getChildWithTagNameIterator ("SLOT"))
    {
        const int c = e->getIntAttribute ("channel", -1), i = e->getIntAttribute ("slot", -1);
        if (! isPositiveAndBelow (c, kNumFxChannels) || ! isPositiveAndBelow (i, kSlotsPerChannel)) continue;

        PluginDescription desc;
        bool ok = false;
        for (auto* d : e->getChildIterator())
            if (desc.loadFromXml (*d)) { ok = true; break; }
        if (! ok) continue;

        // a "dead man's pedal": if this plugin crashes the program, the next start skips it
        const auto restorePedal = settingsFolder().getChildFile ("fx-restore-in-progress.txt");
        const String line = String (c) + "/" + String (i) + " " + desc.name;
        restorePedal.appendText (line + " LOADING\n");

        if (loadSlot (c, i, desc).isEmpty())
        {
            auto& s = slots[(size_t) c][(size_t) i];
            if (auto* st = e->getChildByName ("STATE"))
            {
                MemoryBlock mb;
                if (mb.fromBase64Encoding (st->getAllSubText().trim()) && mb.getSize() > 0)
                    s.plugin->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            s.bypass = e->getBoolAttribute ("bypass", false);
        }
        restorePedal.replaceWithText (restorePedal.loadFileAsString().replace (line + " LOADING", line + " OK"));
    }
    restoring = false;
    // keep the pedal a few seconds, so a plugin that crashes once it starts processing is caught too
    restoreDoneAt = Time::getMillisecondCounter();
    saveSettings();     // a slot left out after a crash is gone from the file now
    notify();
}

#endif
