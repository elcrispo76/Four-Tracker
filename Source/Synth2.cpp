// SPDX-License-Identifier: AGPL-3.0-or-later
// Four Tracker - Copyright (C) 2026 elcrispo76

#include "Synth2.h"

namespace ft::synth2
{

namespace
{
    struct Info { const char* name; const char* key; int max; int def; };
    // order = Synth2Param
    const Info info[s2NumParams] = {
        { "OSC 1 WAVE",     "o1w",  3,   0 },
        { "OSC 1 PW",        "o1pw", 127, 0 },
        { "OSC 2 WAVE",     "o2w",  4,   0 },
        { "OSC 2 SEMI",      "o2s",  48,  24 },
        { "OSC 2 FINE",      "o2f",  127, 64 },
        { "OSC MIX",       "mix",  127, 0 },
        { "CUTOFF",    "cut",  127, 110 },
        { "RESO",      "res",  127, 10 },
        { "ENV AMT",   "env",  127, 20 },
        { "F.DECAY",   "fdec", 127, 60 },
        { "LFO SHAPE",     "lsh",  3,   0 },
        { "LFO SYNC",      "lsy",  8,   0 },
        { "LFO RATE",      "lra",  127, 60 },
        { "ATTACK",    "att",  127, 0 },
        { "DECAY",     "dec",  127, 64 },
        { "SUSTAIN",   "sus",  127, 127 },
        { "RELEASE",   "rel",  127, 35 },
        { "LFO PITCH",     "lpi",  127, 0 },
        { "LFO PWM",       "lpw",  127, 0 },
        { "LFO FILTER",    "lfi",  127, 0 },
        { "CHORUS",    "cho",  127, 0 },
        { "DELAY TIME",  "dti",  6,   3 },
        { "FEEDBACK",  "dfb",  127, 40 },
        { "DELAY MIX",   "dmx",  127, 0 },
        { "REVERB SIZE",  "rsz",  127, 64 },
        { "REVERB MIX",   "rmx",  127, 0 },
    };

    const char* osc1Waves[] = { "SAW", "PULSE", "TRI", "SINE" };
    const char* osc2Waves[] = { "SAW", "PULSE", "TRI", "SINE", "NOISE" };
    const char* lfoShapes[] = { "SINE", "TRI", "SAW", "SQUARE" };
    const char* lfoSyncs[]  = { "FREE", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "1 PATT", "2 PATT" };
    const char* delayTimes[] = { "1/16", "1/8", "1/8 D", "1/4", "1/4 D", "1/2", "1 BAR" };

