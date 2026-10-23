// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "WaveformView.h"

using namespace juce;

WaveformView::WaveformView (bool i) : interactive (i)
{
    if (! interactive) setMouseCursor (MouseCursor::PointingHandCursor);
}

int WaveformView::length() const { return data != nullptr ? data->getLength() : 0; }

void WaveformView::setSample (ft::SampleData::Ptr d, const ft::InstrumentParams* p)
{
    const bool newData = d.get() != data.get();
    data = d;
    hasParams = p != nullptr;
    if (p != nullptr) params = *p;

    if (newData)
    {
        cacheValid = false;
        const int len = length();
        if (! interactive || viewStart + viewLen > len || viewLen <= 1) { viewStart = 0; viewLen = jmax (1, len); }
        cursor = jlimit (0, jmax (0, len - 1), cursor);
        if (selEnd > len) selStart = selEnd = 0;
    }
    repaint();
}

void WaveformView::setCursor (int f)
{
    cursor = jlimit (0, jmax (0, length() - 1), f);
    repaint();
    if (onCursorChanged) onCursorChanged();
}

void WaveformView::setSelection (int from, int to)
{
    if (from > to) std::swap (from, to);
    selStart = jlimit (0, length(), from);
    selEnd = jlimit (0, length(), to + 1);
    repaint();
}

float WaveformView::frameToX (double f) const
{
    return (float) ((f - viewStart) / viewLen * getWidth());
}

double WaveformView::xToFrame (float x) const
{
    return viewStart + (double) x / jmax (1, getWidth()) * viewLen;
}

void WaveformView::clampView()
{
    const int len = jmax (1, length());
    viewLen = jlimit (jmin (64.0, (double) len), (double) len, viewLen);
    viewStart = jlimit (0.0, jmax (0.0, len - viewLen), viewStart);
    cacheValid = false;
    repaint();
}

void WaveformView::zoomIn()
{
    const double centre = hasSelection() ? (selStart + selEnd) * 0.5 : (double) cursor;
    viewLen *= 0.5;
    viewStart = centre - viewLen * 0.5;
    clampView();
}

void WaveformView::zoomOut()
{
    const double centre = viewStart + viewLen * 0.5;
    viewLen *= 2.0;
    viewStart = centre - viewLen * 0.5;
    clampView();
}

void WaveformView::zoomFit()
{
    viewStart = 0;
    viewLen = jmax (1, length());
    clampView();
}

void WaveformView::scroll (int direction)
{
    viewStart += direction * viewLen * 0.25;
    clampView();
}

void WaveformView::rebuildCache()
{
    const int w = jmax (1, getWidth());
    cacheChannels = data != nullptr ? jmax (1, data->getNumChannels()) : 1;
    cache.assign ((size_t) (w * cacheChannels), { 0.0f, 0.0f });
    cacheData = data.get();
    cacheValid = true;
    if (data == nullptr || length() == 0) return;

    const int len = length();
    for (int x = 0; x < w; ++x)
    {
        int a = (int) std::floor (xToFrame ((float) x));
        int b = (int) std::floor (xToFrame ((float) x + 1.0f));
        a = jlimit (0, len - 1, a);
        b = jlimit (a + 1, len, b);
        for (int c = 0; c < cacheChannels; ++c)
        {
            auto range = FloatVectorOperations::findMinAndMax (data->buffer.getReadPointer (c, a), b - a);
            cache[(size_t) (c * w + x)] = { range.getStart(), range.getEnd() };
        }
    }
}

