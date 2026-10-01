#pragma once
// pf_tables.hpp - HU jam/fold Nash (chip EV) from solvers/pf.py via solvers/export_pf.py.
// Class index r1*13+r2 (ranks 0..12 = 2..A; pair r1==r2, suited r1>r2, offsuit r1<r2).
// Bit k of PF_JAM[c] / PF_CALL[c]: at effective stack PF_STACKS[k] BB the SB jams / the BB calls a jam.
// S= 2 BB: SB jams 90.3% of hands, BB calls 100.0%, SB value +0.010 BB/hand
// S= 3 BB: SB jams 77.7% of hands, BB calls 92.8%, SB value +0.052 BB/hand
// S= 4 BB: SB jams 73.8% of hands, BB calls 73.2%, SB value +0.066 BB/hand
// S= 5 BB: SB jams 71.3% of hands, BB calls 62.0%, SB value +0.056 BB/hand
// S= 6 BB: SB jams 68.6% of hands, BB calls 54.4%, SB value +0.038 BB/hand
// S= 7 BB: SB jams 66.5% of hands, BB calls 48.4%, SB value +0.017 BB/hand
// S= 8 BB: SB jams 62.0% of hands, BB calls 45.4%, SB value -0.004 BB/hand
// S= 9 BB: SB jams 59.9% of hands, BB calls 40.6%, SB value -0.025 BB/hand
// S=10 BB: SB jams 58.4% of hands, BB calls 37.6%, SB value -0.045 BB/hand
// S=12 BB: SB jams 53.5% of hands, BB calls 33.0%, SB value -0.082 BB/hand
// S=15 BB: SB jams 45.7% of hands, BB calls 28.4%, SB value -0.127 BB/hand
// S=20 BB: SB jams 40.3% of hands, BB calls 21.7%, SB value -0.183 BB/hand
// S=25 BB: SB jams 36.0% of hands, BB calls 17.3%, SB value -0.229 BB/hand
namespace pf {
const int PF_NS = 13;
const int PF_STACKS[13] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 15, 20, 25};
const unsigned short PF_JAM[169] = {8191, 0, 0, 0, 0, 0, 0, 1, 1, 7, 63, 511, 8191, 0, 8191, 0, 0, 0, 0, 1, 1, 3, 15, 63, 1023, 8191, 0, 505, 8191, 1, 1, 1, 1, 1, 3, 15, 63, 1023, 8191, 1, 1017, 4095, 8191, 1, 1, 3, 3, 7, 31, 127, 1023, 8191, 1, 49, 2047, 8191, 8191, 511, 63, 7, 15, 31, 255, 2047, 8191, 1, 1, 1023, 4095, 8191, 8191, 1023, 511, 255, 127, 511, 2047, 8191, 1, 1, 319, 2047, 8191, 8191, 8191, 4095, 2047, 1023, 1023, 2047, 8191, 3, 7, 15, 1023, 8191, 8191, 8191, 8191, 8191, 8191, 4095, 4095, 8191, 31, 63, 511, 1023, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 127, 511, 1023, 1023, 2047, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 1023, 1023, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 2047, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191};
const unsigned short PF_CALL[169] = {1023, 1, 1, 1, 1, 1, 1, 3, 3, 7, 15, 127, 2047, 3, 4095, 3, 3, 3, 1, 1, 3, 3, 7, 15, 127, 2047, 3, 3, 8191, 3, 3, 3, 3, 3, 3, 7, 31, 255, 2047, 3, 7, 7, 8191, 3, 3, 3, 3, 3, 15, 31, 511, 4095, 3, 3, 7, 7, 8191, 7, 7, 7, 7, 15, 63, 511, 4095, 3, 3, 7, 7, 15, 8191, 7, 15, 15, 31, 127, 1023, 8191, 3, 3, 7, 7, 15, 31, 8191, 31, 31, 63, 255, 1023, 8191, 3, 7, 7, 7, 15, 31, 127, 8191, 127, 255, 511, 2047, 8191, 7, 7, 15, 15, 31, 63, 255, 511, 8191, 1023, 2047, 4095, 8191, 15, 15, 31, 31, 63, 127, 511, 1023, 2047, 8191, 2047, 8191, 8191, 63, 63, 127, 127, 511, 511, 1023, 2047, 4095, 8191, 8191, 8191, 8191, 511, 511, 1023, 1023, 1023, 2047, 2047, 4095, 8191, 8191, 8191, 8191, 8191, 4095, 4095, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191, 8191};
}  // namespace pf