    juce::String hz (double f)
    {
        return f >= 1000.0 ? juce::String (f / 1000.0, 1) + " KHZ" : juce::String (juce::roundToInt (f)) + " HZ";
    }
    juce::String ms (double m)
    {
        return m >= 1000.0 ? juce::String (m / 1000.0, 2) + " S" : juce::String (juce::roundToInt (m)) + " MS";
    }
    juce::String pct (int v) { return juce::String (juce::roundToInt (v * 100.0 / 127.0)) + "%"; }
}

const char* label (int p)
{
    static const char* labels[s2NumParams] = { "WAVE", "PW", "WAVE", "SEMI", "FINE", "MIX", "CUTOFF", "RESO", "ENV AMT", "F.DECAY",
                                               "SHAPE", "SYNC", "RATE", "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "PITCH", "PWM", "FILTER",
                                               "MIX", "TIME", "FEEDBACK", "MIX", "SIZE", "MIX" };
    return juce::isPositiveAndBelow (p, (int) s2NumParams) ? labels[p] : "";
}

const char* name (int p) { return juce::isPositiveAndBelow (p, (int) s2NumParams) ? info[p].name : ""; }
const char* key (int p)  { return juce::isPositiveAndBelow (p, (int) s2NumParams) ? info[p].key : ""; }
int maxValue (int p)     { return juce::isPositiveAndBelow (p, (int) s2NumParams) ? info[p].max : 0; }
int defaultValue (int p) { return juce::isPositiveAndBelow (p, (int) s2NumParams) ? info[p].def : 0; }
int clamp (int p, int v) { return juce::jlimit (0, maxValue (p), v); }

Synth2Values defaults()
{
    Synth2Values v {};
    for (int p = 0; p < s2NumParams; ++p) v[(size_t) p] = info[p].def;
    return v;
}

double envMs (int v)         { const double x = juce::jlimit (0, 127, v) / 127.0; return 5000.0 * x * x * x; }
double filterDecayMs (int v) { const double x = juce::jlimit (0, 127, v) / 127.0; return 5.0 + 4000.0 * x * x * x; }
double cutoffHz (double v)   { return 30.0 * std::pow (2.0, juce::jlimit (0.0, 127.0, v) / 127.0 * 9.4); }
double lfoHz (int v)         { return 0.05 * std::pow (400.0, juce::jlimit (0, 127, v) / 127.0); }   // 0.05 .. 20 Hz

double lfoSyncRows (int c, int patternRows)
{
    switch (c)
    {
        case 1: return 1.0;    // 1/16
        case 2: return 2.0;    // 1/8
        case 3: return 4.0;    // 1/4 (a beat)
        case 4: return 8.0;    // 1/2
        case 5: return 16.0;   // 1 bar
        case 6: return 32.0;   // 2 bars
        case 7: return (double) juce::jmax (1, patternRows);
        case 8: return 2.0 * juce::jmax (1, patternRows);
        default: return 0.0;   // FREE
    }
}

double delayRows (int c)
{
    static const double rows[] = { 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 16.0 };
    return rows[juce::jlimit (0, 6, c)];
}

juce::String format (int p, int v)
{
    v = clamp (p, v);
    switch (p)
    {
        case s2Osc1Wave:  return osc1Waves[v];
        case s2Osc2Wave:  return osc2Waves[v];
        case s2Osc1PW:    return juce::String (juce::roundToInt (50.0 - 45.0 * v / 127.0)) + "%";
        case s2Osc2Semi:  return v == 24 ? juce::String ("0") : (v > 24 ? "+" : "") + juce::String (v - 24);
        case s2Osc2Fine:  { const int c = juce::roundToInt ((v - 64) * 50.0 / 63.0); return (c > 0 ? "+" : "") + juce::String (c) + " CT"; }
        case s2Cutoff:    return v >= 127 ? juce::String ("OPEN") : hz (cutoffHz (v));
        case s2FDecay:    return ms (filterDecayMs (v));
        case s2Attack: case s2Decay: case s2Release: return ms (envMs (v));
        case s2LfoShape:  return lfoShapes[v];
        case s2LfoSync:   return lfoSyncs[v];
        case s2LfoRate:   { const double f = lfoHz (v); return juce::String (f, f < 1.0 ? 2 : 1) + " HZ"; }
        case s2LfoPitch:  { const double st = 12.0 * (v / 127.0) * (v / 127.0); return juce::String (st, st < 1.0 ? 2 : 1) + " ST"; }
        case s2DelayTime: return delayTimes[v];
        default:          return pct (v);
    }
}

// ---------------------------------------------------------------------------
// patches
// ---------------------------------------------------------------------------

const std::vector<Patch>& factoryPatches()
{
    // OSC1 PW  OSC2 SEMI FINE MIX | CUT RES ENV FDEC | LSH LSY RATE | A  D  S  R | LPI LPW LFI | CHO DTI DFB DMX RSZ RMX
    struct Row { const char* name; int v[s2NumParams]; };
    static const Row rows[] = {
        { "INIT",                { 0, 0,  0, 24, 64,  0,  110, 10, 20, 60,  0, 0, 60,   0, 64, 127, 35,   0,  0,  0,    0, 3, 40,  0,  64,  0 } },

        { "TRANCE SUPERSAW",     { 0, 0,  0, 24, 76, 64,  100, 20, 30, 64,  0, 0, 40,  16, 59, 110, 59,   0,  0,  0,   90, 2, 50, 40,  90, 45 } },
        { "TRANCE PLUCK",        { 0, 0,  1, 36, 66, 40,   50, 40, 90, 42,  0, 0, 60,   0, 49,   0, 43,   0,  0,  0,   40, 2, 60, 50,  80, 35 } },
        { "TRANCE GATE PAD",     { 0, 0,  0, 24, 72, 64,   60, 30, 10, 64,  3, 1, 60,  35, 64, 120, 59,   0,  0, 70,   80, 3, 30, 20, 100, 50 } },
        { "TRANCE ACID BASS",    { 0, 0,  1, 12, 64, 20,   45,105,100, 42,  0, 0, 60,   0, 49,  30, 27,   0,  0,  0,    0, 3, 40,  0,  40, 10 } },
        { "TRANCE ROLLING BASS", { 0, 0,  0, 12, 64, 50,   66, 30, 70, 37,  0, 0, 60,   0, 43,  20, 27,   0,  0,  0,    0, 3, 40,  0,  40,  0 } },
        { "TRANCE HOOVER",       { 1,60,  0, 12, 80, 60,   85, 25, 50, 74,  0, 0, 70,  27, 69, 100, 59,  25, 80,  0,  110, 3, 40, 25,  90, 40 } },
        { "TRANCE ARP LEAD",     { 0, 0,  1, 31, 64, 45,   85, 35, 60, 46,  0, 0, 60,   0, 49,  50, 35,   0,  0,  0,   50, 2, 65, 55,  70, 30 } },
        { "TRANCE STRINGS",      { 0, 0,  0, 24, 74, 64,   78, 10,  0, 64,  1, 0, 30,  74, 74, 120, 85,   8,  0,  0,  120, 3, 40,  0, 110, 60 } },
        { "TRANCE SUB SINE",     { 3, 0,  2, 12, 64, 30,  110,  0,  0, 60,  0, 0, 60,  16, 59, 120, 35,   0,  0,  0,    0, 3, 40,  0,  40,  0 } },

        { "ITALO OCTAVE BASS",   { 1,20,  0, 36, 64, 35,   70, 45, 75, 42,  0, 0, 60,   0, 43,  30, 27,   0,  0,  0,   30, 3, 40,  0,  40,  0 } },
        { "ITALO SYNC LEAD",     { 0, 0,  0, 31, 70, 55,   95, 30, 45, 59,  0, 0, 65,  16, 64, 100, 49,  10,  0,  0,   60, 3, 45, 35,  80, 35 } },
        { "ITALO BRASS",         { 0, 0,  0, 24, 70, 50,   60, 15, 75, 64,  0, 0, 60,  27, 64, 100, 43,   0,  0,  0,   70, 3, 40,  0,  70, 25 } },
        { "ITALO POLY STAB",     { 0, 0,  1, 24, 68, 50,   80, 25, 60, 46,  0, 0, 60,   0, 53,  20, 43,   0,  0,  0,   90, 3, 35, 25,  80, 30 } },
        { "ITALO CHOIR PAD",     { 2, 0,  0, 36, 70, 35,   72, 10,  0, 60,  0, 0, 30,  85, 74, 115, 92,   6,  0,  0,  127, 3, 40,  0, 110, 60 } },
        { "ITALO SPACE BELL",    { 3, 0,  3, 43, 66, 45,  110,  0,  0, 60,  0, 0, 60,   0, 85,   0, 85,   0,  0,  0,   20, 4, 60, 45, 110, 55 } },
        { "ITALO PULSE ARP",     { 1, 0,  1, 36, 64, 40,   75, 40, 55, 42,  1, 0, 50,   0, 43,   0, 35,   0, 50,  0,   30, 1, 50, 40,  60, 20 } },
        { "ITALO DREAM LEAD",    { 1,40,  0, 24, 72, 50,   88, 20, 35, 64,  0, 0, 62,  27, 64, 110, 59,  12, 40,  0,   80, 4, 55, 40, 100, 45 } },

        { "80S JUNO PAD",        { 1,30,  0, 12, 64, 40,   75, 15,  0, 60,  1, 0, 35,  59, 74, 115, 85,   0, 90,  0,  127, 3, 40,  0,  90, 40 } },
        { "80S JUMP BRASS",      { 0, 0,  0, 24, 74, 50,   70, 10, 65, 59,  0, 0, 60,  27, 69, 110, 49,   0,  0,  0,   50, 3, 40,  0,  60, 20 } },
        { "80S SOFT KEYS",       { 2, 0,  3, 36, 64, 40,   90,  0, 20, 74,  0, 0, 60,   0, 85,  40, 59,   0,  0,  0,   70, 3, 40,  0,  70, 30 } },
        { "80S SQUARE LEAD",     { 1, 0,  1, 36, 66, 30,   90, 25, 30, 53,  0, 0, 70,  16, 59, 110, 43,  12,  0,  0,    0, 3, 40, 30,  70, 25 } },
        { "80S BASS",            { 0, 0,  1, 12, 64, 50,   66, 20, 60, 46,  0, 0, 60,   0, 53,  60, 27,   0,  0,  0,    0, 3, 40,  0,  40,  0 } },
        { "80S SWEEP PAD",       { 0, 0,  0, 24, 76, 64,   50, 60,  0, 60,  1, 8, 60,  74, 74, 120, 92,   0,  0, 90,  100, 3, 40,  0, 110, 55 } },
        { "80S ARPEGGIO",        { 0, 0,  0, 36, 64, 30,   70, 50, 70, 37,  0, 0, 60,   0, 43,   0, 35,   0,  0,  0,    0, 2, 40, 30,  60, 20 } },
        { "80S STRINGS",         { 0, 0,  0, 24, 72, 64,   82,  5,  0, 60,  0, 0, 40,  69, 74, 120, 85,   5,  0,  0,  120, 3, 40,  0, 100, 45 } },
        { "80S NOISE SWEEP",     { 3, 0,  4, 24, 64,127,   40, 90,  0, 60,  0, 7, 60,  59, 74, 120, 92,   0,  0,120,    0, 3, 60, 30, 120, 60 } },
    };
    static const std::vector<Patch> list = []
    {
        std::vector<Patch> l;
        for (auto& r : rows)
        {
            Patch p;
            p.name = r.name;
            for (int i = 0; i < s2NumParams; ++i) p.values[(size_t) i] = clamp (i, r.v[i]);
            l.push_back (p);
        }
        return l;
    }();
    return list;
}

juce::File userPatchFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Four Tracker").getChildFile ("Synth 2 Patches");
}

