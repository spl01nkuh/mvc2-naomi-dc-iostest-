# sndtrace.py A B [samples]: two MVC2_SNDTRACE files of the AICA's register writes compared, channel by channel:
# each one's first write that differs, and the first that comes more than `samples` (default 0) later or sooner.
# sndtrace.py --onsets A B: the notes each keys on, matched by their sample's address whatever the channel: how
# many of A's B has within 0.1 s, and how far apart
import bisect
import struct
import sys


def load(path):
    data = open(path, 'rb').read()
    per = {}
    for i in range(0, len(data) - 11, 12):
        t, om, v = struct.unpack_from('<III', data, i)
        off, mask = om & 0xFFFF, om >> 16
        key = off >> 7 if off < 0x2000 else 'common'
        per.setdefault(key, []).append((t, off & 0x7F if off < 0x2000 else off, mask, v))
    return per


def onsets(path):
    data = open(path, 'rb').read()
    regs = [[0] * 64 for _ in range(64)]
    keyed = [0] * 64
    out = []
    for i in range(0, len(data) - 11, 12):
        t, om, v = struct.unpack_from('<III', data, i)
        off, mask = om & 0xFFFF, om >> 16
        if off >= 0x2000:
            continue
        c, r = off >> 7, (off & 0x7F) >> 1
        regs[c][r] = (regs[c][r] & ~mask | v & mask) & 0xFFFF
        if r == 0 and (v & mask & 0x8000):
            for k in range(64):
                on = regs[k][0] >> 14 & 1
                if on and not keyed[k]:
                    out.append((t, (regs[k][0] & 0x7F) << 16 | regs[k][2]))
                keyed[k] = on
    return out


def compare_onsets(a, b):
    near = 4410
    times = {}
    for t, sa in b:
        times.setdefault(sa, []).append(t)
    apart = []
    for t, sa in a:
        ts = times.get(sa, [])
        i = bisect.bisect_left(ts, t)
        d = min((abs(ts[j] - t) for j in (i - 1, i) if 0 <= j < len(ts)), default=None)
        if d is not None and d <= near:
            apart.append(d)
    apart.sort()

    def ms(q):
        return apart[min(len(apart) - 1, int(q * len(apart)))] * 1000.0 / 44100 if apart else 0

    print('%d notes and %d; %d of the first within 0.1 s of the second (%.1f%%): apart median %.1f ms, p90 %.1f, '
          'p99 %.1f' % (len(a), len(b), len(apart), 100.0 * len(apart) / max(1, len(a)), ms(0.5), ms(0.9), ms(0.99)))


def main():
    if sys.argv[1] == '--onsets':
        compare_onsets(onsets(sys.argv[2]), onsets(sys.argv[3]))
        return
    a, b = load(sys.argv[1]), load(sys.argv[2])
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    same = 0
    keys = set(a) | set(b)
    for key in sorted(keys, key=lambda k: (k == 'common', k if k != 'common' else 0)):
        x, y = a.get(key, []), b.get(key, [])
        name = 'common' if key == 'common' else 'channel %2d' % key
        diff = next((i for i in range(min(len(x), len(y))) if x[i][1:] != y[i][1:]), None)
        late = next((i for i in range(min(len(x), len(y)) if diff is None else diff)
                     if abs(x[i][0] - y[i][0]) > tol), None)
        if diff is None and late is None and len(x) == len(y):
            same += 1
            continue
        line = '%s: %d and %d writes' % (name, len(x), len(y))
        if diff is not None:
            line += '; write %d differs: %04x=%04x (mask %04x) at %d against %04x=%04x (mask %04x) at %d' % (
                diff, x[diff][1], x[diff][3], x[diff][2], x[diff][0], y[diff][1], y[diff][3], y[diff][2], y[diff][0])
        elif len(x) != len(y):
            line += '; the same as far as the shorter'
        if late is not None:
            line += '; write %d at sample %d against %d' % (late, x[late][0], y[late][0])
        print(line)
    print('%d of %d the same' % (same, len(keys)))


main()
