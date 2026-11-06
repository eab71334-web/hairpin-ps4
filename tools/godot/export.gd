extends Node
## Exports every car model (all meshes, flat triangles) and 3 stages (theme x seed) for the PS4 port.
const OUT := "/tmp/exp"
const MAGENTA := Color(1, 0, 1)

func _ready() -> void:
	L.tutorial_off = true
	DirAccess.make_dir_recursive_absolute(OUT)
	for model in Catalog.CAR_ORDER:
		var car := Car.new()
		car.setup(0, MAGENTA, false, {"car": model}, true)
		add_child(car)
		await get_tree().process_frame
		await get_tree().process_frame
		_export_car(car, model)
		car.queue_free()
		await get_tree().process_frame
	var fw := FileAccess.open(OUT + "/wheels.json", FileAccess.WRITE)
	fw.store_string(JSON.stringify(wheel_info))
	fw.close()
	_export_stages()
	get_tree().quit()

func _mat_of(mi: MeshInstance3D, surf: int) -> StandardMaterial3D:
	var m: Material = mi.material_override
	if m == null:
		m = mi.mesh.surface_get_material(surf)
	if m == null:
		m = mi.get_surface_override_material(surf)
	return m as StandardMaterial3D

var wheel_info := {}

func _export_car(car: Node3D, model: String) -> void:
	var buf := PackedByteArray()
	var wbuf := PackedByteArray()
	var n := 0
	var wn := 0
	var inv := car.global_transform.affine_inverse()
	var wm: Node3D = car._wheel_mesh
	var winv := wm.global_transform.affine_inverse() if wm != null else Transform3D.IDENTITY
	if wm != null:
		var pv: Node3D = car._wheel_pivot
		wheel_info[model] = [pv.position.x, pv.position.y, pv.position.z, pv.rotation.x]
	for node in car.find_children("*", "MeshInstance3D", true, false):
		var mi := node as MeshInstance3D
		if mi.mesh == null or not mi.is_visible_in_tree():
			continue
		var is_wheel := wm != null and wm.is_ancestor_of(mi)
		var xf: Transform3D = (winv if is_wheel else inv) * mi.global_transform
		for s in mi.mesh.get_surface_count():
			var arr := mi.mesh.surface_get_arrays(s)
			if arr.is_empty() or arr[Mesh.ARRAY_VERTEX] == null:
				continue
			var vs: PackedVector3Array = arr[Mesh.ARRAY_VERTEX]
			var idx: PackedInt32Array = arr[Mesh.ARRAY_INDEX] if arr[Mesh.ARRAY_INDEX] != null else PackedInt32Array()
			if idx.is_empty():
				idx = PackedInt32Array(range(vs.size()))
			var m := _mat_of(mi, s)
			var col := Color(0.5, 0.5, 0.5)
			var flags := 0
			if m != null:
				col = m.albedo_color
				if m.albedo_color.a < 0.9:
					flags |= 4
				if m.emission_enabled and m.emission.get_luminance() > 0.3:
					flags |= 2
					col = m.emission
				if absf(col.r - 1.0) < 0.02 and col.g < 0.02 and absf(col.b - 1.0) < 0.02:
					flags |= 1
			for t in range(0, idx.size() - 2, 3):
				var p: Array[Vector3] = []
				for k in 3:
					p.append(xf * vs[idx[t + k]])
				if is_wheel: wn += 1
				else: n += 1
				var tb: PackedByteArray = wbuf if is_wheel else buf
				for k in 3:
					for a in [p[k].x, p[k].y, p[k].z]:
						var q := clampi(int(round(a * 1024.0)), -32767, 32767)
						tb.append(q & 255)
						tb.append((q >> 8) & 255)
				tb.append(int(clampf(col.r, 0, 1) * 255))
				tb.append(int(clampf(col.g, 0, 1) * 255))
				tb.append(int(clampf(col.b, 0, 1) * 255))
				tb.append(flags)
				if is_wheel: wbuf = tb
				else: buf = tb
	var f := FileAccess.open("%s/%s.bin" % [OUT, model], FileAccess.WRITE)
	var hd := PackedByteArray()
	hd.resize(4)
	hd.encode_u32(0, n)
	f.store_buffer(hd)
	f.store_buffer(buf)
	f.close()
	var f2 := FileAccess.open("%s/%s_wheel.bin" % [OUT, model], FileAccess.WRITE)
	var hd2 := PackedByteArray()
	hd2.resize(4)
	hd2.encode_u32(0, wn)
	f2.store_buffer(hd2)
	f2.store_buffer(wbuf)
	f2.close()
	print("CAR ", model, " tris=", n, " wheel=", wn)

func _export_stages() -> void:
	var out := []
	var seeds := {"mountain": 11, "snow": 5, "autumn": 8}
	for th in ["mountain", "snow", "autumn"]:
		var st := Track.design_stage(int(seeds[th]), th)
		var t := Track.from_stage(st, false)
		var pts := []
		for i in t.points.size():
			var fl := 0
			if t.in_tunnel(i): fl |= 1
			if t.is_open(i): fl |= 2
			if t.ice_at(i): fl |= 4
			pts.append([snappedf(t.points[i].x, 0.01), snappedf(t.points[i].y, 0.01), snappedf(t.points[i].z, 0.01), snappedf(t.headings[i], 0.0001), fl])
		var obs := []
		for o in t.obstacles:
			obs.append([int(o["index"]), str(o["kind"]), int(o["side"])])
		out.append({"theme": th, "name": t.stage_name, "relief": t.relief, "points": pts, "obstacles": obs})
		print("STAGE ", th, " ", t.stage_name, " pts=", pts.size())
	var f := FileAccess.open(OUT + "/stages.json", FileAccess.WRITE)
	f.store_string(JSON.stringify(out))
	f.close()
