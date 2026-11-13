class_name ClientConfig
extends RefCounted
## Client-only tunables read from `client_config.json` (camera bands, gesture
## thresholds, time scales, tuning panel entries). Sim numbers stay in content/.

const PATH: String = "res://game/app/client_config.json"

var data: Dictionary = {}


static func load_default() -> ClientConfig:
	var config: ClientConfig = ClientConfig.new()
	if FileAccess.file_exists(PATH):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(PATH))
		if parsed is Dictionary:
			config.data = parsed
	return config


func num(section: String, key: String, fallback: float) -> float:
	var sec: Variant = data.get(section)
	if sec is Dictionary:
		var value: Variant = (sec as Dictionary).get(key)
		if value is float or value is int:
			return float(value)
	return fallback


func text(section: String, key: String, fallback: String) -> String:
	var sec: Variant = data.get(section)
	if sec is Dictionary:
		return str((sec as Dictionary).get(key, fallback))
	return fallback


func flag(section: String, key: String, fallback: bool) -> bool:
	var sec: Variant = data.get(section)
	if sec is Dictionary:
		return bool((sec as Dictionary).get(key, fallback))
	return fallback


func tunables() -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	var list: Variant = data.get("tunables")
	if list is Array:
		for item: Variant in list as Array:
			if item is Dictionary:
				out.append(item)
	return out
