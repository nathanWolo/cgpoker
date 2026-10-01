#pragma GCC optimize("O3")
#pragma GCC optimization("unroll-loops")
// main.cpp - CodinGame entry point: stdin -> bot::Bot -> stdout.  Bundle with tools/bundle.py.
//
// Dev builds (DEBUG below) print per-turn diagnostics to stderr; the first turn also prints the
// platform probes the plan's M0 asks for: __cplusplus, evaluator start-up time, Monte Carlo
// trials per second, and (PONDER) whether a background thread makes progress between turns.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#pragma GCC target("avx2,bmi,bmi2,lzcnt,popcnt")

// ---- bot/bot.hpp
// bot.hpp - the CodinGame Poker bot's decision logic (M0 baseline).  Used by main.cpp (stdin/stdout)
// and by the arena (in-process).  No macros: the minifier renames them inconsistently.
//
// M0 policy: a state tracker (engine/tracker.hpp) replays the hand exactly; Monte Carlo equity
// against uniform random hands (pe7c evaluator) versus pot odds with this game's dead-money pots;
// HU jam/fold Nash thresholds (pf_tables.hpp) at short effective stacks.  Later milestones replace
// the policy, not the plumbing.
#include <chrono>
#include <cstdint>
#include <string>

// ---- cpp/pe7c.hpp
// pe7c.hpp - 5..7-card Texas hold'em hand evaluator, CodinGame-ready (the one to ship).
//
// Usage: pe::init() once (builds every table: ~80 ms with CodinGame's flags here, see README.md),
// then build hands incrementally from pe::E:  H h = add(add(E, c0), c1); ... ; u16 v = ev(h);
// Card c = 4*rank + suit, rank 0..12 = 2..A, suit 0..3.  ev() is defined for 5, 6 or 7 distinct
// cards and returns 1..7462, higher is better: the hand's rank among the standard 7,462
// five-card equivalence classes (1..1277 high card, 1278..4137 pair, 4138..4995 two pair,
// 4996..5853 trips, 5854..5863 straight, 5864..7140 flush, 7141..7296 full house,
// 7297..7452 quads, 7453..7462 straight flush).  add(H, H) combines two hands that were each
// built from E (e.g. board + hole cards).  Tables are plain globals: include from one TU only.
//
// CodinGame form: no std containers and no payload (CG compiles at -O0 and the source's
// `#pragma GCC optimize("O3")` only optimizes function bodies, so hot helpers are
// always_inline).  No preprocessor macros either: crossfish's tools/cg_minify.py renames
// macro names inconsistently, which is why the earlier `#define AI` / PE_SH version failed
// to compile after minification.  `#pragma once` must stay on line 1: the minifier strips
// it only there.  pe7.hpp is the readable std::vector reference version of the same method.
//
// Attribution: the method follows OMPEval by Timo A. (https://github.com/zekyll/OMPEval,
// ISC license; notice reproduced in cpp/THIRD_PARTY.md).  Taken from OMPEval: the 13
// additive rank keys RK[] (OMPEval's RANKS constants, chosen so every multiset of 0..7 ranks
// has a unique sum), the hand layout (rank-key sum plus 4-bit suit counters that start at 3,
// so `& 0x8888` flags a flush, plus 16-bit per-suit rank masks), the 8192-entry flush table,
// and the perfect hash from rank key to class by row displacement (rows placed largest first
// at the lowest offset that does not conflict; OMPEval's PERF_HASH_ROW_OFFSETS scheme).
// Differences: rows are 2^8 keys wide instead of OMPEval's 2^12 so the hash can be rebuilt at
// start-up (OMPEval ships a precomputed 102 KB offset table instead), and ev() returns the
// dense 1..7462 class instead of OMPEval's category*4096 + rank.
#include <cstdint>
#include <cstring>
namespace pe {
typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16;
const u32 RK[13] = {0x2000, 0x8001, 0x11000, 0x3a000, 0x91000, 0x176005, 0x366000,
                    0x41a013, 0x47802e, 0x479068, 0x48c0e4, 0x48f211, 0x494493};
enum { SH = 8, NR = ((4 * 0x494493 + 3 * 0x48f211) >> SH) + 1, LN = 1 << 18, NM = 73775 };
u16 FL[8192], LK[LN]; u32 OFF[NR];
struct H { u64 k, m; };
H C[52]; const H E = {0x3333ull << 32, 0};
__attribute__((always_inline)) inline H add(H a, int c) { return {a.k + C[c].k, a.m | C[c].m}; }
__attribute__((always_inline)) inline H add(H a, H b) { return {a.k + b.k - E.k, a.m | b.m}; }
__attribute__((always_inline)) inline u16 ev(const H& h) {
  u64 f = h.k >> 32 & 0x8888;
  if (f) return FL[h.m >> (4 * __builtin_ctzll(f) & ~15) & 0x1fff];
  u32 k = h.k; return LK[k + OFF[k >> SH]];
}
int st(u32 m) { m = m << 1 | (m >> 12 & 1); for (int r = 13; r > 3; r--) if ((m >> (r - 4) & 31) == 31) return r - 1; return -1; }
u32 tp(u32 m, int n) { u32 v = 0; for (int r = 12; r >= 0 && n; r--) if (m >> r & 1) v = v << 4 | r, n--; while (n--) v <<= 4; return v; }
int hi(u32 m) { return 31 - __builtin_clz(m); }
u32 slowc(int* c) {                                  // non-flush code from rank counts
  u32 a = 0, q = 0, t = 0, p = 0;
  for (int r = 0; r < 13; r++) { u32 b = 1u << r; if (c[r]) a |= b; if (c[r] == 4) q |= b; if (c[r] == 3) t |= b; if (c[r] == 2) p |= b; }
  if (q) { int r = hi(q); return 7 << 20 | r << 16 | tp(a & ~(1u << r), 1) << 12; }
  if (t && (t & (t - 1) || p)) { int r = hi(t); return 6 << 20 | r << 16 | hi(t & ~(1u << r) | p) << 12; }
  int s = st(a); if (s >= 0) return 4 << 20 | s << 16;
  if (t) { int r = hi(t); return 3 << 20 | r << 16 | tp(a & ~(1u << r), 2) << 8; }
  if (p & (p - 1)) { int x = hi(p), y = hi(p & ~(1u << x)); return 2 << 20 | x << 16 | y << 12 | tp(a & ~(1u << x) & ~(1u << y), 1) << 8; }
  if (p) { int x = hi(p); return 1 << 20 | x << 16 | tp(a & ~(1u << x), 3) << 4; }
  return tp(a, 5);
}
u32 slowf(u32 m) { int s = st(m); return s >= 0 ? 8 << 20 | s << 16 : 5 << 20 | tp(m, 5); }
u32 KY[NM], CD[NM], nk, RS[NR + 1], RE[NM], NX[LN + 1]; int cn[13]; u64 BM[(9 << 20) / 64 + 1]; u32 PR[(9 << 20) / 64 + 1];
void rec(int r, int n, u32 key) {
  if (r == 13) { if (n > 4) KY[nk] = key, CD[nk++] = slowc(cn); return; }
  for (int k = 0; k < 5 && n + k < 8; k++) cn[r] = k, rec(r + 1, n + k, key + k * RK[r]);
  cn[r] = 0;
}
__attribute__((always_inline)) inline u16 cls(u32 c) { return PR[c >> 6] + __builtin_popcountll(BM[c >> 6] & ((1ull << (c & 63)) - 1)) + 1; }
u32 fnd(u32 i) { while (NX[i] != i) i = NX[i] = NX[NX[i]]; return i; }
void init() {
  for (int c = 0; c < 52; c++) C[c] = {RK[c >> 2] + (1ull << (32 + 4 * (c & 3))), 1ull << (16 * (c & 3) + (c >> 2))};
  rec(0, 0, 0);
  for (u32 i = 0; i < nk; i++) BM[CD[i] >> 6] |= 1ull << (CD[i] & 63);           // dense ranks via bitmap
  for (u32 m = 0; m < 8192; m++) if (__builtin_popcount(m) > 4) { u32 c = slowf(m); BM[c >> 6] |= 1ull << (c & 63); }
  for (u32 i = 1; i < sizeof BM / 8; i++) PR[i] = PR[i - 1] + __builtin_popcountll(BM[i - 1]);
  for (u32 m = 0; m < 8192; m++) FL[m] = __builtin_popcount(m) > 4 ? cls(slowf(m)) : 0;
  for (u32 i = 0; i < nk; i++) RS[(KY[i] >> SH) + 1]++;                            // bucket keys by row
  for (u32 i = 0; i < NR; i++) RS[i + 1] += RS[i];
  static u32 fill[NR]; memcpy(fill, RS, sizeof fill);
  for (u32 i = 0; i < nk; i++) RE[fill[KY[i] >> SH]++] = i;
  for (u32 i = 0; i <= LN; i++) NX[i] = i;
  for (int sz = 64; sz > 0; sz--) for (u32 r = 0; r < NR; r++) if (RS[r + 1] - RS[r] == (u32)sz) {  // largest rows first
    u32 m = (1 << SH) - 1, e0 = KY[RE[RS[r]]] & m, o;
    for (u32 f = fnd(e0);; f = fnd(f + 1)) {
      o = f - e0; u32 j = RS[r];
      for (; j < RS[r + 1]; j++) { u32 i = RE[j]; u16 v = LK[(KY[i] & m) + o]; if (v && v != cls(CD[i])) break; }
      if (j == RS[r + 1]) break;
    }
    for (u32 j = RS[r]; j < RS[r + 1]; j++) { u32 i = RE[j], s = (KY[i] & m) + o; LK[s] = cls(CD[i]); NX[s] = s + 1; }
    OFF[r] = o - (r << SH);
  }
}
}  // namespace pe
// ---- engine/tracker.hpp
// tracker.hpp - the bot's state tracker: a pk::Board driven by the stdin lines.
//
// Stdin is one snapshot at our turn, so the tracker replays everything that happened since the
// last turn through the same rules the referee uses: it starts each hand (positions, blinds), applies
// every action line (an ALL-IN line carries no amount: the Board knows the stack), handles NONE
// rounds, settles finished hands from their showdown line (all hole cards are revealed), and then
// checks itself against the snapshot: round, hand, stacks, chips in pot, board, and the offered
// actions, which pins down the minimum raise and the raise cap.  engine/check.py runs a Tracker per
// seat alongside the engine on every fuzz and replay decision.
//
// After apply(): players[], board, pot, blinds, positions, next_player (== me) and
// possible_actions() describe the current decision; hole() gives our cards.
#include <sstream>

