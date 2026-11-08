// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Manager - Copyright (C) 2026 elcrispo76

#include "SongList.h"

namespace pmgr
{

double Song::entrySeconds (int i) const
{
    if (! juce::isPositiveAndBelow (i, (int) entries.size())) return 0.0;
    return juce::jlimit (1, 256, entries[(size_t) i].rows) * rowSeconds();
}

double Song::totalSeconds() const
{
    double t = 0.0;
    for (int i = 0; i < (int) entries.size(); ++i) t += entrySeconds (i);
    return t;
}

juce::var Song::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "Four Tracker Pattern Manager song");
    o->setProperty ("version", 1);
    o->setProperty ("bpm", bpm);
    o->setProperty ("speed", speed);
    o->setProperty ("startKey", startKey);
    o->setProperty ("channel", channel);
    o->setProperty ("loop", loop);
    juce::Array<juce::var> list;
    for (auto& e : entries)
    {
        auto* x = new juce::DynamicObject();
        x->setProperty ("pattern", e.pattern);
        x->setProperty ("rows", e.rows);
        list.add (juce::var (x));
    }
    o->setProperty ("positions", list);
    return juce::var (o);
}

bool Song::fromVar (const juce::var& v)
{
    if (! v.isObject() || ! v.getProperty ("positions", {}).isArray()) return false;
    Song s;
    s.bpm      = juce::jlimit (32, 255, (int) v.getProperty ("bpm", 125));
    s.speed    = juce::jlimit (1, 31, (int) v.getProperty ("speed", 6));
    s.startKey = juce::jlimit (0, 126 - kMaxPatterns, (int) v.getProperty ("startKey", 36));
    s.channel  = juce::jlimit (1, 16, (int) v.getProperty ("channel", 1));
    s.loop     = (bool) v.getProperty ("loop", false);
    s.entries.clear();
    for (auto& x : *v.getProperty ("positions", {}).getArray())
        s.entries.push_back ({ juce::jlimit (0, kMaxPatterns - 1, (int) x.getProperty ("pattern", 0)),
                               juce::jlimit (1, 256, (int) x.getProperty ("rows", 64)) });
    if (s.entries.empty()) s.entries.push_back ({});
    *this = s;
    return true;
}

std::vector<KeyEvent> schedule (const Song& song, int from, int loops)
{
    std::vector<KeyEvent> ev;
    const int n = (int) song.entries.size();
    if (n == 0) return ev;
    from = juce::jlimit (0, n - 1, from);

    // the order positions play in: from `from` to the end, then (LOOP) round again from the top
    std::vector<int> order;
    for (int i = from; i < n; ++i) order.push_back (i);
    for (int k = 1; k < juce::jmax (1, loops); ++k)
        for (int i = 0; i < n; ++i) order.push_back (i);

    ev.push_back ({ 0.0, song.noteFor (song.entries[(size_t) order[0]].pattern) });   // picks the pattern (stopped)
    ev.push_back ({ 0.02, song.startKey });                                             // START
    double t = 0.0;
    for (size_t k = 0; k < order.size(); ++k)
    {
        const double len = song.entrySeconds (order[k]);
        if (k + 1 < order.size())
        {
            const int cur = song.entries[(size_t) order[k]].pattern, nxt = song.entries[(size_t) order[k + 1]].pattern;
            if (nxt != cur) ev.push_back ({ t + len * 0.5, song.noteFor (nxt) });
        }
        t += len;
    }
    if (! song.loop) ev.push_back ({ t, song.startKey, true });   // STOP at the end
    return ev;
}

juce::String midiNoteName (int note)
{
    const char* names[] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
}

} // namespace pmgr
