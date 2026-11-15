# Clauses: Section 2, Section 3 Rules 1 to 5, Section 4 Rule 2

Every clause of these parts of the rulebook (WPBSA Official Rules of Snooker 2024-25 with the
August 2024 updates), what it requires in our own words, and the scenario test that holds the
engine to it, or why it cannot arise in the game. `README.md` beside this file says how each
clause is read with no human referee.

The tests are in `core/tests/rules/`:

| File | Clauses |
| --- | --- |
| `test_s2_definitions.cpp` | S2.1, S2.5 to S2.10, S2.16 |
| `test_s2_ball_on.cpp` | S2.11 to S2.13, S2.21 |
| `test_s2_snookered.cpp` | S2.17 to S2.19 |
| `test_s3_r01_05_game.cpp` | S3.1 to S3.3 |
| `test_s3_r01_05_end_in_hand.cpp` | S3.4, S3.5 |
| `test_s4_conceding.cpp` | S4.2 |

In a test, a `REQUIRE` is a premise: that the stroke did physically what the scenario says (read
from the simulator's record, not from `observe`), or that a frame carried on so the next stroke can
be played. A `CHECK` is the rule's outcome. A failed `REQUIRE` means the shot needs retuning (the
table changed); a failed `CHECK` means the engine and the rulebook disagree.

