#!/usr/bin/env python3
"""Play against the bot in your browser: a local poker table with CodinGame's rules.

    python3 play/server.py                       # then open http://localhost:8765
    python3 play/server.py --bot submissions/om1_min.cpp --port 8765 --open

Needs Python 3.8+ and a C++ compiler (g++ or clang++; set CXX to choose). Nothing else to install.

The game is sim/poker_sim.py, the exact port of CodinGame's referee (blinds double every 10 hands, every
non-big-blind player posts the small blind, 600-decision cap, all hole cards shown after every hand). Each bot
seat is the compiled bot as its own process, fed the same stdin CodinGame sends it, one line of stdout per
decision, so you play the submitted bot itself. Your seat waits for the page. The page (play/index.html) polls
/api/state and posts /api/act, /api/new, /api/continue, /api/settings. After each hand the bots' own
diagnostics (decision tag, equity estimate, the opponent model's read) are shown for that hand.
"""
import argparse, http.server, json, os, queue, random, re, shutil, subprocess, sys, threading, time, webbrowser

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REPO, "sim"))
from poker_sim import PokerSim, obs_to_stdin, best7, MAX_REFEREE_TURN   # noqa: E402

CATEGORY = ["high card", "a pair", "two pair", "three of a kind", "a straight", "a flush", "a full house",
            "four of a kind", "a straight flush"]
BOT_NAMES = ["Bot A", "Bot B", "Bot C"]
DEBUG_RE = re.compile(r"^r(\d+) h(\d+) .*?trials=(\S+) eq=(\S+) (\S+)(.*)$")


def pretty(card):
    """'TD' -> '10♦'"""
    return ("10" if card[0] == "T" else card[0]) + {"S": "♠", "H": "♥", "D": "♦", "C": "♣"}[card[1]]


# ----------------------------------------------------------------------------- building the bot
def find_compiler():
    for c in (os.environ.get("CXX"), "g++", "clang++", "c++"):
        if c and shutil.which(c):
            return c
    sys.exit("No C++ compiler found: install g++ or clang++ (macOS: xcode-select --install), or set CXX.")


def build_bot(src):
    out_dir = os.path.join(REPO, "build", "play")
    os.makedirs(out_dir, exist_ok=True)
    exe = os.path.join(out_dir, os.path.splitext(os.path.basename(src))[0] + (".exe" if os.name == "nt" else ""))
    if os.path.exists(exe) and os.path.getmtime(exe) >= os.path.getmtime(src):
        return exe
    cxx = find_compiler()
    print(f"compiling {os.path.relpath(src, REPO)} with {cxx} (once; 10-60 s) ...", flush=True)
    for std in ("-std=gnu++20", "-std=gnu++17"):
        r = subprocess.run([cxx, std, "-O2", "-pthread", "-w", "-o", exe, src], capture_output=True, text=True)
        if r.returncode == 0:
            return exe
    sys.exit(f"compiling the bot failed:\n{r.stderr[-3000:]}")


# ----------------------------------------------------------------------------- seats
class BotSeat:
    """The compiled bot as a process: stdin per decision as CodinGame sends it, one line back."""

    def __init__(self, game, pid, exe):
        self.game, self.pid, self.first = game, pid, True
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  text=True, bufsize=1)
        threading.Thread(target=self._read_stderr, daemon=True).start()

    def _read_stderr(self):
        for line in self.p.stderr:
            m = DEBUG_RE.match(line.strip())
            if m:
                note = m.group(6).strip()
                note = note[note.find("om"):] if "om" in note else ""
                with self.game.lock:
                    self.game.thoughts.append(dict(round=int(m.group(1)), hand=int(m.group(2)), pid=self.pid,
                                                   eq=float(m.group(4)), tag=m.group(5), note=note))

    def __call__(self, obs):
        g = self.game
        text = obs_to_stdin(obs, self.first)
        self.first = False
        g.acting = self.pid
        g.lock.release()                                  # let the page read the table while the bot "thinks"
        try:
            if g.delay > 0 and not g.fast:
                time.sleep(g.delay)
            self.p.stdin.write(text)
            self.p.stdin.flush()
            line = self.p.stdout.readline()
        except (BrokenPipeError, OSError):
            line = ""
        finally:
            g.lock.acquire()
        g.acting = -1
        return line.strip() or None

    def close(self):
        try:
            self.p.stdin.close()
        except OSError:
            pass
        try:
            self.p.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.p.kill()


