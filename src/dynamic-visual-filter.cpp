/*
 * GPU-only visual filter.  All per-pixel work is performed by
 * data/dynamic_visual.effect; this C++ file only transfers user settings.
 */

#include <obs-module.h>

#include <cmath>

#include "filters.hpp"

namespace {

constexpr char kWarpEnabled[] = "warp_enabled";
constexpr char kWarpIntensity[] = "warp_intensity";
constexpr char kWarpFrequency[] = "warp_frequency";
constexpr char kBreathEnabled[] = "breath_enabled";
constexpr char kBreathAmplitude[] = "breath_amplitude";
constexpr char kBreathCycle[] = "breath_cycle_seconds";
constexpr char kTiltAngle[] = "tilt_angle_degrees";
constexpr char kColorEnabled[] = "color_enabled";
constexpr char kColorCycle[] = "color_cycle_seconds";
constexpr char kHueDegrees[] = "hue_degrees";
constexpr char kSaturationDelta[] = "saturation_delta";
constexpr char kGammaMin[] = "gamma_min";
constexpr char kGammaMax[] = "gamma_max";
constexpr char kGrainEnabled[] = "grain_enabled";
constexpr char kGrainOpacity[] = "grain_opacity";

struct DynamicVisualFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;

	gs_eparam_t *warp_enabled = nullptr;
	gs_eparam_t *warp_intensity = nullptr;
	gs_eparam_t *warp_frequency = nullptr;
	gs_eparam_t *breath_enabled = nullptr;
	gs_eparam_t *breath_amplitude = nullptr;
	gs_eparam_t *breath_cycle_seconds = nullptr;
	gs_eparam_t *tilt_angle_degrees = nullptr;
	gs_eparam_t *color_enabled = nullptr;
	gs_eparam_t *color_cycle_seconds = nullptr;
	gs_eparam_t *hue_degrees = nullptr;
	gs_eparam_t *saturation_delta = nullptr;
	gs_eparam_t *gamma_min = nullptr;
	gs_eparam_t *gamma_max = nullptr;
	gs_eparam_t *grain_enabled = nullptr;
	gs_eparam_t *grain_opacity = nullptr;
	gs_eparam_t *time = nullptr;
	gs_eparam_t *aspect = nullptr;

	bool warp = false;
	float warp_strength = 0.01f;
	float warp_freq = 1.0f;
	bool breath = false;
	float breath_amount = 0.008f;
	float breath_cycle = 8.0f;
	float tilt_angle = 0.15f;
	bool dynamic_color = false;
	float color_cycle = 8.0f;
	float hue = 0.5f;
	float saturation = 0.03f;
	float gamma_low = 0.98f;
	float gamma_high = 1.02f;
	bool grain = false;
	float grain_amount = 0.01f;
	float elapsed = 0.0f;
};

void cache_effect_parameters(DynamicVisualFilter &filter)
{
	auto parameter = [&filter](const char *name) { return gs_effect_get_param_by_name(filter.effect, name); };
	filter.warp_enabled = parameter(kWarpEnabled);
	filter.warp_intensity = parameter(kWarpIntensity);
	filter.warp_frequency = parameter(kWarpFrequency);
	filter.breath_enabled = parameter(kBreathEnabled);
	filter.breath_amplitude = parameter(kBreathAmplitude);
	filter.breath_cycle_seconds = parameter(kBreathCycle);
	filter.tilt_angle_degrees = parameter(kTiltAngle);
	filter.color_enabled = parameter(kColorEnabled);
	filter.color_cycle_seconds = parameter(kColorCycle);
	filter.hue_degrees = parameter(kHueDegrees);
	filter.saturation_delta = parameter(kSaturationDelta);
	filter.gamma_min = parameter(kGammaMin);
	filter.gamma_max = parameter(kGammaMax);
	filter.grain_enabled = parameter(kGrainEnabled);
	filter.grain_opacity = parameter(kGrainOpacity);
	filter.time = parameter("time");
	filter.aspect = parameter("aspect");
}

void dynamic_visual_destroy(void *data)
{
	auto *filter = static_cast<DynamicVisualFilter *>(data);
	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}
	delete filter;
}