// ---- engine/poker_engine.hpp
// poker_engine.hpp - C++ port of the CodinGame "Poker" referee (wala-fr/CodingamePoker @ ac30d97),
// method for method the same as sim/poker_sim.py, which is the validated Python port.
//
// Board holds the rules (blinds, betting, action replacement, side pots, elimination ranks) with no
// deck and no RNG, so the bot's tracker (tracker.hpp) can drive it from stdin.  Engine adds the deck,
// the inputs and the Referee.gameTurn loop.
//
// Game loop: Engine eng(n, seed); eng.run(agents) with one Agent per seat.  Agent::act receives the
// Obs the real bot would get on stdin (Obs::to_stdin renders the exact text) and returns the bot's
// raw output line, or false for a timeout.  Rounds, the 600-decision cap with refund, NONE rounds,
// blind posting (every non-BB player posts the SB), action replacement, side pots, the odd chip,
// elimination ranks and the SHA1PRNG deck are all reproduced; engine/check.py proves it against
// poker_sim.py (random-action fuzz) and against the 381 recorded games.
//
// Cards are ints 0..51 = 4*rank + suit, rank 0..12 = 2..A, suit 0..3 = C,D,H,S (the referee's
// CardUtils order and pe7c's encoding).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// ---- engine/sha1prng.hpp
// sha1prng.hpp - bit-exact port of Java's SecureRandom("SHA1PRNG") (sun.security.provider.SecureRandom),
// java.util.Random.nextInt(bound) and Collections.shuffle, as used by the CodinGame SDK
// (MultiplayerGameManager.getRandom()) and the Poker referee's Deck.  Mirrors sim/poker_sim.py.
#include <cstdint>
#include <cstring>
#include <vector>

namespace pk {

struct Sha1 {
  static void digest(const uint8_t* msg, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> m(msg, msg + len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (8 * i)));
    for (size_t off = 0; off < m.size(); off += 64) {
      uint32_t w[80];
      for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)m[off + 4 * i] << 24 | (uint32_t)m[off + 4 * i + 1] << 16 |
               (uint32_t)m[off + 4 * i + 2] << 8 | m[off + 4 * i + 3];
      for (int i = 16; i < 80; i++) {
        uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = x << 1 | x >> 31;
      }
      uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
      for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999u;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1u;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDCu;
        else f = b ^ c ^ d, k = 0xCA62C1D6u;
        uint32_t t = (a << 5 | a >> 27) + f + e + k + w[i];
        e = d; d = c; c = b << 30 | b >> 2; b = a; a = t;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++)
      out[4 * i] = h[i] >> 24, out[4 * i + 1] = h[i] >> 16, out[4 * i + 2] = h[i] >> 8, out[4 * i + 3] = h[i];
  }
};

class Sha1Prng {
 public:
  explicit Sha1Prng(int64_t seed) {
    uint8_t sb[8];                                   // SecureRandom.longToByteArray: little-endian
    for (int i = 0; i < 8; i++) sb[i] = (uint8_t)((uint64_t)seed >> (8 * i));
    Sha1::digest(sb, 8, state_);
  }
  // java.util.Random.next(bits) as implemented by SHA1PRNG.engineNextBytes
  uint32_t next(int bits) {
    int nb = (bits + 7) / 8;
    uint64_t v = 0;
    for (int i = 0; i < nb; i++) v = v << 8 | next_byte();
    return (uint32_t)(v >> (nb * 8 - bits));
  }
  int next_int(int bound) {                          // java.util.Random.nextInt(bound)
    uint32_t r = next(31);
    int m = bound - 1;
    if ((bound & m) == 0) return (int)(((int64_t)bound * (int64_t)r) >> 31);
    int32_t u = (int32_t)r;
    for (;;) {
      int32_t rr = u % bound;
      if ((int64_t)u - rr + m < (1LL << 31)) return rr;
      u = (int32_t)next(31);
    }
  }
  template <class T>
  void shuffle(std::vector<T>& v) {                   // Collections.shuffle, RandomAccess branch
    for (int i = (int)v.size(); i > 1; i--) {
      int j = next_int(i);
      std::swap(v[i - 1], v[j]);
    }
  }

 private:
  uint8_t state_[20];
  uint8_t buf_[20];
  int buf_pos_ = 20;
  uint8_t next_byte() {
    if (buf_pos_ == 20) next_block(), buf_pos_ = 0;
    return buf_[buf_pos_++];
  }
  void next_block() {
    Sha1::digest(state_, 20, buf_);
    // updateState: Java adds *signed* bytes; the carry is v >> 8 and can be -1
    int last = 1;
    bool zf = false;
    for (int i = 0; i < 20; i++) {
      int sv = (int8_t)state_[i], ov = (int8_t)buf_[i];
      int v = sv + ov + last;
      uint8_t t = (uint8_t)(v & 0xFF);
      zf |= state_[i] != t;
      state_[i] = t;
      last = v >> 8;
    }
    if (!zf) state_[0]++;
  }
};

}  // namespace pk

