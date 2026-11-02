# Named table positions

Shared by the physics tests and sim_cli. Each file holds `start` (a table state in the serialize.hpp form), `cue_ball` and a default `strike`. Coordinates are core metres (table_geometry.hpp); R = 0.02625 m.

- `straight_1m.json`: Cue ball and a red 1 m apart (centres) on the long axis; aiming at 0 rad is a full-ball hit. C2, C3, screw and follow.
- `cut_half_ball.json`: As straight_1m with the red offset by R: aiming at 0 rad is a half-ball hit (30 degree cut). C4, C5, C6.
- `cut_quarter_ball.json`: Red offset by 1.5 R: a quarter-ball hit aiming at 0 rad. C4.
- `cut_three_quarter_ball.json`: Red offset by 0.5 R: a three-quarter-ball hit aiming at 0 rad. C4.
- `near_left_cushion.json`: Cue ball alone 0.3 m from the left cushion nose at the table's middle; tests choose the angle and speed. C7, C8.
- `lag.json`: Cue ball on the baulk line's centre, aiming up the table: the lag of C11 (to the top cushion and back to baulk).
- `red_frozen_to_top_cushion.json`: A red touching the top cushion, the cue ball 0.75 m away on the same line: cushion-hugging and frozen-ball rebounds.
- `cue_ball_on_baulk_cushion.json`: Cue ball touching the baulk cushion, aiming up the table: the cue must rise over the rail (obstruction).
- `ball_behind_cue_ball.json`: A red 7.5 mm behind the cue ball on the cue's line: the cue must rise over it (obstruction).
- `frozen_line_3.json`: Three reds touching in a line on the long axis, the cue ball 0.8 m behind: a zero-gap chain (spec 7.7).
- `frozen_pair_side_by_side.json`: Two reds touching side by side across the table, the cue ball hitting the first full: a cluster split.
- `towards_top_left_jaw.json`: A red rolling along the top cushion line into the top-left corner pocket's jaws (set its velocity in the test).
- `slow_to_middle_left.json`: A red 0.3 m from the left middle pocket, heading straight in slowly (set its velocity in the test).