void dynamic_visual_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<DynamicVisualFilter *>(data);
	filter->warp = obs_data_get_bool(settings, kWarpEnabled);
	filter->warp_strength = float(obs_data_get_double(settings, kWarpIntensity));
	filter->warp_freq = float(obs_data_get_double(settings, kWarpFrequency));
	filter->breath = obs_data_get_bool(settings, kBreathEnabled);
	filter->breath_amount = float(obs_data_get_double(settings, kBreathAmplitude));
	filter->breath_cycle = float(obs_data_get_double(settings, kBreathCycle));
	filter->tilt_angle = float(obs_data_get_double(settings, kTiltAngle));
	filter->dynamic_color = obs_data_get_bool(settings, kColorEnabled);
	filter->color_cycle = float(obs_data_get_double(settings, kColorCycle));
	filter->hue = float(obs_data_get_double(settings, kHueDegrees));
	filter->saturation = float(obs_data_get_double(settings, kSaturationDelta));
	filter->gamma_low = float(obs_data_get_double(settings, kGammaMin));
	filter->gamma_high = float(obs_data_get_double(settings, kGammaMax));
	filter->grain = obs_data_get_bool(settings, kGrainEnabled);
	// The UI is expressed in percent; the shader consumes a 0..1 amount.
	filter->grain_amount = float(obs_data_get_double(settings, kGrainOpacity) * 0.01);
}

void *dynamic_visual_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new DynamicVisualFilter;
	filter->source = source;

	char *effect_path = obs_module_file("dynamic_visual.effect");
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, nullptr);
	if (filter->effect)
		cache_effect_parameters(*filter);
	obs_leave_graphics();
	bfree(effect_path);

	if (!filter->effect) {
		dynamic_visual_destroy(filter);
		return nullptr;
	}

	dynamic_visual_update(filter, settings);
	return filter;
}

void dynamic_visual_tick(void *data, float seconds)
{
	auto *filter = static_cast<DynamicVisualFilter *>(data);
	filter->elapsed += seconds;
	// Keep the value small enough to preserve sine-wave precision on long streams.
	if (filter->elapsed > 3600.0f)
		filter->elapsed = std::fmod(filter->elapsed, 3600.0f);
}

void dynamic_visual_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<DynamicVisualFilter *>(data);
	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	obs_source_t *target = obs_filter_get_target(filter->source);
	const uint32_t width = target ? obs_source_get_width(target) : 1U;
	const uint32_t height = target ? obs_source_get_height(target) : 1U;
	const float aspect = height ? float(width) / float(height) : 1.0f;

	gs_effect_set_float(filter->warp_enabled, filter->warp ? 1.0f : 0.0f);
	gs_effect_set_float(filter->warp_intensity, filter->warp_strength);
	gs_effect_set_float(filter->warp_frequency, filter->warp_freq);
	gs_effect_set_float(filter->breath_enabled, filter->breath ? 1.0f : 0.0f);
	gs_effect_set_float(filter->breath_amplitude, filter->breath_amount);
	gs_effect_set_float(filter->breath_cycle_seconds, filter->breath_cycle);
	gs_effect_set_float(filter->tilt_angle_degrees, filter->tilt_angle);
	gs_effect_set_float(filter->color_enabled, filter->dynamic_color ? 1.0f : 0.0f);
	gs_effect_set_float(filter->color_cycle_seconds, filter->color_cycle);
	gs_effect_set_float(filter->hue_degrees, filter->hue);
	gs_effect_set_float(filter->saturation_delta, filter->saturation);
	gs_effect_set_float(filter->gamma_min, filter->gamma_low);
	gs_effect_set_float(filter->gamma_max, filter->gamma_high);
	gs_effect_set_float(filter->grain_enabled, filter->grain ? 1.0f : 0.0f);
	gs_effect_set_float(filter->grain_opacity, filter->grain_amount);
	gs_effect_set_float(filter->time, filter->elapsed);
	gs_effect_set_float(filter->aspect, aspect);

	obs_source_process_filter_end(filter->source, filter->effect, 0, 0);
}

