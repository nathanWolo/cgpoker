package com.codingame.game;
import com.codingame.win_percent.skeval.SKPokerEvalUtils;
import java.util.*;
public class PreEq {
  // card id: 4*(12-r)+s with r=0 for '2'..12 for 'A'
  public static void main(String[] a) {
    String R = "23456789TJQKA";
    Random rnd = new Random(1);
    int N = Integer.parseInt(a[0]);
    for (int r1 = 12; r1 >= 0; r1--) for (int r2 = r1; r2 >= 0; r2--) for (int suited = 0; suited < 2; suited++) {
      if (r1 == r2 && suited == 1) continue;
      int c1 = 4*(12-r1)+0, c2 = 4*(12-r2)+(suited==1?0:1);
      StringBuilder sb = new StringBuilder();
      sb.append(R.charAt(r1)).append(R.charAt(r2)).append(r1==r2?"":(suited==1?"s":"o"));
      for (int opp = 1; opp <= 3; opp++) {
        double eq = 0;
        int[] deck = new int[52];
        for (int it = 0; it < N; it++) {
          int k = 0; for (int c = 0; c < 52; c++) if (c != c1 && c != c2) deck[k++] = c;
          int need = 5 + 2*opp;
          for (int i = 0; i < need; i++) { int j = i + rnd.nextInt(50 - i); int t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
          int[] h = {c1, c2, deck[0], deck[1], deck[2], deck[3], deck[4]};
          int me = SKPokerEvalUtils.calculateBestPossibleScore(h);
          int best = -1, cnt = 0;
          for (int o = 0; o < opp; o++) {
            int[] ho = {deck[5+2*o], deck[6+2*o], deck[0], deck[1], deck[2], deck[3], deck[4]};
            int s = SKPokerEvalUtils.calculateBestPossibleScore(ho);
            if (s > best) { best = s; cnt = 1; } else if (s == best) cnt++;
          }
          if (me > best) eq += 1; else if (me == best) eq += 1.0/(cnt+1);
        }
        sb.append('\t').append(String.format("%.4f", eq / N));
      }
      System.out.println(sb);
    }
  }
}