class HumanSeat:
    """Your seat: waits for /api/act."""

    def __init__(self, game, pid):
        self.game, self.pid = game, pid

    def __call__(self, obs):
        g = self.game
        g.acting = self.pid
        g.pending = obs.possible
        g.lock.release()
        try:
            out = g.human_q.get()
        finally:
            g.lock.acquire()
        g.pending = None
        g.acting = -1
        return out


# ----------------------------------------------------------------------------- one game
class Game:
    def __init__(self, n_players, bot_exe, delay, seed=None):
        self.seed = seed if seed is not None else random.randint(-2**62, 2**62)
        self.n = n_players
        self.sim = PokerSim(n_players, self.seed)
        self.lock = threading.RLock()
        self.human = 0
        self.names = ["You"] + BOT_NAMES[: n_players - 1]
        self.delay, self.fast, self.auto_next = delay, False, False
        self.human_q, self.continue_ev = queue.Queue(), threading.Event()
        self.pending, self.acting, self.aborted = None, -1, False
        self.events, self.thoughts, self.results = [], [], {}
        self.hand_start = {}
        self.last_action = {}
        self.status = "running"                           # running | hand_over | game_over
        self.agents = [HumanSeat(self, 0)] + [BotSeat(self, i, bot_exe) for i in range(1, n_players)]
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def event(self, text, kind="action"):
        self.events.append(dict(hand=self.sim.hand_nb, text=text, kind=kind))

    def name(self, pid):
        return self.names[pid]

    # the referee's loop (PokerSim.run), one game turn at a time, narrating what changed
    def run(self):
        sim = self.sim
        with self.lock:
            t = 0
            while not sim.game_over and t < MAX_REFEREE_TURN and not self.aborted:
                t += 1
                hand, board_n, log_n = sim.hand_nb, len(sim.board), len(sim.log)
                before = [(p.total, p.rnd) for p in sim.players]
                max_rnd_before = max(p.rnd for p in sim.players)
                sim.game_turn(t, self.agents)
                if self.aborted:
                    break
                if sim.hand_nb != hand or t == 1:          # a new hand was dealt
                    self.hand_start[sim.hand_nb] = [p.stack + p.total for p in sim.players]
                    self.last_action = {}
                    dealer = sim.dealer_id if sim.dealer_id >= 0 else sim.sb_id
                    self.event(f"Hand {sim.hand_nb} · blinds {sim.sb}/{sim.bb} · {self.name(dealer)} on the button", "hand")
                for (_, h, pid, shown) in sim.log[log_n:]:
                    self.narrate(pid, shown, before[pid], max_rnd_before)
                if len(sim.board) > board_n:
                    street = {3: "Flop", 4: "Turn", 5: "River"}[len(sim.board)]
                    shown = sim.board if len(sim.board) == 3 else sim.board[-1:]
                    self.event(f"{street}: {' '.join(pretty(c) for c in shown)}", "street")
                if sim.hand_nb in sim.showdowns and sim.hand_nb not in self.results:
                    self.finish_hand(sim.hand_nb)
                    if not sim.game_over and not self.fast:
                        self.pause_between_hands()
            self.status = "game_over"
            if not self.aborted:
                order = sorted(range(self.n), key=lambda i: (-sim.players[i].score, sim.players[i].elim_rank))
                self.event("Game over: " + ", ".join(f"{k + 1}. {self.name(i)}" for k, i in enumerate(order)), "result")
        for a in self.agents:
            if isinstance(a, BotSeat):
                a.close()

    def said(self, pid, third, second):
        """'Bot A folds' / 'You fold'"""
        return f"{self.name(pid)} {second if pid == self.human else third}"

    def narrate(self, pid, shown, before, max_rnd_before):
        p = self.sim.players[pid]
        added = p.total - before[0]
        if shown == "FOLD":
            forms, status = ("folds", "fold"), "folded"
        elif shown == "CHECK":
            forms, status = ("checks", "check"), "checked"
        elif shown == "CALL":
            forms, status = (f"calls {added}", f"call {added}"), f"called {added}"
        elif shown == "ALL-IN" and p.rnd > max_rnd_before:
            forms, status = (f"is all-in ({p.rnd})", f"are all-in ({p.rnd})"), "all-in"
        elif shown == "ALL-IN":
            forms, status = (f"calls all-in ({added})", f"call all-in ({added})"), "all-in"
        elif shown.startswith("BET") and max_rnd_before > 0:
            forms, status = (f"raises to {p.rnd}", f"raise to {p.rnd}"), f"raised to {p.rnd}"
        elif shown.startswith("BET"):
            forms, status = (f"bets {added}", f"bet {added}"), f"bet {added}"
        else:                                             # a timeout (the bot did not answer): the referee folds it
            forms, status = ("times out", "time out"), "timed out"
        self.last_action[pid] = status
        self.event(self.said(pid, *forms))

    def finish_hand(self, h):
        sim = self.sim
        board_str, cards = sim.showdowns[h]
        board = [c for c in board_str.split("_") if c != "X"]
        start = self.hand_start.get(h, [p.stack for p in sim.players])
        live = [p.id for p in sim.players if not p.folded and not p.eliminated]
        showdown = len(live) > 1 and len(board) == 5
        res = dict(hand=h, board=board, showdown=showdown, players=[])
        for p in sim.players:
            c = cards[p.id]
            if c is None:
                res["players"].append(dict(pid=p.id, out=True))
                continue
            net = p.stack - start[p.id]                   # winnings are already in the stack
            best = CATEGORY[best7(c, board)[0]] if showdown and p.id in live else ""
            res["players"].append(dict(pid=p.id, cards=list(c), net=net, folded=p.folded, best=best))
        self.results[h] = res
        winners = [r for r in res["players"] if r.get("net", 0) > 0]
        for r in winners:
            how = f" with {r['best']}" if r["best"] else ""
            self.event(self.said(r["pid"], f"wins {r['net']}{how}", f"win {r['net']}{how}"), "result")
        busted = [p.id for p in sim.players if p.stack == 0 and start[p.id] > 0]
        for b in busted:
            self.event("You are out: the bots play the game out" if b == self.human else f"{self.name(b)} is out", "result")
        if self.human in busted:                          # watch the bots finish at full speed
            self.fast = True

    def pause_between_hands(self):
        self.status = "hand_over"
        self.continue_ev.clear()
        self.lock.release()
        try:
            if self.auto_next:
                self.continue_ev.wait(timeout=2.5)
            else:
                self.continue_ev.wait()
        finally:
            self.lock.acquire()
        self.status = "running"

    def abort(self):
        self.aborted = True
        self.fast = True
        self.human_q.put("FOLD")
        self.continue_ev.set()

    # ------------------------------------------------------------------------- the page's view
    def state(self):
        sim = self.sim
        with self.lock:
            h = sim.hand_nb
            res = self.results.get(h) if self.status in ("hand_over", "game_over") else None
            reveal = {r["pid"]: r["cards"] for r in res["players"] if "cards" in r} if res else {}
            players = []
            for p in sim.players:
                cards = list(p.hand) if (p.id == self.human or p.id in reveal) else (["??", "??"] if p.hand and not p.eliminated else [])
                players.append(dict(
                    id=p.id, name=self.name(p.id), human=p.id == self.human, stack=p.stack, bet=p.rnd, total=p.total,
                    folded=p.folded and not p.eliminated, allin=p.allin, out=p.eliminated or (p.stack == 0 and p.total == 0),
                    dealer=p.id == (sim.dealer_id if sim.dealer_id >= 0 else sim.sb_id), sb=p.id == sim.sb_id,
                    bbpos=p.id == sim.bb_id, cards=cards, acting=p.id == self.acting,
                    last=self.last_action.get(p.id, ""), score=p.score))
            me = sim.players[self.human]
            options = None
            if self.pending is not None and self.acting == self.human:
                call = sim.call_amount(me)
                bet = [int(a[4:]) for a in self.pending if a.startswith("BET_")]
                options = dict(fold="FOLD" in self.pending, check="CHECK" in self.pending,
                               call=call if "CALL" in self.pending else 0,
                               allin=me.stack if "ALL-IN" in self.pending else 0,
                               allin_to=me.rnd + me.stack,
                               raise_min_to=me.rnd + bet[0] if bet else 0, raise_max_to=me.rnd + me.stack - 1 if bet else 0,
                               rnd=me.rnd, pot=sum(p.total for p in sim.players), bb=sim.bb)
            thoughts = []
            done = {k for k in self.results} if self.status != "running" else {k for k in self.results if k < h}
            last_done = max(done) if done else 0
            for t in self.thoughts:
                if t["hand"] == last_done:
                    ri = sim.round_infos.get(t["round"])
                    thoughts.append(dict(t, name=self.name(t["pid"]), action=ri[3] if ri else "", board=ri[4] if ri else ""))
            return dict(
                status=self.status, hand=h, sb=sim.sb, bb=sim.bb, level=sim.level, pot=sum(p.total for p in sim.players),
                board=list(sim.board), players=players, acting=self.acting, options=options, events=self.events[-150:],
                result=res, thoughts=thoughts, thoughts_hand=last_done, seed=self.seed, delay=self.delay,
                auto_next=self.auto_next, rounds=sim.turn, n=self.n)

    def act(self, a):
        """Translate the page's action into the bot protocol's output line ('BET x' adds x chips)."""
        with self.lock:
            if self.pending is None or self.acting != self.human:
                return False
            me = self.sim.players[self.human]
            kind = a.get("type")
            if kind == "RAISE":
                to = int(a.get("to", 0))
                add = to - me.rnd
                out = "ALL-IN" if add >= me.stack else f"BET {max(add, 1)}"
            elif kind in ("FOLD", "CHECK", "CALL", "ALL-IN"):
                out = kind
            else:
                return False
            self.pending = None
        self.human_q.put(out)
        return True


