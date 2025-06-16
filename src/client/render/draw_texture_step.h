// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 Engine

#pragma once

#include "pipeline.h"
#include <IVideoDriver.h>
#include <IVideoDriver.h>
#include <SMaterial.h>

class DrawTextureStep : public RenderStep
{
public:
	virtual void setRenderSource(RenderSource *source) override { m_source = source; }
	virtual void setRenderTarget(RenderTarget *target) override { m_target = target; }

	virtual void reset(PipelineContext &context) override {}
	virtual void run(PipelineContext &context) override;

private:
	RenderSource *m_source {nullptr};
	RenderTarget *m_target {nullptr};
	irr::video::SMaterial m_material;
};
