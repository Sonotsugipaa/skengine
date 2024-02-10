#version 450



layout(set = 0, binding = 0) uniform FrameUbo {
	mat4 projview_transf4;
	mat4 proj_transf4;
	mat4 view_transf4;
	vec4 view_pos;
	vec4 ambient_lighting;
	uint ray_light_count;
	uint point_light_count;
	uint shade_step_count;
	float shade_step_smooth;
	float shade_step_exp;
	float dithering_steps;
	float rnd;
	float time_delta;
	uint flags;
} frame_ubo;


struct RayLight {
	vec4  direction;
	vec4  color;
	float aoa_threshold;
	float unused1;
	float unused2;
	float unused3;
};

struct PointLight {
	vec4  position;
	vec4  color;
	float falloff_exp;
	float unused0;
	float unused1;
	float unused2;
};

layout(std430, set = 0, binding = 1) readonly buffer RayLightBuffer {
	RayLight lights[];
} ray_light_buffer;

layout(std430, set = 0, binding = 1) readonly buffer PointLightBuffer {
	PointLight lights[];
} point_light_buffer;



layout(set = 1, binding = 0) uniform sampler2D tex_dfsSampler;
layout(set = 1, binding = 1) uniform sampler2D tex_nrmSampler;
layout(set = 1, binding = 2) uniform sampler2D tex_spcSampler;
layout(set = 1, binding = 3) uniform sampler2D tex_emiSampler;

layout(set = 1, binding = 4) uniform MaterialUbo {
	float shininess;
} material_ubo;

layout(location = 0) in vec4 frg_pos;
layout(location = 1) in vec4 frg_col;
layout(location = 2) in vec2 frg_tex;
layout(location = 3) in vec2 frg_viewport_pos;
layout(location = 4) in vec3 frg_nrm;
layout(location = 5) in vec3 frg_viewspace_tanu;
layout(location = 6) in vec3 frg_viewspace_tanv;
layout(location = 7) in vec3 frg_viewspace_tanw;
layout(location = 8) in mat3 frg_view3;

layout(location = 0) out vec4 out_col;

const float normal_backface_bias = 0.1;
const float pi                   = 3.14159265358;
const uint  flag_hdr_enabled     = 1;



// -----   Credit for the next functions down to `rndf(...)`:   -----
// -----   https://stackoverflow.com/a/17479300                 -----