namespace pk {

// ----------------------------------------------------------------------------- parameters
constexpr int SMALL_BLIND = 5, BIG_BLIND = 10;    // model/variable/Parameter.java
constexpr int TOTAL_BUY_IN = 4800;
constexpr int HAND_NB_BY_LEVEL = 10, LEVEL_MULT = 2;
constexpr int RAISE_CAP = 10;
constexpr int MAX_TURN = 600;                      // game/RefereeParameter.java (decisions per game)
constexpr int MAX_REFEREE_TURN = 10000;            // frames
constexpr const char* RANKS = "23456789TJQKA";
constexpr const char* SUITS = "CDHS";

inline std::string card_str(int c) { return std::string{RANKS[c >> 2], SUITS[c & 3]}; }
inline int card_from(const char* s) {               // "AS" -> 51; -1 if not a card
  const char* r = std::strchr(RANKS, s[0]);
  const char* u = std::strchr(SUITS, s[1]);
  if (!s[0] || !s[1] || !r || !u) return -1;
  return (int)(r - RANKS) * 4 + (int)(u - SUITS);
}

// ----------------------------------------------------------------------------- hand evaluation
// Same ordering as poker_sim.eval5 (standard rankings, wheel = 5-high straight): category<<20 then
// the tuple's rank fields in 4-bit nibbles, so two values compare like the Python tuples.
inline uint32_t eval5(const int* c) {
  int rs[5], cnt[15] = {0};
  bool flush = true;
  for (int i = 0; i < 5; i++) rs[i] = (c[i] >> 2) + 2, cnt[rs[i]]++, flush &= (c[i] & 3) == (c[0] & 3);
  std::sort(rs, rs + 5, [](int a, int b) { return a > b; });
  int uniq = 0;
  for (int r = 14; r >= 2; r--) uniq += cnt[r] > 0;
  int straight_hi = 0;
  if (uniq == 5) {
    if (rs[0] - rs[4] == 4) straight_hi = rs[0];
    else if (rs[0] == 14 && rs[1] == 5 && rs[4] == 2) straight_hi = 5;
  }
  // counts sorted by (count, rank) descending
  int shape[5], ordered[5], k = 0;
  for (int n = 4; n >= 1; n--)
    for (int r = 14; r >= 2; r--)
      if (cnt[r] == n) shape[k] = n, ordered[k] = r, k++;
  auto pack = [](int cat, const int* v, int n) {
    uint32_t x = (uint32_t)cat << 20;
    for (int i = 0; i < n; i++) x |= (uint32_t)v[i] << (16 - 4 * i);
    return x;
  };
  if (straight_hi && flush) return pack(8, &straight_hi, 1);
  if (k == 2 && shape[0] == 4) return pack(7, ordered, 2);
  if (k == 2 && shape[0] == 3) return pack(6, ordered, 2);
  if (flush) return pack(5, rs, 5);
  if (straight_hi) return pack(4, &straight_hi, 1);
  if (k == 3 && shape[0] == 3) return pack(3, ordered, 3);
  if (k == 3 && shape[0] == 2) return pack(2, ordered, 3);
  if (k == 4) return pack(1, ordered, 4);
  return pack(0, rs, 5);
}

inline uint32_t best7(const int* hole, const std::vector<int>& board) {  // best of C(7,5)
  int all[7] = {hole[0], hole[1], board[0], board[1], board[2], board[3], board[4]};
  uint32_t best = 0;
  int five[5];
  for (int a = 0; a < 7; a++)
    for (int b = a + 1; b < 7; b++) {
      int k = 0;
      for (int i = 0; i < 7; i++)
        if (i != a && i != b) five[k++] = all[i];
      best = std::max(best, eval5(five));
    }
  return best;
}

// ----------------------------------------------------------------------------- model
struct Player {
  int id = 0, stack = 0;
  int total = 0;            // totalBetAmount (whole hand) == "chipInPot" on stdin
  int rnd = 0;              // roundBetAmount (current street)
  bool folded = false, allin = false, spoken = false, eliminated = false, timeout = false;
  int elim_rank = -1, score = 0;
  int hand[2] = {-1, -1};
  int n_hand = 0;
  bool can_act() const { return !folded && !allin; }
};

struct Obs {
  // init (meaningful on the first call for this player)
  int small_blind = 0, big_blind = 0, hand_nb_by_level = 0, level_mult = 0, buy_in = 0, first_bb_id = 0, player_nb = 0, player_id = 0;
  // per turn
  int round = 0, hand_nb = 0;
  std::vector<int> stacks, chip_in_pot;
  std::string board, cards;
  std::vector<std::string> actions;    // "round handNb playerId ACTION BOARD"
  std::vector<std::string> showdowns;  // "handNb BOARD CARDS"
  std::vector<std::string> possible;

  // The exact stdin text the referee sends for this decision (InputSender.sendInputs).
  std::string to_stdin(bool first) const {
    std::string s;
    auto line = [&](const std::string& x) { s += x; s += '\n'; };
    auto num = [&](int x) { line(std::to_string(x)); };
    if (first) {
      num(small_blind); num(big_blind); num(hand_nb_by_level); num(level_mult);
      num(buy_in); num(first_bb_id); num(player_nb); num(player_id);
    }
    num(round); num(hand_nb);
    for (size_t i = 0; i < stacks.size(); i++) line(std::to_string(stacks[i]) + " " + std::to_string(chip_in_pot[i]));
    line(board); line(cards);
    num((int)actions.size()); for (auto& a : actions) line(a);
    num((int)showdowns.size()); for (auto& a : showdowns) line(a);
    num((int)possible.size()); for (auto& a : possible) line(a);
    return s;
  }
};

struct Agent {
  virtual ~Agent() {}
  // Fill `out` with the bot's raw output line (may include ";message"); return false for a timeout.
  virtual bool act(const Obs& obs, std::string& out) = 0;
};

enum ActType { A_FOLD, A_CHECK, A_ALL_IN, A_BET, A_CALL, A_TIMEOUT };
inline const char* act_name(ActType t) {
  static const char* n[] = {"FOLD", "CHECK", "ALL-IN", "BET", "CALL", "TIMEOUT"};
  return n[t];
}

struct RoundInfo { int turn, hand, pid; std::string shown, board; };
struct LogEntry { int turn, hand, pid; std::string shown; };

// The rules of one table: everything in the referee's Board/ActionUtils/WinningCalculator that does
// not touch the deck.  Card identities come from draw(), which a Board alone does not know (-1).
class Board {
 public:
  int n = 0;
  std::vector<Player> players;
  int sb = SMALL_BLIND, bb = BIG_BLIND, level = 1, hand_nb = 0, bb_id = 0, sb_id = -1, dealer_id = -1;
  bool over = true, calc_winnings = false, deal_card = false, calc_next = false, game_over = false, cancelled = false;
  int turn = 0;
  int pot = 0;
  std::vector<int> board;                            // card ids; -1 = dealt but not yet known (tracker)
  int last_round_raise = 0, last_total_round_bet = 0, last_raiser = -1, raise_nb = 0, last_player = -1, next_player = -1;

  Board() {}
  Board(int n_, int first_bb) : n(n_) {
    if (n < 2 || n > 4) throw std::invalid_argument("n must be 2..4");
    players.resize(n);
    for (int i = 0; i < n; i++) players[i].id = i, players[i].stack = TOTAL_BUY_IN / n;
    bb_id = first_bb;
  }
  virtual ~Board() {}
  virtual int draw() { return -1; }