## Section 2: definitions

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S2.1 | A frame runs from its first stroke, the players playing in turn, until one of (a) to (e) completes it. | `S3.3(c): the breaker plays from in hand, and the first stroke starts the frame`; `S3.3(a): the players keep their order of play through the frame` |
| S2.1(a) | A concession that is accepted completes the frame. | `S2.1(a): a concession ends the frame in the other player's favour` |
| S2.1(b) | With only the black left, the striker more than seven points ahead may claim the frame. Seven is not enough, nor is any lead while another colour is up, and the non-striker cannot claim. | `S2.1(b): the striker, more than seven ahead with only the black left, may claim the frame`; `S2.1(b): no claim at seven ahead, with another colour still up, or by the non-striker`; `S2.1(b): potting the pink to go more than seven ahead leaves the striker to claim or play on` |
| S2.1(c) | With only the black left and the non-striker more than seven ahead, the frame is the non-striker's. | `S2.1(c): the striker left on the black alone and more than seven behind loses the frame`; `S2.1(c): a leader's stroke at the black that scores nothing gives up the turn and wins the frame`; `S2.1(c): seven behind on the black alone, the frame goes on` |
| S2.1(d) | With only the black left, the first pot or foul completes the frame. | `S3.4(a), S2.1(d), S2.5(e): on the black alone, potting it ends the frame for the higher score`; `S3.4(a), S2.1(d): on the black alone, a foul ends the frame for the higher score` |
| S2.1(e) | The referee may award the frame: after three misses, or for conduct or time wasting. | Three misses: T3 (S3.14(d)(ii)). Conduct and time wasting cannot arise: there is no conduct to judge. |
| S2.2 | Frames are played in games of an agreed number. | `S3.1(g)(i), S2.2: the match goes to the player first to the frames required` |
| S2.3 | Games in turn make up a match. | Cannot arise: a match in the game is one game, the best of N frames. |
| S2.4 | Play is with one white cue ball at twenty-one object balls: fifteen reds and six colours. | `S3.2(a), S2.4: a frame starts with the cue ball in hand and the twenty-one object balls set` |
| S2.5 | The player about to play is the striker, and stays so while scoring. | `S2.5: a stroke that scores keeps the striker at the table` |
| S2.5(a) | The turn ends with a stroke that scores nothing. | `S2.5(a): a stroke that scores nothing ends the turn` |
| S2.5(b) | The turn ends with a foul. | `S2.5(b): a foul ends the offender's turn` |
| S2.5(c) | A player who asks the offender to play again has given up that turn. | `S2.5(c): asked to play again after a foul, the offender is the striker once more` |
| S2.5(d) | The turn ends when the striker claims the frame. | `S2.1(b): the striker, more than seven ahead with only the black left, may claim the frame` |
| S2.5(e) | The turn ends when the final black is potted. | `S3.4(a), S2.1(d), S2.5(e): on the black alone, potting it ends the frame for the higher score` |
| S2.6(a) | A stroke is made when the tip strikes the cue ball; feathering is not one. | `S2.6(a): each strike of the cue ball is one stroke`. Feathering cannot arise: the game takes one strike and nothing else touches the cue ball. |
| S2.6(b) | The cue ball is struck once and not pushed. | The push: S2.19 below. A double strike cannot arise (README, S3.11(a)(ii)). |
| S2.6(c) | A stroke with no infringement is legal. | `S2.6(c), S3.3(e): a stroke with no infringement is a legal one` |
| S2.6(d) | A stroke is not complete until every ball is at rest, colours are spotted and equipment is cleared. | `S2.6(d): a stroke is complete with every ball at rest and the potted colour back on its spot`. Equipment cannot arise. |
| S2.6(e) | A stroke may reach the object ball directly or off cushions. | `S2.6(e): a stroke made off a cushion is as legal as a direct one` |
| S2.7 | A ball entering a pocket in a legal stroke is potted; in a foul stroke it is pocketed, which scores nothing. | `S2.7: a ball on sent into a pocket by a legal stroke is potted and scores`; `S2.7: a ball that drops in a foul stroke is pocketed, not potted: nothing is scored` |
| S2.8 | A break is the pots made in successive strokes of one turn. | `S2.8: a break is what the striker pots in successive strokes of one turn` |
| S2.9(a)(i) | The cue ball is in hand before each frame starts. | `S2.9(a)(i): the cue ball is in hand before a frame starts`; later frames: `S3.3(b): the players break in turn, frame by frame` |
| S2.9(a)(ii) | It is in hand after being pocketed. | `S2.9(a)(ii): a pocketed cue ball is in hand for the next stroke` |
| S2.9(a)(iii) | It is in hand after being forced off the table. | Cannot arise: balls stay on the cloth (README, S2.14). |
| S2.9(a)(iv) | It is in hand for a re-spotted black. | `S3.4(a), (b)(i)-(iii), S2.9(a)(iv): the black potted to level the scores is re-spotted, the cue ball in hand` |
| S2.9(b)(i) | It stays in hand until played from in hand. Played from the D, it is in play where it stops, a foul stroke included. | `S2.9(b)(i): played from in hand, the cue ball is in play where it stops`; staying in hand while placed: `S2.9(a)(ii): a pocketed cue ball is in hand for the next stroke` |
| S2.9(b)(ii) | Or until a foul involving the cue ball while the striker does not hold it. | Cannot arise: nothing touches a ball except a stroke. |
| S2.9(c) | "In hand" is said of the striker as of the cue ball. | `S2.9(c): whoever plays next is in hand, the offender included when put back in` |
| S2.10(a) | A cue ball that is not in hand is in play. | `S2.10(a): a cue ball in play is not in hand and cannot be placed` |
| S2.10(b) | Object balls are in play until potted, pocketed or forced off. | `S2.10(b): a red is out of play once potted`; pocketed: `S3.3(i): a red pocketed in a foul stroke stays off the table`. Forced off cannot arise. |
| S2.10(c) | Re-spotting puts a colour back in play. | `S2.10(c): a colour potted while reds remain is in play again on its spot` |
| S2.11 | A ball on is one the cue ball may legally hit first, or one that may not be hit first but may be potted. | `S2.11: with red on, every red is a ball on and any of them may be hit first`; `S2.11: after a red every colour could be on, and the one declared may be hit first`; `S2.11: with the reds gone only the lowest colour left is on`; `S2.11: a ball on that may not be hit first may still be potted` |
| S2.12(a) | The nominated ball is the one the striker declares or shows they will hit first. | `S2.12(a): the colour the striker declares is the ball on for that stroke`; `S2.12(a): hitting another colour than the one declared is a foul at the higher of the two values`; `S2.12: with nothing declared, the first colour hit is taken as the one nominated`; `S2.12: a declaration where none is open, or of a ball that cannot be declared, counts for nothing`; `S2.12: which balls may be declared` |
| S2.12(b) | Asked by the referee, the striker must declare. | Cannot arise: nobody is asked. With nothing declared the first ball hit is taken as declared (README), tested above. |
| S2.13 | A free ball is a ball other than the ball on that the striker, snookered after a foul, nominates as the ball on. | `S2.13: snookered after a foul, the next player may nominate any ball and play it as the ball on`; `S2.13: there is no free ball for an offender asked to play again` |
| S2.14 | A ball is forced off the table when it rests anywhere but the bed or a pocket. | Cannot arise (README). |
| S2.15 | An infringement that ends the turn is a foul; one that does not is a penalty. | Foul: `S2.5(b): a foul ends the offender's turn`. Penalties (S3.11(e) to (g)) cannot arise (README). |
| S2.16 | The points for an infringement go to the player who did not commit it. | `S2.16: penalty points go to the player who did not offend` |
| S2.17 | The cue ball is snookered when a straight shot at every ball on is obstructed, wholly or in part, by balls not on; one ball on that can be hit at both extreme edges means no snooker. | `S2.17: a ball not on dead in line with the only red is a snooker`; `S2.17: one extreme edge obstructed is a snooker, though the ball on can be hit full`; `S2.17: one red open at both extreme edges is no snooker, whatever hides the others`; `S2.17: only balls not on obstruct: a red hidden behind another red is not snookered` |
| S2.17(a) | In hand, it is snookered only if that holds from every place in the D. | `S2.17(a): in hand, the cue ball is snookered when no place in the D has both edges clear`; `S2.17(a): in hand, one clear place in the D means no snooker` |
| S2.17(b)(i) | Of several obstructing balls the nearest to the cue ball is the effective snookering ball. | `S2.17(b)(i): of two balls in the way, the one nearer the cue ball is the effective snookering ball` |
| S2.17(b)(ii) | Obstructing balls equally near are all effective. | `S2.17(b)(ii): obstructing balls the same distance from the cue ball are all effective` |
| S2.17(c) | With red on, different reds hidden by different balls leave no effective snookering ball. | `S2.17(c): reds hidden by different balls leave no effective snookering ball` |
| S2.17(d) | "The striker is snookered" means the cue ball is. | The same tests: no behaviour of its own. |
| S2.17(e) | A cushion cannot snooker. | `S2.17(e): a cushion in the way is no snooker` |
| S2.18 | A spot counts as occupied when a ball set on it would touch another ball. | `S2.18: a spot is occupied when a ball put on it would touch another`; `S2.18: a ball clear of the spot, however near, leaves it free`; `S3.2(a)(i), S2.18: the apex red does not occupy the pink spot: a pink potted with the pack untouched goes back` |
| S2.19(a) | A push: the tip stays on the cue ball after it has started to move. | Cannot arise: the strike is a single impulse. |
| S2.19(b) | A push: the tip is still on the cue ball as it meets an object ball, unless the balls were almost touching and only a very fine edge is hit. | `S2.19(b): the cue ball played straight into a ball almost touching it is a push stroke`; `S2.19(b): clipping a very fine edge of a ball almost touching is not a push stroke` |
| S2.20 | A jump shot. | Cannot arise (README). |
| S2.21(a) | A miss: the cue ball fails to hit a ball on first. | `S2.21(a): the cue ball failing to reach a ball on in plain view is a miss`; `S2.21(a): a ball not on taking the first impact is a miss as well`; `S2.21: a foul made after the ball on was hit first is not a miss` |
| S2.21(b) | With a free ball nominated, a miss is failing to hit it first, alone or together with a ball on. | `S2.21(b): with a free ball nominated, another ball taking the first impact is a miss`; `S2.21(b): the free ball and a ball on hit together is no miss and no foul` |
| S2.22 | The consultation period while balls are replaced. | Cannot arise: balls go back from the stored position; nobody places them by hand. |