bool loadPatchFile (const juce::File& f, Patch& out)
{
    auto xml = juce::parseXML (f);
    if (xml == nullptr || ! xml->hasTagName ("FT_SYNTH2_PATCH")) return false;
    out.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension()).toUpperCase();
    out.values = defaults();
    for (int p = 0; p < s2NumParams; ++p)
        out.values[(size_t) p] = clamp (p, xml->getIntAttribute (key (p), defaultValue (p)));
    out.factory = false;
    out.file = f;
    return true;
}

std::vector<Patch> allPatches()
{
    auto l = factoryPatches();
    std::vector<Patch> user;
    for (const auto& e : juce::RangedDirectoryIterator (userPatchFolder(), false, "*.ft2patch"))
    {
        Patch p;
        if (loadPatchFile (e.getFile(), p)) user.push_back (p);
    }
    std::sort (user.begin(), user.end(), [] (const Patch& a, const Patch& b) { return a.name.compareNatural (b.name) < 0; });
    l.insert (l.end(), user.begin(), user.end());
    return l;
}

juce::Result savePatch (const juce::String& patchName, const Synth2Values& values, juce::File& written)
{
    const auto clean = juce::File::createLegalFileName (patchName.trim().toUpperCase());
    if (clean.isEmpty()) return juce::Result::fail ("Give the patch a name");
    auto folder = userPatchFolder();
    if (! folder.createDirectory()) return juce::Result::fail ("Could not make " + folder.getFullPathName());
    juce::XmlElement x ("FT_SYNTH2_PATCH");
    x.setAttribute ("name", patchName.trim().toUpperCase());
    for (int p = 0; p < s2NumParams; ++p) x.setAttribute (key (p), clamp (p, values[(size_t) p]));
    written = folder.getChildFile (clean + ".ft2patch");
    if (! x.writeTo (written)) return juce::Result::fail ("Could not write " + written.getFullPathName());
    return juce::Result::ok();
}

juce::String toString (const Synth2Values& v)
{
    juce::StringArray s;
    for (auto x : v) s.add (juce::String (x));
    return s.joinIntoString (",");
}

Synth2Values fromString (const juce::String& str)
{
    auto v = defaults();
    juce::StringArray s;
    s.addTokens (str, ",", "");
    for (int p = 0; p < s2NumParams && p < s.size(); ++p) v[(size_t) p] = clamp (p, s[p].getIntValue());
    return v;
}

} // namespace ft::synth2
