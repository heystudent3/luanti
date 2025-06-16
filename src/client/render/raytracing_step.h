// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 Engine

#pragma once

#include "pipeline.h"
#include <IVideoDriver.h>

class RaytracingStep : public RenderStep
{
public:
	virtual void setRenderSource(RenderSource *source) override {}
	virtual void setRenderTarget(RenderTarget *target) override { m_target = target; }

	virtual void reset(PipelineContext &context) override {}
	virtual void run(PipelineContext &context) override;

private:
	RenderTarget *m_target {nullptr};
};