// A single iteration of Bob Jenkins' One-At-A-Time hashing algorithm.
uint hash(uint x) {
    x += (x << 10u);
    x ^= (x >>  6u);
    x += (x <<  3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

uint hash(uvec2 v) { return hash(v.x ^ hash(v.y)); }

// Construct a float with half-open range [0:1] using low 23 bits.
// All zeroes yields 0.0, all ones yields the next smallest representable value below 1.0.
float randomFloatFromUint(uint m) {
	const uint ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
	const uint ieeeOne      = 0x3F800000u; // 1.0 in IEEE binary32

	m &= ieeeMantissa; // Keep only mantissa bits (fractional part)
	m |= ieeeOne;      // Add fractional part to 1.0

	float  f = uintBitsToFloat(m); // Range [1:2]
	return f - 1.0;                // Range [0:1]
}

float random1from2(vec2 v) {
	uint h = hash(uvec2(floatBitsToUint(v.x), floatBitsToUint(v.y)));
	return randomFloatFromUint(h);
}


float unorm_dither(float v) {
	const float steps = frame_ubo.dithering_steps;
	float v_step = v * steps;
	float up = ceil(v_step);
	float dn = floor(v_step);
	float chance = v_step - dn;
	float rnd = random1from2(gl_FragCoord.xy + frame_ubo.rnd);
	if(rnd < chance) return up / steps;
	else             return dn / steps;
}


struct LuminanceInfo {
	// the alpha channel is the luminance
	vec4 dfs;
	vec4 spc;
};


vec3 unorm_correct(vec3 v) {
	// v = v * 255;
	// if(v >= 127 && v <= 128) v = 127.5;
	// v = v / 255;
	v = v * 255.0;
	v.x = v.x + (v.x < 126.9? 0.0 : 0.5) - (v.x < 127.9? 0.0 : 0.5);
	v.y = v.y + (v.y < 126.9? 0.0 : 0.5) - (v.y < 127.9? 0.0 : 0.5);
	v.z = v.z + (v.z < 126.9? 0.0 : 0.5) - (v.z < 127.9? 0.0 : 0.5);
	return v / 255.0;
}

float shinify_exp(float x, float exp) {
	x = clamp(x, 0.0, 1.0);
	return pow(x, exp);
}

float aoa_fade(float value, float angle_of_attack) {
	float fade = angle_of_attack / normal_backface_bias;
	return value * clamp(fade, 0, 1);
}

float aoa_with_threshold(float value, float threshold) {
	return min(value * (threshold + 1.0), 1.0);
}

vec3 color_rgb_non_zero(vec4 v) {
	const float tiny_value = 0.000001;
	if((v.r + v.g + v.b) < tiny_value) {
		v.r = tiny_value;
		v.g = tiny_value;
		v.b = tiny_value;
	}
	return v.rgb;
}


float compute_flat_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace, vec3 view_dir, float angle_of_attack, float aoa_threshold) {
	float lighting = dot(
		view_dir,
		reflect(light_dir_viewspace, tex_nrm_viewspace) );
	float light_exp = material_ubo.shininess;
	lighting = shinify_exp(lighting, light_exp);
	angle_of_attack = aoa_with_threshold(angle_of_attack, aoa_threshold);
	lighting        = aoa_with_threshold(lighting,        aoa_threshold);
	lighting = aoa_fade(lighting, angle_of_attack);
	lighting = max(lighting, 0);
	return lighting;
}

float compute_rough_reflection(vec3 tex_nrm_viewspace, vec3 light_dir_viewspace, float angle_of_attack, float aoa_threshold) {
	float lighting = dot(tex_nrm_viewspace, light_dir_viewspace);
	lighting        = aoa_with_threshold(lighting,        aoa_threshold);
	angle_of_attack = aoa_with_threshold(angle_of_attack, aoa_threshold);
	lighting = aoa_fade(lighting, angle_of_attack);
	lighting = max(lighting, 0);
	return lighting;
}


float multistep(float v) {
	float r      = v;
	float fcount = frame_ubo.shade_step_count;
	r = pow(r, 1.0 / frame_ubo.shade_step_exp);
	r = r * fcount;
	r = round(r);
	r = r / fcount;
	r = pow(r, frame_ubo.shade_step_exp);
	r += (v * frame_ubo.shade_step_smooth);
	r /= (1.0 + abs(frame_ubo.shade_step_smooth));
	return r;
}


LuminanceInfo sum_ray_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	LuminanceInfo luminance;
	luminance.dfs = vec4(0.0, 0.0, 0.0, 0.0);
	luminance.spc = vec4(0.0, 0.0, 0.0, 0.0);

	// Sum luminances
	for(uint i = 0; i < frame_ubo.ray_light_count; ++i) {
		vec3 light_dir =
			frg_view3
			* ray_light_buffer.lights[i].direction.xyz;

		float aoa = dot(frg_nrm, light_dir);
		float aoa_threshold = ray_light_buffer.lights[i].aoa_threshold;

		float luminance_dfs = (
			ray_light_buffer.lights[i].color.a
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aoa, aoa_threshold) );
		float luminance_spc = (
			ray_light_buffer.lights[i].color.a
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aoa, aoa_threshold) );

		luminance.dfs.a += luminance_dfs;
		luminance.spc.a += luminance_spc;

		luminance.dfs.rgb += point_light_buffer.lights[i].color.rgb * luminance_dfs;
		luminance.spc.rgb += point_light_buffer.lights[i].color.rgb * luminance_spc;
	}

	luminance.dfs.rgb = color_rgb_non_zero(luminance.dfs);
	luminance.spc.rgb = color_rgb_non_zero(luminance.spc);
	return luminance;
}


