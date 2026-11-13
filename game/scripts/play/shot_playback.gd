class_name ShotPlayback
extends RefCounted
## What the shot player reads (spec 5: the game animates by evaluating the shot record). In the
## game it wraps an OfsShotRecord (from_record); tests subclass it with scripted motion.

## Doubles per ball in state_at(): x, y, vx, vy, wx, wy, wz, motion, pocket (OfsShotRecord.kStride).
const STRIDE := 9
const X := 0
const Y := 1
const VX := 2
const VY := 3
const WX := 4
const WY := 5
const WZ := 6
const MOTION := 7
const POCKET := 8
## ofs::phys::Motion.
const STATIONARY := 0
const SPINNING := 1
const SLIDING := 2
const ROLLING := 3
const POCKETED := 4


static func from_record(record: OfsShotRecord) -> ShotPlayback:
	return RecordPlayback.new(record)


func duration() -> float:
	return 0.0


func ball_count() -> int:
	return 0


## Every ball at time t (core coordinates, SI units), STRIDE doubles each.
func state_at(_t: float) -> PackedFloat64Array:
	return PackedFloat64Array()


## {t, kind, a, b, body, from, to, speed} in time order (OfsShotRecord.events()).
func events() -> Array[Dictionary]:
	return []


class RecordPlayback extends ShotPlayback:
	var _record: OfsShotRecord
	var _events: Array[Dictionary] = []

	func _init(record: OfsShotRecord) -> void:
		_record = record
		for e: Dictionary in record.events():
			_events.append(e)

	func duration() -> float:
		return _record.t_end()

	func ball_count() -> int:
		return _record.ball_count()

	func state_at(t: float) -> PackedFloat64Array:
		return _record.state_at(t)

	func events() -> Array[Dictionary]:
		return _events
