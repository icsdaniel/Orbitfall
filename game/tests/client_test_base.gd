class_name ClientTestBase
extends GdUnitTestSuite
## Shared helpers for the M1 client tests: a mock sim that only advances when told,
## and synthetic input events.

const SCREEN: Vector2 = Vector2(1280.0, 720.0)


func make_sim() -> MockSimServer:
	var sim: MockSimServer = auto_free(MockSimServer.new())
	sim.auto_advance = false
	add_child(sim)
	sim.load_content("res://content")
	sim.new_run(7, 0)
	sim.clear_calls()
	return sim


func make_router(sim: MockSimServer) -> InputRouter:
	var router: InputRouter = auto_free(InputRouter.new())
	router.sim = sim
	router.viewport_size_override = SCREEN
	router.set_process(false)
	router.set_process_unhandled_input(false)
	add_child(router)
	return router


func touch(index: int, pos: Vector2, pressed: bool) -> InputEventScreenTouch:
	var event: InputEventScreenTouch = InputEventScreenTouch.new()
	event.index = index
	event.position = pos
	event.pressed = pressed
	return event


func drag(index: int, pos: Vector2) -> InputEventScreenDrag:
	var event: InputEventScreenDrag = InputEventScreenDrag.new()
	event.index = index
	event.position = pos
	return event


func key(code: Key, pressed: bool) -> InputEventKey:
	var event: InputEventKey = InputEventKey.new()
	event.keycode = code
	event.physical_keycode = code
	event.pressed = pressed
	return event


func wheel(button: MouseButton, pos: Vector2) -> InputEventMouseButton:
	var event: InputEventMouseButton = InputEventMouseButton.new()
	event.button_index = button
	event.position = pos
	event.pressed = true
	return event


func tap(router: InputRouter, pos: Vector2) -> void:
	router.handle_event(touch(0, pos, true))
	router.advance(0.05)
	router.handle_event(touch(0, pos, false))


## Last value sent with rotate_input, or NAN when none was sent.
func last_rotate(sim: MockSimServer) -> float:
	var calls: Array[Array] = sim.calls_to(&"rotate_input")
	if calls.is_empty():
		return NAN
	return float(calls[-1][0])