When a miss is called or waived (S3.14) is T3's.

## Section 3 Rule 1: description

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S3.1 | Two players, or more as sides. | Sides cannot arise: one person against the computer (README, S3.18). |
| S3.1(a) | One cue ball; reds worth 1; yellow 2, green 3, brown 4, blue 5, pink 6, black 7. | `S3.1(a): a red is worth 1 and the colours 2 to 7` |
| S3.1(b) | A turn scores by potting reds and colours alternately, then the colours in ascending order. | `S3.1(b): a turn scores by reds and colours alternately, then the colours in ascending order` |
| S3.1(c) | Points scored go on the striker's score. | `S3.1(c): the points of a scoring stroke go on the striker's score` |
| S3.1(d) | Penalty points go on the opponent's score. | `S3.1(d): penalty points go on the opponent's score, and the offender keeps the break made` |
| S3.1(e) | Leaving a snooker is fair play; a player may need more points than the balls left hold. | `S3.1(e): a snooker laid by a legal stroke is no foul and brings no free ball`; `S3.1(e): the points left on the table, and who needs penalty points to catch up` |
| S3.1(f)(i) | The frame goes to the higher score. | `S3.1(f)(i): the frame goes to the higher score, even an offender's`; the S3.4(a) tests |
| S3.1(f)(ii) | Or to the player it is conceded to. | `S4.2, S3.1(f)(ii): a conceded frame counts for the opponent in the match` |
| S3.1(f)(iii) | Or to the player it is awarded to. | Three misses: T3. Conduct cannot arise. |
| S3.1(g)(i) | The game goes to the player who wins the most, or the required number of, frames. | `S3.1(g)(i), S2.2: the match goes to the player first to the frames required`; `S3.1(g)(i): a match is over as soon as it cannot be caught` |
| S3.1(g)(ii), (iii) | Or on aggregate points, or by award for conduct. | Cannot arise: matches are decided on frames, and there is no conduct to judge. |
| S3.1(h) | The match goes to the winner of the most games. | Cannot arise: one game to a match. |

