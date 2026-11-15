# Clauses to tests: Section 3 Rules 6 to 11

Every clause of Section 3 Rules 6 (two balls at once), 7 (spotting colours), 8 (touching ball),
9 (ball on the edge of a pocket), 10 (fouls) and 11 (penalty values) of the WPBSA Official Rules of
Snooker 2024-25, with the scenario test that holds it or the reason it cannot arise in the game.
The middle column is the requirement in our words; the rulebook itself is not in the repository.

The tests are in `core/tests/rules/`, named by clause:

| File | Rules |
| --- | --- |
| `test_s3_r06_11_first_impact.cpp` | 6, 8 |
| `test_s3_r06_11_spotting.cpp` | 7, 9 |
| `test_s3_r06_11_fouls.cpp` | 10 |
| `test_s3_r06_11_penalties.cpp` | 11 |

"Played" means the stroke runs on the real simulator and the test first requires, from the shot
record, that the balls did what the scenario says. "From facts" means the referee is handed facts
written by hand, where only the arithmetic is in question. "Oracle" means the geometry is asked
directly, with coordinates worked out by hand.

## Rule 6: two balls at once

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.6 | The cue ball's first impact may be on two balls at the same instant only when they are two reds, or the nominated free ball and a ball on. Any other pair is a foul. | Played, the cue ball running up the centre line at two balls side by side: "S3.6: two reds struck at the same instant, red on, is a fair stroke"; "... a red and a colour ... is a foul"; "... the nominated free ball and a ball on ... is a fair stroke"; "... the colour on and another colour ... is a foul"; "... the declared colour and another colour ... is a foul"; "S3.6: a red struck a moment before a colour is a fair stroke" (5 ms apart: two impacts, the first on a red). The values are S3.11(c). |

## Rule 7: spotting colours

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.7 | A colour that goes into a pocket is put back before the next stroke, whether potted fairly or pocketed in a foul, until it is potted in its turn once the reds are gone. | Played: "S3.7: a colour potted after a red is spotted before the next stroke"; "S3.7: a colour pocketed in a foul stroke is spotted, also once the reds are gone"; "S3.7: a colour potted in its turn once the reds are gone stays off the table". |
| S3.7(a) | A player is not answerable for the referee spotting a ball wrongly. | Cannot arise: the engine spots every ball by rule; there is no wrong spotting to forgive. |
| S3.7(b) | A colour put back by mistake after being potted in its turn is taken off again without penalty. | Cannot arise: the same. |
| S3.7(c) | Balls wrongly spotted count as right once a stroke is played; a colour wrongly missing is put back. | Cannot arise: the same; and nothing can be played while spotting is unfinished, since spotting is part of the judgement. |
| S3.7(d) | A red spotted in place of a colour is taken off when found, or the frame goes on with it. | Cannot arise: reds are never spotted. |
| S3.7(e) | A colour whose own spot is occupied goes on the highest-value spot that is free. A spot is occupied when a ball put on it would touch another (S2.18). | Oracle: "S3.7(e): a colour whose spot is taken goes on the highest-value spot that is free" (four layouts); "S2.18, S3.7(e), (i): a spot is taken when a ball placed on it would touch another". Played: "S3.7(e): the black potted while a red lies on its spot comes back on the blue spot". |
| S3.7(f) | With several colours to spot and their own spots occupied, the highest value is placed first. | Oracle: "S3.7(f): two colours whose spots are both taken: the higher value is spotted first". From facts: "S3.7(f): the black and the pink pocketed in one foul stroke, their spots taken: black first". The precedence is only among colours whose own spots are occupied: "S3.7(e), (f), (j): a colour whose own spot is free goes on it, whatever else is being spotted". |
| S3.7(g) | With every spot occupied, the colour goes as near its own spot as possible, on the straight line from that spot to the nearest part of the top cushion. | Oracle: "S3.7(g): every spot taken: as near its own spot as it will go, straight towards the top cushion" (all six colours); "S3.7(g): the nearest place is measured from the balls in the way, not from the spot". |
| S3.7(h) | Pink and black, when there is no room on that line either, go as near their own spot as possible on the centre line of the table, which leaves the baulk side. | Oracle: "S3.7(h): the black, with no room above its spot, goes below it on the centre line"; "S3.7(h): the pink, with no room above its spot, goes below it on the centre line". The rulebook says nothing for the other four colours with that line blocked all the way to the cushion; no frame comes to that. |
| S3.7(i) | A spotted colour never touches another ball. | Oracle: "S3.7(i): a gap a ball only just fits is no place to spot a colour"; every "as near as possible" test also checks the colour is clear by `touching_gap_m` and no more than 0.5 mm further. |
| S3.7(j) | A colour is properly spotted only on the spot the rules give it. | Oracle: "S3.7(j): each colour is spotted on its own spot when that is free", against Section 1's measurements. |

