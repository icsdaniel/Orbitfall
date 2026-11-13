extends Node2D
## M0.1 smoke scene: proves the GDExtension loads by showing a value computed in C++.

@onready var _label: Label = $Label

func _ready() -> void:
	var server: Node = ClassDB.instantiate("SimServer") as Node
	if server == null:
		_label.text = "SimServer extension NOT loaded"
		return
	add_child(server)
	_label.text = "%s — answer from C++: %d" % [server.call("core_version"), server.call("core_answer")]
