// SPDX-License-Identifier: AGPL-3.0-or-later
// Graphic EQ - tests (run on Linux / CI)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include "EqEngine.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

static int failures = 0;
static void CHECK (bool ok, const String& what)
{
    std::cout << (ok ? "ok:   " : "FAIL: ") << what << "\n";
    if (! ok) ++failures;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File outDir = argc > 1 ? File (argv[1]) : File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    constexpr int block = 512;

    // the level of a sine at hz through the engine, in dB relative to the input
    auto gainAt = [&] (const std::array<float, eq::kBands>& knobs, double hz, double sr = 48000.0, float outDb = 0.0f)
    {
        eq::Engine e; e.prepare (sr, block);
        std::vector<float> L ((size_t) block), R ((size_t) block);
        double eIn = 0, eOut = 0;
        const int total = (int) (sr * 0.6);
        for (int i = 0; i < total; i += block)
        {
            for (int k = 0; k < block; ++k) L[(size_t) k] = R[(size_t) k] = 0.25f * (float) std::sin (2.0 * MathConstants<double>::pi * hz * (i + k) / sr);
            e.setKnobs (knobs, outDb);
            std::vector<float> dry = L;
            e.process (L.data(), R.data(), block);
            if (i > total / 2)
                for (int k = 0; k < block; ++k) { eIn += (double) dry[(size_t) k] * dry[(size_t) k]; eOut += (double) L[(size_t) k] * L[(size_t) k]; }
        }
        return 10.0 * std::log10 (eOut / eIn);
    };

    {
        eq::Engine e; e.prepare (48000.0, block);
        std::array<float, eq::kBands> flat {};
        std::vector<float> L ((size_t) block), R ((size_t) block);
        bool same = true;
        for (int b = 0; b < 20; ++b)
        {
            for (int k = 0; k < block; ++k) L[(size_t) k] = R[(size_t) k] = (float) std::sin ((b * block + k) * 0.01);
            auto dry = L;
            e.setKnobs (flat, 0.0f);
            e.process (L.data(), R.data(), block);
            same = same && L == dry && R == dry;
        }
        CHECK (same, "all knobs at 0: the sound passes through untouched");
    }

    for (int b = 0; b < eq::kBands; ++b)
    {
        std::array<float, eq::kBands> k {};
        k[(size_t) b] = 12.0f;
        const double up = gainAt (k, eq::bandHz (b));
        k[(size_t) b] = -12.0f;
        const double down = gainAt (k, eq::bandHz (b));
        CHECK (std::abs (up - 12.0) < 0.5 && std::abs (down + 12.0) < 0.5,
               String (eq::bandLabel (b)) + " Hz: +12 gives " + String (up, 2) + " dB, -12 gives " + String (down, 2) + " dB");
    }

    {
        // neighbours don't add up: 600, 800, 1000 all at +6 is +6 there, not +10
        std::array<float, eq::kBands> k {};
        k[3] = k[4] = k[5] = 6.0f;
        const double at800 = gainAt (k, 800.0);
        CHECK (std::abs (at800 - 6.0) < 0.5, "600 + 800 + 1000 at +6: 800 Hz is " + String (at800, 2) + " dB (the sound follows the curve)");
        // a zig-zag: every band's own frequency is where its knob is
        std::array<float, eq::kBands> z { 6, -6, 6, -6, 6, -6, 6, -6, 6, -6 };
        double worst = 0;
        for (int b = 0; b < eq::kBands; ++b) worst = jmax (worst, std::abs (gainAt (z, eq::bandHz (b)) - z[(size_t) b]));
        CHECK (worst < 1.0, "zig-zag +6 / -6: every band within " + String (worst, 2) + " dB of its knob");
        // and at 44.1 kHz too, with a curve from a preset
        std::array<float, eq::kBands> smile { 6, 4, 1, -1, -2, -2, 0, 2, 4, 5 };
        worst = 0;
        for (int b = 0; b < eq::kBands; ++b) worst = jmax (worst, std::abs (gainAt (smile, eq::bandHz (b), 44100.0) - smile[(size_t) b]));
        CHECK (worst < 0.5, "SMILE at 44.1 kHz: every band within " + String (worst, 2) + " dB");
    }

    {
        std::array<float, eq::kBands> k {};
        const double o = gainAt (k, 1000.0, 48000.0, -6.0f);
        CHECK (std::abs (o + 6.0) < 0.1, "OUT -6 dB turns it down 6 dB (" + String (o, 2) + ")");
    }

    {
        // moving a knob glides: no jump in the output
        eq::Engine e; e.prepare (48000.0, block);
        std::array<float, eq::kBands> k {};
        std::vector<float> L ((size_t) block), R ((size_t) block);
        float maxStep = 0.0f, last = 0.0f;
        for (int b = 0; b < 60; ++b)
        {
            if (b == 30) k[0] = 12.0f;
            for (int i = 0; i < block; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (2.0 * MathConstants<double>::pi * 60.0 * (b * block + i) / 48000.0);
            e.setKnobs (k, 0.0f);
            e.process (L.data(), R.data(), block);
            for (int i = 0; i < block; ++i) { if (b > 0) maxStep = jmax (maxStep, std::abs (L[(size_t) i] - last)); last = L[(size_t) i]; }
        }
        CHECK (maxStep < 0.06f, "a knob going from 0 to +12 at once glides (biggest step " + String (maxStep, 4) + ")");
    }

    // ---- the plugin ------------------------------------------------------------------
    {
        EqProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, block);
        p.prepareToPlay (48000.0, block);
        CHECK (p.getNumPrograms() >= 14, String (p.getNumPrograms()) + " presets");
        int idx = -1;
        for (int i = 0; i < p.getNumPrograms(); ++i) if (p.getProgramName (i) == "SMILE (LOUDNESS)") idx = i;
        p.setCurrentProgram (idx);
        CHECK (p.readKnobs()[0] == 6.0f && p.readKnobs()[5] == -2.0f, "preset SMILE sets the knobs");

        MemoryBlock state;
        p.getStateInformation (state);
        EqProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        CHECK (p2.readKnobs() == p.readKnobs(), "the curve is saved with the song");
        p.flat();
        CHECK (p.readKnobs() == std::array<float, eq::kBands> {}, "FLAT puts every band at 0");

        // the window: drag a knob, draw over several bars, double-click back to 0
        p.setCurrentProgram (idx);
        std::unique_ptr<AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<EqEditor*> (base.get());
        CHECK (ed != nullptr && ed->getWidth() == 1120 && ed->getHeight() == 462, "editor opens at 1120 x 462");
        auto mouse = [&] (float x, float y, int clicks = 1, ModifierKeys mods = {})
        {
            return MouseEvent (Desktop::getInstance().getMainMouseSource(), { x, y }, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                               ed, ed, Time::getCurrentTime(), { x, y }, Time::getCurrentTime(), clicks, false);
        };
        ed->mouseDown (mouse (ed->barX (6), ed->yOf (9.0f)));
        ed->mouseUp (mouse (ed->barX (6), ed->yOf (9.0f)));
        CHECK (std::abs (p.readKnobs()[6] - 9.0f) < 0.11f, "clicking on the 3000 bar at +9 sets it to +9 (" + String (p.readKnobs()[6], 1) + ")");
        ed->mouseDown (mouse (ed->barX (1), ed->yOf (-4.0f)));
        for (int b = 1; b <= 4; ++b) ed->mouseDrag (mouse (ed->barX (b), ed->yOf (-4.0f)));
        ed->mouseUp (mouse (ed->barX (4), ed->yOf (-4.0f)));
        bool drawn = true;
        for (int b = 1; b <= 4; ++b) drawn = drawn && std::abs (p.readKnobs()[(size_t) b] + 4.0f) < 0.11f;
        CHECK (drawn, "dragging sideways draws the curve over 170 - 800 at -4");
        ed->mouseDown (mouse (ed->barX (0), ed->yOf (13.0f)));
        ed->mouseUp (mouse (ed->barX (0), ed->yOf (13.0f)));
        CHECK (p.readKnobs()[0] == 12.0f, "the knob stops at +12");
        ed->mouseDoubleClick (mouse (ed->barX (6), ed->yOf (9.0f), 2));
        CHECK (p.readKnobs()[6] == 0.0f, "double-click puts a band back at 0");

        // the picture: the SMILE curve with a few changes, the mouse on 3000
        p.setCurrentProgram (idx);
        ed->mouseMove (mouse (ed->barX (7), ed->yOf (2.0f)));
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = outDir.getChildFile ("graphic-eq.png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) PNGImageFormat().writeImageToStream (img, *os);
        CHECK (f.existsAsFile(), "editor picture: " + f.getFullPathName());
        base.reset();
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : String (failures) + " FAILED\n");
    return failures == 0 ? 0 : 1;
}
