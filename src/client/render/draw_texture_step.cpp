// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 Engine

#include "draw_texture_step.h"
#include <IVideoDriver.h>

void DrawTextureStep::run(PipelineContext &context)
{
	if (!m_source || !m_target)
		return;

	video::IVideoDriver *driver = context.device->getVideoDriver();
	m_target->activate(context);

	video::ITexture *texture = m_source->getTexture(0);
	if (!texture)
		return;

	// Set up material for drawing the texture
	m_material.setFlag(video::EMF_LIGHTING, false);
	m_material.setFlag(video::EMF_ZWRITE_ENABLE, false);
	m_material.setFlag(video::EMF_ANISOTROPIC_FILTER, false);
	m_material.setFlag(video::EMF_BILINEAR_FILTER, false);
	m_material.setFlag(video::EMF_TRILINEAR_FILTER, false);
	m_material.setTexture(0, texture);
	m_material.MaterialType = video::EMT_SOLID;

	driver->setMaterial(m_material);

	// Draw a fullscreen quad
	core::dimension2du screen_size = driver->getCurrentRenderTargetSize();
	driver->draw2DImage(texture, core::rect<s32>(0, 0, screen_size.Width, screen_size.Height), core::rect<s32>(0, 0, texture->getOriginalSize().Width, texture->getOriginalSize().Height));
}