  // ------------------------------------------------------------------ Board helpers
  void reset_round() {
    for (auto& p : players) p.rnd = 0, p.spoken = false;
    last_round_raise = 0; last_total_round_bet = 0; last_raiser = -1; raise_nb = 0;
  }
  void reset_hand() {
    board.clear();
    for (auto& p : players) {
      p.n_hand = 0; p.hand[0] = p.hand[1] = -1;
      p.eliminated = p.stack == 0;
      p.allin = false;
      p.folded = p.stack == 0;
      p.total = 0;
    }
    over = false; pot = 0;
    reset_round();
    hand_nb++;
    if (hand_nb % HAND_NB_BY_LEVEL == 0) level++, sb *= LEVEL_MULT, bb *= LEVEL_MULT;   // Board.increaseLevel
    init_positions();
  }
  void init_positions() {
    if (hand_nb > 1) {                               // calculateNextBigBlindId
      do bb_id = (bb_id + 1) % n; while (players[bb_id].stack == 0);
    }
    sb_id = dealer_id = -1;
    int nb = 0;
    for (int i = 0; i < n; i++) {
      int idx = ((bb_id - 1 - i) % n + n) % n;
      if (!players[idx].folded) {
        nb++;
        if (sb_id == -1) sb_id = idx;
        else if (dealer_id == -1) dealer_id = idx;
      }
    }
    if (nb == 2) dealer_id = sb_id;
    last_player = -1; next_player = bb_id;
    last_total_round_bet = bb; last_round_raise = sb; last_raiser = -1; raise_nb = 1;
  }
  void init_blind() {
    // every non-folded player posts at least the SMALL blind (Board.initBlind)
    for (auto& p : players)
      if (!p.folded) {
        int bet = p.id == bb_id ? bb : sb;
        if (p.stack < (p.id == sb_id ? sb : bb)) bet = p.stack;
        bet_chips(p, bet);
      }
  }
  bool is_first_bet() const { return last_raiser == -1; }
  void bet_chips(Player& p, int value) {
    value = std::min(value, p.stack);
    p.stack -= value; p.total += value; p.rnd += value;
    if (p.stack == 0) p.allin = true;
    int raise = p.rnd - last_total_round_bet;
    if ((is_first_bet() && raise >= bb) || (!is_first_bet() && raise >= last_round_raise)) {
      last_round_raise = raise; last_raiser = p.id; last_total_round_bet += raise; raise_nb++;
    }
    pot += value;
  }
  void deal_first() {
    for (int k = 0; k < 2; k++)
      for (int i = 0; i < n; i++) {
        Player& p = players[(dealer_id + 1 + i) % n];
        if (!p.folded) p.hand[p.n_hand++] = draw();
      }
  }
  int max_total() const { int m = 0; for (auto& p : players) m = std::max(m, p.total); return m; }
  bool no_more_can_act() const {
    int k = 0;
    for (auto& p : players) k += p.can_act();
    if (k <= 1) {
      int mx = max_total();
      for (auto& p : players) if (p.can_act() && p.total < mx) return false;
      return true;
    }
    return false;
  }
  void calculate_next_player() {
    int idx = next_player + 1;
    next_player = -1;
    if (!no_more_can_act())
      for (int i = 0; i < n; i++, idx++) {
        const Player& p = players[idx % n];
        if (p.can_act()) { next_player = p.id; break; }
      }
  }
  int not_folded() const { int k = 0; for (auto& p : players) k += !p.folded; return k; }
  bool is_turn_over() const {
    int mx = max_total();
    for (auto& p : players)
      if (p.can_act()) {
        if (!p.spoken) {
          bool other = false;
          for (auto& q : players) other |= q.can_act() && q.id != p.id;
          if (!other) { if (p.total < mx) return false; }
          else return false;
        }
        if (p.total < mx) return false;
      }
    return true;
  }
  void end_turn() {
    if (next_player != -1) last_player = next_player, players[next_player].spoken = true;
    deal_card = false;
    if (not_folded() == 1) calc_winnings = true;
    else if (is_turn_over()) {
      if (no_more_can_act()) {
        while (board.size() < 5) deal_board_cards();
        calc_winnings = true;
      } else if (board.size() == 5) calc_winnings = true;
      else { deal_card = true; last_player = -1; next_player = dealer_id; }
    }
  }
  void deal_board_cards() {
    draw();                                           // burn
    int k = board.empty() ? 3 : 1;
    for (int i = 0; i < k; i++) board.push_back(draw());
    reset_round();
  }
  bool deal() {
    bool ret = deal_card;
    if (ret) deal_board_cards();
    deal_card = false;
    return ret;
  }
  bool preflop() const { return board.empty(); }
  int call_amount(const Player& p) const {
    if (preflop()) return std::max(bb, max_total()) - p.total;
    return max_total() - p.total;
  }
  bool check_possible() const { return call_amount(players[next_player]) == 0; }
  bool raise_cap() const { return raise_nb > RAISE_CAP; }

  // ------------------------------------------------------------------ ActionUtils
  std::vector<std::string> possible_actions() const {
    int pid = next_player;
    const Player& p = players[pid];
    int call = call_amount(p);
    bool cap = raise_cap();
    std::vector<std::string> out;
    bool allin = false;
    if (call > 0) {
      if (call < p.stack) out.push_back("CALL");
      else if (!cap) out.push_back("ALL-IN"), allin = true;
    }
    if (last_raiser != pid && !cap) {
      if (!allin) out.push_back("ALL-IN");
      int min_raise = is_first_bet() ? bb : last_round_raise;
      int min_amount = min_raise + last_total_round_bet - p.rnd;
      if (min_amount < p.stack) out.push_back("BET_" + std::to_string(min_amount));
    }
    if (check_possible()) out.push_back("CHECK");
    out.push_back("FOLD");
    return out;
  }

  // Action.create / ActionInfo.create on the first ';'-part of the output, upper-cased and trimmed.
  // Returns {type, amount, error}; an unparseable string is FOLD with error = true.
  struct Parsed { ActType t; int amount; bool err; };
  static Parsed parse(std::string s) {
    // Referee: outputs[0].toUpperCase().trim()
    for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    s = s.substr(a, b - a);
    if (s.rfind("BET_", 0) == 0) {                   // Action.create: replaceAll("BET_", "BET ")
      std::string t;
      for (size_t i = 0; i < s.size();) {
        if (s.compare(i, 4, "BET_") == 0) t += "BET ", i += 4; else t += s[i++];
      }
      s = t;
    }
    std::vector<std::string> tok;                    // split(" ", -1): keeps empty tokens
    size_t start = 0;
    for (;;) {
      size_t sp = s.find(' ', start);
      tok.push_back(s.substr(start, sp == std::string::npos ? std::string::npos : sp - start));
      if (sp == std::string::npos) break;
      start = sp + 1;
    }
    ActType t;
    const std::string& h = tok[0];
    if (h == "FOLD") t = A_FOLD; else if (h == "CHECK") t = A_CHECK;
    else if (h == "ALL-IN" || h == "ALL_IN") t = A_ALL_IN; else if (h == "BET") t = A_BET;
    else if (h == "CALL") t = A_CALL; else if (h == "TIMEOUT") t = A_TIMEOUT;
    else return {A_FOLD, 0, true};
    if (tok.size() > 2) return {A_FOLD, 0, true};
    if (t == A_BET) {
      if (tok.size() != 2) return {A_FOLD, 0, true};
      int64_t v = 0; bool neg = false; size_t i = 0;
      const std::string& d = tok[1];                 // Integer.parseInt: optional sign, ASCII digits
      if (!d.empty() && (d[0] == '-' || d[0] == '+')) neg = d[0] == '-', i = 1;
      if (i >= d.size()) return {A_FOLD, 0, true};
      for (; i < d.size(); i++) {
        if (d[i] < '0' || d[i] > '9') return {A_FOLD, 0, true};
        v = v * 10 + (d[i] - '0');
        if (v > (1LL << 31)) return {A_FOLD, 0, true};
      }
      if (neg) v = -v;
      if (v < -(1LL << 31) || v >= (1LL << 31)) return {A_FOLD, 0, true};
      return {A_BET, (int)v, false};
    }
    if (tok.size() > 1) return {A_FOLD, 0, true};
    return {t, 0, false};
  }

  // ActionUtils.calculatePossibleBet: replacement of illegal actions
  std::pair<ActType, int> replace(ActType t, int amount) const {
    if (t == A_FOLD || t == A_TIMEOUT) return {t, amount};
    int pid = next_player;
    const Player& p = players[pid];
    int call = call_amount(p);
    bool cap = raise_cap();
    std::pair<ActType, int> call_action = call < p.stack ? std::make_pair(A_CALL, 0) : std::make_pair(A_ALL_IN, 0);
    if (t == A_CALL) {
      if (check_possible()) return {A_CHECK, 0};
      if (call >= p.stack) return {A_ALL_IN, 0};
      return {A_CALL, 0};
    }
    if (t == A_ALL_IN) {
      std::pair<ActType, int> nw = {A_ALL_IN, 0};
      if (last_raiser == pid && call < p.stack) nw = {A_CALL, 0};
      if (cap) nw = call_action;
      return nw;
    }
    if (t == A_BET) {
      if (amount <= 0) return {A_FOLD, 0};
      if (amount <= call) return call_action;
      if (last_raiser == pid || cap) return call_action;
      if (amount >= p.stack) return {A_ALL_IN, 0};
      int raise = p.rnd + amount - last_total_round_bet;
      int min_raise = is_first_bet() ? bb : last_round_raise;
      if (raise < min_raise) {
        int nb = min_raise + last_total_round_bet - p.rnd;
        return nb >= p.stack ? std::make_pair(A_ALL_IN, 0) : std::make_pair(A_BET, nb);
      }
      return {A_BET, amount};
    }
    // CHECK
    if (!check_possible()) return {A_FOLD, 0};
    return {A_CHECK, 0};
  }

