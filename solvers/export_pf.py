"""Export the HU jam/fold Nash strategy (pf.py's fictitious play) as a C++ header for the bot.

    python3 solvers/export_pf.py            # -> bot/pf_tables.hpp (about 20 s)

For each of the 169 preflop classes (index r1*13+r2, as in eq.c/pf.py) and each effective stack S
in STACKS, one bit says whether the SB jams (J[c] > 0.5) and one whether the BB calls a jam
(C[c] > 0.5).  The header holds two uint16 arrays, bit k = STACKS[k].
"""
import os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
STACKS = [2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25]

src = open(os.path.join(HERE, "pf.py")).read().split("for S in")[0]
exec(src)                                     # defines E, W, cnt, name, solve (as the other solvers do)

jam = np.zeros(169, dtype=np.uint16)
call = np.zeros(169, dtype=np.uint16)
lines = []
for k, S in enumerate(STACKS):
    J, C, v = solve(S)
    jam |= (J > 0.5).astype(np.uint16) << k
    call |= (C > 0.5).astype(np.uint16) << k
    jp = (cnt * (J > 0.5)).sum() / 1326
    cp = (cnt * (C > 0.5)).sum() / 1326
    lines.append(f"// S={S:>2} BB: SB jams {jp:5.1%} of hands, BB calls {cp:5.1%}, SB value {v:+.3f} BB/hand")
    print(lines[-1][3:])

out = os.path.join(REPO, "bot", "pf_tables.hpp")
with open(out, "w") as f:
    f.write("#pragma once\n// pf_tables.hpp - HU jam/fold Nash (chip EV) from solvers/pf.py via solvers/export_pf.py.\n")
    f.write("// Class index r1*13+r2 (ranks 0..12 = 2..A; pair r1==r2, suited r1>r2, offsuit r1<r2).\n")
    f.write("// Bit k of PF_JAM[c] / PF_CALL[c]: at effective stack PF_STACKS[k] BB the SB jams / the BB calls a jam.\n")
    f.write("\n".join(lines) + "\n")
    f.write(f"namespace pf {{\nconst int PF_NS = {len(STACKS)};\nconst int PF_STACKS[{len(STACKS)}] = {{{', '.join(map(str, STACKS))}}};\n")
    f.write("const unsigned short PF_JAM[169] = {" + ", ".join(str(int(x)) for x in jam) + "};\n")
    f.write("const unsigned short PF_CALL[169] = {" + ", ".join(str(int(x)) for x in call) + "};\n}  // namespace pf\n")
print("wrote", os.path.relpath(out, REPO), os.path.getsize(out), "bytes")
