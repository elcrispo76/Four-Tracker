#!/usr/bin/env python3
"""Four Tracker Sample Pack - Loops.
Builds tempo-exact loops from the pack's own CC0 one-shots (drums, bass, synth),
so the loops are CC0 too. Tails that ring past the end are wrapped to the start,
so every loop repeats without a seam. Each WAV carries loop points (smpl chunk).
usage: make_loops.py <Sample Pack folder> <output Loops folder>"""
import sys, os, struct, wave
import numpy as np

PACK, OUT = sys.argv[1], sys.argv[2]
SR = 44100

def load(rel):
    w = wave.open(os.path.join(PACK, rel))
    n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
    a = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float32) / 32768.0
    a = a.reshape(-1, ch)
    if ch == 1: a = np.repeat(a, 2, axis=1)
    return a

def pitch(a, semis):
    if semis == 0: return a
    r = 2.0 ** (semis / 12.0)
    n = int(len(a) / r)
    x = np.arange(n) * r
    return np.stack([np.interp(x, np.arange(len(a)), a[:, c]) for c in range(2)], axis=1).astype(np.float32)

def gate(a, frames, rel=0.012):
    """keep `frames`, then a short release"""
    rl = int(rel * SR)
    out = a[:frames + rl].copy()
    if len(out) > frames:
        k = len(out) - frames
        out[frames:] *= np.linspace(1, 0, k, dtype=np.float32)[:, None]
    return out

class Loop:
    def __init__(self, bpm, bars):
        self.bpm, self.bars = bpm, bars
        self.steps = bars * 16
        self.len = int(round(bars * 4 * 60.0 / bpm * SR))
        self.buf = np.zeros((self.len, 2), np.float32)
    def at(self, step, swing=0.0):
        t = step * self.len / self.steps
        if swing and step % 2 == 1: t += swing * self.len / self.steps
        return int(round(t))
    def add(self, a, pos, gain=1.0, pan=0.0):
        g = np.array([gain * min(1, 1 - pan), gain * min(1, 1 + pan)], np.float32)
        n = len(a); p = pos % self.len; i = 0
        while i < n:                       # wrap tails round to the start
            k = min(n - i, self.len - p)
            self.buf[p:p + k] += a[i:i + k] * g
            i += k; p = 0
    def pattern(self, a, pat, gain=1.0, pan=0.0, swing=0.0):
        """pat: one char per step: x = hit, o = soft (ghost), . = rest"""
        pat = pat.replace(' ', '')
        for s, c in enumerate(pat[:self.steps]):
            if c in 'xXo':
                v = {'X': 1.15, 'x': 1.0, 'o': 0.42}[c]
                self.add(a, self.at(s, swing), gain * v, pan)
    def notes(self, a, seq, root_semis=0, gain=1.0, pan=0.0, swing=0.0, glide=False):
        """seq: list of (step, semitones from the sample's note, length in steps)"""
        for s, n, l in seq:
            frames = int(l * self.len / self.steps)
            self.add(gate(pitch(a, n + root_semis), frames), self.at(s, swing), gain, pan)
    def save(self, folder, name, peak_db=-1.0):
        b = self.buf
        pk = np.max(np.abs(b)) or 1.0
        b = b * (10 ** (peak_db / 20) / pk)
        d = os.path.join(OUT, folder); os.makedirs(d, exist_ok=True)
        fn = os.path.join(d, f"{name} {self.bpm} BPM {self.steps}ST.wav")
        write_wav(fn, b)
        return fn

