# The computer opponent's report

Written by `tools/opponent_report.py` from `ofs_sim_cli tournament --report` files; docs/opponent/README.md says what each count is.

## Stage 1's marks

| Mark | Value | Wanted | |
| --- | --- | --- | --- |
| Beginner's frames against itself | 2000 (beginner.json) | at least 2000 | PASS |
| Beginner's medium pots | 29544/84595 (34.9%) | 30 to 40% | PASS |
| Beginner's long pots | 8143/75744 (10.8%) | 5 to 15% | PASS |
| Beginner's mean break | 3.12 | under 10 | PASS |
| Beginner's safeties with a pot to play | 0/171933 (0.0%) | under 10% of strokes with a pot to play | PASS |
| Beginner's strokes with no pot at all | 37914/211848 (17.9%) | reported, not marked | REPORTED |
| The 147's frames won against Beginner | 100/100 (100.0%) (maximum.json) | at least 95% | PASS |
| The 147 against Beginner: no failure | 0 | 0 | PASS |
| The 147 on every exhibition table | 147 on 1 of 1 tables (maximum.json) | 147 on each | PASS |

## Beginner against Beginner

`beginner.json`: 2000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `beginner` (`67acf911c767ecb0`), B is `beginner` (`67acf911c767ecb0`). A breaks in the even frames.

| | A: Beginner | B: Beginner |
| --- | --- | --- |
| frames won | 999 | 1001 |
| strokes | 105746 | 106102 |
| pots made | 23225/85847 (27.1%) | 23412/86086 (27.2%) |
| - short | 4417/5758 (76.7%) | 4533/5836 (77.7%) |
| - medium | 14770/42275 (34.9%) | 14774/42320 (34.9%) |
| - long | 4038/37814 (10.7%) | 4105/37930 (10.8%) |
| - unknown | 0/0 | 0/0 |
| breaks | 17841 | 18019 |
| - mean / median | 3.1 / 1.0 | 3.1 / 1.0 |
| - highest | 27 | 22 |
| - 50+ / 100+ / 147 | 0 / 0 / 0 | 0 / 0 / 0 |
| safeties | 15615/105746 (14.8%) | 15675/106102 (14.8%) |
| escapes | 3283 | 3341 |
| safe with a pot to play | 0/85847 (0.0%) | 0/86086 (0.0%) |
| no pot at all | 18898/105746 (17.9%) | 19016/106102 (17.9%) |
| fouls | 14286/105746 (13.5%) | 14514/106102 (13.7%) |
| points scored | 55614 | 56428 |
| visits with the maximum on / 147s | 4897 / 0 | 4910 / 0 |
| simulations per stroke (mean / max) | 25.6 / 156 | 25.5 / 156 |
| steps per stroke (mean) | 5.6 | 5.6 |
| thinking, ms (mean / p99 / max) | 2.11 / 12.38 / 73.31 | 2.10 / 12.27 / 37.15 |
| how thinking stopped | done 104452, sims 1770 | done 104854, sims 1756 |
| fallbacks | 0 | 0 |

Failures: 0.

## The 147 against Beginner

`maximum.json`: 100 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `maximum` (`3eeff931a596d3c7`), B is `beginner` (`67acf911c767ecb0`). A breaks in the even frames.

| | A: The 147 | B: Beginner |
| --- | --- | --- |
| frames won | 100 | 0 |
| strokes | 3677 | 115 |
| pots made | 3617/3617 (100.0%) | 5/14 (35.7%) |
| - short | 813/813 (100.0%) | 0/0 |
| - medium | 2063/2063 (100.0%) | 5/11 (45.5%) |
| - long | 741/741 (100.0%) | 0/3 (0.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 102 | 4 |
| - mean / median | 144.8 / 147.0 | 3.8 / 4.0 |
| - highest | 147 | 6 |
| - 50+ / 100+ / 147 | 101 / 101 / 98 | 0 / 0 / 0 |
| safeties | 9/3677 (0.2%) | 51/115 (44.3%) |
| escapes | 1 | 0 |
| safe with a pot to play | 0/3617 (0.0%) | 0/14 (0.0%) |
| no pot at all | 10/3677 (0.3%) | 51/115 (44.3%) |
| fouls | 0/3677 (0.0%) | 10/115 (8.7%) |
| points scored | 14769 | 15 |
| visits with the maximum on / 147s | 153 / 98 | 104 / 0 |
| simulations per stroke (mean / max) | 5767.7 / 6008 | 4.5 / 69 |
| steps per stroke (mean) | 1457.0 | 0.8 |
| thinking, ms (mean / p99 / max) | 430.00 / 911.04 / 1422.96 | 0.88 / 5.40 / 6.66 |
| how thinking stopped | done 202, sims 3478 | done 115 |
| fallbacks | 0 | 0 |

Exhibitions, The 147 alone:

| Table | Break | Strokes | 147 |
| --- | --- | --- | --- |
| open 1 | 147 | 36 | yes |

Failures: 0.
