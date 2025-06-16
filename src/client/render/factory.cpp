// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 numzero, Lobachevskiy Vitaliy <numzer0@yandex.ru>

#include "factory.h"
#include "log.h"
#include "plain.h"
#include "anaglyph.h"
#include "interlaced.h"
#include "sidebyside.h"
#include "secondstage.h"
#include "client/shadows/dynamicshadowsrender.h"
#include "shader.h" // Added for IShaderSource
#include "raytracing_step.h"
#include "draw_texture_step.h"

// Forward declaration for the new cartoon pipeline
void populateCartoonPipeline(RenderPipeline *pipeline, Client *client);

struct CreatePipelineResult
{
    v2f virtual_size_scale;
    ShadowRenderer *shadow_renderer { nullptr };
    RenderPipeline *pipeline { nullptr };
};

void createPipeline(const std::string &stereo_mode, IrrlichtDevice *device, Client *client, Hud *hud, CreatePipelineResult &result);

RenderingCore *createRenderingCore(const std::string &stereo_mode, IrrlichtDevice *device,
        Client *client, Hud *hud)
{
    CreatePipelineResult created_pipeline;
    createPipeline(stereo_mode, device, client, hud, created_pipeline);
    return new RenderingCore(device, client, hud,
            created_pipeline.shadow_renderer, created_pipeline.pipeline, created_pipeline.virtual_size_scale);
}

void createPipeline(const std::string &stereo_mode, IrrlichtDevice *device, Client *client, Hud *hud, CreatePipelineResult &result)
{
    result.shadow_renderer = createShadowRenderer(device, client);
    result.virtual_size_scale = v2f(1.0f);
    result.pipeline = new RenderPipeline();

    if (result.shadow_renderer)
        result.pipeline->addStep<RenderShadowMapStep>();

    if (stereo_mode == "none") {
        // Create TextureBuffer for raytraced output
        TextureBuffer *raytracing_output_buffer = result.pipeline->createOwned<TextureBuffer>();
        raytracing_output_buffer->setTexture(0, v2f(1.0f, 1.0f), "raytraced_output", video::ECF_A8R8G8B8, true);

        // Create TextureBufferOutput as a render target for RaytracingStep
        TextureBufferOutput *raytracing_output_target = result.pipeline->createOwned<TextureBufferOutput>(raytracing_output_buffer, 0);

        // Add RaytracingStep to the pipeline
        RaytracingStep *raytracing_step = result.pipeline->addStep<RaytracingStep>();
        raytracing_step->setRenderTarget(raytracing_output_target);

        // Add DrawTextureStep to draw the raytraced output to the screen
        DrawTextureStep *draw_texture_step = result.pipeline->addStep<DrawTextureStep>();
        draw_texture_step->setRenderSource(raytracing_output_buffer);
        draw_texture_step->setRenderTarget(result.pipeline->createOwned<ScreenTarget>());

        // Temporarily disable original 3D rendering for testing raytracing
        // populatePlainPipeline(result.pipeline, client);
        return;
    }
    if (stereo_mode == "anaglyph") {
        populateAnaglyphPipeline(result.pipeline, client);
        return;
    }
    if (stereo_mode == "interlaced") {
        populateInterlacedPipeline(result.pipeline, client);
        return;
    }
    if (stereo_mode == "sidebyside") {
        populateSideBySidePipeline(result.pipeline, client, false, false, result.virtual_size_scale);
        return;
    }
    if (stereo_mode == "topbottom") {
        populateSideBySidePipeline(result.pipeline, client, true, false, result.virtual_size_scale);
        return;
    }
    if (stereo_mode == "crossview") {
        populateSideBySidePipeline(result.pipeline, client, false, true, result.virtual_size_scale);
        return;
    }
    if (stereo_mode == "cartoon") {
        populateCartoonPipeline(result.pipeline, client);
        return;
    }

    // fallback to plain renderer
    errorstream << "Invalid rendering mode: " << stereo_mode << std::endl;
    populatePlainPipeline(result.pipeline, client);
}

void populateCartoonPipeline(RenderPipeline *pipeline, Client *client)
{
    IShaderSource *shsrc = client->getShaderSource();
    u32 cartoon_shader_id = shsrc->getShader("cartoon", ShaderConstants(), video::EMT_SOLID);

    pipeline->addStep<Draw3D>(cartoon_shader_id, client->getEnv()->getMap());
    pipeline->addStep<DrawWield>();
    pipeline->addStep<DrawHUD>();
}