  void do_action(ActType t, int amount) {
    Player& p = players[next_player];
    switch (t) {
      case A_FOLD: p.folded = true; break;
      case A_CHECK: break;
      case A_ALL_IN: bet_chips(p, p.stack); break;
      case A_CALL: bet_chips(p, call_amount(p)); break;
      case A_BET: bet_chips(p, amount); break;
      case A_TIMEOUT: p.stack = 0; p.timeout = true; p.folded = true; break;
    }
  }

  // ------------------------------------------------------------------ winnings
  bool calculate_player_winnings() {
    if (!calc_winnings) return false;
    calc_winnings = false;
    deal();
    std::vector<int> bets(n), win(n, 0);
    std::vector<char> folded(n);
    std::vector<int64_t> value(n, -1);
    for (int i = 0; i < n; i++) bets[i] = players[i].total, folded[i] = players[i].folded;
    if (not_folded() > 1)
      for (auto& p : players)
        if (!p.folded && board.size() == 5) value[p.id] = best7(p.hand, board);
    for (;;) {
      int mn = 1 << 30, mx = 0;
      int64_t best = -2;
      for (int i = 0; i < n; i++)
        if (!folded[i]) mn = std::min(mn, bets[i]), mx = std::max(mx, bets[i]), best = std::max(best, value[i]);
      std::vector<int> winners;
      for (int i = 0; i < n; i++) if (!folded[i] && value[i] == best) winners.push_back(i);
      int tot = 0;
      for (int i = 0; i < n; i++) {
        int t = std::min(bets[i], mn);
        tot += t; bets[i] -= t;
        if (bets[i] == 0) folded[i] = true;
      }
      if (winners.empty()) {                             // impossible in the referee; a desynced Tracker
        for (int i = 0; i < n; i++) win[i] += std::min(players[i].total, mn);   //   gets its chips back
        break;
      }
      int share = tot / (int)winners.size(), rem = tot % (int)winners.size();
      for (int w : winners) win[w] += share;
      int j = dealer_id + 1;                           // odd chips: first winner after the dealer
      while (std::find(winners.begin(), winners.end(), j % n) == winners.end()) j++;
      win[j % n] += rem;
      if (mn == mx) break;
    }
    for (int i = 0; i < n; i++) win[i] += bets[i];      // uncalled remainder returned
    for (auto& p : players) p.stack += win[p.id];
    calculate_elimination_ranks();
    over = true;
    return true;
  }
  void calculate_elimination_ranks() {
    int next_rank = 0;
    for (auto& p : players) next_rank += p.elim_rank >= 0;
    for (;;) {
      int mn = 1 << 30, cnt = 0;
      for (auto& p : players) if (p.elim_rank == -1 && p.stack == 0) mn = std::min(mn, p.total), cnt++;
      if (!cnt) break;
      int grp = 0;
      for (auto& p : players)
        if (p.elim_rank == -1 && p.stack == 0 && p.total == mn) p.elim_rank = next_rank, p.score = next_rank - n, grp++;
      next_rank += grp;
    }
  }
  bool is_game_over() const {
    int alive = 0;
    for (auto& p : players) alive += p.stack != 0;
    return over && alive == 1;
  }

  static std::string cards_str(const int* c, int k, int pad) {
    std::string s;
    for (int i = 0; i < pad; i++) {
      if (i) s += '_';
      s += i < k ? (c[i] < 0 ? "?" : card_str(c[i])) : "X";
    }
    return s;
  }
  std::string board_str() const { return cards_str(board.data(), (int)board.size(), 5); }
};

class Engine : public Board {
 public:
  Sha1Prng rng;
  int first_bb;
  std::vector<int> deck;
  int deck_idx = 0;
  std::vector<RoundInfo> round_infos;                // index = turn (0 unused)
  struct Showdown { std::string board; std::vector<std::string> cards; std::vector<char> elim; };
  std::vector<Showdown> showdowns;                   // index = hand
  std::vector<int> last_sent_round, last_sent_hand;
  int last_hand_round = 0;
  std::vector<LogEntry> log;
  std::vector<int> replaced;                         // per seat: outputs the referee replaced (illegal or unparseable)
  std::vector<std::string> replaced_log;             // the first few, "seat: raw -> shown" (debugging)

  Engine(int n_, int64_t seed) : rng(seed) {
    if (n_ < 2 || n_ > 4) throw std::invalid_argument("n must be 2..4");
    first_bb = rng.next_int(n_);                     // Referee.java: first BB
    static_cast<Board&>(*this) = Board(n_, first_bb);
    round_infos.resize(MAX_TURN + 2);
    showdowns.resize(1);
    last_sent_round.assign(n, 0);
    last_sent_hand.assign(n, 0);
    replaced.assign(n, 0);
    // Referee.init
    reset_hand(); init_deck(); init_blind(); calculate_next_player();
  }
  void init_deck() {
    deck.resize(52);
    for (int i = 0; i < 52; i++) deck[i] = i;          // rank-major, CardUtils order
    rng.shuffle(deck);
    int r = rng.next_int(52);                           // Deck.cut
    std::rotate(deck.begin(), deck.begin() + r, deck.end());
    deck_idx = 0;
  }
  int draw() override { return deck[deck_idx++]; }

  // ------------------------------------------------------------------ input protocol
  void record_showdown() {
    // ShowDownInfo: every non-eliminated player's hole cards are revealed (SHOW_FOLDED_CARDS=true)
    Showdown sd;
    sd.board = board_str();
    for (auto& p : players) sd.cards.push_back(cards_str(p.hand, p.n_hand, p.n_hand)), sd.elim.push_back(p.eliminated);
    if ((int)showdowns.size() <= hand_nb) showdowns.resize(hand_nb + 1);
    showdowns[hand_nb] = sd;
  }
  Obs build_obs(int pid) {
    Obs o;
    o.small_blind = SMALL_BLIND; o.big_blind = BIG_BLIND; o.hand_nb_by_level = HAND_NB_BY_LEVEL;
    o.level_mult = LEVEL_MULT; o.buy_in = TOTAL_BUY_IN / n; o.first_bb_id = first_bb; o.player_nb = n; o.player_id = pid;
    o.round = turn; o.hand_nb = hand_nb;
    for (auto& p : players) o.stacks.push_back(p.stack), o.chip_in_pot.push_back(p.total);
    o.board = board_str();
    o.cards = cards_str(players[pid].hand, players[pid].n_hand, players[pid].n_hand);
    for (int r = last_sent_round[pid] + 1; r < turn; r++) {
      const RoundInfo& ri = round_infos[r];
      o.actions.push_back(std::to_string(ri.turn) + " " + std::to_string(ri.hand) + " " + std::to_string(ri.pid) + " " + ri.shown + " " + ri.board);
    }
    last_sent_round[pid] = turn - 1;
    for (int h = last_sent_hand[pid] + 1; h < hand_nb; h++) {
      const Showdown& sd = showdowns[h];
      std::string parts;
      for (size_t i = 0; i < sd.cards.size(); i++) {
        if (i) parts += '_';
        parts += sd.elim[i] ? "E_E" : sd.cards[i];
      }
      o.showdowns.push_back(std::to_string(h) + " " + sd.board + " " + parts);
    }
    last_sent_hand[pid] = hand_nb - 1;
    o.possible = possible_actions();
    return o;
  }

