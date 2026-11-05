extends Node
## Renders every car model from behind, twice (white and black paint), transparent background -> PNGs for the PS4 port.
const OUT := "/tmp/spr/out"
var vp: SubViewport
var cam: Camera3D
var holder: Node3D

func _ready() -> void:
	L.tutorial_off = true
	DirAccess.make_dir_recursive_absolute(OUT)
	vp = SubViewport.new()
	vp.own_world_3d = true
	vp.transparent_bg = true
	vp.size = Vector2i(1024, 640)
	vp.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	add_child(vp)
	var env := WorldEnvironment.new()
	var e := Environment.new()
	e.background_mode = Environment.BG_CLEAR_COLOR
	e.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	e.ambient_light_color = Color(0.85, 0.88, 0.95)
	e.ambient_light_energy = 0.9
	env.environment = e
	vp.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-35, 150, 0)      # light from behind-above (we look at the rear)
	sun.light_energy = 1.2
	vp.add_child(sun)
	var fill := DirectionalLight3D.new()
	fill.rotation_degrees = Vector3(-15, 20, 0)
	fill.light_energy = 0.45
	vp.add_child(fill)
	cam = Camera3D.new()
	cam.fov = 15.0
	vp.add_child(cam)
	holder = Node3D.new()
	vp.add_child(holder)
	cam.position = Vector3(0, 1.7, 14.0)
	cam.look_at(Vector3(0, 0.78, 0))
	await _run()
	get_tree().quit()

func _run() -> void:
	for model in Catalog.CAR_ORDER:
		for pass_i in 2:
			for c in holder.get_children():
				holder.remove_child(c)
				c.free()
			var car := Car.new()
			var paint := "white" if pass_i == 0 else "black"
			car.setup(0, Color(0.2, 0.8, 0.3), false, {"car": model, "paint": paint, "stripe": "none"}, true)
			holder.add_child(car)
			for n in car.find_children("*", "Label3D", true, false):
				n.visible = false
			for i in 6:
				await RenderingServer.frame_post_draw
			var img := vp.get_texture().get_image()
			img.save_png("%s/%s_%d.png" % [OUT, model, pass_i])
			print("SPRITE ", model, " ", pass_i)
