# Physics references

Papers and code the physics is built from (spec 7). Paywalled papers are cited, not copied
into this public repo. pooltool is Apache-2.0: its code may be ported with attribution in
`THIRD_PARTY.md`, and its implementations are the most direct way to read the papers' equations.

| Topic | Source | Where to read it |
| --- | --- | --- |
| Event-based simulation, motion states, event detection | Leckie & Greenspan (2005), "An Event-Based Pool Physics Simulator", Advances in Computer Games 11, LNCS 4250 | the paper; pooltool `pooltool/evolution/event_based/` (`simulate.py`, `detect/`) and `pooltool/physics/evolve/` |
| Motion equations by state (sliding, rolling, spinning) | Kiefl, "pooltool theory" | https://ekiefl.github.io/2020/04/24/pooltool-theory/ |
| Slide-to-roll time and distance | Alciatore, technical proof TP 4.1 | https://billiards.colostate.edu/technical_proofs/ |
| 30° rule, 90° rule | Alciatore TP A.4, TP 3.1 | as above |
| Ball-ball friction and throw (mu = a + b e^(-c v)) | Alciatore TP A.14, "Throw" FAQ; pooltool `physics/resolve/ball_ball/friction.py` | as above; https://billiards.colostate.edu/faq/throw/ |
| Frictional ball-ball collision | pooltool `physics/resolve/ball_ball/frictional_mathavan/` | the code |
| Cushion impacts | Mathavan, Jackson & Parkin (2010), "A theoretical analysis of billiard ball dynamics under cushion impacts", Proc. IMechE C 224(9):1863-1873, doi:10.1243/09544062JMES1964 | pooltool `physics/resolve/ball_cushion/mathavan_2010/model.py` |
| Cue strike, squirt | Marlow (1995), "The Physics of Pocket Billiards"; Alciatore squirt FAQ; pooltool `physics/resolve/stick_ball/instantaneous_point/`, `squirt.py` | https://billiards.colostate.edu/faq/squirt/ |
| Physical properties (mass, friction, restitution ranges) | Alciatore, "Pool and billiards physics resources: physical properties" | https://billiards.colostate.edu/faq/physics/physical-properties/ |
| Table, spots, balls | WPBSA Official Rules (2022) | spec section 6 |
| Pocket geometry | not public (spec 6.3); pooltool `objects/table/specs.py` SnookerTableSpecs and table makers' figures | recorded in `core/data/table_shape.json` |