  // ------------------------------------------------------------------ Referee.gameTurn
  void game_turn(int t, std::vector<Agent*>& agents) {
    if (calculate_player_winnings()) return do_board_over();
    if (over || t == 1) {                              // initBoard
      if (t != 1) { reset_hand(); init_deck(); init_blind(); calculate_next_player(); }
      deal_first();
      calc_next = false;
      return;
    }
    size_t n_cards = board.size();
    end_turn();
    if (n_cards != board.size()) {                     // all-in runout: a NONE round, no inputs
      if (last_hand_round != hand_nb) {
        turn++;
        if (turn > MAX_TURN) throw std::runtime_error("referee would crash: NONE round 601 (InputSender.java:21,30)");
        round_infos[turn] = {turn, hand_nb, -1, "NONE", board_str()};
        last_hand_round = hand_nb;
      }
      return;
    }
    if (calculate_player_winnings()) return do_board_over();
    if (turn == MAX_TURN) {                            // cancel the unfinished hand, end the game
      if (!over) for (auto& p : players) p.stack += p.total, p.total = 0;
      cancelled = true;
      final_scores();
      game_over = true;
      return;
    }
    if (deal()) { calculate_next_player(); calc_next = false; return; }
    if (calc_next) calculate_next_player();
    calc_next = true;
    int pid = next_player;
    turn++;
    Obs obs = build_obs(pid);
    std::string out;
    bool ok = agents[pid]->act(obs, out);
    Parsed pr;
    if (!ok) pr = {A_TIMEOUT, 0, true};
    else pr = parse(out.substr(0, out.find(';')));
    auto rp = replace(pr.t, pr.amount);
    if (ok && (pr.err || rp.first != pr.t || (rp.first == A_BET && rp.second != pr.amount))) {
      replaced[pid]++;
      if (replaced_log.size() < 50) replaced_log.push_back(std::to_string(pid) + ": " + out + " -> " + act_name(rp.first) + (rp.first == A_BET ? "_" + std::to_string(rp.second) : ""));
    }
    std::string shown = act_name(rp.first);
    if (rp.first == A_BET) shown += "_" + std::to_string(rp.second);
    round_infos[turn] = {turn, hand_nb, pid, shown, board_str()};
    last_hand_round = hand_nb;
    do_action(rp.first, rp.second);
    log.push_back({turn, hand_nb, pid, shown});
  }
  void do_board_over() {
    record_showdown();
    if (is_game_over()) { final_scores(); game_over = true; }
  }
  void final_scores() { for (auto& p : players) if (p.stack > 0) p.score = p.stack; }

  struct Result { int hands, rounds; bool cancelled; std::vector<int> scores, stacks; };
  Result run(std::vector<Agent*>& agents) {
    for (int t = 1; !game_over && t <= MAX_REFEREE_TURN; t++) game_turn(t, agents);
    Result r{hand_nb, turn, cancelled, {}, {}};
    for (auto& p : players) r.scores.push_back(p.score), r.stacks.push_back(p.stack);
    return r;
  }
};

}  // namespace pk

namespace pk {

// Parse one turn of stdin into an Obs (InputSender.sendInputs layout).  `first` reads the 8 init
// lines too; keep the same Obs across turns, since later turns rely on its player_nb.  Returns
// false at end of input.
inline bool read_obs(std::istream& in, bool first, Obs& o) {
  auto getline = [&](std::string& s) { if (!std::getline(in, s)) return false; if (!s.empty() && s.back() == '\r') s.pop_back(); return true; };
  std::string s;
  if (first) {
    if (!(in >> o.small_blind >> o.big_blind >> o.hand_nb_by_level >> o.level_mult >> o.buy_in >> o.first_bb_id >> o.player_nb >> o.player_id)) return false;
  }
  if (o.player_nb < 2 || o.player_nb > 4) return false;
  if (!(in >> o.round >> o.hand_nb)) return false;
  o.stacks.assign(o.player_nb, 0); o.chip_in_pot.assign(o.player_nb, 0);
  for (int i = 0; i < o.player_nb; i++) in >> o.stacks[i] >> o.chip_in_pot[i];
  in >> o.board >> o.cards;
  int k;
  in >> k; getline(s);                                  // rest of the count line
  o.actions.clear(); for (int i = 0; i < k; i++) { getline(s); o.actions.push_back(s); }
  in >> k; getline(s);
  o.showdowns.clear(); for (int i = 0; i < k; i++) { getline(s); o.showdowns.push_back(s); }
  in >> k; getline(s);
  o.possible.clear(); for (int i = 0; i < k; i++) { getline(s); o.possible.push_back(s); }
  return (bool)in;
}

class Tracker : public Board {
 public:
  int me = -1;
  int first_bb = -1;
  int last_hand_round = 0;
  bool started = false;
  bool decision_pending = false;
  bool desynced = false;                 // a check failed this hand: stacks/pot were resynced from stdin
  int mismatches = 0;                    // total failed checks (dev builds log them)
  std::string last_error;

  const int* hole() const { return players[me].hand; }

  // "AD_QH_2S_X_X" -> {card ids, -1 for X}
  static std::vector<int> parse_cards(const std::string& b) {
    std::vector<int> out;
    size_t i = 0;
    while (i <= b.size()) {
      size_t j = b.find('_', i);
      std::string tok = b.substr(i, j == std::string::npos ? std::string::npos : j - i);
      if (!tok.empty()) out.push_back(tok == "X" ? -1 : card_from(tok.c_str()));
      if (j == std::string::npos) break;
      i = j + 1;
    }
    return out;
  }

  enum Ev { EV_SETTLE, EV_NONE, EV_CANCEL, EV_DECISION, EV_CONTINUE };

  // Referee.gameTurn between decisions, one branch per call; EV_CONTINUE means call again.
  Ev step() {
    if (decision_pending) return EV_DECISION;
    if (calc_winnings) return EV_SETTLE;                 // (A)/(D): needs the showdown line
    if (over) {                                          // (B) initBoard
      reset_hand(); init_blind(); calculate_next_player(); deal_first(); calc_next = false;
      return EV_CONTINUE;
    }
    size_t n_cards = board.size();
    end_turn();
    if (n_cards != board.size()) {                       // (C) all-in runout
      if (last_hand_round != hand_nb) { turn++; last_hand_round = hand_nb; return EV_NONE; }
      return EV_CONTINUE;
    }
    if (calc_winnings) return EV_SETTLE;
    if (turn == MAX_TURN) { cancelled = true; game_over = true; return EV_CANCEL; }
    if (deal()) { calculate_next_player(); calc_next = false; return EV_CONTINUE; }
    if (calc_next) calculate_next_player();
    calc_next = true;
    decision_pending = true;
    return EV_DECISION;
  }

  bool fail(const std::string& what) {
    mismatches++; last_error = what; desynced = true;
    return false;
  }

  // "AD_QH_2S_X_X": record known board cards; false on a contradiction
  bool fill_board(const std::string& b) {
    size_t i = 0, idx = 0;
    while (i < b.size()) {
      size_t j = b.find('_', i);
      std::string tok = b.substr(i, j == std::string::npos ? std::string::npos : j - i);
      if (tok != "X" && !tok.empty()) {
        int c = card_from(tok.c_str());
        if (c < 0) return fail("bad card " + tok);
        if (idx >= board.size()) return fail("board longer than tracked: " + b + " vs " + board_str());
        if (board[idx] == -1) board[idx] = c;
        else if (board[idx] != c) return fail("board card differs: " + b + " vs " + board_str());
      }
      idx++;
      if (j == std::string::npos) break;
      i = j + 1;
    }
    return true;
  }

  // Apply a shown (post-replacement) action from an action line to next_player.
  bool apply_shown(const std::string& a) {
    if (a == "FOLD") do_action(A_FOLD, 0);
    else if (a == "CHECK") do_action(A_CHECK, 0);
    else if (a == "CALL") do_action(A_CALL, 0);
    else if (a == "ALL-IN") do_action(A_ALL_IN, 0);
    else if (a == "TIMEOUT") do_action(A_TIMEOUT, 0);
    else if (a.rfind("BET_", 0) == 0) do_action(A_BET, atoi(a.c_str() + 4));
    else return fail("unknown action " + a);
    return true;
  }

