// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#pragma once

#include "Widgets.h"
#include "../Instrument.h"

/** Draws a sample. In interactive mode (Sample Edit) it also shows and
    drags the start/end/loop markers, a selection and a cursor, and zooms. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (bool interactive = false);

    void setSample (ft::SampleData::Ptr data, const ft::InstrumentParams* params);
    void setPlayhead (int frame) { if (frame != playhead) { playhead = frame; repaint(); } }

    // selection / cursor (interactive)
    int  getCursor() const noexcept { return cursor; }
    void setCursor (int f);
    bool hasSelection() const noexcept { return selEnd > selStart; }
    int  getSelectionStart() const noexcept { return selStart; }
    int  getSelectionEnd()   const noexcept { return selEnd - 1; }   // inclusive
    void clearSelection() { selStart = selEnd = 0; repaint(); }
    void setSelection (int from, int toInclusive);

    void zoomIn();
    void zoomOut();
    void zoomFit();
    void scroll (int direction);

    std::function<void (const ft::InstrumentParams&)> onPointsChanged;
    std::function<void()> onCursorChanged;
    std::function<void()> onClicked;    // non-interactive mode
    bool showMarkers = true;
    float lineThickness = 1.4f;

    void paint (juce::Graphics&) override;
    void resized() override { cacheValid = false; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Start, End, LoopStart, LoopEnd, LoopMove, Select };

    float frameToX (double f) const;
    double xToFrame (float x) const;
    int length() const;
    Drag hitTestMarker (juce::Point<float>) const;
    void clampView();
    void rebuildCache();

    bool interactive;
    ft::SampleData::Ptr data;
    ft::InstrumentParams params;
    bool hasParams = false;

    double viewStart = 0, viewLen = 1;
    int cursor = 0, selStart = 0, selEnd = 0, selAnchor = 0;
    int playhead = -1;
    Drag drag = Drag::None;
    int dragLoopOffset = 0;

    std::vector<std::pair<float, float>> cache;   // min/max per pixel column, per channel ([ch * width + x])
    int cacheChannels = 1;
    bool cacheValid = false;
    const void* cacheData = nullptr;

    static constexpr float kMarkerStrip = 34.0f;
};