## Section 3 Rule 2: position of balls

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S3.2(a) | Each frame starts with the cue ball in hand and the object balls set. | `S3.2(a), S2.4: a frame starts with the cue ball in hand and the twenty-one object balls set` |
| S3.2(a)(i) | The reds in a tight equilateral triangle, apex on the centre line behind the pink and as near it as can be without occupying its spot, base parallel to the top cushion. | `S3.2(a)(i): the reds start as a tight triangle behind the pink, its base square to the top cushion`; `S3.2(a)(i), S2.18: the apex red does not occupy the pink spot: a pink potted with the pack untouched goes back` |
| S3.2(a)(ii) | The colours on their spots (Section 1 Rule 1(f)). | `S3.2(a)(ii), S1.1(f): the six colours start on their spots` |
| S3.2(b) | A mistake in setting up. | Cannot arise: the rack is generated. |
| S3.2(c) | Cleaning a ball. | Cannot arise (README). |

## Section 3 Rule 3: mode of play

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S3.3 | Who plays first is settled by lot or by agreement; the winner of the lot chooses. | `S3.3: who breaks is settled by lot, or as the players agree`. The winner's choice cannot arise: the lot names the breaker. |
| S3.3(a) | The order of play holds for the frame, except that an offender may be asked to play again. | `S3.3(a): the players keep their order of play through the frame`; `S2.5(c): asked to play again after a foul, the offender is the striker once more` |
| S3.3(b) | The players break alternately, frame by frame. | `S3.3(b): the players break in turn, frame by frame` |
| S3.3(c) | The first player plays from in hand; the frame starts when the tip meets the cue ball. | `S3.3(c): the breaker plays from in hand, and the first stroke starts the frame`. Starting it by touching the cue ball while addressing (ii) cannot arise. |
| S3.3(d) | A frame started by the wrong player. | Cannot arise (README). |
| S3.3(e) | A legal stroke has none of the Rule 11 infringements. | `S2.6(c), S3.3(e): a stroke with no infringement is a legal one` |
| S3.3(f) | Clearing equipment from the table. | Cannot arise: there is none. |
| S3.3(g) | Each turn starts on a red while reds remain, and every red potted in the stroke scores. | `S3.3(g): each turn starts on a red while a red is left`; `S3.3(g): every red potted in the one stroke scores`. A free ball taken as a red: T3 (S3.12). |
| S3.3(h)(i) | After a red the same player plays at a colour of choice; potted, it scores and is spotted. | `S3.3(h)(i): after a red the same player plays at a colour of choice, which scores and is spotted` |
| S3.3(h)(ii) | So on until the reds are gone and a colour has been played at after the last of them. | `S3.3(h)(ii): the colour after the last red is still one of choice and is spotted; then the colours go in order`; `S3.3(h)(ii): a colour missed after the last red leaves the next player on the yellow` |
| S3.3(h)(iii) | Then the colours are on in ascending order and stay off when potted. | `S3.3(h)(iii): with the reds gone the colours are on in ascending order and stay down when potted` |
| S3.3(h)(iv) | Playing before a colour is spotted. | Cannot arise: the stroke is complete before the next can be played (README). |
| S3.3(i) | Reds stay off the table once down, even to the fouler's gain. | `S3.3(i): a red pocketed in a foul stroke stays off the table`. Reds put back with a replaced position: T3 (S3.14(b)). |
| S3.3(j) | After failing to pot, the next stroke is from where the cue ball lies, or from in hand. | `S3.3(j): the next stroke is from where the cue ball stopped, or from in hand if it went in a pocket`. Fouls while leaving the table cannot arise. |
| S3.3(k), (l) | Infringements by a player not at the table, or before the balls are at rest. | Cannot arise (README). |
| S3.3(m) | A ball that rebounds out of a pocket. | Cannot arise (README). |

