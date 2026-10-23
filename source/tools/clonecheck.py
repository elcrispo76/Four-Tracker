"""Usage: python3 clonecheck.py <four-tracker-source-dir> <other-project-dir> [<other-project-dir> ...]

Token-based clone detection: Four Tracker vs open-source trackers.

Two passes, like jscpd / Moss-style k-gram matching:
  exact      - identifiers and numbers kept as written, window K_EXACT tokens
  normalized - every identifier -> I, number -> N, string -> S (catches renamed copies), window K_NORM tokens
Matching windows are merged into spans; spans are reported with their length in tokens and line ranges.
"""
import os, re, sys, json, hashlib
from collections import defaultdict

TARGET = sys.argv[1]
CORPORA = sys.argv[2:]
K_EXACT, K_NORM = 30, 60
EXTS = ('.c', '.cpp', '.cc', '.h', '.hpp', '.cxx', '.hxx', '.inl')

KEYWORDS = set('''alignas alignof auto bool break case catch char class const constexpr continue default delete do double
else enum explicit extern false float for friend goto if inline int long mutable namespace new noexcept nullptr operator
private protected public register return short signed sizeof static static_cast dynamic_cast reinterpret_cast const_cast
struct switch template this throw true try typedef typename union unsigned using virtual void volatile while uint8_t
int8_t uint16_t int16_t uint32_t int32_t int64_t uint64_t size_t'''.split())

TOK = re.compile(r'''
   (?P<ws>\s+)
 | (?P<lc>//[^\n]*)
 | (?P<bc>/\*.*?\*/)
 | (?P<pp>\#[^\n]*)
 | (?P<str>"(?:\\.|[^"\\\n])*")
 | (?P<chr>'(?:\\.|[^'\\\n])+')
 | (?P<num>(?:0[xX][0-9a-fA-F']+|\d[\d']*\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)[uUlLfF]*)
 | (?P<id>[A-Za-z_]\w*)
 | (?P<op>::|->|\+\+|--|<<=|>>=|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^!~<>=?:;,.(){}\[\]])
 | (?P<other>.)
''', re.S | re.X)

def tokens(text):
    out = []   # (exact, normalized, line)
    line = 1
    for m in TOK.finditer(text):
        kind, val = m.lastgroup, m.group()
        if kind in ('ws', 'lc', 'bc', 'pp', 'other'):
            line += val.count('\n'); continue
        if kind == 'str':   e, n = val, 'S'
        elif kind == 'chr': e, n = val, 'C'
        elif kind == 'num': e, n = val, 'N'
        elif kind == 'id':  e, n = val, (val if val in KEYWORDS else 'I')
        else:               e, n = val, val
        out.append((e, n, line))
        line += val.count('\n')
    return out

def files(root):
    for d, _, fs in os.walk(root):
        if '/.git' in d: continue
        for f in fs:
            if f.endswith(EXTS):
                p = os.path.join(d, f)
                try:
                    if os.path.getsize(p) < 3_000_000:
                        yield p
                except OSError:
                    pass

def h(seq):
    return hashlib.blake2b('\x1f'.join(seq).encode(), digest_size=8).digest()

# --- index the target (Four Tracker) ----------------------------------------
target_toks = {}
index = {'exact': defaultdict(list), 'norm': defaultdict(list)}
for p in files(TARGET):
    t = tokens(open(p, encoding='utf-8', errors='replace').read())
    target_toks[p] = t
    for mode, k, col in (('exact', K_EXACT, 0), ('norm', K_NORM, 1)):
        seq = [x[col] for x in t]
        for i in range(len(seq) - k + 1):
            index[mode][h(seq[i:i + k])].append((p, i))

total_target_tokens = sum(len(t) for t in target_toks.values())

# --- scan corpora --------------------------------------------------------------
hits = {'exact': defaultdict(set), 'norm': defaultdict(set)}   # (targetfile) -> set of token idx covered
spans = []
scanned = defaultdict(int)
for root in CORPORA:
    name = os.path.basename(root.rstrip('/'))
    for p in files(root):
        try:
            t = tokens(open(p, encoding='utf-8', errors='replace').read())
        except Exception:
            continue
        scanned[name] += 1
        for mode, k, col in (('exact', K_EXACT, 0), ('norm', K_NORM, 1)):
            seq = [x[col] for x in t]
            pairs = []
            for j in range(len(seq) - k + 1):
                for (tp, ti) in index[mode].get(h(seq[j:j + k]), ()):
                    pairs.append((tp, ti, j))
            # merge diagonal runs into spans
            pairs.sort()
            run = None
            for tp, ti, j in pairs:
                if run and run[0] == tp and ti == run[2] + 1 and j == run[4] + 1:
                    run[2], run[4] = ti, j
                else:
                    if run: spans.append((mode, name, p, *run))
                    run = [tp, ti, ti, j, j]
            if run: spans.append((mode, name, p, *run))

results = []
for mode, name, cp, tp, ti0, ti1, j0, j1 in spans:
    k = K_EXACT if mode == 'exact' else K_NORM
    length = ti1 - ti0 + k
    tt = target_toks[tp]
    for i in range(ti0, ti1 + k): hits[mode][tp].add(i)
    results.append({
        'mode': mode, 'corpus': name, 'tokens': length,
        'ours': f"{os.path.relpath(tp, TARGET)}:{tt[ti0][2]}-{tt[ti1 + k - 1][2]}",
        'theirs_path': cp, 'ours_path': tp, 'ti0': ti0, 'len': length,
        'text': ' '.join(x[0] for x in tt[ti0:ti0 + min(length, 80)]),
    })

summary = {
    'target_files': len(target_toks), 'target_tokens': total_target_tokens,
    'scanned_files': dict(scanned),
    'exact_tokens_matched': sum(len(v) for v in hits['exact'].values()),
    'norm_tokens_matched': sum(len(v) for v in hits['norm'].values()),
}
json.dump({'summary': summary, 'results': sorted(results, key=lambda r: -r['tokens'])}, open('clone_results.json', 'w'), indent=1)
print(json.dumps(summary, indent=1))
for r in sorted(results, key=lambda r: -r['tokens'])[:60]:
    print(f"{r['mode']:5} {r['tokens']:4} tok  ours {r['ours']}  <->  {r['corpus']}: {r['theirs_path'].split(r['corpus'] + '/', 1)[-1]}")
    print('      ', r['text'][:220])
