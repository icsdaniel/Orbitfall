extends GdUnitTestSuite
## M0.1: the extension loads and returns a value from the C++ core.

func test_sim_server_loads() -> void:
	assert_bool(ClassDB.class_exists("SimServer")).is_true()
	var server: Node = auto_free(ClassDB.instantiate("SimServer")) as Node
	assert_object(server).is_not_null()
	assert_int(server.call("core_answer")).is_equal(42)
