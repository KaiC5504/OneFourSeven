# The rules engine: how the rulebook is read

The engine (`core/include/ofs/rules/`) implements the WPBSA Official Rules of Snooker, 2024-25
edition with the August 2024 updates. `S3.14(c)` means Section 3 Rule 14(c). The rulebook is not in
this repository (copyright); agents read the copy in the main checkout's gitignored
`local/reference/wpbsa/`, and tests cite clause numbers with their own words.

A game has no human referee, so wherever the rulebook leaves something to the referee's eye or to a
spoken declaration, this page says what the engine does. Builders implement it; the scenario tests
(`core/tests/rules/`, one per clause) are written from the rulebook and this page, never from the
engine's code. Where the two disagree, the rulebook wins and this page is corrected.

`clauses-*.md` beside this file map every clause to its scenario test or to the reason it cannot
arise.

## One stroke

1. `place_cue_ball` while the cue ball is in hand (any number of times).
2. The stroke is simulated from the frame's balls.
3. `observe` reads the physical facts from the shot record; `judge` applies the rules to them and
   returns the frame after the stroke with the referee's calls.
4. If the frame is then `pending`, the `decider` chooses one of `options`; `decide` applies it.

## The ball on and the declaration (S2.11, S2.12, S3.3(g), (h))

- Red on (`red`): every red is on. A colour of choice (`any_colour`, after a red was potted): every
  colour could be on, and the declared colour is on. Colours in order (`colour`): that colour.
- The striker's declaration is `ShotInput::nominated`. It counts only when it is a colour on the
  table that `nominations()` lists; anything else is ignored and logged as `rules.invalid`.
- No declaration given where one was open: the first ball hit, when it is one ball, is taken as
  declared (the referee asks only when it is not obvious, S2.12). If no ball was hit and exactly one ball that could be
  on was touching the cue ball and did not move, that ball is taken as declared (S3.8(c)(ii)).
  Otherwise nothing was declared.
- A colour of choice on, nothing declared: hitting a red first is `reds_in_succession`, 7
  (S3.11(d)(iii)); any other foul in that stroke is also `before_nominating`, 7 (S3.11(d)(vi)).
  S3.11(d)(v) (failing to declare when asked) cannot arise: nobody is asked mid-stroke.

## First impact (S3.6, S3.8, S3.11(b)(iv), (c), S3.12(b)(i))

- Legal: one ball on; or, with red on, any number of reds at once; or the nominated free ball, alone
  or at once with a ball on.
- Nothing hit: `no_hit`, the value of the ball on (S3.11(a)(vi)).
- A ball not on first, or with a free ball nominated any ball but it first: `first_hit_not_on`, the
  ball on or the ball concerned, whichever is higher (S3.11(b)(iv)).
- Two balls at once other than the legal pairs: `simultaneous_hit`, the ball on or the higher of the
  two (S3.11(c)). "At once" is within `simultaneous_hit_s`.
- Touching ball (S3.8): when the cue ball started touching a ball that is on, or that could be on
  and is the declared one, and that ball did not move, the first impact is taken as made on it:
  whatever the cue ball hits afterwards, or nothing, is no foul for the first impact. A ball that
  touched the cue ball before the stroke and was moved by it as the cue ball left makes it a
  `push_stroke` (S3.8(b)); one the cue ball was played away from and that is moved later in the
  stroke, by another ball or the returning cue ball, does not.
- Push stroke otherwise (S2.19): `ShotFacts::push`, by `push_contact_time_s` and
  `push_fine_edge_rad`; the ball on or the ball concerned (S3.11(b)(v)).

## Pots (S2.7, S3.3(g), (h), S3.10(e), S3.11(a)(vii), (b)(iii), S3.12(c)-(e))

- Red on: every red potted scores 1. A colour of choice: the declared colour scores its value and
  is spotted. Colours in order: the colour on scores and stays off.
- Free ball nominated: potted, it scores the value of the ball on and is spotted (c). A ball on
  potted after the free ball was hit first scores and stays off (d). Both potted: only the ball on
  scores, unless red was on, when each ball potted scores (e).
- Any other object ball in a pocket: `pot_not_on`, the ball on or the ball concerned (S3.11(b)(iii)).
- The cue ball in a pocket: `in_off`, the value of the ball on; it is then in hand.
- A foul stroke scores nothing. Reds pocketed in it stay off; colours pocketed in it are spotted
  (S3.7), also once the reds are gone.
- Spotting (S3.7(e) to (j)): every colour whose own spot is free goes on it; S3.7(f)'s order,
  highest value first, applies to the colours whose own spots are occupied.

## The penalty (S3.10(g), S3.11)

- Each foul's value is at least 4. The value of the ball on is 1 for red, the declared colour's
  value for a colour of choice, the colour's value in order, and with a free ball the value of the
  ball on.
- The penalty is the highest value among the stroke's fouls; `Judgement::fouls` lists them all,
  highest first, ties in `FoulKind` order.
- Free-ball snooker (S3.12(b)(ii)): after a stroke that used a free ball and scored nothing, the
  cue ball must not be snookered on the balls on by that free ball: `free_ball_snooker`, the value
  of the ball on, when `snookered` holds for the resting position and `snookering_balls` contains
  the free ball. Not when only two object balls remain.

## After a legal stroke

- A score keeps the striker at the table: after a red, a colour of choice; after that colour (potted
  or not, once the last red has gone) the colours in ascending order; the lowest-value colour on
  the table is on.
- No score passes the turn: red is on while a red remains, else the lowest-value colour on the
  table.
- The break (S2.8) adds each stroke's points and ends with the turn. FREE BALL lapses after the
  stroke.
