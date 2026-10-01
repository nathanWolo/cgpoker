"""Regression check: run every solver script and compare its key output lines with the numbers in solvers/README.md.

    python3 solvers/check.py           # ~20 s: eq.c smoke run (200 trials/pair) + all Python solvers
    python3 solvers/check.py --full    # also rebuild eq169.bin at 20,000 trials/pair (~50 s) and cmp it with the committed file

Exit status 0 when every expected line is present. Runs from any working directory; eq.c is compiled into build/solvers/.
"""
import argparse, filecmp, os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BUILD = os.path.join(REPO, "build", "solvers")

# (argv, substrings that must appear in stdout); numbers as documented in solvers/README.md
CHECKS = [
    (["pf.py"], ["S= 5BB  SB jam 71.4%  BB call 62.1%  SB EV of jam/fold eq = +0.056",
                 "S= 7BB  SB jam 66.2%  BB call 48.5%  SB EV of jam/fold eq = +0.017",
                 "S=10BB  SB jam 58.3%  BB call 37.5%  SB EV of jam/fold eq = -0.045",
                 "S=15BB  SB jam 45.7%  BB call 28.2%  SB EV of jam/fold eq = -0.127",
                 "S=20BB  SB jam 40.2%  BB call 21.7%  SB EV of jam/fold eq = -0.183"]),
    (["exploit.py"], ["S=10BB vs nit calls top 15%     : SB Nash +0.077 BB/hand, SB best response +0.355 (jam 100%), gain +0.278",
                      "S=10BB vs station calls top 70% : SB Nash +0.120 BB/hand, SB best response +0.192 (jam 48%), gain +0.072",
                      "S=15BB vs nit calls top 15%     : SB Nash -0.088 BB/hand, SB best response +0.110 (jam 100%), gain +0.198"]),
    (["mmdstep2.py"], ["10BB vs nit top15%     eta=     3: gain vs model +0.042 BB/hand | worst-case value -0.067",
                       "10BB vs nit top15%     eta=    10: gain vs model +0.252 BB/hand | worst-case value -0.328",
                       "10BB vs station top70% eta=    10: gain vs model +0.062 BB/hand | worst-case value -0.060"]),
    (["mmd_ab.py", "0"], ["10BB vs nit top15%     eta=    10: gain vs model +0.261 BB/hand | worst-case value -0.343"]),
    (["mmd_ab.py", "0.02"], ["10BB vs nit top15%     eta=    10: gain vs model +0.252 BB/hand | worst-case value -0.328"]),
    (["icm2.py"], ["3p equal 0.6\n", "4p equal 0.646\n", "4p (600,1800,1800,600) big vs big 0.737\n",
                   "4p (2400,1200,800,400) big calls short 0.519\n"]),
    (["trueskill_payouts.py"], ["3p equal ratings (mu=25, sigma=8.33): (1.0, 0.5, 0.0)",
                                "4p equal ratings (mu=25, sigma=8.33): (1.0, 0.6444, 0.3556, 0.0)",
                                "4p unequal, me mu=30 vs 29/28/27 (sigma=1.00): (1.0, 0.6233, 0.3255, 0.0)"]),
]


def run(argv):
    t = time.time()
    r = subprocess.run(argv, capture_output=True, text=True)
    return r, time.time() - t


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--full", action="store_true", help="also rebuild eq169.bin at 20k trials/pair and compare bytes")
    a = ap.parse_args()
    fails = 0

    os.makedirs(BUILD, exist_ok=True)
    eq = os.path.join(BUILD, "eq")
    subprocess.run(["gcc", "-O2", "-o", eq, os.path.join(HERE, "eq.c")], check=True)
    r, dt = run([eq, "200", os.path.join(BUILD, "eq169_200.bin")])
    ok = r.returncode == 0 and "AA vs KK 0.8" in r.stdout
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'}  eq 200 (smoke run, {dt:.1f} s)")
    if a.full:
        out = os.path.join(BUILD, "eq169_full.bin")
        r, dt = run([eq, "20000", out])
        ok = r.returncode == 0 and filecmp.cmp(out, os.path.join(HERE, "eq169.bin"), shallow=False)
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'}  eq 20000 byte-identical to solvers/eq169.bin ({dt:.1f} s)")

    for argv, expected in CHECKS:
        r, dt = run([sys.executable, os.path.join(HERE, argv[0])] + argv[1:])
        missing = [e for e in expected if e not in r.stdout]
        ok = r.returncode == 0 and not missing
        fails += not ok
        print(f"{'PASS' if ok else 'FAIL'}  {' '.join(argv)} ({len(expected)} lines, {dt:.1f} s)")
        for m in missing:
            print("      missing:", m.strip())
        if r.returncode:
            print("      exit status", r.returncode, r.stderr.strip().splitlines()[-1:] )
    print("solvers-check:", "all passed" if not fails else f"{fails} FAILED")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
