# The computer opponent's report

Written by `tools/opponent_report.py` from `ofs_sim_cli tournament --report` files; docs/opponent/README.md says what each count is.

## Stage 1's marks

| Mark | Value | Wanted | |
| --- | --- | --- | --- |
| Beginner's frames against itself | 2000 (beginner.json) | at least 2000 | PASS |
| Beginner's medium pots | 27526/75333 (36.5%) | 30 to 40% | PASS |
| Beginner's long pots | 7040/65362 (10.8%) | 5 to 15% | PASS |
| Beginner's mean break | 3.21 | under 10 | PASS |
| Beginner's safeties with a pot to play | 0/156651 (0.0%) | under 10% of strokes with a pot to play | PASS |
| Beginner's strokes with no pot at all | 19831/178483 (11.1%) | reported, not marked | REPORTED |
| The 147's frames won against Beginner | 100/100 (100.0%) (maximum.json) | at least 95% | PASS |
| The 147 against Beginner: no failure | 0 | 0 | PASS |
| The 147 on every exhibition table | 147 on 50 of 50 tables (maximum.json) | 147 on each | PASS |

## Stage 2's marks

| Mark | Value | Wanted | |
| --- | --- | --- | --- |
| Club's frames against itself | 1000 (club.json) | at least 1000 | PASS |
| Club's medium pots | 18655/32256 (57.8%) | 55% to 65% | PASS |
| Club's long pots | 4334/16834 (25.7%) | 20% to 30% | PASS |
| Club's typical break | 15 (each player's own: 12) | 10 to 20 | PASS |
| Amateur's frames against itself | 1000 (amateur.json) | at least 1000 | PASS |
| Amateur's medium pots | 23201/29162 (79.6%) | 75% to 85% | PASS |
| Amateur's long pots | 3495/7825 (44.7%) | 40% to 50% | PASS |
| Amateur's typical break | 29 (each player's own: 21) | 20 to 40 | PASS |
| Pro's frames against itself | 1000 (pro.json) | at least 1000 | PASS |
| Pro's medium pots | 22684/24836 (91.3%) | 86% to 94% | PASS |
| Pro's long pots | 2957/4444 (66.5%) | 60% to 70% | PASS |
| Pro's typical break | 50 (each player's own: 31) | 40 to 70 | PASS |
| World class's frames against itself | 1000 (world.json) | at least 1000 | PASS |
| World class's medium pots | 24069/24849 (96.9%) | 92% to 98% | PASS |
| World class's long pots | 2909/3954 (73.6%) | 70% to 80% | PASS |
| World class's typical break | 81 (each player's own: 42) | 60 and over | PASS |
| World class's frames with a century | 285/1000 (28.5%) | at least 10% | PASS |
| Club over Beginner: frames | 300 (club-beginner.json) | at least 300 | PASS |
| Club over Beginner: frames won | 283/300 (94.3%) | at least 70% | PASS |
| Amateur over Club: frames | 300 (amateur-club.json) | at least 300 | PASS |
| Amateur over Club: frames won | 284/300 (94.7%) | at least 70% | PASS |
| Pro over Amateur: frames | 300 (pro-amateur.json) | at least 300 | PASS |
| Pro over Amateur: frames won | 263/300 (87.7%) | at least 70% | PASS |
| World class over Pro: frames | 300 (world-pro.json) | at least 300 | PASS |
| World class over Pro: frames won | 233/300 (77.7%) | at least 70% | PASS |
| No stroke refused by the engine | 0 in 8 reports | 0 | PASS |
| No fallback | 0 in 8 reports | 0 | PASS |
| World class's slowest decision | 1.00 s | within 1.5 s | PASS |

## The ladder

| Pairing | Frames | Won by the higher | |
| --- | --- | --- | --- |
| Club over Beginner | 300 | 283/300 (94.3%) | `club-beginner.json` |
| Amateur over Club | 300 | 284/300 (94.7%) | `amateur-club.json` |
| Pro over Amateur | 300 | 263/300 (87.7%) | `pro-amateur.json` |
| World class over Pro | 300 | 233/300 (77.7%) | `world-pro.json` |

## Tactics per level

Each level against itself, both sides added; README "The ladder" says what each is. The typical break and the centuries are each frame's highest break by either player; "each player's own" is the median of each player's highest in each frame.