void WaveformView::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::screen);
    g.fillRect (r);

    if (! cacheValid || cacheData != data.get() || (int) cache.size() != getWidth() * cacheChannels
        || (data != nullptr && data->getNumChannels() != cacheChannels))
        rebuildCache();

    auto wave = r;
    if (interactive) wave.removeFromTop (kMarkerStrip * 0.4f);

    if (interactive)
    {
        g.setColour (Theme::screenFaint);
        for (int i = 1; i < 8; ++i) g.fillRect (r.getX() + r.getWidth() * i / 8.0f, r.getY(), 1.0f, r.getHeight());
        for (int i = 1; i < 4; ++i) g.fillRect (r.getX(), wave.getY() + wave.getHeight() * i / 4.0f, r.getWidth(), 1.0f);
    }

    const int len = length();

    // loop region
    if (len > 0 && hasParams && params.loop && showMarkers)
    {
        const float x1 = frameToX (params.loopStart), x2 = frameToX (params.loopEnd + 1);
        g.setColour (Colour (0x26ffffff));
        g.fillRect (Rectangle<float> (x1, r.getY(), x2 - x1, r.getHeight()));
        g.setColour (Theme::screenDim);
        g.fillRect (x1, r.getY(), 1.5f, r.getHeight());
        g.fillRect (x2 - 1.5f, r.getY(), 1.5f, r.getHeight());

        if (interactive)
        {
            auto bar = Rectangle<float> (x1, r.getY(), x2 - x1, kMarkerStrip);
            g.setColour (Colour (0xff2b2d32));
            g.fillRect (bar);
            if (bar.getWidth() > 120.0f)
            {
                Theme::iconArrow (g, Rectangle<float> (12.0f, 18.0f).withCentre ({ bar.getX() + 16.0f, bar.getCentreY() }), Theme::screenText, false);
                Theme::iconArrow (g, Rectangle<float> (12.0f, 18.0f).withCentre ({ bar.getRight() - 16.0f, bar.getCentreY() }), Theme::screenText, true);
            }
            Theme::text (g, params.pingPong ? "PING PONG" : (params.loopBackward ? "LOOP <" : "LOOP"),
                         bar, 15.0f, Theme::screenText, Justification::centred);
        }
    }

    // selection
    if (interactive && hasSelection())
    {
        const float x1 = frameToX (selStart), x2 = frameToX (selEnd);
        g.setColour (Colour (0x55ffffff));
        g.fillRect (Rectangle<float> (x1, wave.getY(), jmax (1.0f, x2 - x1), wave.getHeight()));
    }

    // waveform: one lane per channel (stereo samples show L on top, R below)
    if (len > 0)
    {
        const int nch = jmax (1, data->getNumChannels());
        const int w = jmax (1, getWidth());
        const bool dense = viewLen / w > 1.5;
        const float laneH = wave.getHeight() / (float) nch;

        for (int c = 0; c < nch; ++c)
        {
            const auto lane = Rectangle<float> (wave.getX(), wave.getY() + c * laneH, wave.getWidth(), laneH);
            const float mid = lane.getCentreY(), half = lane.getHeight() * 0.46f;

            if (c > 0)
            {
                g.setColour (Theme::screenDim);
                g.fillRect (r.getX(), lane.getY() - 1.0f, r.getWidth(), 2.0f);
            }
            g.setColour (Theme::screenText);
            g.fillRect (r.getX(), mid - 0.75f, r.getWidth(), nch > 1 ? 1.0f : 1.5f);

            if (dense)
            {
                for (int x = 0; x < w && (size_t) (c * w + x) < cache.size(); ++x)
                {
                    const auto [mn, mx] = cache[(size_t) (c * w + x)];
                    const float y1 = mid - mx * half, y2 = mid - mn * half;
                    g.fillRect ((float) x, y1, 1.0f, jmax (1.0f, y2 - y1));
                }
            }
            else
            {
                Path p;
                const float* d = data->buffer.getReadPointer (c);
                const int a = jmax (0, (int) viewStart - 1), b = jmin (len, (int) (viewStart + viewLen) + 2);
                for (int f = a; f < b; ++f)
                {
                    const float x = frameToX (f), y = mid - d[f] * half;
                    if (f == a) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, PathStrokeType (nch > 1 ? jmax (1.0f, lineThickness * 0.8f) : lineThickness));
            }

            if (nch == 2 && lane.getHeight() > 40.0f)
                Theme::text (g, c == 0 ? "L" : "R", { lane.getX() + 8.0f, lane.getBottom() - 24.0f, 22.0f, 18.0f },
                             15.0f, Theme::screenDim, Justification::centredLeft);
        }
    }

    // start / end markers
    if (len > 0 && hasParams && showMarkers && interactive)
    {
        auto marker = [&] (int frame, const String& label, bool right)
        {
            const float x = frameToX (frame + (right ? 1 : 0));
            g.setColour (Theme::screenText);
            g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
            float bx = right ? x + 2.0f : x;
            if (bx + 30.0f > r.getRight()) bx = x - 32.0f;
            auto box = Rectangle<float> (bx, r.getY() + 2.0f, 30.0f, 30.0f);
            g.setColour (Theme::buttonLight);
            g.fillRect (box);
            Theme::text (g, label, box, 16.0f, Theme::ink, Justification::centred);
        };
        marker (params.start, "S", false);
        marker (params.end, "E", true);

        // dim the parts outside start..end
        g.setColour (Colour (0x99000000));
        g.fillRect (Rectangle<float> (0.0f, wave.getY(), jmax (0.0f, frameToX (params.start)), wave.getHeight()));
        const float ex = frameToX (params.end + 1);
        g.fillRect (Rectangle<float> (ex, wave.getY(), jmax (0.0f, (float) getWidth() - ex), wave.getHeight()));
    }

    // cursor and playhead
    if (interactive && len > 0)
    {
        g.setColour (Colour (0xffb8c4ff));
        g.fillRect (frameToX (cursor) - 0.5f, wave.getY(), 1.5f, wave.getHeight());
    }
    if (playhead >= 0 && len > 0)
    {
        g.setColour (Colours::white);
        g.fillRect (frameToX (playhead) - 1.0f, r.getY(), 2.0f, r.getHeight());
    }

    if (len == 0)
        Theme::text (g, interactive ? "NO SAMPLE - LOAD ONE ON THE INSTRUMENTS PAGE" : "EMPTY", r, 16.0f, Theme::screenDim, Justification::centred);

    // keyboard shortcuts (Ctrl+A/C/X/V) are active: thin white frame
    if (keysActive)
    {
        g.setColour (Theme::screenText);
        g.drawRect (r, 2.0f);
    }
}

