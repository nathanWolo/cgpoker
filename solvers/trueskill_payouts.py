"""TrueSkill-implied placement payouts used as ICM prizes by icm2.py.

CodinGame ranks bots by TrueSkill on finishing order only. For one player, the mean (mu) change after a game,
as a function of its finishing place, normalised so that 1st = 1 and last = 0, acts as the "prize" of each place.
Needs `pip install trueskill` (0.4.5; on Debian/Ubuntu pythons: SETUPTOOLS_USE_DISTUTILS=stdlib pip install trueskill).
"""
import trueskill


def payouts(mus, sigma, me=0):
    env = trueskill.TrueSkill(draw_probability=0)
    n = len(mus)
    others = [i for i in range(n) if i != me]
    delta = []
    for place in range(n):
        order = others[:place] + [me] + others[place:]          # other players keep their rating order
        ranks = [order.index(i) for i in range(n)]
        new = env.rate([(env.create_rating(mu=m, sigma=sigma),) for m in mus], ranks=ranks)
        delta.append(new[me][0].mu - mus[me])
    return [(d - delta[-1]) / (delta[0] - delta[-1]) for d in delta]


if __name__ == "__main__":
    for n in (2, 3, 4):
        for sigma in (25 / 3, 1.0):
            print(f"{n}p equal ratings (mu=25, sigma={sigma:.2f}):", tuple(round(x, 4) for x in payouts([25] * n, sigma)))
    for sigma in (25 / 3, 1.0):
        print(f"4p unequal, me mu=30 vs 29/28/27 (sigma={sigma:.2f}):",
              tuple(round(x, 4) for x in payouts([30, 29, 28, 27], sigma)))
