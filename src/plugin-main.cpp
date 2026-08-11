/*
 * OBS Live Dynamic Visual & Audio FX
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <obs-module.h>

#include "filters.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-live-dynamic-fx", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "GPU visual effects and low-latency audio effects for OBS Studio.";
}

bool obs_module_load(void)
{
	obs_register_source(&dynamic_visual_filter);
	obs_register_source(&dynamic_audio_filter);
	blog(LOG_INFO, "[obs-live-dynamic-fx] loaded");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[obs-live-dynamic-fx] unloaded");
}