WaveformView::Drag WaveformView::hitTestMarker (Point<float> p) const
{
    if (! hasParams || length() == 0) return Drag::Select;
    const float tol = 8.0f;

    if (p.y <= kMarkerStrip)
    {
        const float sx = frameToX (params.start), ex = frameToX (params.end + 1);
        if (p.x >= sx - 2.0f && p.x <= sx + 32.0f) return Drag::Start;
        if (p.x >= ex - 2.0f && p.x <= ex + 32.0f) return Drag::End;
        if (params.loop)
        {
            const float lx = frameToX (params.loopStart), rx = frameToX (params.loopEnd + 1);
            if (std::abs (p.x - lx) < tol + 20.0f && p.x < (lx + rx) * 0.5f) return Drag::LoopStart;
            if (std::abs (p.x - rx) < tol + 20.0f && p.x > (lx + rx) * 0.5f) return Drag::LoopEnd;
            if (p.x > lx && p.x < rx) return Drag::LoopMove;
        }
    }
    else if (params.loop)
    {
        if (std::abs (p.x - frameToX (params.loopStart)) < tol)   return Drag::LoopStart;
        if (std::abs (p.x - frameToX (params.loopEnd + 1)) < tol) return Drag::LoopEnd;
    }
    return Drag::Select;
}

void WaveformView::mouseMove (const MouseEvent& e)
{
    if (! interactive) return;
    switch (hitTestMarker (e.position))
    {
        case Drag::Start: case Drag::End: case Drag::LoopStart: case Drag::LoopEnd:
            setMouseCursor (MouseCursor::LeftRightResizeCursor); break;
        case Drag::LoopMove: setMouseCursor (MouseCursor::DraggingHandCursor); break;
        case Drag::None: case Drag::Select: default: setMouseCursor (MouseCursor::IBeamCursor); break;
    }
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    if (! interactive) { if (onClicked) onClicked(); return; }
    if (onActivated) onActivated();
    if (length() == 0) return;

    drag = hitTestMarker (e.position);
    const int f = jlimit (0, length() - 1, (int) std::round (xToFrame (e.position.x)));

    if (drag == Drag::Select)
    {
        if (e.mods.isShiftDown())
        {
            selAnchor = cursor;
            setSelection (selAnchor, f);
        }
        else
        {
            selAnchor = f;
            selStart = selEnd = 0;
        }
        setCursor (f);
    }
    else if (drag == Drag::LoopMove)
    {
        dragLoopOffset = f - params.loopStart;
    }
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    if (! interactive || length() == 0) return;
    const int len = length();
    const int f = jlimit (0, len - 1, (int) std::round (xToFrame (e.position.x)));
    auto p = params;

    switch (drag)
    {
        case Drag::Select:
            setSelection (selAnchor, f);
            setCursor (f);
            return;
        case Drag::Start:     p.start = jmin (f, p.end); break;
        case Drag::End:       p.end = jmax (f - 1, p.start); break;
        case Drag::LoopStart: p.loopStart = jlimit (p.start, p.loopEnd, f); break;
        case Drag::LoopEnd:   p.loopEnd = jlimit (p.loopStart, p.end, f - 1); break;
        case Drag::LoopMove:
        {
            const int width = p.loopEnd - p.loopStart;
            p.loopStart = jlimit (p.start, p.end - width, f - dragLoopOffset);
            p.loopEnd = p.loopStart + width;
            break;
        }
        case Drag::None:
        default: return;
    }

    params = p;
    repaint();
    if (onPointsChanged) onPointsChanged (p);
}

void WaveformView::mouseUp (const MouseEvent&)
{
    drag = Drag::None;
}

void WaveformView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (! interactive || length() == 0) return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double anchor = xToFrame (e.position.x);
        const double factor = w.deltaY > 0 ? 0.8 : 1.25;
        viewStart = anchor - (anchor - viewStart) * factor;
        viewLen *= factor;
    }
    else
    {
        viewStart -= (w.deltaY + w.deltaX) * viewLen * 0.5;
    }
    clampView();
}