| | Beginner | Club | Amateur | Pro | World class |
| --- | --- | --- | --- | --- | --- |
| strokes | 178483 | 74734 | 65486 | 46922 | 38570 |
| safeties | 14822 | 17301 | 20567 | 10827 | 4108 |
| - then a pot attempt | 10918/14822 (73.7%) | 8273/17301 (47.8%) | 5727/20567 (27.8%) | 2837/10827 (26.2%) | 1315/4108 (32.0%) |
| - then potted | 2602/14822 (17.6%) | 3469/17301 (20.1%) | 3288/20567 (16.0%) | 1968/10827 (18.2%) | 850/4108 (20.7%) |
| snookers laid | 5401 | 2769 | 3138 | 2666 | 1731 |
| - called snookers | 0/5401 (0.0%) | 0/2769 (0.0%) | 0/3138 (0.0%) | 1867/2666 (70.0%) | 865/1731 (50.0%) |
| - then a foul | 1527/5401 (28.3%) | 461/2769 (16.6%) | 230/3138 (7.3%) | 161/2666 (6.0%) | 53/1731 (3.1%) |
| - foul points given after them | 7116 | 2100 | 1074 | 755 | 285 |
| escapes that hit a ball on | 2728/5009 (54.5%) | 1388/2034 (68.2%) | 1939/2183 (88.8%) | 1754/1972 (88.9%) | 1300/1375 (94.5%) |
| - by cushions (hit/played) | unknown: 2728/5009 | unknown: 1388/2034 | 0: 52/82, 1: 1847/2050, 2: 40/47, 4: 0/2, 5+: 0/2 | 0: 75/109, 1: 1288/1375, 2: 391/483, 3: 0/2, 5+: 0/2, 4: 0/1 | 0: 55/68, 1: 1090/1146, 2: 154/160, 5+: 1/1 |
| break-offs then a pot | 1573/2001 (78.6%) | 753/1002 (75.1%) | 710/1009 (70.4%) | 327/1003 (32.6%) | 114/1003 (11.4%) |
| typical break | 9 | 15 | 29 | 50 | 81 |
| - each player's own | 8 | 12 | 21 | 31 | 42 |
| frames with a century | 0/2000 (0.0%) | 0/1000 (0.0%) | 0/1000 (0.0%) | 47/1000 (4.7%) | 285/1000 (28.5%) |
| concessions / claims | 0 / 980 | 0 / 501 | 0 / 514 | 503 / 336 | 494 / 467 |

## Thinking per level

Each level against itself; the mean is over both sides' strokes, p99 and max the higher side's.

| | Beginner | Club | Amateur | Pro | World class |
| --- | --- | --- | --- | --- | --- |
| simulations (mean / max) | 25.5 / 155 | 206.3 / 809 | 872.9 / 1507 | 1515.6 / 2441 | 3139.1 / 4702 |
| steps (mean) | 6.6 | 46.0 | 260.5 | 402.9 | 825.7 |
| ms (mean / p99 / max) | 1.47 / 7.77 / 30.40 | 10.80 / 49.75 / 385.26 | 40.97 / 145.29 / 439.49 | 72.81 / 188.61 / 404.37 | 172.75 / 381.77 / 1003.13 |
| how thinking stopped | done 177381, sims 2082 | done 74175, sims 1060 | done 57708, sims 8292 | done 47761 | done 39531 |
| fallbacks | 0 | 0 | 0 | 0 | 0 |

## Beginner against Beginner

`beginner.json`: 2000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `beginner` (`20959b7fa92b234c`), B is `beginner` (`20959b7fa92b234c`). A breaks in the even frames.