  // Settle hand `hand_nb` from its showdown line "h BOARD c0_c1_E_E_..."
  bool settle(const std::string& line) {
    std::istringstream is(line);
    int h; std::string b, cards;
    is >> h >> b >> cards;
    if (h != hand_nb) return fail("showdown for hand " + std::to_string(h) + " while tracking " + std::to_string(hand_nb));
    if (!fill_board(b)) return false;
    std::vector<std::string> tok;
    size_t i = 0;
    while (i <= cards.size()) { size_t j = cards.find('_', i); tok.push_back(cards.substr(i, j == std::string::npos ? std::string::npos : j - i)); if (j == std::string::npos) break; i = j + 1; }
    if ((int)tok.size() != 2 * n) return fail("showdown cards: " + cards);
    for (int p = 0; p < n; p++) {
      if (tok[2 * p] == "E") { if (!players[p].eliminated) return fail("E_E for a live player"); continue; }
      int c0 = card_from(tok[2 * p].c_str()), c1 = card_from(tok[2 * p + 1].c_str());
      if (c0 < 0 || c1 < 0) return fail("bad showdown cards: " + cards);
      players[p].hand[0] = c0; players[p].hand[1] = c1; players[p].n_hand = 2;
    }
    if (not_folded() > 1 && board.size() == 5)
      for (int k = 0; k < 5; k++) if (board[k] < 0) return fail("showdown with unknown board card");
    calculate_player_winnings();                         // sets over = true
    if (is_game_over()) game_over = true;
    return true;
  }

  // Process one turn's stdin.  Returns false if any check failed (state resynced from the snapshot).
  bool apply(const Obs& o) {
    bool ok = true;
    if (!started) {
      static_cast<Board&>(*this) = Board(o.player_nb, o.first_bb_id);
      me = o.player_id; first_bb = o.first_bb_id; started = true;
    }
    desynced = false;
    auto find_showdown = [&](int h) -> const std::string* {
      for (auto& s : o.showdowns) if (atoi(s.c_str()) == h) return &s;
      return nullptr;
    };
    // run the referee up to the next event, settling hands as their showdown lines allow
    auto advance = [&]() -> Ev {
      for (;;) {
        Ev e = step();
        if (e == EV_SETTLE) {
          const std::string* sd = find_showdown(hand_nb);
          if (!sd) { fail("no showdown line for hand " + std::to_string(hand_nb)); return EV_CANCEL; }
          if (!settle(*sd)) return EV_CANCEL;
          if (game_over) return EV_CANCEL;
          continue;
        }
        if (e != EV_CONTINUE) return e;
      }
    };
    for (auto& line : o.actions) {
      std::istringstream is(line);
      int r, h, pid; std::string a, b;
      is >> r >> h >> pid >> a >> b;
      Ev e = advance();
      if (e == EV_CANCEL) { ok = false; break; }
      if (a == "NONE") {
        if (e != EV_NONE || turn != r || hand_nb != h) { ok = fail("NONE line out of sync: " + line); break; }
        if (!fill_board(b)) { ok = false; break; }
        continue;
      }
      if (e != EV_DECISION || turn + 1 != r || hand_nb != h || next_player != pid) {
        ok = fail("action line out of sync (turn " + std::to_string(turn) + " hand " + std::to_string(hand_nb) +
                  " next " + std::to_string(next_player) + "): " + line);
        break;
      }
      turn++; last_hand_round = hand_nb; decision_pending = false;
      if (!fill_board(b)) { ok = false; break; }
      if (!apply_shown(a)) { ok = false; break; }
    }
    if (ok) {
      Ev e = advance();
      if (e != EV_DECISION || next_player != me || turn + 1 != o.round || hand_nb != o.hand_nb)
        ok = fail("snapshot out of sync: tracked turn " + std::to_string(turn) + " hand " + std::to_string(hand_nb) +
                  " next " + std::to_string(next_player) + " vs stdin round " + std::to_string(o.round) + " hand " + std::to_string(o.hand_nb));
    }
    if (ok) {
      // our hole cards
      if (o.cards.size() == 5) {
        int c0 = card_from(o.cards.c_str()), c1 = card_from(o.cards.c_str() + 3);
        if (c0 < 0 || c1 < 0) ok = fail("bad hole cards " + o.cards);
        else { players[me].hand[0] = c0; players[me].hand[1] = c1; players[me].n_hand = 2; }
      }
      ok = ok && fill_board(o.board);
      int shown = 0; for (int c : parse_cards(o.board)) shown += c >= 0;
      if (ok && shown != (int)board.size()) ok = fail("board size: " + o.board + " vs " + board_str());
      for (int p = 0; ok && p < n; p++)
        if (players[p].stack != o.stacks[p] || players[p].total != o.chip_in_pot[p])
          ok = fail("stacks differ at seat " + std::to_string(p) + ": tracked " + std::to_string(players[p].stack) + "/" +
                    std::to_string(players[p].total) + " stdin " + std::to_string(o.stacks[p]) + "/" + std::to_string(o.chip_in_pot[p]));
      if (ok && possible_actions() != o.possible) {
        std::string a, b; for (auto& x : possible_actions()) a += x + ","; for (auto& x : o.possible) b += x + ",";
        ok = fail("possible actions differ: tracked " + a + " stdin " + b);
      }
    }
    if (!ok) resync(o);
    return ok;
  }

  // Overwrite what the snapshot gives when the replay went wrong (stacks, chips, board); the
  // betting state (min raise, raise cap) stays approximate until the next hand.
  void resync(const Obs& o) {
    desynced = true;
    if (!started) return;
    for (int p = 0; p < n && p < (int)o.stacks.size(); p++) players[p].stack = o.stacks[p], players[p].total = o.chip_in_pot[p];
    turn = o.round - 1; hand_nb = o.hand_nb; next_player = me; decision_pending = true; calc_winnings = false; over = false;
    board.clear();
    for (int c : parse_cards(o.board)) if (c >= 0) board.push_back(c);
    pot = 0; for (auto& p : players) pot += p.total;
  }
};

}  // namespace pk
// ---- bot/pf_tables.hpp
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

