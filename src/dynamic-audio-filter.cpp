/*
 * Low-latency audio effects.  The callback is allocation-free and operates on
 * OBS' planar float buffers in place.
 */

#include <obs-module.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "filters.hpp"

namespace {

constexpr char kPitchEnabled[] = "pitch_enabled";
constexpr char kPitchCents[] = "pitch_cents";
constexpr char kEqEnabled[] = "eq_enabled";
constexpr char kEqDb[] = "eq_db";
constexpr char kAmbientEnabled[] = "ambient_enabled";
constexpr char kAmbientDb[] = "ambient_db";

constexpr size_t kMaxChannels = 8;
constexpr size_t kPitchBufferSize = 4096;
constexpr size_t kPitchBufferMask = kPitchBufferSize - 1;
constexpr float kPitchMinDelay = 96.0f;
constexpr float kPitchGrainSize = 1024.0f;
constexpr float kPi = 3.14159265358979323846f;

static_assert((kPitchBufferSize & kPitchBufferMask) == 0, "Pitch buffer must be a power of two");

struct ChannelState {
	std::array<float, kPitchBufferSize> pitch_buffer{};
	float low_state = 0.0f;
	float high_low_state = 0.0f;
	float pink_b0 = 0.0f;
	float pink_b1 = 0.0f;
	float pink_b2 = 0.0f;
	float pink_b3 = 0.0f;
	float pink_b4 = 0.0f;
	float pink_b5 = 0.0f;
	float pink_b6 = 0.0f;
	uint32_t noise_state = 0x1234567u;
};

struct DynamicAudioFilter {
	obs_source_t *source = nullptr;
	size_t channels = 2;
	uint32_t sample_rate = 48000;
	bool pitch_enabled = false;
	float pitch_cents = 12.0f;
	bool eq_enabled = false;
	float eq_db = 1.0f;
	bool ambient_enabled = false;
	float ambient_db = -65.0f;
	float pitch_phase = 0.0f;
	size_t write_index = 0;
	size_t pitch_samples = 0;
	std::array<ChannelState, kMaxChannels> channel{};
};

float db_to_gain(float db)
{
	return std::pow(10.0f, db / 20.0f);
}

float wrap01(float value)
{
	return value - std::floor(value);
}

float read_pitch_sample(const ChannelState &state, size_t write_index, float delay)
{
	float read_position = float(write_index) - delay;
	while (read_position < 0.0f)
		read_position += float(kPitchBufferSize);
	while (read_position >= float(kPitchBufferSize))
		read_position -= float(kPitchBufferSize);

	const size_t index0 = size_t(read_position) & kPitchBufferMask;
	const size_t index1 = (index0 + 1) & kPitchBufferMask;
	const float fraction = read_position - std::floor(read_position);
	return state.pitch_buffer[index0] + (state.pitch_buffer[index1] - state.pitch_buffer[index0]) * fraction;
}

float granular_pitch_shift(DynamicAudioFilter &filter, ChannelState &state, float input)
{
	state.pitch_buffer[filter.write_index] = input;
	if (filter.pitch_samples < kPitchBufferSize)
		return input;

	const float ratio = std::exp2(filter.pitch_cents / 1200.0f);
	const float difference = ratio - 1.0f;
	if (std::fabs(difference) < 0.00001f)
		return input;

	auto grain_sample = [&filter, &state, difference](float phase) {
		const float delay = difference > 0.0f ? kPitchMinDelay + kPitchGrainSize * (1.0f - phase)
												 : kPitchMinDelay + kPitchGrainSize * phase;
		return read_pitch_sample(state, filter.write_index, delay);
	};

	const float phase_a = filter.pitch_phase;
	const float phase_b = wrap01(phase_a + 0.5f);
	const float gain_a = std::sin(kPi * phase_a);
	const float gain_b = std::sin(kPi * phase_b);
	return (grain_sample(phase_a) * gain_a + grain_sample(phase_b) * gain_b) / (gain_a + gain_b);
}

float process_extreme_eq(DynamicAudioFilter &filter, ChannelState &state, float input)
{
	const float sample_rate = float(std::max(filter.sample_rate, 1U));
	const float low_cutoff = std::min(60.0f, sample_rate * 0.45f);
	const float high_cutoff = std::min(15000.0f, sample_rate * 0.45f);
	const float low_alpha = 1.0f - std::exp(-2.0f * kPi * low_cutoff / sample_rate);
	const float high_alpha = 1.0f - std::exp(-2.0f * kPi * high_cutoff / sample_rate);
	state.low_state += low_alpha * (input - state.low_state);
	state.high_low_state += high_alpha * (input - state.high_low_state);
	const float high_band = input - state.high_low_state;
	const float gain_delta = db_to_gain(filter.eq_db) - 1.0f;
	return input + gain_delta * (state.low_state + high_band);
}

float next_pink_noise(ChannelState &state)
{
	// Paul Kellet's inexpensive pink-noise approximation, scaled to +/-1.
	state.noise_state ^= state.noise_state << 13;
	state.noise_state ^= state.noise_state >> 17;
	state.noise_state ^= state.noise_state << 5;
	const float white = (float(state.noise_state) / 4294967295.0f) * 2.0f - 1.0f;
	state.pink_b0 = 0.99886f * state.pink_b0 + white * 0.0555179f;
	state.pink_b1 = 0.99332f * state.pink_b1 + white * 0.0750759f;
	state.pink_b2 = 0.96900f * state.pink_b2 + white * 0.1538520f;
	state.pink_b3 = 0.86650f * state.pink_b3 + white * 0.3104856f;
	state.pink_b4 = 0.55000f * state.pink_b4 + white * 0.5329522f;
	state.pink_b5 = -0.7616f * state.pink_b5 - white * 0.0168980f;
	const float pink = state.pink_b0 + state.pink_b1 + state.pink_b2 + state.pink_b3 + state.pink_b4 + state.pink_b5 + state.pink_b6 + white * 0.5362f;
	state.pink_b6 = white * 0.115926f;
	return pink * 0.11f;
}

void dynamic_audio_destroy(void *data)
{
	delete static_cast<DynamicAudioFilter *>(data);
}

void dynamic_audio_update(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<DynamicAudioFilter *>(data);
	filter->channels = std::min(audio_output_get_channels(obs_get_audio()), kMaxChannels);
	filter->sample_rate = std::max(audio_output_get_sample_rate(obs_get_audio()), 1U);
	filter->pitch_enabled = obs_data_get_bool(settings, kPitchEnabled);
	filter->pitch_cents = float(obs_data_get_double(settings, kPitchCents));
	filter->eq_enabled = obs_data_get_bool(settings, kEqEnabled);
	filter->eq_db = float(obs_data_get_double(settings, kEqDb));
	filter->ambient_enabled = obs_data_get_bool(settings, kAmbientEnabled);
	filter->ambient_db = float(obs_data_get_double(settings, kAmbientDb));
}

void *dynamic_audio_create(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new DynamicAudioFilter;
	filter->source = source;
	for (size_t channel = 0; channel < kMaxChannels; ++channel)
		filter->channel[channel].noise_state += uint32_t(channel * 0x9e3779b9u);
	dynamic_audio_update(filter, settings);
	return filter;
}

obs_audio_data *dynamic_audio_filter_audio(void *data, obs_audio_data *audio)
{
	auto *filter = static_cast<DynamicAudioFilter *>(data);
	auto **planes = reinterpret_cast<float **>(audio->data);
	const size_t channels = filter->channels;
	const float ambient_gain = filter->ambient_enabled ? db_to_gain(filter->ambient_db) : 0.0f;

	for (uint32_t frame = 0; frame < audio->frames; ++frame) {
		for (size_t channel = 0; channel < channels; ++channel) {
			if (!planes[channel])
				continue;

			auto &state = filter->channel[channel];
			float sample = planes[channel][frame];
			if (filter->pitch_enabled)
				sample = granular_pitch_shift(*filter, state, sample);
			else
				state.pitch_buffer[filter->write_index] = sample;
			if (filter->eq_enabled)
				sample = process_extreme_eq(*filter, state, sample);
			if (filter->ambient_enabled)
				sample += next_pink_noise(state) * ambient_gain;
			planes[channel][frame] = sample;
		}

		filter->write_index = (filter->write_index + 1) & kPitchBufferMask;
		filter->pitch_samples = std::min(filter->pitch_samples + 1, kPitchBufferSize);
		const float ratio = std::exp2(filter->pitch_cents / 1200.0f);
		filter->pitch_phase = wrap01(filter->pitch_phase + std::fabs(ratio - 1.0f) / kPitchGrainSize);
	}

	return audio;
}

obs_properties_t *dynamic_audio_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_properties_add_bool(props, kPitchEnabled, obs_module_text("Audio.Pitch.Enabled"));
	auto *pitch = obs_properties_add_float_slider(props, kPitchCents, obs_module_text("Audio.Pitch.Cents"), -50.0, 50.0, 1.0);
	obs_property_float_set_suffix(pitch, " cents");
	obs_properties_add_bool(props, kEqEnabled, obs_module_text("Audio.EQ.Enabled"));
	auto *eq = obs_properties_add_float_slider(props, kEqDb, obs_module_text("Audio.EQ.Gain"), -3.0, 3.0, 0.1);
	obs_property_float_set_suffix(eq, " dB");
	obs_properties_add_bool(props, kAmbientEnabled, obs_module_text("Audio.Ambient.Enabled"));
	auto *ambient = obs_properties_add_float_slider(props, kAmbientDb, obs_module_text("Audio.Ambient.Level"), -80.0, -50.0, 0.5);
	obs_property_float_set_suffix(ambient, " dB");
	return props;
}

void dynamic_audio_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, kPitchEnabled, false);
	obs_data_set_default_double(settings, kPitchCents, 12.0);
	obs_data_set_default_bool(settings, kEqEnabled, false);
	obs_data_set_default_double(settings, kEqDb, 1.0);
	obs_data_set_default_bool(settings, kAmbientEnabled, false);
	obs_data_set_default_double(settings, kAmbientDb, -65.0);
}

const char *dynamic_audio_name(void *)
{
	return obs_module_text("DynamicAudioFilter");
}

} // namespace

obs_source_info dynamic_audio_filter = {
	.id = "obs_live_dynamic_audio_fx",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = dynamic_audio_name,
	.create = dynamic_audio_create,
	.destroy = dynamic_audio_destroy,
	.update = dynamic_audio_update,
	.filter_audio = dynamic_audio_filter_audio,
	.get_properties = dynamic_audio_properties,
	.get_defaults = dynamic_audio_defaults,
};