## Rule 8: touching ball

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.8(a) | When the stroke ends with the cue ball touching a ball that is on or could be on, the referee calls it and says which. Against one or more colours after a red, the striker must also say which colour they are on. | Played: "S3.8(a): the cue ball coming to rest against a red is called a touching ball" (the cue ball rolls up at the slowest pace that reaches the red). From facts: "S3.8(a): resting against two colours after potting a red, both are indicated and the choice stays open". Played: "S3.8(a): against two colours of choice and declaring neither, playing away from both costs seven". |
| S3.8(b) | The striker must play away from the touching ball without moving it; moving it is a push stroke. | Played: "S3.8(b): playing into the touching red moves it: a push stroke"; "S3.8(b): a glancing stroke that moves the touching black, declared, is a push stroke of seven"; "S3.8(b), S2.19(b): moving a touching ball that is not on is a push stroke at that ball's value"; "S3.8(b), S2.19: played away, off a cushion and back onto the touching red is a fair stroke". |
| S3.8(c)(i) | No penalty for playing away from a touching ball that is on, provided it does not move: the stroke need hit nothing else, and may hit anything afterwards. | Played: "S3.8(c)(i): playing away from a touching red and hitting nothing else is a fair stroke"; "S3.8(c)(i): after playing away from a touching red, hitting a colour first is no foul"; "S3.8(c)(i): playing away from the touching colour on, the reds gone, is a fair stroke". |
| S3.8(c)(ii) | The same for a touching ball that could be on, when the striker declares it. | Played: "S3.8(c)(ii): declaring the touching colour and playing away from it is a fair stroke"; "S3.8(c)(ii): the only touching colour, played away from with nothing said, is taken as declared" (the README's reading of a declaration nobody asks for). |
| S3.8(c)(iii) | The same when the striker declares, and first hits, a different ball that could be on. | Played: "S3.8(c)(iii): declaring and first hitting another colour, the touching one left still, is a fair stroke"; and its limit, "S3.8(c): declaring another colour and missing it is a foul, whatever the cue ball was touching". |
| S3.8(d) | Against a ball that is not on nothing is called; the referee only answers if asked. | From facts: "S3.8(d): resting against a ball that is not on is not called". Played: "S3.8(d): a touching ball that is not on gives no cover: playing away and hitting nothing is a foul". The player can see for themselves whether the balls touch; nobody is asked. |
| S3.8(e) | Touching a ball on and a ball not on, only the ball on is indicated. | From facts: "S3.8(e): against a ball on and a ball not on, only the ball on is indicated". |
| S3.8(f) | No foul when a touching ball moves at the moment of striking through no act of the striker. | Cannot arise: a ball at rest moves only when another ball hits it. |
| S3.8(g) | Balls found touching, or no longer touching, after the referee has looked are put right. | Cannot arise: positions do not change between the referee's look and the stroke. |

## Rule 9: ball on the edge of a pocket

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.9(a) | A ball that drops by itself, outside any stroke, is put back. | Cannot arise: a ball at rest never falls later. Played, to hold that: "S3.9: a ball at rest on the brink of a pocket stays there" (half a millimetre from the drop, through a stroke played elsewhere). |
| S3.9(b)(i), (ii) | A ball that drops by itself in the path of a stroke: the balls are replaced and the stroke replayed, or, if the stroke was a foul, the penalty stands and the balls are replaced. | Cannot arise: the same. |
| S3.9(c) | A ball that hesitates on the edge and then drops is potted and stays down. | Played, as far as the table goes: "S3.9(c): a red that reaches the pocket at a crawl and drops has been potted". A ball is in the pocket the moment its centre crosses the drop; there is no hesitating. |

## Rule 10: fouls

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.10 | A foul is called as a foul. | Played: "S3.10: FOUL is called, with the penalty, when the stroke is judged". Also: "S3.10: after a foul the next stroke starts a new turn: red while one remains, else the lowest colour". |
| S3.10(a) | A foul with no stroke made ends the turn at once. | Cannot arise: there is nothing to do at the table but play a stroke. |
| S3.10(b) | With a stroke made, the penalty is announced when the stroke is complete. | Played: "S3.10(b): the stroke is judged when it is complete: a red that drops after the foul stays down". |
| S3.10(c) | A foul nobody awards or claims before the next stroke is condoned. | Cannot arise: the referee sees every foul. |
| S3.10(d) | After a foul a wrongly spotted colour stays where it is, but one off the table is spotted. | Played: "S3.10(d): a colour pocketed in a foul stroke is put back on its spot". The first half cannot arise (S3.7(a)). |
| S3.10(e) | Points made earlier in the break stand; nothing pocketed in the foul stroke scores. | Played: "S3.10(e): the break's earlier points stand; the ball pocketed in the foul stroke scores nothing". |
| S3.10(f) | The next stroke is from where the cue ball stopped, or from in hand if it left the table. | Played: "S3.10(f): after a foul that leaves the cue ball on the table, play goes on from where it lies"; "S3.10(f): after an in-off the next stroke is played from in hand". |
| S3.10(g) | Several fouls in one stroke cost the highest of their penalties, not the sum. | Played and from facts: "S3.10(g): several fouls in one stroke cost the highest penalty among them, once". |
| S3.10(h)(i) | The offender incurs the penalty: the points go to the opponent. | Played: "S3.10(h)(i): the penalty points go to the other player; the offender's score is untouched". |
| S3.10(h)(ii) | The offender plays the next stroke if the next player asks. | Played: "S3.10(h)(ii): asked to play again, the offender plays the next stroke". |
| S3.10(i) | A ball fouled before the cue ball is struck: penalty, and the other player may have the balls replaced. | Cannot arise: no ball can be touched except by striking the cue ball. |
| S3.10(j) | An object ball disturbed while the cue ball is in hand: foul, the cue ball stays in hand. | Cannot arise: the cue ball in hand can only be put where it touches nothing. "S3.10(j), S3.11(b)(vii): the cue ball in hand cannot be put on or against a ball in play" holds that. |

## Rule 11: penalty values

Each value test plays the foul once and then judges it from facts against the other balls on: red;
each colour in its turn; each colour declared after a red; a free ball nominated.

| Clause | Requirement | Test |
| --- | --- | --- |
| S3.11 | Every foul costs four, unless (a) to (d) make it more. | "S3.11: no foul costs less than four". |
| S3.11(a)(i) | Value of the ball on: striking before a free-ball colour is spotted. | Cannot arise: spotting is done before the next stroke can be played. |
| S3.11(a)(ii) | Value of the ball on: striking the cue ball twice. | Cannot arise: one strike per stroke. |
| S3.11(a)(iii) | Value of the ball on: both feet off the floor. | Cannot arise: no body. |
| S3.11(a)(iv) | Value of the ball on: out of turn in four-handed play. | Cannot arise: four-handed snooker is not in the game. |
| S3.11(a)(v) | Value of the ball on: playing wrongly from in hand. | Cannot arise: the D is enforced. "S3.11(a)(v): nothing can be played from outside the D: no such place is accepted" holds that. |
| S3.11(a)(vi) | Value of the ball on: the cue ball hits no ball. | "S3.11(a)(vi): a stroke that hits no ball costs the value of the ball on, four at least". |
| S3.11(a)(vii) | Value of the ball on: the cue ball goes into a pocket. | "S3.11(a)(vii): the cue ball in a pocket costs the value of the ball on, four at least". |
| S3.11(a)(viii) | Value of the ball on: the cue ball is left snookered by the nominated free ball (but for S3.12(b)(ii)'s two-ball exception: `clauses-t3.md`). | From facts: "S3.11(a)(viii): the cue ball left snookered behind the free ball costs the value of the ball on". |
| S3.11(a)(ix) | Value of the ball on: the same in six-red snooker. | Cannot arise: six-red snooker is not in the game. |
| S3.11(a)(x) | Value of the ball on: a jump shot. | Cannot arise: balls stay on the cloth. |
| S3.11(a)(xi) | Value of the ball on: a non-standard cue. | Cannot arise: one cue. |
| S3.11(a)(xii) | Value of the ball on: conferring with a partner. | Cannot arise: no partners. |
| S3.11(b)(i) | Ball on or ball concerned, the higher: striking while a ball is moving. | Cannot arise: a stroke starts only when the last one is complete. |
| S3.11(b)(ii) | The same: striking before a colour is spotted. | Cannot arise: as (a)(i). |
| S3.11(b)(iii) | The same: a ball not on goes into a pocket. | "S3.11(b)(iii): pocketing a ball not on costs the ball on or that ball, whichever is higher". |
| S3.11(b)(iv) | The same: the cue ball first hits a ball not on, or, with a free ball nominated, any ball but it (unless together with a ball on). | "S3.11(b)(iv): hitting a ball not on first costs the ball on or that ball, whichever is higher". |
| S3.11(b)(v) | The same: a push stroke. | "S3.11(b)(v): a push stroke costs the ball on or the ball pushed, whichever is higher"; the touching-ball pushes are under S3.8(b). |
| S3.11(b)(vi) | The same: touching a ball in play with body, clothes or equipment. | Cannot arise: nothing touches a ball but the tip on the cue ball. |
| S3.11(b)(vii) | The same: touching a ball in play with the cue ball while in hand. | Cannot arise: see S3.10(j) and its test. |
| S3.11(b)(viii) | The same: a ball hits something left on the table. | Cannot arise: nothing is left on the table. |
| S3.11(b)(ix) | The same: striking before balls taken off for cleaning are back. | Cannot arise: balls are not cleaned. |
| S3.11(b)(x) | The same: a ball forced off the table. | Cannot arise: balls stay on the cloth. |
| S3.11(c) | Ball on or the higher of the two balls hit at once, whichever is higher. | "S3.11(c): two balls hit at once cost the ball on or the higher of the two, whichever is higher"; played under S3.6. |
| S3.11(d)(i) | Seven: using a ball off the table. | Cannot arise: nothing to pick up. |
| S3.11(d)(ii) | Seven: measuring a gap with an object. | Cannot arise: the same. |
| S3.11(d)(iii) | Seven: playing at a red straight after a red, or after a free ball taken as a red. | Played, two strokes each: "S3.11(d)(iii): a red played at straight after a red costs seven"; "S3.11(d)(iii): a red played at straight after a free ball taken as a red costs seven". From facts: "S3.11(d)(iii): the value is seven whichever red is hit and whatever else happens". A red hit by mistake with a colour declared is (b)(iv), held there. |
| S3.11(d)(iv) | Seven: using another ball as the cue ball. | Cannot arise: only the white can be struck. |
| S3.11(d)(v) | Seven: not declaring when asked. | Cannot arise: nobody is asked in mid-stroke; an undeclared foul is (d)(vi). |
| S3.11(d)(vi) | Seven: any foul after potting a red and before a colour is declared. | "S3.11(d)(vi): a foul after potting a red, before any colour is declared, costs seven"; also "S3.8(a): against two colours of choice and declaring neither, playing away from both costs seven". |
| S3.11(e) | Penalty for an infringement by the non-striker out of turn. | Cannot arise: only the striker can act. |
| S3.11(f) | Seven for touching a ball while balls are being replaced. | Cannot arise: the engine replaces the balls. |
| S3.11(g) | Seven for the non-striker using a ball or measuring. | Cannot arise: the same as (d)(i) and (ii). |

## Where these tests read the rulebook differently from the README or the headers

- S3.7(f): `oracle.hpp` says the highest value is spotted first, always. The rulebook gives that
  precedence only among colours whose own spots are occupied, so a colour whose spot is free keeps
  it: with a red on the black spot and the pink spot free, the pink goes on the pink spot and the
  black on the blue spot, not the black on the pink spot.
- S3.8(b): the README makes any ball that touched the cue ball before the stroke and then moved a
  push stroke. The rulebook's push is the ball moving as the cue ball is played away (the tip is
  still on the cue ball, S2.19). A touching red that the cue ball comes back onto off a cushion is
  an ordinary first impact on a ball on.