obs_properties_t *dynamic_visual_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_bool(props, kWarpEnabled, obs_module_text("Visual.Warp.Enabled"));
	obs_properties_add_float_slider(props, kWarpIntensity, obs_module_text("Visual.Warp.Intensity"), 0.0, 0.05, 0.0005);
	obs_properties_add_float_slider(props, kWarpFrequency, obs_module_text("Visual.Warp.Frequency"), 0.1, 5.0, 0.05);

	obs_properties_add_bool(props, kBreathEnabled, obs_module_text("Visual.Breath.Enabled"));
	obs_properties_add_float_slider(props, kBreathAmplitude, obs_module_text("Visual.Breath.Amplitude"), 0.0, 0.03, 0.0005);
	auto *breath_cycle = obs_properties_add_float_slider(props, kBreathCycle, obs_module_text("Visual.Breath.Cycle"), 1.0, 30.0, 0.1);
	obs_property_float_set_suffix(breath_cycle, " s");

	auto *tilt_angle = obs_properties_add_float_slider(props, kTiltAngle, obs_module_text("Visual.Tilt.MaxAngle"), 0.0, 0.5, 0.01);
	obs_property_float_set_suffix(tilt_angle, "°");

	obs_properties_add_bool(props, kColorEnabled, obs_module_text("Visual.Color.Enabled"));
	auto *color_cycle = obs_properties_add_float_slider(props, kColorCycle, obs_module_text("Visual.Color.Cycle"), 3.0, 15.0, 0.1);
	obs_property_float_set_suffix(color_cycle, " s");
	auto *hue = obs_properties_add_float_slider(props, kHueDegrees, obs_module_text("Visual.Color.Hue"), -2.0, 2.0, 0.05);
	obs_property_float_set_suffix(hue, "°");
	obs_properties_add_float_slider(props, kSaturationDelta, obs_module_text("Visual.Color.Saturation"), -0.20, 0.20, 0.005);
	obs_properties_add_float_slider(props, kGammaMin, obs_module_text("Visual.Color.GammaMin"), 0.90, 1.10, 0.005);
	obs_properties_add_float_slider(props, kGammaMax, obs_module_text("Visual.Color.GammaMax"), 0.90, 1.10, 0.005);

	obs_properties_add_bool(props, kGrainEnabled, obs_module_text("Visual.Grain.Enabled"));
	auto *grain = obs_properties_add_float_slider(props, kGrainOpacity, obs_module_text("Visual.Grain.Opacity"), 0.0, 3.0, 0.05);
	obs_property_float_set_suffix(grain, " %");
	return props;
}

void dynamic_visual_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, kWarpEnabled, false);
	obs_data_set_default_double(settings, kWarpIntensity, 0.01);
	obs_data_set_default_double(settings, kWarpFrequency, 1.0);
	obs_data_set_default_bool(settings, kBreathEnabled, false);
	obs_data_set_default_double(settings, kBreathAmplitude, 0.008);
	obs_data_set_default_double(settings, kBreathCycle, 8.0);
	obs_data_set_default_double(settings, kTiltAngle, 0.15);
	obs_data_set_default_bool(settings, kColorEnabled, false);
	obs_data_set_default_double(settings, kColorCycle, 8.0);
	obs_data_set_default_double(settings, kHueDegrees, 0.5);
	obs_data_set_default_double(settings, kSaturationDelta, 0.03);
	obs_data_set_default_double(settings, kGammaMin, 0.98);
	obs_data_set_default_double(settings, kGammaMax, 1.02);
	obs_data_set_default_bool(settings, kGrainEnabled, false);
	obs_data_set_default_double(settings, kGrainOpacity, 1.0);
}

const char *dynamic_visual_name(void *)
{
	return obs_module_text("DynamicVisualFilter");
}

} // namespace

obs_source_info dynamic_visual_filter = {
	.id = "obs_live_dynamic_visual_fx",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = dynamic_visual_name,
	.create = dynamic_visual_create,
	.destroy = dynamic_visual_destroy,
	.update = dynamic_visual_update,
	.video_tick = dynamic_visual_tick,
	.video_render = dynamic_visual_render,
	.get_properties = dynamic_visual_properties,
	.get_defaults = dynamic_visual_defaults,
};