## Section 3 Rule 4: end of frame

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S3.4(a) | With only the black left, the first pot or foul ends the frame, unless the scores are then level. | `S3.4(a), S2.1(d), S2.5(e): on the black alone, potting it ends the frame for the higher score`; `S3.4(a), S2.1(d): on the black alone, a foul ends the frame for the higher score`; `S3.4(a): a stroke at the last black that neither pots nor fouls leaves the frame open` |
| S3.4(a)(i) | The exception: the scores are level. | `S3.4(a), (b)(i)-(iii), S2.9(a)(iv): the black potted to level the scores is re-spotted, the cue ball in hand`; `S3.4(a), (b): a foul on the last black that levels the scores brings the re-spotted black too` |
| S3.4(a)(ii) | And aggregate scores do not decide. | Always so: cannot be otherwise. |
| S3.4(b)(i) | The black is spotted. | `S3.4(a), (b)(i)-(iii), S2.9(a)(iv): ...` |
| S3.4(b)(ii) | The players draw lots; the winner chooses who plays. | `S3.4(b)(ii): the lots can fall to either player, and one frame always draws the same`; `S3.4(b)(ii), (iii): the winner of the lots chooses who plays the re-spotted black, from in hand` |
| S3.4(b)(iii) | The next player plays from in hand. | The same two tests. |
| S3.4(b)(iv) | The first pot or infringement ends the frame. | `S3.4(b)(iv): on a re-spotted black the first pot wins the frame`; `S3.4(b)(iv): on a re-spotted black the first foul loses the frame`; `S3.4(b): a stroke at the re-spotted black that neither pots nor fouls passes the turn` |
| S3.4(c) | A re-spotted black to settle level aggregate scores. | Cannot arise: no aggregate scoring. |

