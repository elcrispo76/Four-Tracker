// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker Pattern Maker - Copyright (C) 2026 elcrispo76

#include "PatternGen.h"

namespace pm
{

namespace
{
    // One bar (16 steps) per lane and style:
    //   X always   x often (75 %)   ? sometimes (35 %)   o ghost often   g ghost sometimes
    //   . only when BUSY is high    - never.   A lane that is all '-' starts switched off.
    struct StyleDef
    {
        const char* name;
        int bpm, swing;
        const char* lanes[kNumLanes];      // Kick, Snare, Clap, ClosedHat, OpenHat, Perc, Bass, Arp
        int chords[4];                     // the chord (scale degree) of each bar
        bool longBass;                     // bass notes ring until the next one
        int arpMode;                       // 0 stabs (chords), 1 up, 2 up and down, 3 bells (8ths, 2 steps long), 4 long chord tones (pads)
    };

    const StyleDef kStyles[] = {
        { "HOUSE", 124, 1,
          { "X---X---X---X---", "--------------?-", "----X-------X---", "x.x.x.x.x.x.x.x.", "--X---X---X---X-", "--?--?-?---?-?--", "--X---X---X--?X-", "X--x--X---x--x--" },
          { 0, 0, 5, 3 }, false, 0 },
        { "TECHNO", 130, 0,
          { "X---X---X---X---", "----------------", "----x-------X---", "xxXxxxXxxxXxxxXx", "--X---X---X---X-", "---x--x----x--x-", "-xxx-xxx-xxx-xxx", "X?x?X?x?X?x?X?x?" },
          { 0, 0, 0, 0 }, false, 1 },
        { "DRUM & BASS", 174, 0,
          { "X---------X--?--", "----X--g-g--X--g", "----------------", "x.x.x.x.x.x.x.x.", "-------------?--", "?-?-?-?-?-?-?-?-", "X-----?---X---?-", "X-x-X-x-X-x-X-x-" },
          { 0, 5, 3, 4 }, true, 3 },
        { "JUNGLE", 165, 0,
          { "X-X-------XX----", "----X-gX-g--X-gg", "----------------", "xxxxxxxxxxxxxxxx", "---------?------", "--?-----?-----?-", "X-------?-------", "X---x---X---x---" },
          { 0, 0, 5, 6 }, true, 3 },
        { "BREAKBEAT", 136, 1,
          { "X-X-------X--?--", "----X--g-g--X---", "----------------", "x-xxx-x-x-x-x-xx", "--------------X-", "?--?--?--?------", "X--x--X---X--x--", "X---X---?---X---" },
          { 0, 3, 0, 4 }, false, 0 },
        { "ELECTRO", 125, 0,
          { "X-----X---X-----", "----------------", "----X-------X---", "xxxxxxxxxxxxxxxx", "-------?-------?", "--?--?----?--?--", "X--X--X---X--X--", "X-xX-xX-xX-xX-x-" },
          { 0, 0, 3, 3 }, false, 1 },
        { "HIP HOP", 90, 2,
          { "X------X-?X-----", "----X-------X---", "----------------", "x-x-x-x-x-x-x-x-", "---------------?", "------?-------?-", "X------X--X-----", "X-------X-------" },
          { 0, 5, 3, 4 }, true, 0 },
        { "TRANCE", 138, 0,
          { "X---X---X---X---", "----------------", "----X-------X---", "xxxxxxxxxxxxxxxx", "--X---X---X---X-", "----?-------?---", "-XXX-XXX-XXX-XXX", "XxXxXxXxXxXxXxXx" },
          { 0, 5, 2, 6 }, false, 2 },
        { "ITALO DISCO", 120, 0,
          { "X---X---X---X---", "----X-------X---", "----X-------X---", "x-x-x-x-x-x-x-x-", "--X---X---X---X-", "?---?---?---?---", "X-X-X-X-X-X-X-X-", "XxXxXxXxXxXxXxXx" },
          { 0, 5, 2, 6 }, false, 1 },
        { "AMBIENT", 90, 0,
          { "X---------?-----", "----------------", "----------------", "...?....?...?...", "----------------", "--?-----?-----?-", "X---------------", "X-------?-------" },
          { 0, 5, 3, 4 }, true, 4 },
    };

    const StyleDef& def (Style s) { return kStyles[juce::jlimit (0, (int) Style::NumStyles - 1, (int) s)]; }

    int degreeToSemis (Scale sc, int degree)
    {
        const auto& st = scaleSteps (sc);
        const int n = (int) st.size();
        const int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
        const int d = degree - oct * n;
        return st[(size_t) d] + 12 * oct;
    }