# ----------------------------------------------------------------------------- HTTP
class App:
    def __init__(self, bot_exe, delay):
        self.bot_exe, self.delay, self.game = bot_exe, delay, None
        self.lock = threading.Lock()

    def new_game(self, n, seed=None):
        with self.lock:
            if self.game:
                self.game.abort()
            self.game = Game(n, self.bot_exe, self.delay, seed)


def make_handler(app):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def send_json(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                body = open(os.path.join(HERE, "index.html"), "rb").read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path.startswith("/api/state"):
                self.send_json(app.game.state() if app.game else dict(status="none"))
            else:
                self.send_error(404)

        def do_POST(self):
            n = int(self.headers.get("Content-Length") or 0)
            try:
                data = json.loads(self.rfile.read(n) or b"{}")
            except ValueError:
                return self.send_json(dict(ok=False, error="bad json"), 400)
            g = app.game
            if self.path == "/api/new":
                players = min(4, max(2, int(data.get("players", 2))))
                seed = data.get("seed")
                app.new_game(players, int(seed) if seed not in (None, "") else None)
                return self.send_json(dict(ok=True))
            if not g:
                return self.send_json(dict(ok=False, error="no game"), 409)
            if self.path == "/api/act":
                return self.send_json(dict(ok=g.act(data)))
            if self.path == "/api/continue":
                g.continue_ev.set()
                return self.send_json(dict(ok=True))
            if self.path == "/api/settings":
                if "delay" in data:
                    g.delay = app.delay = max(0.0, min(3.0, float(data["delay"])))
                if "auto_next" in data:
                    g.auto_next = bool(data["auto_next"])
                    if g.auto_next:
                        g.continue_ev.set()
                return self.send_json(dict(ok=True))
            self.send_error(404)
    return Handler


def main():
    ap = argparse.ArgumentParser(description="Play against the CodinGame poker bot in your browser.")
    ap.add_argument("--bot", default=os.path.join(REPO, "submissions", "m3_0_min.cpp"), help="bot source (a submission file)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1", help="127.0.0.1 keeps it on this machine")
    ap.add_argument("--delay", type=float, default=0.8, help="seconds a bot waits before acting, so you can follow")
    ap.add_argument("--open", action="store_true", help="open the page in your browser")
    a = ap.parse_args()
    exe = build_bot(os.path.abspath(a.bot))
    app = App(exe, a.delay)
    srv = http.server.ThreadingHTTPServer((a.host, a.port), make_handler(app))
    url = f"http://localhost:{a.port}" if a.host in ("127.0.0.1", "localhost", "0.0.0.0") else f"http://{a.host}:{a.port}"
    print(f"bot: {os.path.relpath(a.bot, REPO) if os.path.abspath(a.bot).startswith(REPO) else a.bot}\nplay at {url}  (Ctrl+C to stop)", flush=True)
    if a.open:
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if app.game:
            app.game.abort()


if __name__ == "__main__":
    main()