LuminanceInfo sum_point_lighting(vec3 tex_nrm_viewspace, vec3 view_dir) {
	LuminanceInfo luminance;
	luminance.dfs = vec4(0.0, 0.0, 0.0, 0.0);
	luminance.spc = vec4(0.0, 0.0, 0.0, 0.0);
	uint light_count = frame_ubo.ray_light_count + frame_ubo.point_light_count;

	// Sum luminances
	for(uint i = frame_ubo.ray_light_count; i < light_count; ++i) {
		vec3 light_dir =
			point_light_buffer.lights[i].position.xyz
			- frg_pos.xyz;

		light_dir = normalize(frg_view3 * light_dir);

		float aoa = dot(frg_nrm, light_dir);
		float aoa_threshold = 0.0;

		float intensity         = point_light_buffer.lights[i].color.a;
		float fragm_distance    = distance(frg_pos.xyz, point_light_buffer.lights[i].position.xyz);
		float intensity_falloff = intensity / pow(fragm_distance, point_light_buffer.lights[i].falloff_exp);

		float luminance_dfs = (
			intensity_falloff
			* compute_rough_reflection(tex_nrm_viewspace, light_dir, aoa, aoa_threshold) );
		float luminance_spc = (
			intensity_falloff
			* compute_flat_reflection(tex_nrm_viewspace, light_dir, view_dir, aoa, aoa_threshold) );

		luminance.dfs.a += luminance_dfs;
		luminance.spc.a += luminance_spc;

		luminance.dfs.rgb += point_light_buffer.lights[i].color.rgb * luminance_dfs;
		luminance.spc.rgb += point_light_buffer.lights[i].color.rgb * luminance_spc;
	}

	luminance.dfs.rgb = color_rgb_non_zero(luminance.dfs);
	luminance.spc.rgb = color_rgb_non_zero(luminance.spc);
	return luminance;
}


vec3 color_excess_filter(vec3 col) {
	float len = length(col);
	if(len > 1.0) {
		float excess_len = len - 1.0;
		return (col / len) + (excess_len);
	}
	return col;
}


vec4 mix_weighted_colors(vec4 c0, vec4 c1) {
	// This assumes that  normalize(col.rgb) == col.rgb / col.a  ,
	// which should be the case for sum_*_lighting if all light colors
	// are already normalized
	return vec4(normalize(c0.rgb + c1.rgb), c0.a + c1.a);
}



void main() {
	mat3 tbn = transpose(inverse(mat3(
		normalize(frg_viewspace_tanu),
		normalize(frg_viewspace_tanv),
		normalize(frg_viewspace_tanw) )));

	vec4 tex_dfs = texture(tex_dfsSampler, frg_tex);
	vec3 tex_nrm = texture(tex_nrmSampler, frg_tex).rgb;
	vec4 tex_spc = texture(tex_spcSampler, frg_tex);
	vec4 tex_emi = texture(tex_emiSampler, frg_tex);

	tex_nrm = unorm_correct(tex_nrm);
	tex_nrm = normalize((tex_nrm * 2.0) - 1.0);

	vec3 tex_nrm_viewspace = normalize(tbn * tex_nrm);
	vec3 view_dir          = normalize(frg_view3 * ((frg_pos.xyz) - (frame_ubo.view_pos.xyz)));

	LuminanceInfo luminance;
	LuminanceInfo ray_luminance = sum_ray_lighting(tex_nrm_viewspace, view_dir);
	LuminanceInfo pt_luminance  = sum_point_lighting(tex_nrm_viewspace, view_dir);
	luminance.dfs = mix_weighted_colors(ray_luminance.dfs, pt_luminance.dfs);
	luminance.spc = mix_weighted_colors(ray_luminance.spc, pt_luminance.spc);

	if(frame_ubo.shade_step_count > 0) {
		luminance.dfs.a = multistep(luminance.dfs.a);
		luminance.spc.a = multistep(luminance.spc.a);
	}
	if(frame_ubo.dithering_steps >= 1.0) {
		luminance.dfs.a = unorm_dither(luminance.dfs.a);
		luminance.spc.a = unorm_dither(luminance.spc.a);
	}

	out_col.rgb =
		max(vec3(0,0,0), frg_col.rgb * (
			(tex_dfs.rgb * luminance.dfs.rgb * luminance.dfs.a) +
			(tex_spc.rgb * luminance.spc.rgb * luminance.spc.a) +
			(tex_dfs.rgb * frame_ubo.ambient_lighting.rgb * frame_ubo.ambient_lighting.a)
		))
		+ tex_emi.rgb;
	out_col.a = frg_col.a;

	// Handle colors > 1 in a LDR-friendly way
	if((frame_ubo.flags & flag_hdr_enabled) != 0) {
		out_col.rgb = color_excess_filter(out_col.rgb);
	}
}