def write_wav(fn, b):
    pcm = np.clip(np.round(b * 32767), -32768, 32767).astype('<i2').tobytes()
    n = len(b)
    fmt = struct.pack('<HHIIHH', 1, 2, SR, SR * 4, 4, 16)
    # smpl chunk: one forward loop over the whole file
    smpl = struct.pack('<9I', 0, 0, int(1e9 / SR), 60, 0, 0, 0, 1, 0) + struct.pack('<6I', 0, 0, 0, n - 1, 0, 0)
    body = b'WAVE' + b'fmt ' + struct.pack('<I', len(fmt)) + fmt + b'smpl' + struct.pack('<I', len(smpl)) + smpl \
           + b'data' + struct.pack('<I', len(pcm)) + pcm
    with open(fn, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<I', len(body)) + body)

S = {k: load(v) for k, v in {
    'k909': 'Drums/Kicks/KICK 909.wav', 'khard': 'Drums/Kicks/KICK HARD.wav', 'ksoft': 'Drums/Kicks/KICK SOFT.wav',
    'k808': 'Drums/Kicks/KICK 808 LONG.wav', 'ktr': 'Drums/Kicks/KICK TRANCE.wav',
    's909': 'Drums/Snares/SNARE 909.wav', 's808': 'Drums/Snares/SNARE 808.wav', 'sgate': 'Drums/Snares/SNARE 80S GATED.wav',
    'rim': 'Drums/Snares/RIMSHOT.wav', 'str': 'Drums/Snares/SNARE TRANCE.wav',
    'clap': 'Drums/Claps/CLAP 808.wav', 'clapb': 'Drums/Claps/CLAP BIG.wav', 'snap': 'Drums/Claps/CLAP SNAP.wav',
    'ch': 'Drums/Hats/HAT 909 CLOSED.wav', 'oh': 'Drums/Hats/HAT 909 OPEN.wav', 'ch8': 'Drums/Hats/HAT 808 CLOSED.wav',
    'oh8': 'Drums/Hats/HAT 808 OPEN.wav', 'ped': 'Drums/Hats/HAT PEDAL.wav', 'off': 'Drums/Hats/HAT TRANCE OFFBEAT.wav',
    'ride': 'Drums/Cymbals/RIDE.wav', 'shk': 'Drums/Percussion/SHAKER.wav', 'tamb': 'Drums/Percussion/TAMBOURINE.wav',
    'clave': 'Drums/Percussion/CLAVE.wav', 'cgl': 'Drums/Percussion/CONGA LOW.wav', 'cgh': 'Drums/Percussion/CONGA HIGH.wav',
    'cow': 'Drums/Percussion/COWBELL 808.wav', 'wood': 'Drums/Percussion/WOODBLOCK.wav', 'zap': 'Drums/Percussion/ZAP.wav',
    'tl': 'Drums/Toms/TOM 808 LOW.wav', 'tm': 'Drums/Toms/TOM 808 MID.wav',
    'bacid': 'Bass/BASS ACID C2.wav', 'bsq': 'Bass/BASS ACID SQUELCH C2.wav', 'broll': 'Bass/BASS ROLLING C2.wav',
    'bsub': 'Bass/BASS SUB C2.wav', 'breese': 'Bass/BASS REESE C2.wav', 'boct': 'Bass/BASS OCTAVE C2.wav',
    'bpluck': 'Bass/BASS PLUCK C2.wav', 'b80': 'Bass/BASS 80S C2.wav', 'bsquare': 'Bass/BASS SQUARE C2.wav',
    'larp': 'Synth/Leads/LEAD ARP C4.wav', 'lsaw': 'Synth/Leads/LEAD SUPERSAW C4.wav', 'lsq': 'Synth/Leads/LEAD SQUARE C4.wav',
    'bell': 'Synth/Plucks/BELL SPACE C5.wav', 'keys': 'Synth/Plucks/KEYS SOFT C4.wav',
    'stabm7': 'Synth/Chords/CHORD STAB C MINOR 7.wav', 'stabsus': 'Synth/Chords/CHORD STAB C SUS4.wav',
    'plkmin': 'Synth/Chords/CHORD PLUCK C MINOR.wav',
}.items()}
# a few more plucks if present
for k, v in {'ptr': 'Synth/Plucks/PLUCK TRANCE C4.wav', 'p80': 'Synth/Plucks/PLUCK 80S C4.wav',
             'ppulse': 'Synth/Plucks/PLUCK PULSE C4.wav', 'pstab': 'Synth/Plucks/STAB POLY C4.wav'}.items():
    if os.path.exists(os.path.join(PACK, v)): S[k] = load(v)

made = []
def mk(bpm, bars, folder, name, fn):
    L = Loop(bpm, bars); fn(L); made.append(L.save(folder, name))

# ---------------------------------------------------------------- DnB 174
D = 'Drums/DnB'
mk(174, 1, D, 'DNB BEAT 01 TWO STEP', lambda L: (
    L.pattern(S['k909'], 'x.........x.....', 1.0), L.pattern(S['s909'], '....x.......x...', 0.9),
    L.pattern(S['ch'], 'x.x.x.x.x.x.x.x.', 0.35, 0.2), L.pattern(S['shk'], '.x.x.x.x.x.x.x.x', 0.22, -0.3)))
mk(174, 1, D, 'DNB BEAT 02 ROLLER', lambda L: (
    L.pattern(S['k909'], 'x.........x..x..', 1.0), L.pattern(S['s909'], '....x..o....x..o', 0.9),
    L.pattern(S['ch'], 'xxxxxxxxxxxxxxxx', 0.25, 0.25), L.pattern(S['ride'], 'x...x...x...x...', 0.18, -0.2)))
mk(174, 1, D, 'DNB BEAT 03 JUNGLE BREAK', lambda L: (
    L.pattern(S['ksoft'], 'x.x.......xx....', 1.0), L.pattern(S['s808'], '....x.o..o..x.oo', 0.95),
    L.pattern(S['ch8'], 'x.xxx.x.x.xxx.x.', 0.35, 0.2), L.pattern(S['oh8'], '.......x........', 0.25, -0.2),
    L.pattern(S['rim'], '...o.......o....', 0.4, 0.3)))
mk(174, 1, D, 'DNB BEAT 04 HALF TIME', lambda L: (
    L.pattern(S['khard'], 'x.....x...x.....', 1.0), L.pattern(S['sgate'], '........x.......', 1.0),
    L.pattern(S['ch'], 'x.x.x.x.x.x.x.xx', 0.3, 0.2), L.pattern(S['clap'], '........x.......', 0.4)))
mk(174, 2, D, 'DNB BEAT 05 LIQUID', lambda L: (
    L.pattern(S['ksoft'], 'x.........x.....' 'x.........x..x..', 1.0), L.pattern(S['s909'], '....x.......x...' '....x.......x..o', 0.8),
    L.pattern(S['shk'], 'xxxxxxxxxxxxxxxx' 'xxxxxxxxxxxxxxxx', 0.18, -0.25), L.pattern(S['ride'], 'x.x.x.x.x.x.x.x.' 'x.x.x.x.x.x.x.x.', 0.16, 0.3),
    L.pattern(S['tamb'], '....x.......x...' '....x.......x...', 0.2, 0.4)))
mk(174, 2, D, 'DNB BEAT 06 AMEN STYLE CHOPS', lambda L: (
    L.pattern(S['ksoft'], 'x.x.......xx....' 'x.x.......x.....', 1.0), L.pattern(S['s808'], '....x.ox.o..x..o' '.o..x.o..o..xoxo', 0.95),
    L.pattern(S['ride'], 'x.x.x.x.x.x.x.x.' 'x.x.x.x.x.x.x.x.', 0.22, 0.25), L.pattern(S['ch8'], '.x.x.x.x.x.x.x.x' '.x.x.x.x.x.x.x.x', 0.2, -0.2)))

# ---------------------------------------------------------------- Breakbeats 136 / 160
D = 'Drums/Breakbeats'
mk(136, 1, D, 'BREAK 01 FUNKY', lambda L: (
    L.pattern(S['ksoft'], 'x.x.......x..x..', 1.0), L.pattern(S['s808'], '....x..o.o..x...', 0.9),
    L.pattern(S['ch8'], 'x.xxx.x.x.x.x.xx', 0.33, 0.2), L.pattern(S['oh8'], '..............x.', 0.25, -0.2)))
mk(136, 1, D, 'BREAK 02 BIG BEAT', lambda L: (
    L.pattern(S['khard'], 'x.....x.x.......', 1.0), L.pattern(S['sgate'], '....x.......x...', 0.9),
    L.pattern(S['ch'], 'x.x.x.x.x.x.x.x.', 0.3, 0.2), L.pattern(S['tamb'], '....x.......x...', 0.3, -0.3)))
mk(160, 1, D, 'BREAK 03 BREAKBEAT HARDCORE', lambda L: (
    L.pattern(S['k909'], 'x.x.......x.....', 1.0), L.pattern(S['s909'], '....x..o.o..x..o', 0.95),
    L.pattern(S['ch8'], 'xxxxxxxxxxxxxxxx', 0.25, 0.2), L.pattern(S['clap'], '....x.......x...', 0.4)))
mk(160, 1, D, 'BREAK 04 GHOST SNARES', lambda L: (
    L.pattern(S['ksoft'], 'x.......x.x.....', 1.0), L.pattern(S['s808'], '..o.x.o..oo.x.o.', 0.9),
    L.pattern(S['ride'], 'x.x.x.x.x.x.x.x.', 0.2, 0.3), L.pattern(S['rim'], '.......o.......o', 0.35, -0.3)))

# ---------------------------------------------------------------- Techno 130
D = 'Drums/Techno'
mk(130, 1, D, 'TECHNO BEAT 01 STRAIGHT', lambda L: (
    L.pattern(S['khard'], 'x...x...x...x...', 1.0), L.pattern(S['oh'], '..x...x...x...x.', 0.3, 0.2),
    L.pattern(S['ch'], 'xx.xxx.xxx.xxx.x', 0.2, -0.2), L.pattern(S['clap'], '....x.......x...', 0.45)))
mk(130, 1, D, 'TECHNO BEAT 02 RUMBLE', lambda L: (
    L.pattern(S['k909'], 'x...x...x...x...', 1.0), L.pattern(pitch(S['k808'], -2), '..o...o...o...o.', 0.35),
    L.pattern(S['ch'], 'x.x.x.x.x.x.x.x.', 0.22, 0.2), L.pattern(S['rim'], '...x..x....x..x.', 0.3, -0.3)))
mk(130, 1, D, 'TECHNO BEAT 03 WAREHOUSE', lambda L: (
    L.pattern(S['khard'], 'x...x...x...x...', 1.0), L.pattern(S['oh'], '..x...x...x...x.', 0.28, 0.25),
    L.pattern(S['ride'], 'x.x.x.x.x.x.x.x.', 0.16, -0.25), L.pattern(S['sgate'], '....x.......x...', 0.3),
    L.pattern(S['cow'], '......x.......x.', 0.18, 0.3)))
mk(130, 1, D, 'TECHNO BEAT 04 MINIMAL', lambda L: (
    L.pattern(S['k909'], 'x...x...x...x...', 1.0), L.pattern(S['ch8'], '..x...x...x...xx', 0.3, 0.2),
    L.pattern(S['clave'], '...x..x.....x...', 0.25, -0.3), L.pattern(S['snap'], '....x.......x...', 0.3)))
mk(130, 2, D, 'TECHNO BEAT 05 INDUSTRIAL', lambda L: (
    L.pattern(S['khard'], 'x...x...x...x.x.' 'x...x...x...x...', 1.0), L.pattern(S['s909'], '....x.......x...' '....x.......x.xx', 0.6),
    L.pattern(S['ch'], 'xxxxxxxxxxxxxxxx' 'xxxxxxxxxxxxxxxx', 0.2, 0.2), L.pattern(S['zap'], '.......x........' '...............x', 0.25, -0.3),
    L.pattern(S['tm'], '..............x.' '..........x.....', 0.3, 0.3)))
mk(130, 1, D, 'TECHNO BEAT 06 HYPNOTIC', lambda L: (
    L.pattern(S['khard'], 'x...x...x...x...', 1.0), L.pattern(S['oh8'], '..x...x...x...x.', 0.3, 0.2),
    L.pattern(S['ped'], 'xxx.xxx.xxx.xxx.', 0.18, -0.2), L.pattern(S['tamb'], '....x.......x...', 0.25, 0.3)))

# ---------------------------------------------------------------- House 124 (a little swing)
D = 'Drums/House'
SW = 0.12
mk(124, 1, D, 'HOUSE BEAT 01 CLASSIC', lambda L: (
    L.pattern(S['k909'], 'x...x...x...x...', 1.0), L.pattern(S['clap'], '....x.......x...', 0.6),
    L.pattern(S['oh'], '..x...x...x...x.', 0.3, 0.2), L.pattern(S['ch'], 'x.x.x.x.x.x.x.x.', 0.18, -0.2, SW)))
mk(124, 1, D, 'HOUSE BEAT 02 DEEP', lambda L: (
    L.pattern(S['ksoft'], 'x...x...x...x...', 1.0), L.pattern(S['snap'], '....x.......x...', 0.5),
    L.pattern(S['shk'], 'xxxxxxxxxxxxxxxx', 0.2, -0.25, SW), L.pattern(S['oh8'], '..x...x...x...x.', 0.25, 0.25),
    L.pattern(S['rim'], '.......x......x.', 0.25, 0.3, SW)))
mk(124, 1, D, 'HOUSE BEAT 03 GARAGE SHUFFLE', lambda L: (
    L.pattern(S['k909'], 'x.....x...x.....', 1.0), L.pattern(S['clap'], '....x.......x...', 0.6),
    L.pattern(S['ch'], 'x.xxx.xxx.xxx.xx', 0.2, 0.2, 0.2), L.pattern(S['oh'], '......x.......x.', 0.25, -0.2)))
mk(124, 1, D, 'HOUSE BEAT 04 DISCO', lambda L: (
    L.pattern(S['k909'], 'x...x...x...x...', 1.0), L.pattern(S['s909'], '....x.......x...', 0.5),
    L.pattern(S['oh'], '..x...x...x...x.', 0.35, 0.2), L.pattern(S['tamb'], 'x.x.x.x.x.x.x.x.', 0.2, -0.3, SW),
    L.pattern(S['clapb'], '....x.......x...', 0.3)))
mk(124, 2, D, 'HOUSE BEAT 05 LATIN PERC', lambda L: (
    L.pattern(S['k909'], 'x...x...x...x...' 'x...x...x...x...', 1.0), L.pattern(S['clap'], '....x.......x...' '....x.......x...', 0.5),
    L.pattern(S['cgh'], '..x..x....x..x..' '..x..x..x...x...', 0.3, 0.35, SW), L.pattern(S['cgl'], 'x......x......x.' 'x......x...x....', 0.35, -0.35, SW),
    L.pattern(S['shk'], 'x.x.x.x.x.x.x.x.' 'x.x.x.x.x.x.x.x.', 0.18, 0.1, SW), L.pattern(S['clave'], '...x..x...x.....' '...x..x...x.....', 0.18, -0.2)))
mk(124, 1, D, 'HOUSE BEAT 06 TECH HOUSE', lambda L: (
    L.pattern(S['khard'], 'x...x...x...x...', 1.0), L.pattern(S['snap'], '....x.......x...', 0.5),
    L.pattern(S['off'], '..x...x...x...x.', 0.35, 0.2), L.pattern(S['ch8'], 'xx.xxx.xxx.xxx.x', 0.15, -0.2, SW),
    L.pattern(S['wood'], '.....x.....x..x.', 0.2, 0.35, SW)))

# ---------------------------------------------------------------- top loops (no kick)
D = 'Drums/Top Loops'
mk(128, 1, D, 'TOP LOOP 01 HATS', lambda L: (L.pattern(S['ch'], 'xxoxxxoxxxoxxxox', 0.4, 0.15), L.pattern(S['oh'], '..x...x...x...x.', 0.35, -0.15)))
mk(128, 1, D, 'TOP LOOP 02 SHAKER TAMB', lambda L: (L.pattern(S['shk'], 'xoxoxoxoxoxoxoxo', 0.45, -0.2, 0.1), L.pattern(S['tamb'], '....x.......x...', 0.4, 0.3)))
mk(128, 1, D, 'TOP LOOP 03 PERCUSSION', lambda L: (
    L.pattern(S['cgh'], '..x..x....x..x..', 0.45, 0.3), L.pattern(S['cgl'], 'x......x......x.', 0.5, -0.3),
    L.pattern(S['clave'], '...x..x...x.....', 0.3), L.pattern(S['cow'], '..............x.', 0.2, 0.2)))
mk(174, 1, D, 'TOP LOOP 04 DNB RIDE', lambda L: (L.pattern(S['ride'], 'x.x.x.x.x.x.x.x.', 0.45, 0.2), L.pattern(S['ch8'], '.xxx.xxx.xxx.xxx', 0.3, -0.2)))

# ---------------------------------------------------------------- bass (sample note C2, semitones relative)
# keys: DnB F minor, techno A minor, house G minor (offsets from C)
D = 'Bass/DnB'
F = 5; A = -3; G = 7
mk(174, 2, D, 'DNB BASS 01 REESE F MIN', lambda L: L.notes(S['breese'], [(0, 0, 6), (6, 0, 2), (10, 3, 6), (16, -2, 8), (24, -4, 7)], F - 12, 1.0))
mk(174, 2, D, 'DNB BASS 02 SUB F MIN', lambda L: L.notes(S['bsub'], [(0, 0, 6), (10, 0, 4), (16, 3, 6), (26, -2, 4), (30, 0, 2)], F - 12, 1.0))
mk(174, 1, D, 'DNB BASS 03 ROLLING F MIN', lambda L: L.notes(S['broll'], [(0, 0, 2), (3, 0, 1), (6, 12, 1), (8, 0, 2), (10, 3, 2), (13, 0, 1), (14, -2, 2)], F - 12, 1.0))
D = 'Bass/Techno'
mk(130, 1, D, 'TECHNO BASS 01 OFFBEAT A MIN', lambda L: L.notes(S['broll'], [(s, 0, 1.6) for s in (2, 6, 10, 14)], A, 1.0))
mk(130, 1, D, 'TECHNO BASS 02 ROLLING A MIN', lambda L: L.notes(S['broll'], [(s, 0, 0.8) for s in range(16) if s % 4 != 0], A, 1.0))
mk(130, 1, D, 'TECHNO BASS 03 ACID A MIN', lambda L: L.notes(S['bacid'], [(0, 0, 1), (2, 12, 1), (3, 0, 1), (5, 3, 1), (6, 15, 1), (8, 0, 2), (10, 7, 1), (11, 12, 1), (13, 10, 1), (14, 0, 1), (15, 12, 1)], A, 1.0))
mk(130, 2, D, 'TECHNO BASS 04 SQUELCH A MIN', lambda L: L.notes(S['bsq'], [(0, 0, 1), (1, 0, 1), (3, 12, 1), (4, 0, 1), (6, 10, 1), (7, 12, 1), (9, 0, 1), (11, 3, 2), (14, 0, 1), (16, 0, 1), (17, 0, 1), (19, 12, 1), (21, 7, 1), (22, 5, 2), (25, 3, 1), (27, 0, 1), (29, 12, 1), (30, 15, 1)], A, 1.0))
D = 'Bass/House'
mk(124, 1, D, 'HOUSE BASS 01 OFFBEAT G MIN', lambda L: L.notes(S['b80'], [(2, 0, 1.5), (6, 0, 1.5), (10, 0, 1.5), (13, 3, 1), (14, 0, 1.5)], G - 12, 1.0, 0, SW))
mk(124, 2, D, 'HOUSE BASS 02 DEEP G MIN', lambda L: L.notes(S['bsub'], [(0, 0, 3), (3, 0, 1), (6, 12, 1), (8, 0, 3), (14, -2, 2), (16, 3, 3), (19, 3, 1), (22, 5, 2), (24, -2, 4), (30, 0, 2)], G - 12, 1.0, 0, SW))
mk(124, 1, D, 'HOUSE BASS 03 OCTAVE G MIN', lambda L: L.notes(S['boct'], [(s, 12 if s % 2 else 0, 0.9) for s in range(0, 16, 2)] + [(15, 10, 0.9)], G - 12, 1.0, 0, SW))

# ---------------------------------------------------------------- arps and chords (lead samples on C4)
D = 'Arps'
arp1 = [0, 3, 7, 12, 7, 3, 0, 3, 7, 10, 15, 10, 7, 3, 0, -2]
mk(130, 1, D, 'ARP 01 TRANCE A MIN', lambda L: L.notes(S.get('ptr', S['larp']), [(s, arp1[s], 0.9) for s in range(16)], A, 1.0))
arp2 = [0, 12, 7, 12, 3, 12, 7, 12, -2, 10, 5, 10, 2, 10, 5, 10] * 2
mk(130, 2, D, 'ARP 02 PULSE A MIN', lambda L: L.notes(S.get('ppulse', S['lsq']), [(s, arp2[s], 0.7) for s in range(32)], A, 1.0))
mk(124, 1, D, 'ARP 03 HOUSE KEYS G MIN', lambda L: L.notes(S['keys'], [(0, 0, 1), (3, 3, 1), (6, 7, 1), (8, 10, 1), (11, 7, 1), (14, 3, 1)], G - 12, 1.0, 0, SW))
arp4 = [0, 7, 3, 7, 12, 7, 3, 7]
mk(174, 2, D, 'ARP 04 DNB BELLS F MIN', lambda L: L.notes(S['bell'], [(s * 2, arp4[s % 8] + (5 if s >= 8 else 0) - 12, 2) for s in range(16)], F, 0.9))
arp5 = [0, 0, 12, 0, 3, 0, 15, 0, 7, 0, 12, 0, 10, 0, 19, 0]
mk(130, 1, D, 'ARP 05 SUPERSAW A MIN', lambda L: L.notes(S['lsaw'], [(s, arp5[s], 0.6) for s in range(16)], A - 12, 1.0))
D = 'Chords'
mk(124, 2, D, 'CHORDS 01 HOUSE STABS G MIN7', lambda L: L.notes(S['stabm7'], [(0, 0, 1), (3, 0, 1), (6, 0, 1), (10, 0, 1), (16, -2, 1), (19, -2, 1), (22, -4, 1), (26, -4, 2)], G - 12 + 0, 1.0, 0, SW))
mk(130, 1, D, 'CHORDS 02 DUB TECHNO A MIN7', lambda L: L.notes(S['stabm7'], [(3, 0, 0.8), (6, 0, 0.6), (11, 0, 0.8), (14, 0, 0.6)], A, 0.9))
mk(174, 2, D, 'CHORDS 03 DNB PLUCKS F MIN', lambda L: L.notes(S['plkmin'], [(0, 0, 3), (6, 0, 2), (10, 3, 3), (16, -4, 3), (22, -4, 2), (26, -2, 4)], F, 0.9))

print(len(made), 'loops')