- TOUCHING BALL (S3.8(a)) is called when the cue ball rests within `touching_gap_m` of a ball on or
  one that could be on for the next stroke; `Position::touching` lists them.

## After a foul (S3.10, S3.12, S3.13)

- The penalty goes to the other player, the break ends, the offender's turn ends.
- The ball on becomes red while a red remains, else the lowest-value colour on the table.
- `pending` is `after_foul`: the non-offender is `decider` and `striker`. `original` holds the
  position before the stroke.
- FREE BALL (S3.12): called when the cue ball is snookered on every ball on (S2.17), from every
  position in the D when it is in hand.
- `play`: the non-offender plays, with the free ball if one was called.
- `play_again`: the offender plays from the position left; the free ball is void (S3.12(f)); a miss
  sequence ends (S3.14(d)(iii)).
- `replace` (offered only after FOUL AND A MISS, S3.14(b)): every ball, the cue ball included, goes
  back to `original`, with the ball on, a free ball the offender then had and the cue ball in hand
  as they were. The offender plays. The penalty stands. The request cannot be withdrawn (S3.13):
  there is nothing to withdraw it with.

## Foul and a miss (S2.21, S3.14). The owner chose the television standard.

A miss is considered only for a stroke whose first impact was not on a ball on (`no_hit`,
`first_hit_not_on`, or a free ball nominated and not hit first). With a free ball available, the
path tests below take every object ball as one that could be on. In order:

1. The stroke was played from a replaced position in a S3.14(d) run (`miss_run` at least 1): a
   miss, whatever the scores (`S3.14(d)`).
2. Any player required penalty points before the stroke, or does once its penalty is added: no
   miss (`S3.14(a)(i)`). Intent is never judged.
3. `can_hit` is false: no miss (`S3.14(a)(ii)`).
4. `clear_path` holds: a miss (`S3.14(c)`).
5. Otherwise (snookered): a miss (`S3.14(a)`), unless `miss_lenient_gap_m` is above 0 and the cue
   ball passed within it of a ball on (`lenient`). It is 0.

The run (S3.14(d)): a miss called when `full_ball` held, or when the cue ball was touching a ball
that could be on, makes `miss_run` one more; any other stroke, `play` or `play_again` makes it 0
and clears the warning. Choosing `replace` with `miss_run` at 2 warns the offender (`miss_warned`,
the `warning` call). A further such miss with the warning standing awards the frame to the other
player (`three_misses`).

## End of frame (S2.1, S3.4, S4.2)

- Only the black left: the first pot or foul ends the frame (S3.4(a)); a foul there costs 7. Scores
  then level: the black is re-spotted (S3.4(b)), the cue ball is in hand, `pending` is `toss` and
  the lots' winner (from the frame's seed and stroke count) chooses `play_first` or `play_second`.
  The first pot or foul then ends the frame.
- Only the black left, more than seven points apart, and not a re-spotted black: when the player to
  play trails, the frame is awarded to the other (S2.1(c)) at once; when they lead, `claim_frame`
  is among their options and they may play on (S2.1(b)).
- `concede_frame` (S4.2): offered to a player with nothing pending when `concede_any_time` is set
  for them or any player requires penalty points; `concede_match` likewise with nothing pending.
  A concession takes effect at once: there is nobody to decline it (S4.2(a)).
- Three misses (S3.14(d)(ii)) as above.

## Stalemate (S3.17; spec 9.1 leaves the thresholds to us)

A stroke is quiet when nothing is potted and the object balls end, summed, less than
`stalemate_quiet_travel_m` from where they began. After `stalemate_offer_strokes` quiet strokes in
a row, with nothing else pending, a re-rack is offered to the player about to play
(`pending` `rerack`); if they accept, the other is asked. Both accept: `restart`. Either refuses:
play goes on, and if the next `stalemate_grace_strokes` strokes are all quiet the frame restarts
anyway (S3.17(b)). Any stroke that is not quiet clears the count and the proviso; an in-off is
never quiet. The match racks a restarted frame again with the same breaker and no score. In a
re-spotted black the engine itself sets the black and the cue ball again, the same player to
play, without `restart` (S3.17(d)).

## In hand (S2.9, S3.5; spec 8.6)

The cue ball may be placed anywhere on or within the lines of the D where it touches no ball. A
place the oracle refuses is refused: nothing is played from outside the D, so S3.5(b) and
S3.11(a)(v) cannot arise. Left alone, the cue ball stays at the legal place nearest the middle of
the D between the brown and yellow spots.

## Clauses that cannot arise in the game

| Clause | Why |
| --- | --- |
| S2.14, S3.11(b)(x): forced off the table | balls stay on the cloth in the physics (spec 7.9) |
| S2.20, S3.11(a)(x): jump shot | the same |
| S3.3(m): rebounding from a pocket | a ball whose centre enters a drop circle is potted |
| S3.9: ball on the edge of a pocket | a ball at rest never falls later |
| S3.2(c), S3.10(i), (j), S3.11(a)(i)-(iii), (b)(i), (ii), (vi)-(ix), (d)(i), (ii), (iv), (v), (e)-(g), S3.15, S3.16, S3.20 | touching balls, striking early or twice, equipment and penalties away from the table have no way to happen |
| S3.3(d), (k), (l): wrong player, out of turn | the game decides who plays |
| S3.5(b), S3.11(a)(v): improperly from in hand | the D is enforced |
| S3.18, S3.19: four-handed, six reds | not in the game |
| S4.1, S4.3 to S4.7, Section 5: conduct, time, officials | no conduct to judge |
| S4.2(b): conceding only when penalty points are required | the owner's ruling: the person may concede at any time; the opponent only then |