namespace bot {

typedef std::chrono::steady_clock Clock;

struct Rng {                                           // splitmix64
  uint64_t x;
  __attribute__((always_inline)) inline uint64_t next() {
    uint64_t z = (x += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  __attribute__((always_inline)) inline int below(int n) { return (int)((next() >> 32) * (uint64_t)n >> 32); }
};

struct Budget {
  double ms = 20;            // wall-clock budget for the Monte Carlo (from t0); <= 0: trials only
  int max_trials = 100000;   // hard cap on trials
  int min_trials = 2000;     // always run at least this many
};

// Preflop class index as in solvers/eq.c and pf_tables.hpp: pair r*13+r, suited hi*13+lo, offsuit lo*13+hi.
inline int hand_class(int c0, int c1) {
  int r0 = c0 >> 2, r1 = c1 >> 2, hi = r0 > r1 ? r0 : r1, lo = r0 > r1 ? r1 : r0;
  if (r0 == r1) return r0 * 13 + r0;
  return (c0 & 3) == (c1 & 3) ? hi * 13 + lo : lo * 13 + hi;
}

class Bot {
 public:
  pk::Tracker tr;
  Rng rng{0x1234567ull};
  // diagnostics of the last decision
  int last_trials = 0;
  double last_equity = 0;
  std::string last_tag;
  long decisions = 0, trials_total = 0;

  // Our share of the pot at showdown against n_opp uniformly random hands, given the known board.
  double equity(const int* hole, const int* board, int n_board, int n_opp, const Budget& b, Clock::time_point t0) {
    int used[7], nu = 0;
    used[nu++] = hole[0]; used[nu++] = hole[1];
    for (int i = 0; i < n_board; i++) used[nu++] = board[i];
    int deck[52], nd = 0;
    for (int c = 0; c < 52; c++) {
      bool u = false;
      for (int i = 0; i < nu; i++) u |= used[i] == c;
      if (!u) deck[nd++] = c;
    }
    pe::H base = pe::E;
    for (int i = 0; i < n_board; i++) base = pe::add(base, board[i]);
    pe::H mine = pe::add(pe::add(base, hole[0]), hole[1]);
    int need = 5 - n_board + 2 * n_opp;
    double share = 0;
    int trials = 0;
    for (;;) {
      for (int t = 0; t < 512; t++, trials++) {
        // partial Fisher-Yates for `need` cards
        for (int i = 0; i < need; i++) { int j = i + rng.below(nd - i); int tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp; }
        pe::H bd = base, me = mine;
        for (int i = 0; i < 5 - n_board; i++) bd = pe::add(bd, deck[i]), me = pe::add(me, deck[i]);
        uint16_t mv = pe::ev(me);
        int better = 0, tie = 0;
        for (int o = 0; o < n_opp; o++) {
          int k = 5 - n_board + 2 * o;
          uint16_t ov = pe::ev(pe::add(pe::add(bd, deck[k]), deck[k + 1]));
          if (ov > mv) { better = 1; break; }
          tie += ov == mv;
        }
        if (!better) share += 1.0 / (1 + tie);
      }
      if (trials >= b.max_trials) break;
      if (trials >= b.min_trials && b.ms > 0 &&
          std::chrono::duration<double, std::milli>(Clock::now() - t0).count() > b.ms) break;
      if (b.ms <= 0 && trials >= b.min_trials) break;
    }
    last_trials = trials; trials_total += trials;
    return share / trials;
  }

  // The decision for this turn.  t0 is when the turn's input arrived.
  std::string act(const pk::Obs& o, const Budget& b, Clock::time_point t0) {
    decisions++;
    tr.apply(o);                                     // on failure the tracker resyncs from the snapshot
    const pk::Player& me = tr.players[tr.me];
    rng.x ^= (uint64_t)o.round * 0x9E3779B97F4A7C15ull ^ (uint64_t)me.hand[0] << 8 ^ (uint64_t)me.hand[1] << 16;
    // what the referee offers
    bool can_check = false, can_raise = false, can_allin = false, can_call = false;
    int min_bet = 0;
    for (auto& a : o.possible) {
      if (a == "CHECK") can_check = true; else if (a == "CALL") can_call = true; else if (a == "ALL-IN") can_allin = true;
      else if (a.compare(0, 4, "BET_") == 0) can_raise = true, min_bet = atoi(a.c_str() + 4);
    }
    int stack = me.stack;
    int pot = 0, live = 0, alive = 0, opp_max_start = 0;
    for (auto& p : tr.players) {
      pot += p.total;
      live += !p.folded;
      alive += p.stack + p.total > 0;
      if (p.id != tr.me && !p.folded) opp_max_start = std::max(opp_max_start, p.stack + p.total);
    }
    int n_opp = live - 1;
    int call = tr.call_amount(me);
    int bb = tr.bb;
    int my_start = stack + me.total;
    int eff = std::min(my_start, opp_max_start);         // effective stack this hand, chips
    double eff_bb = (double)eff / bb;
    bool preflop = tr.board.empty();
    const int* hole = me.hand;

    // ---- HU jam/fold Nash at short effective stacks (chip EV; heads-up, preflop)
    if (preflop && alive == 2 && live == 2 && eff_bb <= 8.0 && hole[0] >= 0) {
      int k = 0;
      for (int i = 1; i < pf::PF_NS; i++)
        if (std::abs(pf::PF_STACKS[i] - eff_bb) < std::abs(pf::PF_STACKS[k] - eff_bb)) k = i;
      int cls = hand_class(hole[0], hole[1]);
      bool first_in = tr.last_raiser == -1;              // nobody has raised: SB to act first
      const pk::Player* opp = nullptr;
      for (auto& p : tr.players) if (p.id != tr.me && !p.folded) opp = &p;
      if (first_in && tr.me == tr.sb_id && (can_raise || can_allin)) {
        last_tag = "pf-sb"; last_equity = -1; last_trials = 0;
        return (pf::PF_JAM[cls] >> k & 1) ? "ALL-IN" : "FOLD";
      }
      if (tr.me == tr.bb_id && opp && opp->allin && call > 0) {
        last_tag = "pf-bb"; last_equity = -1; last_trials = 0;
        return (pf::PF_CALL[cls] >> k & 1) ? (call >= stack ? "ALL-IN" : "CALL") : "FOLD";
      }
    }

    // ---- equity versus pot odds
    double e = hole[0] >= 0 ? equity(hole, tr.board.data(), (int)tr.board.size(), std::max(1, n_opp), b, t0) : 0.5;
    last_equity = e;
    double odds = call > 0 ? (double)call / (pot + call) : 0;
    auto bet = [&](int amount) -> std::string {           // a raise adding `amount` chips, made legal
      if (!can_raise || amount >= stack) return can_allin ? "ALL-IN" : (can_call ? "CALL" : "CHECK");
      return "BET " + std::to_string(std::max(amount, min_bet));
    };
    auto call_or_check = [&]() -> std::string { return can_check ? "CHECK" : "CALL"; };
    if (call > 0) {
      bool allin_call = call >= stack;
      if (allin_call) {                                   // calling off: ICM-style caution multiway
        double margin = alive > 2 ? 0.08 : 0.0;
        last_tag = "callff";
        return e > odds + margin ? "ALL-IN" : "FOLD";    // a CALL for the whole stack is replaced by ALL-IN
      }
      if (preflop && tr.last_raiser == -1) {              // unopened pot, we are not the BB: open or complete
        double thr = 0.50 + 0.03 * (n_opp - 1);
        if (e > thr && can_raise) { last_tag = "open"; return bet((int)(2.5 * bb) - me.rnd); }
        last_tag = "complete";
        return e > odds + 0.03 ? "CALL" : "FOLD";
      }
      if (e > 0.85 && can_raise) { last_tag = "raise"; return bet(pot + call); }
      last_tag = "call";
      return e > odds + 0.03 ? "CALL" : "FOLD";
    }
    // check is free
    double thr = 0.62 + 0.04 * (n_opp - 1);
    if (preflop && tr.me == tr.bb_id && tr.last_raiser == -1) thr = 0.55 + 0.03 * (n_opp - 1);   // BB option
    if (e > thr && can_raise) { last_tag = "bet"; return bet(std::max(bb, (int)(0.6 * pot))); }
    last_tag = "check";
    return call_or_check();
  }
};

}  // namespace bot

const bool DEBUG = true;      // stderr diagnostics (stderr is free on CodinGame; stdout is the action)
const bool PONDER = false;    // probe: a spinning thread; log its progress between turns

std::atomic<long> ponder_counter{0};

int main() {
  typedef std::chrono::steady_clock Clock;
  auto ms = [](Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::ios::sync_with_stdio(false);
  auto t_start = Clock::now();
  pe::init();
  auto t_init = Clock::now();
  bot::Bot b;
  bot::Budget budget;
  budget.ms = 20;                                       // placeholder until CodinGame latency is measured
  pk::Obs o;
  bool first = true;
  Clock::time_point t_last_flush = t_init;
  long last_counter = 0;
  if (PONDER) std::thread([] { for (;;) ponder_counter.fetch_add(1, std::memory_order_relaxed); }).detach();
  while (pk::read_obs(std::cin, first, o)) {
    auto t0 = Clock::now();
    if (first && DEBUG) {
      // probes: compile mode, evaluator start-up, Monte Carlo speed over 10 ms
      int hole[2] = {0, 5}, board[3] = {10, 23, 40};
      bot::Budget bench; bench.ms = 10; bench.max_trials = 1 << 30; bench.min_trials = 1;
      auto tb = Clock::now();
      b.equity(hole, board, 3, 1, bench, tb);
      double bms = ms(tb, Clock::now());
      fprintf(stderr, "probe __cplusplus=%ld pe_init_ms=%.1f mc_trials_per_s=%.2fM n=%d id=%d\n",
              (long)__cplusplus, ms(t_start, t_init), b.last_trials / bms / 1000.0, o.player_nb, o.player_id);
    }
    std::string out = b.act(o, budget, t0);
    std::cout << out << "\n" << std::flush;
    auto t1 = Clock::now();
    if (DEBUG) {
      long c = ponder_counter.load(std::memory_order_relaxed);
      fprintf(stderr, "r%d h%d turn_ms=%.2f since_last_flush_ms=%.0f trials=%d eq=%.3f %s%s%s\n", o.round, o.hand_nb, ms(t0, t1),
              ms(t_last_flush, t0), b.last_trials, b.last_equity, b.last_tag.c_str(),
              b.tr.desynced ? " DESYNC:" : "", b.tr.desynced ? b.tr.last_error.c_str() : "");
      if (PONDER) fprintf(stderr, "ponder_delta=%ld\n", c - last_counter);
      last_counter = c;
    }
    t_last_flush = t1;
    first = false;
  }
  return 0;
}