    /** Makes the steps of one lane. */
    void makeLane (Lane& lane, const Settings& s, int laneIndex, juce::Random& rng)
    {
        const auto& d = def (s.style);
        const char* tpl = d.lanes[laneIndex];
        const int steps = juce::jlimit (16, kMaxSteps, s.bars * 16);
        const float busy = (float) juce::jlimit (0, 100, s.busy) / 100.0f;
        lane.steps = {};

        auto hitChance = [&] (char c) -> std::pair<float, int>
        {
            switch (c)
            {
                case 'X': return { 1.0f, 64 };
                case 'x': return { 0.55f + 0.4f * busy, 64 };
                case '?': return { 0.15f + 0.4f * busy, 64 };
                case 'o': return { 0.4f + 0.5f * busy, 26 };
                case 'g': return { 0.1f + 0.45f * busy, 24 };
                case '.': return { juce::jmax (0.0f, busy - 0.45f) * 1.2f, 36 };
                default:  return { 0.0f, 64 };
            }
        };

        const bool hat = lane.kind == LaneKind::ClosedHat;
        for (int bar = 0; bar < steps / 16; ++bar)
            for (int i = 0; i < 16; ++i)
            {
                const int st = bar * 16 + i;
                auto [chance, vel] = hitChance (tpl[i]);
                if (bar > 0 && tpl[i] != 'X' && tpl[i] != '-')   // later bars vary a little
                    chance = juce::jlimit (0.0f, 1.0f, chance + (rng.nextFloat() - 0.5f) * 0.3f);
                if (chance <= 0.0f || rng.nextFloat() >= chance) continue;
                auto& stp = lane.steps[(size_t) st];
                stp.on = true;
                stp.vel = vel;
                if (hat && vel == 64 && (i % 4) != 2 && d.swing == 0 && tpl[i] == 'x') stp.vel = 44;   // the offbeat hat is the loud one
            }

        // the fill in the last bar
        if (s.fill && (lane.kind == LaneKind::Snare || lane.kind == LaneKind::Clap || lane.kind == LaneKind::Kick || lane.kind == LaneKind::OpenHat))
        {
            const int last = steps - 16;
            auto used = [] (const char* t) { return std::string (t).find_first_not_of ('-') != std::string::npos; };
            const bool rollLane = lane.kind == LaneKind::Snare ? used (tpl) : (lane.kind == LaneKind::Clap && ! used (d.lanes[1]));
            if (rollLane)
            {
                const int vels[] = { 30, 40, 52, 64 };
                const int rollStart = rng.nextBool() ? 12 : 13;
                for (int i = rollStart; i < 16; ++i) { auto& stp = lane.steps[(size_t) (last + i)]; stp.on = true; stp.vel = vels[juce::jmin (3, i - 12)]; }
            }
            if (lane.kind == LaneKind::Kick && rng.nextFloat() < 0.6f)
                for (int i = 13; i < 16; ++i) lane.steps[(size_t) (last + i)].on = false;   // room for the roll
            if (lane.kind == LaneKind::OpenHat) lane.steps[(size_t) (last + 14)] = { true, 64, 0, 1 };
        }

        if (! isMelodic (lane.kind)) return;

        // notes: the bar's chord root for the bass (some fifths and octaves), chord tones going round for the arp
        int arpIndex = 0;
        for (int st = 0; st < steps; ++st)
        {
            auto& stp = lane.steps[(size_t) st];
            if (! stp.on) continue;
            const int chord = d.chords[(st / 16) % 4];
            if (lane.kind == LaneKind::Bass)
            {
                int deg = chord;
                const float r = rng.nextFloat();
                const bool rolling = s.style == Style::Techno || s.style == Style::Trance;
                const bool octave = s.style == Style::ItaloDisco;       // italo: root on the beat, an octave up between
                if (! rolling && ! octave && r < 0.18f) deg = chord + 4;            // the fifth
                int semis = degreeToSemis (s.scale, deg);
                if (octave) { if (st % 4 == 2) semis += 12; }
                else if (rolling ? (st % 4 == 3 && r < 0.25f) : (r > 0.85f)) semis += 12;   // an octave up now and then
                stp.note = semis;
            }
            else
            {
                const int tones[] = { 0, 2, 4, 7 };
                int idx = 0;
                switch (d.arpMode)
                {
                    case 1:  idx = arpIndex % 4; break;
                    case 2:  { const int ud[] = { 0, 1, 2, 3, 2, 1 }; idx = ud[arpIndex % 6]; } break;
                    case 3:  idx = (arpIndex * 2) % 4; break;
                    case 4:  idx = arpIndex % 3; break;
                    default: idx = 0; break;
                }
                stp.note = degreeToSemis (s.scale, chord + tones[idx]) + (d.arpMode == 1 && arpIndex % 8 >= 4 ? 12 : 0);
                ++arpIndex;
            }
        }

        // how long each note sounds
        for (int st = 0; st < steps; ++st)
        {
            auto& stp = lane.steps[(size_t) st];
            if (! stp.on) continue;
            int next = st + 1;
            while (next < steps && ! lane.steps[(size_t) next].on) ++next;
            const int gap = next - st;
            if (lane.kind == LaneKind::Bass) stp.len = d.longBass ? gap : juce::jmin (gap, 2);
            else                             stp.len = d.arpMode == 4 ? gap : (d.arpMode == 3 ? juce::jmin (gap, 2) : 1);
        }
    }

