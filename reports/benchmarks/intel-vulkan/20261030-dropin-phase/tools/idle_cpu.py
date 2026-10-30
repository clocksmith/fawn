import os, time, sys
HZ = os.sysconf("SC_CLK_TCK")
def procs():
    d = {}
    for p in os.listdir("/proc"):
        if not p.isdigit(): continue
        try:
            s = open(f"/proc/{p}/stat").read(); comm = s[s.index("(")+1:s.rindex(")")]; f = s.rsplit(")", 1)[1].split()
            d[p] = (comm, int(f[11]) + int(f[12]))
        except Exception: pass
    return d
def busy():
    f = [int(x) for x in open("/proc/stat").readline().split()[1:]]; return f[0]+f[1]+f[2]+f[5]+f[6]+f[7]
secs = float(sys.argv[1])
a = procs(); b0 = busy(); t0 = time.time(); time.sleep(secs); b = procs(); b1 = busy(); t = time.time() - t0
tot = {}
for p, (c, v) in b.items():
    tot[c] = tot.get(c, 0) + v - (a[p][1] if p in a else 0)
cap = t * os.cpu_count()
print("window", round(t, 1), "system busy %cap", round(100 * (b1 - b0) / HZ / cap, 2), "sum of process %cap", round(100 * sum(tot.values()) / HZ / cap, 2))
for c, v in sorted(tot.items(), key=lambda x: -x[1])[:10]: print("  ", c, round(100 * v / HZ / cap, 2))
