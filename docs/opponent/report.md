# The computer opponent's report

Written by `tools/opponent_report.py` from `ofs_sim_cli tournament --report` files; docs/opponent/README.md says what each count is.

## Stage 1's marks

| Mark | Value | Wanted | |
| --- | --- | --- | --- |
| Beginner's frames against itself | 2000 (beginner.json) | at least 2000 | PASS |
| Beginner's medium pots | 27461/75133 (36.5%) | 30 to 40% | PASS |
| Beginner's long pots | 7062/64961 (10.9%) | 5 to 15% | PASS |
| Beginner's mean break | 3.20 | under 10 | PASS |
| Beginner's safeties with a pot to play | 0/156072 (0.0%) | under 10% of strokes with a pot to play | PASS |
| Beginner's strokes with no pot at all | 20142/178215 (11.3%) | reported, not marked | REPORTED |
| The 147's frames won against Beginner | 100/100 (100.0%) (maximum.json) | at least 95% | PASS |
| The 147 against Beginner: no failure | 0 | 0 | PASS |
| The 147 on every exhibition table | 147 on 50 of 50 tables (maximum.json) | 147 on each | PASS |

## Beginner against Beginner

`beginner.json`: 2000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `beginner` (`1a3d1b715663a46d`), B is `beginner` (`1a3d1b715663a46d`). A breaks in the even frames.

| | A: Beginner | B: Beginner |
| --- | --- | --- |
| frames won | 987 | 1013 |
| strokes | 88940 | 89275 |
| pots made | 23605/77964 (30.3%) | 23864/78108 (30.6%) |
| - short | 6374/7858 (81.1%) | 6572/8120 (80.9%) |
| - medium | 13748/37620 (36.5%) | 13713/37513 (36.6%) |
| - long | 3483/32486 (10.7%) | 3579/32475 (11.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 17965 | 18191 |
| - mean / median | 3.2 / 1.0 | 3.2 / 1.0 |
| - highest | 27 | 30 |
| - 50+ / 100+ / 147 | 0 / 0 / 0 | 0 / 0 / 0 |
| safeties | 7310/88940 (8.2%) | 7454/89275 (8.3%) |
| escapes | 2666 | 2712 |
| safe with a pot to play | 0/77964 (0.0%) | 0/78108 (0.0%) |
| no pot at all | 9976/88940 (11.2%) | 10166/89275 (11.4%) |
| fouls | 10792/88940 (12.1%) | 10984/89275 (12.3%) |
| points scored | 57709 | 57984 |
| visits with the maximum on / 147s | 4567 / 0 | 4558 / 0 |
| simulations per stroke (mean / max) | 25.5 / 155 | 25.5 / 155 |
| steps per stroke (mean) | 6.6 | 6.6 |
| thinking, ms (mean / p99 / max) | 1.71 / 8.83 / 36.10 | 1.72 / 9.09 / 28.68 |
| how thinking stopped | done 88428, sims 1003 | done 88734, sims 1052 |
| fallbacks | 0 | 0 |

Failures: 0.

## The 147 against Beginner

`maximum.json`: 100 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `maximum` (`db0bc54f0635609d`), B is `beginner` (`1a3d1b715663a46d`). A breaks in the even frames.

| | A: The 147 | B: Beginner |
| --- | --- | --- |
| frames won | 100 | 0 |
| strokes | 5451 | 108 |
| pots made | 5395/5395 (100.0%) | 2/8 (25.0%) |
| - short | 1092/1092 (100.0%) | 0/0 |
| - medium | 3148/3148 (100.0%) | 2/7 (28.6%) |
| - long | 1155/1155 (100.0%) | 0/1 (0.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 150 | 2 |
| - mean / median | 146.9 / 147.0 | 4.0 / 4.0 |
| - highest | 147 | 7 |
| - 50+ / 100+ / 147 | 150 / 150 / 148 | 0 / 0 / 0 |
| safeties | 6/5451 (0.1%) | 50/108 (46.3%) |
| escapes | 0 | 0 |
| safe with a pot to play | 0/5395 (0.0%) | 0/8 (0.0%) |
| no pot at all | 6/5451 (0.1%) | 50/108 (46.3%) |
| fouls | 0/5451 (0.0%) | 9/108 (8.3%) |
| points scored | 22032 | 8 |
| visits with the maximum on / 147s | 203 / 148 | 105 / 0 |
| simulations per stroke (mean / max) | 5784.6 / 6007 | 3.6 / 92 |
| steps per stroke (mean) | 1704.5 | 0.7 |
| thinking, ms (mean / p99 / max) | 278.78 / 587.61 / 975.63 | 0.57 / 3.65 / 4.47 |
| how thinking stopped | done 361, sims 5090 | done 108 |
| fallbacks | 0 | 0 |

Exhibitions, The 147 alone:

| Table | Break | Strokes | 147 |
| --- | --- | --- | --- |
| Open table 1 | 147 | 36 | yes |
| Open table 2 | 147 | 36 | yes |
| Open table 3 | 147 | 36 | yes |
| Open table 4 | 147 | 36 | yes |
| Open table 5 | 147 | 36 | yes |
| Open table 6 | 147 | 36 | yes |
| Open table 7 | 147 | 36 | yes |
| Open table 8 | 147 | 36 | yes |
| Open table 9 | 147 | 36 | yes |
| Open table 10 | 147 | 36 | yes |
| Open table 11 | 147 | 36 | yes |
| Open table 12 | 147 | 36 | yes |
| Open table 13 | 147 | 36 | yes |
| Open table 14 | 147 | 36 | yes |
| Open table 15 | 147 | 36 | yes |
| Open table 16 | 147 | 36 | yes |
| Open table 17 | 147 | 36 | yes |
| Open table 18 | 147 | 36 | yes |
| Open table 19 | 147 | 36 | yes |
| Open table 20 | 147 | 36 | yes |
| Open table 21 | 147 | 36 | yes |
| Open table 22 | 147 | 36 | yes |
| Open table 23 | 147 | 36 | yes |
| Open table 24 | 147 | 36 | yes |
| Open table 25 | 147 | 36 | yes |
| Open table 26 | 147 | 36 | yes |
| Open table 27 | 147 | 36 | yes |
| Open table 28 | 147 | 36 | yes |
| Open table 29 | 147 | 36 | yes |
| Open table 30 | 147 | 36 | yes |
| Open table 31 | 147 | 36 | yes |
| Open table 32 | 147 | 36 | yes |
| Open table 33 | 147 | 36 | yes |
| Open table 34 | 147 | 36 | yes |
| Open table 35 | 147 | 36 | yes |
| Open table 36 | 147 | 36 | yes |
| Open table 37 | 147 | 36 | yes |
| Open table 38 | 147 | 36 | yes |
| Open table 39 | 147 | 36 | yes |
| Open table 40 | 147 | 36 | yes |
| Open table 41 | 147 | 36 | yes |
| Open table 42 | 147 | 36 | yes |
| Open table 43 | 147 | 36 | yes |
| Open table 44 | 147 | 36 | yes |
| Open table 45 | 147 | 36 | yes |
| Open table 46 | 147 | 36 | yes |
| Open table 47 | 147 | 36 | yes |
| Open table 48 | 147 | 36 | yes |
| Open table 49 | 147 | 36 | yes |
| Open table 50 | 147 | 36 | yes |

Failures: 0.