    const LaneKind kKinds[kNumLanes] = { LaneKind::Kick, LaneKind::Snare, LaneKind::Clap, LaneKind::ClosedHat,
                                         LaneKind::OpenHat, LaneKind::Perc, LaneKind::Bass, LaneKind::Arp };
}

const std::vector<int>& scaleSteps (Scale s)
{
    static const std::vector<int> minor { 0, 2, 3, 5, 7, 8, 10 }, major { 0, 2, 4, 5, 7, 9, 11 }, dorian { 0, 2, 3, 5, 7, 9, 10 },
                                  phrygian { 0, 1, 3, 5, 7, 8, 10 }, harm { 0, 2, 3, 5, 7, 8, 11 }, pent { 0, 3, 5, 7, 10 };
    switch (s)
    {
        case Scale::Major:           return major;
        case Scale::Dorian:          return dorian;
        case Scale::Phrygian:        return phrygian;
        case Scale::HarmonicMinor:   return harm;
        case Scale::MinorPentatonic: return pent;
        default:                     return minor;
    }
}

juce::String styleName (Style s)  { return def (s).name; }
int defaultBpm (Style s)          { return def (s).bpm; }
int defaultSwing (Style s)        { return def (s).swing; }

juce::String scaleName (Scale s)
{
    const char* n[] = { "MINOR", "MAJOR", "DORIAN", "PHRYGIAN", "HARMONIC MINOR", "MINOR PENTATONIC" };
    return n[juce::jlimit (0, (int) Scale::NumScales - 1, (int) s)];
}

juce::String keyName (int key)
{
    const char* n[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n[((key % 12) + 12) % 12];
}

juce::String laneName (LaneKind k)
{
    const char* n[] = { "KICK", "SNARE", "CLAP", "CLOSED HAT", "OPEN HAT", "PERC", "BASS", "ARP / KEYS" };
    return n[(int) k];
}

bool isMelodic (LaneKind k) { return k == LaneKind::Bass || k == LaneKind::Arp; }

Pattern generate (const Settings& s)
{
    Pattern p;
    p.settings = s;
    juce::Random rng (s.seed);
    const auto& d = def (s.style);
    for (int i = 0; i < kNumLanes; ++i)
    {
        auto& lane = p.lanes[(size_t) i];
        lane.kind = kKinds[i];
        lane.on = std::string (d.lanes[i]).find_first_not_of ('-') != std::string::npos;
        makeLane (lane, s, i, rng);
    }
    return p;
}

void regenerateLane (Pattern& p, int lane, juce::int64 seed)
{
    if (! juce::isPositiveAndBelow (lane, kNumLanes)) return;
    juce::Random rng (seed);
    auto& l = p.lanes[(size_t) lane];
    l.kind = kKinds[lane];
    makeLane (l, p.settings, lane, rng);
}

void copyFirstBar (Pattern& p, int lane)
{
    auto& l = p.lanes[(size_t) lane];
    for (int st = 16; st < p.steps(); ++st) l.steps[(size_t) st] = l.steps[(size_t) (st % 16)];
}

std::vector<CellOut> toCells (const Pattern& p, const std::array<int, kNumLanes>& slots)
{
    std::vector<CellOut> out;
    const int steps = p.steps();
    const int key = p.settings.key > 6 ? p.settings.key - 12 : p.settings.key;   // stay close to the sample's own pitch
    int channel = 0;
    for (int li = 0; li < kNumLanes; ++li)
    {
        const auto& lane = p.lanes[(size_t) li];
        if (! lane.on) continue;
        bool any = false;
        for (int st = 0; st < steps; ++st)
        {
            const auto& stp = lane.steps[(size_t) st];
            if (! stp.on) continue;
            any = true;
            ft::Cell c;
            const int note = isMelodic (lane.kind) ? ft::kNativeNote + key + stp.note : ft::kNativeNote;
            c.note = (uint8_t) juce::jlimit (1, 96, note);
            c.inst = (uint8_t) slots[(size_t) li];
            if (p.settings.swing > 0 && (st % 2) == 1) { c.fx = 0x0E; c.param = (uint8_t) (0xD0 | juce::jlimit (1, 5, p.settings.swing)); }
            else if (stp.vel < 64)                     { c.fx = 0x0C; c.param = (uint8_t) juce::jlimit (0, 64, stp.vel); }
            out.push_back ({ st, channel, c });
            if (isMelodic (lane.kind))
            {
                const int offRow = st + juce::jmax (1, stp.len);
                if (offRow < steps && ! lane.steps[(size_t) offRow].on)
                {
                    ft::Cell off; off.note = ft::kNoteOff;
                    out.push_back ({ offRow, channel, off });
                }
            }
        }
        juce::ignoreUnused (any);
        ++channel;
    }
    return out;
}

} // namespace pm
