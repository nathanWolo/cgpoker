package com.codingame.game;

import java.io.*;
import java.util.*;
import com.codingame.model.object.*;
import com.codingame.model.object.board.Board;
import com.codingame.model.object.enumeration.ActionType;
import com.codingame.model.utils.ActionUtils;
import com.codingame.model.utils.RandomUtils;

/** Replays a CodinGame poker game from seed + recorded outputs; prints one TSV event per line. */
public class Replayer {
  static PrintStream out = System.out;
  static int n;
  static String cards(Card[] cs) {
    StringBuilder sb = new StringBuilder();
    for (Card c : cs) { if (sb.length() > 0) sb.append('_'); sb.append(c == null ? "?" : c.toString()); }
    return sb.toString();
  }
  static String boardStr(Board b) {
    StringBuilder sb = new StringBuilder();
    for (Card c : b.getBoardCards()) { if (sb.length() > 0) sb.append('_'); sb.append(c.toString()); }
    return sb.length() == 0 ? "-" : sb.toString();
  }
  /** final scores as the referee reports them (Referee.onEnd: PlayerModel.getScore()) */
  static void printScores(Board board) {
    StringBuilder sb = new StringBuilder("SCORES");
    for (int i = 0; i < n; i++) sb.append('\t').append(board.getPlayer(i).getScore());
    out.println(sb);
  }
  /** returns true if game over */
  static boolean boardOver(Board board, int turn) {
    StringBuilder sb = new StringBuilder("END\t" + board.getHandNb());
    for (int i = 0; i < n; i++) { PlayerModel p = board.getPlayer(i);
      sb.append('\t').append(i).append(':').append(cards(p.getHand().getCards())).append(':').append(p.getWinAmount()).append(':').append(p.getStack()).append(':').append(p.isFolded() ? "F" : "-"); }
    sb.append('\t').append(boardStr(board));
    out.println(sb);
    board.endTurnView();
    if (board.isGameOver()) { board.calculateFinalScores(); out.println("GAMEOVER\t" + turn); printScores(board); return true; }
    return false;
  }
  public static void main(String[] args) throws Exception {
    BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
    long seed = Long.parseLong(in.readLine().trim());
    n = Integer.parseInt(in.readLine().trim());
    List<Integer> who = new ArrayList<>();
    List<String> outs = new ArrayList<>();
    String line;
    while ((line = in.readLine()) != null) {
      if (line.isEmpty()) continue;
      int tab = line.indexOf('\t');
      who.add(Integer.parseInt(line.substring(0, tab)));
      outs.add(line.substring(tab + 1));
    }
    Random rr = java.security.SecureRandom.getInstance("SHA1PRNG"); rr.setSeed(seed); RandomUtils.init(rr);
    int firstBB = RandomUtils.nextInt(n);
    Board board = new Board(n, firstBB);
    board.resetHand();
    board.initDeck();
    board.initDemoBoard();
    board.initBlind();
    board.calculateNextPlayer();
    int turn = 0; boolean calcNext = false; int lastHandRound = -1; int ai = 0;
    out.println("START\t" + n + "\t" + firstBB + "\t" + outs.size());
    for (int t = 1; t < 30000; t++) {
      if (board.calculatePlayerWinnings()) { if (boardOver(board, turn)) break; continue; }
      if (board.isOver() || t == 1) {
        if (t != 1) { board.resetHand(); board.initDeck(); board.initDemoBoard(); board.initBlind(); board.calculateNextPlayer(); }
        board.dealFirst();
        StringBuilder sb = new StringBuilder("HAND\t" + board.getHandNb() + "\t" + board.getSmallBlind() + "\t" + board.getBigBlind() + "\t" + board.getDealerId() + "\t" + board.getSbId() + "\t" + board.getBbId());
        for (int i = 0; i < n; i++) { PlayerModel p = board.getPlayer(i);
          sb.append('\t').append(i).append(':').append(p.getStack() + p.getTotalBetAmount()).append(':').append(p.isFolded() && p.getStack() + p.getTotalBetAmount() == 0 ? "OUT" : cards(p.getHand().getCards())); }
        out.println(sb);
        calcNext = false;
        continue;
      }
      int cardNb = board.getBoardCards().size();
      board.endTurn();
      if (cardNb != board.getBoardCards().size()) {
        if (lastHandRound != board.getHandNb()) { turn++; lastHandRound = board.getHandNb(); }
        continue;
      }
      if (board.calculatePlayerWinnings()) { if (boardOver(board, turn)) break; continue; }
      if (turn == RefereeParameter.MAX_TURN) { out.println("CANCEL\t" + board.getHandNb());
        board.cancelCurrentHand(); board.calculateFinalScores(); printScores(board); break; }  // Referee.doCancelLastHand
      if (board.deal()) { board.calculateNextPlayer(); calcNext = false; continue; }
      if (calcNext) board.calculateNextPlayer();
      calcNext = true;
      int pid = board.getNextPlayerId();
      turn++;
      List<Action> poss = ActionUtils.calculatePossibleActions(board);
      PlayerModel p = board.getPlayer(pid);
      int callAmt = board.calculateCallAmount(p);
      int pot = 0; for (int i = 0; i < n; i++) pot += board.getPlayer(i).getTotalBetAmount();
      int alive = 0, notFolded = 0; for (int i = 0; i < n; i++) { PlayerModel q = board.getPlayer(i); if (q.getStack() + q.getTotalBetAmount() > 0) alive++; if (!q.isFolded()) notFolded++; }
      if (ai >= outs.size()) { out.println("ERR\tno more actions at turn " + turn); break; }
      if (who.get(ai) != pid) { out.println("ERR\tplayer mismatch expected " + pid + " got " + who.get(ai) + " at ai=" + ai); break; }
      String raw = outs.get(ai++);
      ActionInfo info;
      String msg = "";
      if (raw.equals("__TIMEOUT__")) {
        info = ActionInfo.create(pid, ActionType.TIMEOUT);
        p.setStack(p.getStack()); // referee deactivates; board.timeout handles chips
      } else {
        String[] o = raw.split(";", -1);
        if (o.length > 1) msg = o[1];
        info = ActionInfo.create(pid, o[0].toUpperCase().trim());
      }
      StringBuilder ps = new StringBuilder();
      for (Action a : poss) { if (ps.length() > 0) ps.append(','); ps.append(a.toInputString()); }
      StringBuilder st = new StringBuilder();
      for (int i = 0; i < n; i++) { PlayerModel q = board.getPlayer(i); if (st.length() > 0) st.append(','); st.append(q.getStack()).append('/').append(q.getTotalBetAmount()).append('/').append(q.getRoundBetAmount()).append(q.isFolded() ? "F" : ""); }
      int stackBefore = p.getStack();
      ActionUtils.doAction(board, info);
      int put = stackBefore - p.getStack();
      lastHandRound = board.getHandNb();
      out.println("ACT\t" + turn + "\t" + board.getHandNb() + "\t" + pid + "\t" + boardStr(board) + "\t" + cards(p.getHand().getCards())
        + "\t" + board.getBigBlind() + "\t" + pot + "\t" + callAmt + "\t" + stackBefore + "\t" + st + "\t" + ps + "\t" + info.getAction().toInputString() + "\t" + put
        + "\t" + alive + "\t" + notFolded + "\t" + board.getDealerId() + "\t" + board.getSbId() + "\t" + board.getBbId() + "\t" + raw.replace('\t',' '));
    }
  }
}