## Section 3 Rule 5: playing from in hand

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S3.5 | From in hand the cue ball is played from on or within the lines of the D, in any direction. | `S3.5: in hand, the cue ball may be placed anywhere on or within the lines of the D`; `S3.5: the cue ball cannot be placed on or against another ball`; `S3.5: from in hand the cue ball may be played in any direction`; `S3.5 (spec 8.6): a pocketed cue ball waits at the legal place nearest its usual one` |
| S3.5(a) | The referee says, if asked, whether the cue ball is properly placed. | `S3.5, (a): a place outside the D is refused` |
| S3.5(b) | Played from outside the D. | Cannot arise: the D is enforced (README). |
| S3.5(c) | The tip touching the cue ball while it is positioned. | Cannot arise: the cue ball is placed by dragging it. |

## Section 4 Rule 2: conceding

| Clause | What the rulebook requires | Test, or why it cannot arise |
| --- | --- | --- |
| S4.2(a) | A concession is an offer, void if the opponent plays on. | `S4.2(a), (e): a concession stands at once and cannot be taken back`. Declining it cannot arise: the game takes a concession as accepted. |
| S4.2(b) | No concession unless a player requires penalty points. The owner's ruling: the person may concede at any time, the opponent only then. | `S4.2(b): a frame may not be conceded while nobody requires penalty points`; `S4.2(b): once a player requires penalty points the frame may be conceded`; `S4.2(b), the owner's ruling: a player given leave may concede at any time, the other only then`; `S4.2(b): in a match the frame's leave to concede is each player's own`. The sanction for conceding early cannot arise: the option is not offered. |
| S4.2(c) | Moving balls to signal a concession. | Cannot arise. |
| S4.2(d) | Fouling on purpose so as to need penalty points. | Cannot arise: intent is never judged. |
| S4.2(e) | An offer or acceptance cannot be withdrawn. | `S4.2(a), (e): a concession stands at once and cannot be taken back` |
| S4.2(f) | Points credited for a conceded frame where aggregate scores decide. | Cannot arise: no aggregate scoring. |
| S4.2 in the game | A concession waits until nothing is pending; a conceded frame counts in the match; the match itself may be conceded (spec 9). | `S4.2: a frame is not conceded while the referee waits on a choice after a foul`; `S4.2, S3.1(f)(ii): a conceded frame counts for the opponent in the match`; `S4.2: the match may be conceded at any time, whatever the frames stand at`; `S4.2: a player a frame up who concedes the match still loses it` |

## Cannot arise, and not in README's list

S2.3 (games in a match), S2.6(a) (feathering), S2.9(b)(ii), S2.12(b), S2.19(a), S2.22, S3.1 (sides),
S3.1(g)(ii), (iii) and (h), S3.2(b), S3.3 (the lot winner's choice), S3.3(c)(ii), (f), (h)(iv),
S3.4(a)(ii) and (c), S3.5(c), S4.2(a) (declining), (c), (d), (f).

## Where the tests and the contracts may part

- S3.2(a)(i) with S2.18. `opening_rack` leaves the apex red 0.039 to 0.071 mm from the pink, and
  the referee calls anything within `touching_gap_m` (0.05 mm) touching. On about a third of racks
  (seeds 0, 1, 7, 8, 10 and 11 of the first sixteen) the pink spot therefore counts as occupied
  while the pack is untouched, and a potted pink could not go back on it. The rulebook has the apex
  red clear of the spot, so the test expects the pink on its own spot for every rack.
- S2.9(b)(i). README does not say what becomes of a cue ball played from the D in a stroke that
  fouls without pocketing it. The test reads the rulebook (S3.10(f)): it is in play where it lies.
- S3.3. The rulebook gives the winner of the lot the choice of who breaks; `MatchConfig` lets the
  lot name the breaker. Nothing is tested for the choice.
- `engine.hpp` offers the concessions "with nothing pending"; README says `concede_match` is always
  offered. The tests follow `engine.hpp` and the fixtures: while a choice is pending only that
  choice is offered.
