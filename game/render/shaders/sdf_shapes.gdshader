shader_type canvas_item;
// M0.4 SDF shape shader for BatchRenderer multimesh batches (placeholder visuals).
// INSTANCE_CUSTOM: x = shape id, y = colour tier (< 0: use instance colour only),
// z = trait bit flags (bit0 camo, bit1 armored, bit2 shielded), w = outline width
// override as a fraction of the shape radius (0 = default).

uniform sampler2D palette : filter_nearest, repeat_disable;
uniform float palette_size = 8.0;
uniform float zoom = 1.0;
uniform float shape_size = 16.0; // world-space shape diameter at instance scale 1
uniform float min_px = 3.0;
uniform float max_px = 4096.0;
uniform float outline_width = 0.14;
uniform float armored_outline_width = 0.22;
uniform vec4 armored_color : source_color = vec4(0.62, 0.64, 0.68, 1.0);
uniform vec4 shield_color : source_color = vec4(0.45, 0.85, 1.0, 1.0);

// The quad is QUAD_PAD times larger than the shape so outline and glow fit.
const float QUAD_PAD = 1.5;

varying flat vec4 v_custom;
varying flat float v_px; // on-screen shape diameter in pixels

void vertex() {
	v_custom = INSTANCE_CUSTOM;
	float world = max(length(MODEL_MATRIX[0].xy) * shape_size, 1e-6);
	float px = world * max(zoom, 1e-6);
	float target = clamp(px, min_px, max(min_px, max_px));
	VERTEX *= target / px;
	v_px = target;
}

float sd_box(vec2 p, float b) {
	vec2 q = abs(p) - vec2(b);
	return length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0);
}

float sd_triangle(vec2 p, float r) {
	const float k = 1.7320508;
	p.x = abs(p.x) - r;
	p.y = p.y + r / k;
	if (p.x + k * p.y > 0.0) {
		p = vec2(p.x - k * p.y, -k * p.x - p.y) / 2.0;
	}
	p.x -= clamp(p.x, -2.0 * r, 0.0);
	return -length(p) * sign(p.y);
}

float sd_hexagon(vec2 p, float r) {
	const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
	p = abs(p);
	p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
	p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
	return length(p) * sign(p.y);
}

float sd_star5(vec2 p, float r, float rf) {
	const vec2 k1 = vec2(0.809016994375, -0.587785252292);
	const vec2 k2 = vec2(-0.809016994375, -0.587785252292);
	p.x = abs(p.x);
	p -= 2.0 * max(dot(k1, p), 0.0) * k1;
	p -= 2.0 * max(dot(k2, p), 0.0) * k2;
	p.x = abs(p.x);
	p.y -= r;
	vec2 ba = rf * vec2(-k1.y, k1.x) - vec2(0.0, 1.0);
	float h = clamp(dot(p, ba) / dot(ba, ba), 0.0, r);
	return length(p - ba * h) * sign(p.y * ba.x - p.x * ba.y);
}

float ndot(vec2 a, vec2 b) {
	return a.x * b.x - a.y * b.y;
}

float sd_rhombus(vec2 p, vec2 b) {
	p = abs(p);
	float h = clamp(ndot(b - 2.0 * p, b) / dot(b, b), -1.0, 1.0);
	float d = length(p - 0.5 * b * vec2(1.0 - h, 1.0 + h));
	return d * sign(p.x * b.y + p.y * b.x - b.x * b.y);
}

float shape_sdf(int shape, vec2 p) {
	vec2 up = vec2(p.x, -p.y); // canvas y points down; shapes point up on screen
	if (shape == 1) {
		// Narrowed so the nose is unmistakable: an equilateral triangle reads
		// the same in three headings.
		return sd_triangle(vec2(up.x * 1.45, up.y), 0.85) / 1.45;
	} else if (shape == 2) {
		return sd_box(p, 0.8);
	} else if (shape == 3) {
		return sd_hexagon(p, 0.86);
	} else if (shape == 4) {
		return sd_star5(up, 1.0, 0.5);
	} else if (shape == 5) {
		return sd_rhombus(p, vec2(0.68, 1.0));
	}
	// 6 = disc: a full circle; fragment() adds an inner rim so it reads as a
	// saucer, not as a big Drifter.
	return length(p) - 1.0;
}

void fragment() {
	vec2 p = (UV - vec2(0.5)) * 2.0 * QUAD_PAD; // shape radius = 1
	int shape = int(v_custom.x + 0.5);
	float tier = v_custom.y;
	int flags = int(v_custom.z + 0.5);
	bool camo = (flags & 1) != 0;
	bool armored = (flags & 2) != 0;
	bool shielded = (flags & 4) != 0;

	float px_unit = 2.0 / max(v_px, 1.0); // one screen pixel in shape units
	float d = shape_sdf(shape, p);

	vec4 tint = COLOR; // instance colour
	vec3 fill = tint.rgb;
	if (tier >= 0.0) {
		float u = (min(floor(tier + 0.5), palette_size - 1.0) + 0.5) / palette_size;
		fill = texture(palette, vec2(u, 0.5)).rgb * tint.rgb;
	}

	float lum = dot(fill, vec3(0.299, 0.587, 0.114));
	vec3 line_col = lum < 0.3 ? mix(fill, vec3(1.0), 0.55) : fill * 0.35;
	float ow = v_custom.w > 0.0 ? v_custom.w : outline_width;
	if (armored) {
		ow = max(ow, armored_outline_width);
		line_col = armored_color.rgb;
	}
	// Tiny shapes are fill only so they stay readable as dots.
	float detail = smoothstep(5.0, 12.0, v_px);
	ow *= detail;

	float cover = clamp(0.5 - d / px_unit, 0.0, 1.0);
	float inner = clamp(0.5 - (d + ow) / px_unit, 0.0, 1.0);
	float fill_alpha = 1.0;
	float line_alpha = 1.0;
	if (camo) {
		fill_alpha = 0.4;
		float dash = fract(atan(p.y, p.x) / TAU * 10.0);
		line_alpha = mix(0.0, 1.0, smoothstep(0.4, 0.5, dash) * (1.0 - smoothstep(0.9, 1.0, dash)));
	}
	vec3 col = mix(line_col, fill, inner);
	if (shape == 6) {
		float rim = 1.0 - smoothstep(0.0, px_unit, abs(length(p) - 0.58) - 0.04);
		col = mix(col, line_col, rim * detail);
	}
	float alpha = cover * mix(line_alpha, fill_alpha, inner);

	if (shielded) {
		float od = d;
		float g = (od - 0.26) / 0.085;
		float glow = exp(-g * g) * 0.95 + 0.25 * exp(-max(od, 0.0) * 6.0) * step(0.0, od);
		glow *= detail;
		float total = alpha + glow * (1.0 - alpha);
		col = mix(shield_color.rgb, col, alpha / max(total, 1e-4));
		alpha = total;
	}

	COLOR = vec4(col, alpha * tint.a);
}