| | A: Beginner | B: Beginner |
| --- | --- | --- |
| frames won | 978 | 1022 |
| strokes | 89034 | 89449 |
| pots made | 23574/78215 (30.1%) | 23915/78436 (30.5%) |
| - short | 6402/7893 (81.1%) | 6521/8063 (80.9%) |
| - medium | 13715/37621 (36.5%) | 13811/37712 (36.6%) |
| - long | 3457/32701 (10.6%) | 3583/32661 (11.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 17976 | 18176 |
| - mean / median | 3.2 / 1.0 | 3.2 / 1.0 |
| - highest | 27 | 30 |
| - 50+ / 100+ / 147 | 0 / 0 / 0 | 0 / 0 / 0 |
| safeties | 7373/89034 (8.3%) | 7449/89449 (8.3%) |
| escapes | 2446 | 2563 |
| safe with a pot to play | 0/78215 (0.0%) | 0/78436 (0.0%) |
| no pot at all | 9819/89034 (11.0%) | 10012/89449 (11.2%) |
| fouls | 10597/89034 (11.9%) | 10787/89449 (12.1%) |
| points scored | 57734 | 58218 |
| visits with the maximum on / 147s | 4556 / 0 | 4553 / 0 |
| simulations per stroke (mean / max) | 25.5 / 155 | 25.6 / 155 |
| steps per stroke (mean) | 6.5 | 6.6 |
| thinking, ms (mean / p99 / max) | 1.47 / 7.35 / 30.40 | 1.48 / 7.77 / 24.49 |
| how thinking stopped | done 88487, sims 1007 | done 88894, sims 1075 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 8.0 | 8.0 |
| frames with its own century | 0/2000 (0.0%) | 0/2000 (0.0%) |
| safeties then a pot attempt | 5393/7373 (73.1%) | 5525/7449 (74.2%) |
| - then potted | 1282/7373 (17.4%) | 1320/7449 (17.7%) |
| snookers laid / called | 2674 / 0 | 2727 / 0 |
| - then fouled / foul points | 795 / 3689 | 732 / 3427 |
| escapes that hit a ball on | 1403/2446 (57.4%) | 1325/2563 (51.7%) |
| - by cushions | unknown: 1403/2446 | unknown: 1325/2563 |
| break-offs then a pot | 789/1000 (78.9%) | 784/1001 (78.3%) |
| concessions / claims | 0 / 460 | 0 / 520 |

Each frame's highest break by either player: typical 9, 50 or more in 0/2000 (0.0%), a century in 0/2000 (0.0%).

Failures: 0.

## The 147 against Beginner

`maximum.json`: 100 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `maximum` (`bd923875e57c49d4`), B is `beginner` (`20959b7fa92b234c`). A breaks in the even frames.

| | A: The 147 | B: Beginner |
| --- | --- | --- |
| frames won | 100 | 0 |
| strokes | 5464 | 136 |
| pots made | 5392/5392 (100.0%) | 5/11 (45.5%) |
| - short | 1026/1026 (100.0%) | 0/0 |
| - medium | 3763/3763 (100.0%) | 4/6 (66.7%) |
| - long | 603/603 (100.0%) | 1/5 (20.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 150 | 3 |
| - mean / median | 146.7 / 147.0 | 5.0 / 7.0 |
| - highest | 147 | 7 |
| - 50+ / 100+ / 147 | 150 / 150 / 147 | 0 / 0 / 0 |
| safeties | 19/5464 (0.3%) | 12/136 (8.8%) |
| escapes | 3 | 63 |
| safe with a pot to play | 0/5392 (0.0%) | 0/11 (0.0%) |
| no pot at all | 22/5464 (0.4%) | 75/136 (55.1%) |
| fouls | 0/5464 (0.0%) | 22/136 (16.2%) |
| points scored | 22012 | 15 |
| visits with the maximum on / 147s | 219 / 147 | 131 / 0 |
| simulations per stroke (mean / max) | 8097.8 / 9006 | 3.9 / 80 |
| steps per stroke (mean) | 2475.1 | 0.8 |
| thinking, ms (mean / p99 / max) | 376.77 / 949.70 / 1420.02 | 1.02 / 5.31 / 10.52 |
| how thinking stopped | done 3230, sims 2234 | done 136 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 147.0 | 0.0 |
| frames with its own century | 100/100 (100.0%) | 0/100 (0.0%) |
| safeties then a pot attempt | 6/19 (31.6%) | 11/12 (91.7%) |
| - then potted | 3/19 (15.8%) | 11/12 (91.7%) |
| snookers laid / called | 57 / 7 | 1 / 0 |
| - then fouled / foul points | 16 / 82 | 0 / 0 |
| escapes that hit a ball on | 3/3 (100.0%) | 48/63 (76.2%) |
| - by cushions | 1: 3/3 | unknown: 48/63 |
| break-offs then a pot | 0/50 (0.0%) | 46/50 (92.0%) |
| concessions / claims | 0 / 0 | 0 / 0 |

Each frame's highest break by either player: typical 147, 50 or more in 100/100 (100.0%), a century in 100/100 (100.0%).

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

## Club against Club

`club.json`: 1000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `club` (`affbcfac4440699a`), B is `club` (`affbcfac4440699a`). A breaks in the even frames.

| | A: Club | B: Club |
| --- | --- | --- |
| frames won | 511 | 489 |
| strokes | 37378 | 37356 |
| pots made | 13818/27260 (50.7%) | 13842/27137 (51.0%) |
| - short | 2330/2647 (88.0%) | 2341/2660 (88.0%) |
| - medium | 9332/16209 (57.6%) | 9323/16047 (58.1%) |
| - long | 2156/8404 (25.7%) | 2178/8430 (25.8%) |
| - unknown | 0/0 | 0/0 |
| breaks | 7455 | 7533 |
| - mean / median | 5.3 / 5.0 | 5.2 / 5.0 |
| - highest | 48 | 43 |
| - 50+ / 100+ / 147 | 0 / 0 / 0 | 0 / 0 / 0 |
| safeties | 8627/37378 (23.1%) | 8674/37356 (23.2%) |
| escapes | 989 | 1045 |
| safe with a pot to play | 3902/31162 (12.5%) | 3954/31091 (12.7%) |
| no pot at all | 5714/37378 (15.3%) | 5765/37356 (15.4%) |
| fouls | 2234/37378 (6.0%) | 2248/37356 (6.0%) |
| points scored | 39202 | 39131 |
| visits with the maximum on / 147s | 1961 / 0 | 1994 / 0 |
| simulations per stroke (mean / max) | 206.7 / 808 | 205.8 / 809 |
| steps per stroke (mean) | 46.0 | 45.9 |
| thinking, ms (mean / p99 / max) | 10.83 / 49.75 / 385.26 | 10.78 / 49.06 / 302.95 |
| how thinking stopped | done 37103, sims 537 | done 37072, sims 523 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 12.0 | 12.0 |
| frames with its own century | 0/1000 (0.0%) | 0/1000 (0.0%) |
| safeties then a pot attempt | 4090/8627 (47.4%) | 4183/8674 (48.2%) |
| - then potted | 1741/8627 (20.2%) | 1728/8674 (19.9%) |
| snookers laid / called | 1383 / 0 | 1386 / 0 |
| - then fouled / foul points | 224 / 1021 | 237 / 1079 |
| escapes that hit a ball on | 660/989 (66.7%) | 728/1045 (69.7%) |
| - by cushions | unknown: 660/989 | unknown: 728/1045 |
| break-offs then a pot | 377/502 (75.1%) | 376/500 (75.2%) |
| concessions / claims | 0 / 262 | 0 / 239 |

Each frame's highest break by either player: typical 15, 50 or more in 0/1000 (0.0%), a century in 0/1000 (0.0%).

Failures: 0.

## Amateur against Amateur

`amateur.json`: 1000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `amateur` (`0a23958b34f78023`), B is `amateur` (`0a23958b34f78023`). A breaks in the even frames.

| | A: Amateur | B: Amateur |
| --- | --- | --- |
| frames won | 496 | 504 |
| strokes | 32776 | 32710 |
| pots made | 15662/20884 (75.0%) | 15565/20843 (74.7%) |
| - short | 2264/2373 (95.4%) | 2267/2367 (95.8%) |
| - medium | 11656/14616 (79.7%) | 11545/14546 (79.4%) |
| - long | 1742/3895 (44.7%) | 1753/3930 (44.6%) |
| - unknown | 0/0 | 0/0 |
| breaks | 4795 | 4722 |
| - mean / median | 10.3 / 7.0 | 10.4 / 7.0 |
| - highest | 93 | 89 |
| - 50+ / 100+ / 147 | 52 / 0 / 0 | 47 / 0 / 0 |
| safeties | 10313/32776 (31.5%) | 10254/32710 (31.3%) |
| escapes | 1074 | 1109 |
| safe with a pot to play | 4816/25700 (18.7%) | 4727/25570 (18.5%) |
| no pot at all | 6571/32776 (20.0%) | 6636/32710 (20.3%) |
| fouls | 1120/32776 (3.4%) | 1086/32710 (3.3%) |
| points scored | 49270 | 48999 |
| visits with the maximum on / 147s | 2072 / 0 | 2079 / 0 |
| simulations per stroke (mean / max) | 873.3 / 1506 | 872.5 / 1507 |
| steps per stroke (mean) | 260.7 | 260.3 |
| thinking, ms (mean / p99 / max) | 41.03 / 145.29 / 282.99 | 40.91 / 141.74 / 439.49 |
| how thinking stopped | done 28964, sims 4082 | done 28744, sims 4210 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 21.0 | 21.0 |
| frames with its own century | 0/1000 (0.0%) | 0/1000 (0.0%) |
| safeties then a pot attempt | 2872/10313 (27.8%) | 2855/10254 (27.8%) |
| - then potted | 1633/10313 (15.8%) | 1655/10254 (16.1%) |
| snookers laid / called | 1572 / 0 | 1566 / 0 |
| - then fouled / foul points | 110 / 511 | 120 / 563 |
| escapes that hit a ball on | 966/1074 (89.9%) | 973/1109 (87.7%) |
| - by cushions | 0: 32/45, 1: 915/1007, 2: 19/21, 4: 0/1 | 0: 20/37, 1: 932/1043, 2: 21/26, 4: 0/1, 5+: 0/2 |
| break-offs then a pot | 372/505 (73.7%) | 338/504 (67.1%) |
| concessions / claims | 0 / 270 | 0 / 244 |

Each frame's highest break by either player: typical 29, 50 or more in 99/1000 (9.9%), a century in 0/1000 (0.0%).

Failures: 0.

## Pro against Pro

`pro.json`: 1000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `pro` (`c530bfb961fd22b6`), B is `pro` (`c530bfb961fd22b6`). A breaks in the even frames.

| | A: Pro | B: Pro |
| --- | --- | --- |
| frames won | 477 | 523 |
| strokes | 22941 | 23981 |
| pots made | 14161/15997 (88.5%) | 15215/17123 (88.9%) |
| - short | 1844/1896 (97.3%) | 1891/1944 (97.3%) |
| - medium | 10888/11959 (91.0%) | 11796/12877 (91.6%) |
| - long | 1429/2142 (66.7%) | 1528/2302 (66.4%) |
| - unknown | 0/0 | 0/0 |
| breaks | 2228 | 2321 |
| - mean / median | 21.0 / 14.0 | 21.8 / 14.0 |
| - highest | 128 | 129 |
| - 50+ / 100+ / 147 | 242 / 17 / 0 | 268 / 30 / 0 |
| safeties | 5424/22941 (23.6%) | 5403/23981 (22.5%) |
| escapes | 1018 | 954 |
| safe with a pot to play | 2745/18742 (14.6%) | 2678/19801 (13.5%) |
| no pot at all | 3697/22941 (16.1%) | 3679/23981 (15.3%) |
| fouls | 616/22941 (2.7%) | 596/23981 (2.5%) |
| points scored | 46818 | 50520 |
| visits with the maximum on / 147s | 2919 / 0 | 2939 / 0 |
| simulations per stroke (mean / max) | 1506.9 / 2398 | 1524.0 / 2441 |
| steps per stroke (mean) | 400.8 | 404.9 |
| thinking, ms (mean / p99 / max) | 72.57 / 186.63 / 401.72 | 73.05 / 188.61 / 404.37 |
| how thinking stopped | done 23361 | done 24400 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 30.0 | 32.5 |
| frames with its own century | 17/1000 (1.7%) | 30/1000 (3.0%) |
| safeties then a pot attempt | 1443/5424 (26.6%) | 1394/5403 (25.8%) |
| - then potted | 1029/5424 (19.0%) | 939/5403 (17.4%) |
| snookers laid / called | 1335 / 942 | 1331 / 925 |
| - then fouled / foul points | 77 / 347 | 84 / 408 |
| escapes that hit a ball on | 899/1018 (88.3%) | 855/954 (89.6%) |
| - by cushions | 0: 43/58, 1: 662/704, 2: 194/253, 3: 0/2, 5+: 0/1 | 0: 32/51, 1: 626/671, 2: 197/230, 4: 0/1, 5+: 0/1 |
| break-offs then a pot | 163/502 (32.5%) | 164/501 (32.7%) |
| concessions / claims | 265 / 155 | 238 / 181 |

Each frame's highest break by either player: typical 50, 50 or more in 502/1000 (50.2%), a century in 47/1000 (4.7%).

Failures: 0.

## World class against World class

`world.json`: 1000 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `world` (`546b4467296cc7cb`), B is `world` (`546b4467296cc7cb`). A breaks in the even frames.

| | A: World class | B: World class |
| --- | --- | --- |
| frames won | 487 | 513 |
| strokes | 19363 | 19207 |
| pots made | 15187/16130 (94.2%) | 15018/15954 (94.1%) |
| - short | 1628/1654 (98.4%) | 1599/1627 (98.3%) |
| - medium | 12068/12462 (96.8%) | 12001/12387 (96.9%) |
| - long | 1491/2014 (74.0%) | 1418/1940 (73.1%) |
| - unknown | 0/0 | 0/0 |
| breaks | 1226 | 1178 |
| - mean / median | 43.9 / 31.0 | 45.1 / 35.0 |
| - highest | 133 | 137 |
| - 50+ / 100+ / 147 | 449 / 150 / 0 | 459 / 135 / 0 |
| safeties | 2031/19363 (10.5%) | 2077/19207 (10.8%) |
| escapes | 700 | 675 |
| safe with a pot to play | 518/16648 (3.1%) | 500/16454 (3.0%) |
| no pot at all | 2213/19363 (11.4%) | 2252/19207 (11.7%) |
| fouls | 265/19363 (1.4%) | 269/19207 (1.4%) |
| points scored | 53788 | 53178 |
| visits with the maximum on / 147s | 2685 / 0 | 2666 / 0 |
| simulations per stroke (mean / max) | 3134.4 / 4702 | 3143.8 / 4615 |
| steps per stroke (mean) | 824.5 | 826.9 |
| thinking, ms (mean / p99 / max) | 173.77 / 381.77 / 1003.13 | 171.71 / 380.67 / 557.31 |
| how thinking stopped | done 19876 | done 19655 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 40.0 | 43.5 |
| frames with its own century | 150/1000 (15.0%) | 135/1000 (13.5%) |
| safeties then a pot attempt | 644/2031 (31.7%) | 671/2077 (32.3%) |
| - then potted | 407/2031 (20.0%) | 443/2077 (21.3%) |
| snookers laid / called | 854 / 424 | 877 / 441 |
| - then fouled / foul points | 26 / 146 | 27 / 139 |
| escapes that hit a ball on | 656/700 (93.7%) | 644/675 (95.4%) |
| - by cushions | 0: 31/43, 1: 544/571, 2: 81/86 | 0: 24/25, 1: 546/575, 2: 73/74, 5+: 1/1 |
| break-offs then a pot | 57/502 (11.4%) | 57/501 (11.4%) |
| concessions / claims | 267 / 246 | 227 / 221 |

Each frame's highest break by either player: typical 81, 50 or more in 871/1000 (87.1%), a century in 285/1000 (28.5%).

Failures: 0.

## Club against Beginner

`club-beginner.json`: 300 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `club` (`affbcfac4440699a`), B is `beginner` (`20959b7fa92b234c`). A breaks in the even frames.

| | A: Club | B: Beginner |
| --- | --- | --- |
| frames won | 283 | 17 |
| strokes | 13449 | 10861 |
| pots made | 5241/10036 (52.2%) | 2567/9023 (28.4%) |
| - short | 998/1107 (90.2%) | 660/845 (78.1%) |
| - medium | 3491/6008 (58.1%) | 1512/4273 (35.4%) |
| - long | 752/2921 (25.7%) | 395/3905 (10.1%) |
| - unknown | 0/0 | 0/0 |
| breaks | 2884 | 2013 |
| - mean / median | 5.0 / 4.0 | 3.1 / 1.0 |
| - highest | 34 | 20 |
| - 50+ / 100+ / 147 | 0 / 0 / 0 | 0 / 0 / 0 |
| safeties | 3033/13449 (22.6%) | 1323/10861 (12.2%) |
| escapes | 230 | 365 |
| safe with a pot to play | 1732/11768 (14.7%) | 0/9023 (0.0%) |
| no pot at all | 1531/13449 (11.4%) | 1688/10861 (15.5%) |
| fouls | 742/13449 (5.5%) | 1325/10861 (12.2%) |
| points scored | 14548 | 6244 |
| visits with the maximum on / 147s | 647 / 0 | 632 / 0 |
| simulations per stroke (mean / max) | 216.4 / 810 | 24.1 / 155 |
| steps per stroke (mean) | 47.6 | 6.2 |
| thinking, ms (mean / p99 / max) | 11.97 / 49.41 / 223.37 | 1.31 / 7.42 / 25.19 |
| how thinking stopped | done 13424, sims 197 | done 10724, sims 141 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 13.0 | 7.0 |
| frames with its own century | 0/300 (0.0%) | 0/300 (0.0%) |
| safeties then a pot attempt | 2108/3033 (69.5%) | 655/1323 (49.5%) |
| - then potted | 471/3033 (15.5%) | 282/1323 (21.3%) |
| snookers laid / called | 526 / 0 | 349 / 0 |
| - then fouled / foul points | 154 / 706 | 73 / 336 |
| escapes that hit a ball on | 179/230 (77.8%) | 241/365 (66.0%) |
| - by cushions | unknown: 179/230 | unknown: 241/365 |
| break-offs then a pot | 112/150 (74.7%) | 122/150 (81.3%) |
| concessions / claims | 0 / 172 | 0 / 4 |

Each frame's highest break by either player: typical 13, 50 or more in 0/300 (0.0%), a century in 0/300 (0.0%).

Failures: 0.

## Amateur against Club

`amateur-club.json`: 300 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `amateur` (`0a23958b34f78023`), B is `club` (`affbcfac4440699a`). A breaks in the even frames.

| | A: Amateur | B: Club |
| --- | --- | --- |
| frames won | 284 | 16 |
| strokes | 12199 | 8433 |
| pots made | 6407/8391 (76.4%) | 2556/5149 (49.6%) |
| - short | 960/999 (96.1%) | 416/462 (90.0%) |
| - medium | 4765/5996 (79.5%) | 1735/3000 (57.8%) |
| - long | 682/1396 (48.9%) | 405/1687 (24.0%) |
| - unknown | 0/0 | 0/0 |
| breaks | 1999 | 1441 |
| - mean / median | 10.0 / 7.0 | 5.2 / 5.0 |
| - highest | 79 | 33 |
| - 50+ / 100+ / 147 | 22 / 0 / 0 | 0 / 0 / 0 |
| safeties | 3355/12199 (27.5%) | 2755/8433 (32.7%) |
| escapes | 303 | 379 |
| safe with a pot to play | 2095/10486 (20.0%) | 799/5948 (13.4%) |
| no pot at all | 1563/12199 (12.8%) | 2335/8433 (27.7%) |
| fouls | 393/12199 (3.2%) | 500/8433 (5.9%) |
| points scored | 19923 | 7489 |
| visits with the maximum on / 147s | 588 / 0 | 562 / 0 |
| simulations per stroke (mean / max) | 940.3 / 1505 | 175.3 / 809 |
| steps per stroke (mean) | 277.4 | 40.8 |
| thinking, ms (mean / p99 / max) | 45.76 / 115.27 / 212.27 | 10.38 / 50.51 / 134.68 |
| how thinking stopped | done 10717, sims 1655 | done 8321, sims 121 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 23.0 | 9.0 |
| frames with its own century | 0/300 (0.0%) | 0/300 (0.0%) |
| safeties then a pot attempt | 1159/3355 (34.5%) | 906/2755 (32.9%) |
| - then potted | 466/3355 (13.9%) | 559/2755 (20.3%) |
| snookers laid / called | 577 / 0 | 451 / 0 |
| - then fouled / foul points | 98 / 454 | 38 / 176 |
| escapes that hit a ball on | 259/303 (85.5%) | 283/379 (74.7%) |
| - by cushions | 0: 3/15, 1: 252/278, 2: 4/9, 4: 0/1 | unknown: 283/379 |
| break-offs then a pot | 115/150 (76.7%) | 115/150 (76.7%) |
| concessions / claims | 0 / 173 | 0 / 9 |

Each frame's highest break by either player: typical 24, 50 or more in 22/300 (7.3%), a century in 0/300 (0.0%).

Failures: 0.

## Pro against Amateur

`pro-amateur.json`: 300 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `pro` (`c530bfb961fd22b6`), B is `amateur` (`0a23958b34f78023`). A breaks in the even frames.

| | A: Pro | B: Amateur |
| --- | --- | --- |
| frames won | 263 | 37 |
| strokes | 10766 | 6246 |
| pots made | 7099/7811 (90.9%) | 2473/3383 (73.1%) |
| - short | 954/968 (98.6%) | 342/370 (92.4%) |
| - medium | 5492/5941 (92.4%) | 1836/2303 (79.7%) |
| - long | 653/902 (72.4%) | 295/710 (41.5%) |
| - unknown | 0/0 | 0/0 |
| breaks | 1155 | 737 |
| - mean / median | 20.8 / 14.0 | 10.6 / 7.0 |
| - highest | 126 | 80 |
| - 50+ / 100+ / 147 | 114 / 12 / 0 | 12 / 0 / 0 |
| safeties | 2545/10766 (23.6%) | 2195/6246 (35.1%) |
| escapes | 260 | 518 |
| safe with a pot to play | 1374/9185 (15.0%) | 904/4287 (21.1%) |
| no pot at all | 1431/10766 (13.3%) | 1809/6246 (29.0%) |
| fouls | 229/10766 (2.1%) | 250/6246 (4.0%) |
| points scored | 23996 | 7805 |
| visits with the maximum on / 147s | 734 / 0 | 679 / 0 |
| simulations per stroke (mean / max) | 1502.1 / 2428 | 777.6 / 1506 |
| steps per stroke (mean) | 396.3 | 233.9 |
| thinking, ms (mean / p99 / max) | 78.41 / 197.43 / 511.42 | 38.56 / 114.49 / 169.03 |
| how thinking stopped | done 10981 | done 5640, sims 614 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 42.0 | 14.0 |
| frames with its own century | 12/300 (4.0%) | 0/300 (0.0%) |
| safeties then a pot attempt | 592/2545 (23.3%) | 585/2195 (26.7%) |
| - then potted | 321/2545 (12.6%) | 435/2195 (19.8%) |
| snookers laid / called | 734 / 592 | 319 / 0 |
| - then fouled / foul points | 67 / 319 | 13 / 59 |
| escapes that hit a ball on | 224/260 (86.2%) | 442/518 (85.3%) |
| - by cushions | 0: 19/38, 1: 145/151, 2: 60/70, 5+: 0/1 | 0: 7/20, 1: 426/484, 2: 9/11, 3: 0/2, unknown: 0/1 |
| break-offs then a pot | 40/150 (26.7%) | 105/150 (70.0%) |
| concessions / claims | 13 / 202 | 0 / 8 |

Each frame's highest break by either player: typical 45.5, 50 or more in 124/300 (41.3%), a century in 12/300 (4.0%).

Failures: 0.

## World class against Pro

`world-pro.json`: 300 frames, seed 1, physics `aa56554cccd79697`, table `5b1972fc1db4890b`.
A is `world` (`546b4467296cc7cb`), B is `pro` (`c530bfb961fd22b6`). A breaks in the even frames.

| | A: World class | B: Pro |
| --- | --- | --- |
| frames won | 233 | 67 |
| strokes | 7747 | 4150 |
| pots made | 6297/6711 (93.8%) | 2659/2947 (90.2%) |
| - short | 694/701 (99.0%) | 356/367 (97.0%) |
| - medium | 4939/5100 (96.8%) | 2056/2226 (92.4%) |
| - long | 664/910 (73.0%) | 247/354 (69.8%) |
| - unknown | 0/0 | 0/0 |
| breaks | 496 | 366 |
| - mean / median | 45.1 / 33.5 | 24.2 / 15.0 |
| - highest | 138 | 120 |
| - 50+ / 100+ / 147 | 190 / 59 / 0 | 54 / 4 / 0 |
| safeties | 701/7747 (9.0%) | 805/4150 (19.4%) |
| escapes | 185 | 247 |
| safe with a pot to play | 198/6909 (2.9%) | 336/3283 (10.2%) |
| no pot at all | 688/7747 (8.9%) | 716/4150 (17.3%) |
| fouls | 88/7747 (1.1%) | 108/4150 (2.6%) |
| points scored | 22381 | 8839 |
| visits with the maximum on / 147s | 793 / 0 | 768 / 0 |
| simulations per stroke (mean / max) | 3263.5 / 4633 | 1470.3 / 2404 |
| steps per stroke (mean) | 855.8 | 390.9 |
| thinking, ms (mean / p99 / max) | 170.37 / 383.35 / 715.37 | 78.88 / 194.48 / 359.75 |
| how thinking stopped | done 7893 | done 4285 |
| fallbacks | 0 | 0 |
| own typical break (median of its highest in each frame) | 65.5 | 12.5 |
| frames with its own century | 59/300 (19.7%) | 4/300 (1.3%) |
| safeties then a pot attempt | 133/701 (19.0%) | 362/805 (45.0%) |
| - then potted | 92/701 (13.1%) | 243/805 (30.2%) |
| snookers laid / called | 299 / 167 | 230 / 123 |
| - then fouled / foul points | 23 / 108 | 4 / 22 |
| escapes that hit a ball on | 181/185 (97.8%) | 222/247 (89.9%) |
| - by cushions | 0: 7/7, 1: 139/142, 2: 35/36 | 0: 10/10, 1: 188/205, 2: 24/32 |
| break-offs then a pot | 13/150 (8.7%) | 46/151 (30.5%) |
| concessions / claims | 43 / 103 | 113 / 22 |

Each frame's highest break by either player: typical 71.5, 50 or more in 233/300 (77.7%), a century in 63/300 (21.0%).

Failures: 0.
