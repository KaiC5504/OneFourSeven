# Rules fixtures

The JSON forms of `core/include/ofs/rules/serialize.hpp` in full, one example each. The serialiser's
tests read each back and write it out unchanged; the game's tests use them as engine answers
(`scripts/sync_data.py` copies them to `game/data/rules/`).

- `example_frame.json`: a frame after the opponent (player 1) missed every ball with two reds
  left: FOUL AND A MISS, a free ball called, player 0 to choose.
- `example_frame_view.json`: the same with the `view` the binding adds (`frame_view_json`).
- `example_judgement.json`: the judgement that led to it.
- `example_match.json`: a best of three, one frame played, with that frame in play.
