// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 Engine

#include "raytracing_step.h"
#include <IVideoDriver.h>

void RaytracingStep::run(PipelineContext &context)
{
	if (!m_target)
		return;

	video::IVideoDriver *driver = context.device->getVideoDriver();
	m_target->activate(context);

	// Clear the screen to a distinct green color for testing
	driver->beginScene(true, true, video::SColor(255, 0, 255, 0)); // ARGB: Opaque Green
	driver->endScene();
}
