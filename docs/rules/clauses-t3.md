# Clauses: Section 3 Rules 12 to 17, and spec 9.1

Each clause of the WPBSA Official Rules of Snooker (2024-25, with the August 2024 updates) in these
rules, what it requires in our own words, and the scenario test that holds the engine to it, or why
it cannot arise in the game. The tests are written from the rulebook and `README.md`, not from the
engine. A test's name starts with its clause.

Tests live in `core/tests/rules/`: `test_s3_r12_17_free_ball.cpp` (Rules 12 and 13),
`test_s3_r12_17_miss.cpp` (Rule 14), `test_s3_r12_17_stalemate.cpp` (Rule 17) and
`test_spec_9_1_referee.cpp` (spec 9.1). In each, `REQUIRE` states a premise read from the raw shot
record or worked out by hand, and `CHECK` states what the rule demands.

## Rule 12: snookered after a foul

| Clause | What the rulebook requires | Test |
| --- | --- | --- |
| S3.12 | After any foul, a cue ball left snookered gets FREE BALL called. Snookered (S2.17) means no ball on can be hit on both of its edges past the balls not on; in hand (S2.17(a)), that this holds from everywhere in the D. | `S3.12: a foul that leaves the next player snookered on the only red brings FREE BALL`; `S3.12: no FREE BALL when the ball on can be hit on both edges`; `S3.12: one covered edge of the ball on is a snooker, and brings FREE BALL`; `S3.12: no FREE BALL while another red is in the open`; `S3.12 with S2.17(a): in hand after an in-off, FREE BALL when the red is hidden from the whole D`; `S3.12 with S2.17(a): in hand, no FREE BALL when some of the D sees the red` |
| S3.12(a)(i) | The next player, if they play the stroke themselves, may name any ball as the ball on for it. A ball that is already on cannot be the free ball. | `S3.12(a)(i): electing to play, the next player may nominate any ball that is not on`; `S3.12(a)(i): in the colours, the colour on is not among the free balls`; `S3.12(a)(i): with nothing declared, the ball hit first is taken as the free ball` (the README's reading of an undeclared stroke) |
| S3.12(a)(ii) | The named ball is played as the ball on and takes the ball on's value; potted, it goes back on a spot. | `S3.12(a)(ii): the nominated free ball is played as the ball on`; `S3.12(a)(ii): a foul with a free ball nominated costs the value of the ball on, not the free ball's`; spotting under S3.12(c) |
| S3.12(b)(i) | A foul unless the cue ball's first impact is on the named free ball, alone or at once with a ball on. By S2.21(b) that failure is also a miss. | `S3.12(b)(i): another colour hit before the nominated free ball is a foul`; `S3.12(b)(i): a ball on hit before the nominated free ball is a foul too`; `S3.12(b)(i): the nominated free ball and a ball on hit at once is a fair stroke` |
| S3.12(b)(ii) | A foul when a stroke that scored nothing leaves the cue ball snookered on every red, or on the ball on, by the named free ball: that is, when the free ball is the effective snookering ball of S2.17(b) and (c). | `S3.12(b)(ii): leaving the cue ball snookered behind the nominated free ball is a foul`; `S3.12(b)(ii) with S2.17(b)(i): no foul when another ball, nearer the cue ball, is the snooker`; `S3.12(b)(ii) with S2.17(b)(i): a foul when the free ball is the nearer of two in the way`; `S3.12(b)(ii) with S2.17(b)(ii): a foul when the free ball is one of two equally near in the way`; `S3.12(b)(ii) with S2.17(c): no foul when the free ball hides one red and another ball hides the other`; `S3.12(b)(ii): a foul when every red lies behind the nominated free ball`; `S3.12(b)(ii): no foul after a stroke that scored` |
| S3.12(b)(ii), exception | No such foul when only two object balls are left. Since August 2024 that is any two: pink and black, or the black and one other colour. | `S3.12(b)(ii): with only pink and black left the snooker behind the free ball is allowed`; `S3.12(b)(ii) as updated in August 2024: with black and one other colour left it is allowed`; `S3.12(b)(ii): with three object balls left the foul stands` |
| S3.12(c) | A potted free ball scores the value of the ball on and is spotted. | `S3.12(c): a potted free ball scores the value of the ball on and is spotted`; `S3.12(c): in the colours a potted free ball scores the colour on, which stays on` |
| S3.12(d) | A ball on potted after the free ball was hit first, or at once with it, scores and stays off the table. | `S3.12(d): a ball on potted off the nominated free ball scores and stays down`; `S3.12(d): the same when the free ball and the ball on were hit at once` |
| S3.12(e) | Free ball and ball on both potted: only the ball on scores, unless red was on, when each scores. The free ball is spotted and the ball on stays down. | `S3.12(e): the free ball and a red both potted: each scores, the free ball is spotted`; `S3.12(e): the free ball and the colour on both potted: only the colour on scores` |
| S3.12(f) | The free ball lapses when the offender is asked to play again or the balls are replaced. | `S3.12(f): asked to play again, the offender has no free ball`; `S3.12(f): asking for the balls to be replaced gives the free ball up` |

## Rule 13: play again

| Clause | What the rulebook requires | Test |
| --- | --- | --- |
| S3.13 | A request that the offender play again, or that the balls be replaced, stands once made. | `S3.13: a request to play again cannot be taken back`; `S3.13: a request for the balls to be replaced cannot be taken back` |
| S3.13(a)(i), (ii) | The offender, asked to play again, is free to play any stroke and at any ball on. | `S3.13(a): asked to play again, the offender may play a different stroke at a different ball on`; `S3.14(b)(iii) with S3.13(a)(ii): replaced with a colour of choice, the offender may choose another` |
| S3.13(b) | The offender scores for whatever they then pot. | `S3.13(b): asked to play again, the offender scores for the ball they pot` |

## Rule 14: foul and a miss

| Clause | What the rulebook requires | Test |
| --- | --- | --- |
| S3.14(a) | The striker must do their best to hit a ball on (S2.21: a miss is failing to hit one first). When the referee thinks they did not, it is FOUL AND A MISS. The owner's television standard: every such failure, bar the two exceptions. | `S3.14(a): a failed escape from a snooker is FOUL AND A MISS` |
| S3.14(a)(i) | No miss when either player needed penalty points before the stroke, or needs them because of it. The rulebook adds that the referee must think the miss unintended: intent is never judged (README). | `S3.14(a)(i): no miss when the striker needed penalty points before the stroke`; `S3.14(a)(i): no miss when the other player needed penalty points before the stroke`; `S3.14(a)(i): no miss when the stroke's own penalty leaves the striker needing penalty points`; `S3.14(a)(i): still a miss while the penalty leaves the scores able to be levelled`; `S3.14(a)(i): no miss when the stroke, pocketing the last red in the foul, leaves the striker needing them` |
| S3.14(a)(ii) | No miss when the ball on cannot be hit at all, as long as the stroke was played at it, directly or not, hard enough to have reached it but for the balls in the way. | `S3.14(a)(ii): no miss when the ball on cannot be hit and the stroke is played at it`; `S3.14(a)(ii): with the ball on impossible to hit, a stroke not played at it is still a miss` |
| S3.14(b) | After FOUL AND A MISS the other player may play, have the offender play from where the balls lie, or have every ball put back and the offender play the original position. Without a miss there is no putting back. | `S3.14(b): after FOUL AND A MISS the non-offender may play, pass the position back or have it replaced`; `S3.14(b): a foul after a fair first contact is no miss and offers no replacement`; `S3.14(b): asked to play again from the position left, the offender plays the balls as they lie`; `S3.14(b): a red pocketed in the stroke goes back with the rest`; `S3.14(b): a stroke from in hand is replaced in hand`; `S3.14(b): the free ball the offender had comes back with the position` (the README's reading) |
| S3.14(b)(i) | Replaced with red having been on: any red is on. | `S3.14(b)(i): replaced with red on, every ball is back where it lay and the penalty stands` |
| S3.14(b)(ii) | Replaced in the colours: the same colour is on. | `S3.14(b)(ii): replaced in the colours, the same colour is on` |
| S3.14(b)(iii) | Replaced after a red: a colour of the striker's choice is on again. | `S3.14(b)(iii) with S3.13(a)(ii): replaced with a colour of choice, the offender may choose another` |
| S3.14(c) | Failing to hit a ball on first when a straight path was open to any part of any ball that is or could be on is FOUL AND A MISS, unless (a)(i) applies. | `S3.14(c): a clear path to the ball on makes a failure FOUL AND A MISS`; `S3.14(c): a clear path to any part of the ball on is enough`; `S3.14(c): a clear path to a ball that could be on counts`; `S3.14(c): with a free ball every object ball could be on` |
| S3.14(d) | A run begins with a miss under (c) where a central, full-ball contact was open (for reds, a full ball's width of red that no colour covers), or where the cue ball touched a ball that could be on. | `S3.14(d): a miss with central full-ball contact open begins the run`; `S3.14(d): a red masked only by another red counts as open to a full-ball contact`; `S3.14(d): no run when a colour covers the middle of every red`; `S3.14(d): a miss with the cue ball touching a ball that could be on begins the run` |
| S3.14(d)(i) | In a run, a second failure from the original position is a miss whatever the scores. | `S3.14(d)(i): the second failure from the original position is a miss whatever the scores`; `S3.14(d)(i): outside a run the scores are still respected` |
| S3.14(d)(ii) | Asked to play the original position again after that second failure, the offender is warned that one more loses the frame. No frame is awarded without the warning; a warning not yet given is given at the first chance. | `S3.14(d)(ii): warned on the second replacement, the frame goes on the third failure`; `S3.14(d)(ii): a third failure with the warning standing loses the frame`; `S3.14(d)(ii): no frame is awarded without the warning, which comes at the next replacement` |
| S3.14(d)(iii) | The run ends when the offender is asked to play from where the balls lie. It also ends, there being no further failure from the original position, when the other player plays on or the offender's next stroke is not such a failure. | `S3.14(d)(iii): asked to play from the position left, the run ends`; `S3.14(d): the non-offender playing on ends the run`; `S3.14(d): hitting a ball on from the replaced position ends the run and the warning`; `S3.14(d): a foul after a fair first contact ends the run` |
| S3.14(e) | A ball fouled after the balls were replaced, before any stroke is played, is no miss; the options and the standing warning that follow. | Cannot arise: a ball is only ever moved by a stroke. Nothing can be touched while addressing the cue ball. |
| S3.14(f) | Asking for the original position opens a consultation period. | Cannot arise: the engine puts the balls back from the stored position exactly, so there is nothing to consult about. |

## Rules 15 and 16

| Clause | What the rulebook requires | Test |
| --- | --- | --- |
| S3.15, (a), (b) | A ball disturbed by anyone or anything but the striker is put back without penalty; nobody is penalised for what the referee disturbs. | Cannot arise: only the simulated stroke moves a ball. There is no other person, no referee's hand and no table defect, and a ball at rest stays at rest (spotted balls included). |
| S3.16, (a), (b), (c) | A ball the striker moves or picks up on purpose, other than by a stroke, is a foul, with replacement options for the other player; a stroke played as unsporting conduct is treated alike. | Cannot arise: the striker's only way to move a ball is to strike the cue ball, and conduct is not judged (README, Section 4). |

## Rule 17: stalemate

| Clause | What the rulebook requires | Test |
| --- | --- | --- |
| S3.17 | When the referee judges a stalemate reached or near, the players are offered a restart at once. The README makes that judgement a count of strokes that change next to nothing. The players themselves indicating a stalemate cannot arise: there is nothing to indicate it with before the offer. | `S3.17: after a long run of strokes that change nothing, a re-rack is offered to the player about to play`; `S3.17: a stroke that changes the position starts the count again`; `S3.17: when both players accept, the frame is to be started again` |
| S3.17(a) | If either player objects, play goes on, on condition that the position changes within a stated number of strokes. | `S3.17(a): when the player offered the re-rack objects, play goes on under a proviso`; `S3.17(a): when the other player objects, play goes on under the same proviso`; `S3.17(a): the position changing within the stated period lifts the proviso` |
| S3.17(b) | Still unchanged when the period runs out: the scores are wiped and the balls set as for a new frame. | `S3.17(b): unchanged at the end of the stated period, the frame is restarted whatever the players say`; `S3.17(b), (c): the restarted frame is racked afresh with no score and the same player breaks` |
| S3.17(c) | The same player breaks again and the order of play is kept. The reference to a frame begun by the wrong player (S3.3(d)(iii)) cannot arise: the game decides who breaks. | `S3.17(b), (c): the restarted frame is racked afresh with no score and the same player breaks` |
| S3.17(d) | A stalemate on a re-spotted black: only the black is spotted again, and the player who opened on it opens again. | `S3.17(d): on a re-spotted black only the black is set again, the same player to open` |

## Spec 9.1: the referee's judgement

| Heuristic | What spec 9.1, the README and `referee_params.json` fix | Test |
| --- | --- | --- |
| Push stroke, contact time (S2.19(b), `push_contact_time_s`) | A first contact sooner after the strike than the tip stays on the ball is a push; later is fair. The same gap is a push or not by the pace of the stroke. S2.19(a), a tip that stays on a moving cue ball, cannot arise: the strike is a single impulse. | `spec 9.1 push stroke: a first contact inside the tip's contact time is a push`; `spec 9.1 push stroke: a first contact just after the tip's contact time is fair`; `spec 9.1 push stroke: the gap that was a push is fair at a slower pace`; `spec 9.1 push stroke: the gap that was fair is a push at a faster pace` |
| Push stroke, fine edge (S2.19(b), `push_fine_edge_rad`) | On an almost touching ball, a contact finer than the limit is no push, a thicker one is. | `spec 9.1 push stroke: a very fine edge of an almost touching ball is no push`; `spec 9.1 push stroke: a thicker contact on an almost touching ball is a push` |
| Miss, the order of decision | A run of misses first, then the scores, then a ball on that cannot be hit, then a clear path, then the snooker; the same for both players. | `spec 9.1 miss order: a run of misses comes before the scores`; `spec 9.1 miss order: the scores come before a ball on that cannot be hit`; `spec 9.1 miss order: the scores come before a clear path and before a snooker`; `spec 9.1 miss order: a ball on that cannot be hit comes before the snooker it is`; `spec 9.1 miss order: a clear path is a miss under S3.14(c), a snooker under S3.14(a)`; `spec 9.1 miss: the same judgement whichever player is at the table` |
| Miss, television standard (`miss_lenient_gap_m` 0) | With no clear path, every failed escape is a miss, however near it came. | `spec 9.1 miss, television standard: a failed escape is a miss however near it came` |
| Miss, lenient setting (`miss_lenient_gap_m` above 0) | With no clear path, a failure whose cue ball passed within the gap of a ball on is a foul only. Nothing else in the order changes. | `spec 9.1 miss, lenient setting: a failed escape that passed within the gap is a foul only`; `spec 9.1 miss, lenient setting: a failed escape that passed outside the gap is a miss`; `spec 9.1 miss, lenient setting: hitting the snookering ball first, close to the ball on, is a foul only`; `spec 9.1 miss, lenient setting: with a clear path nearness excuses nothing`; `spec 9.1 miss, lenient setting: a run of misses still comes first` |
| Stalemate thresholds (`stalemate_quiet_travel_m`, `stalemate_offer_strokes`, `stalemate_grace_strokes`) | A stroke is quiet when nothing is potted and the object balls end less than the travel threshold from where they lay; the offer comes on the stroke that completes the count, not before; the values in use are the ones the strokes are judged with. The period after an objection is held either side of its length by the S3.17(a) and (b) tests. | `spec 9.1 stalemate: a stroke that moves the balls less than the travel threshold is quiet`; `spec 9.1 stalemate: a stroke that moves the balls more than the travel threshold is not`; `spec 9.1 stalemate: a pot is never quiet, however little the balls moved`; `spec 9.1 stalemate: a played safety that leaves the red half a metre on is quiet`; `spec 9.1 stalemate: the same safety played harder, the red ending well up the table, is not`; `spec 9.1 stalemate: no offer one quiet stroke short of the threshold, the offer on the next`; `spec 9.1 stalemate: the thresholds are the referee values the strokes are judged with` |

## Where a test follows the rulebook past the README

- S3.14(a)(ii): the README excuses every stroke when the ball on cannot be hit. The rulebook
  assumes an attempt only of a stroke played at the ball on with the strength to have reached it,
  so `S3.14(a)(ii): with the ball on impossible to hit, a stroke not played at it is still a miss`
  expects a miss for a cue ball rolled away from a walled-in red.
- S3.14(a)(i): the README counts the need for penalty points "once its penalty is added". The
  rulebook says as a result of the stroke, which also takes in a red pocketed in the foul and left
  down (`S3.14(a)(i): no miss when the stroke, pocketing the last red in the foul, leaves the striker
  needing them`).
- S3.14(d)(ii): the README warns on a replacement "with `miss_run` at 2". The rulebook has a
  warning that was not given come at the first chance, so the test hands the engine a run of 2 with
  no warning, expects no frame award on the next failure, and expects the warning at the
  replacement that follows (the run then at 3).
- S3.17(d): the README does not say who plays first when a re-spotted black is set again. The
  rulebook says the player who opened on it. The test brings the offer after an odd number of
  strokes, so that this is not the player about to play.
