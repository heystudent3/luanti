// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2010-2013 blue42u, Jonathon Anderson <anderjon@umail.iu.edu>
// Copyright (C) 2010-2013 kwolekr, Ryan Kwolek <kwolekr@minetest.net>

#include "client/hud.h"
#include <string>
#include <iostream>
#include <cmath>
#include "settings.h"
#include "util/numeric.h"
#include "log.h"
#include "client.h"
#include "inventory.h"
#include "shader.h"
#include "client/tile.h"
#include "localplayer.h"
#include "camera.h"
#include "fontengine.h"
#include "guiscalingfilter.h"
#include "mesh.h"
#include "client/renderingengine.h"
#include "client/minimap.h"
#include "client/texturesource.h"
#include "gui/touchcontrols.h"
#include "util/enriched_string.h"
#include "irrlicht_changes/CGUITTFont.h"
#include "gui/drawItemStack.h"

#define OBJECT_CROSSHAIR_LINE_SIZE 8
#define CROSSHAIR_LINE_SIZE 10

static void setting_changed_callback(const std::string &name, void *data)
{
	static_cast<Hud*>(data)->readScalingSetting();
}

Hud::Hud(Client *client, LocalPlayer *player,
		Inventory *inventory)
{
	driver            = RenderingEngine::get_video_driver();
	this->client      = client;
	this->player      = player;
	this->inventory   = inventory;

	readScalingSetting();
	g_settings->registerChangedCallback("dpi_change_notifier", setting_changed_callback, this);
	g_settings->registerChangedCallback("display_density_factor", setting_changed_callback, this);
	g_settings->registerChangedCallback("hud_scaling", setting_changed_callback, this);

	for (auto &hbar_color : hbar_colors)
		hbar_color = video::SColor(255, 255, 255, 255);

	tsrc = client->getTextureSource();

	v3f crosshair_color = g_settings->getV3F("crosshair_color").value_or(v3f());
	u32 cross_r = rangelim(myround(crosshair_color.X), 0, 255);
	u32 cross_g = rangelim(myround(crosshair_color.Y), 0, 255);
	u32 cross_b = rangelim(myround(crosshair_color.Z), 0, 255);
	u32 cross_a = rangelim(g_settings->getS32("crosshair_alpha"), 0, 255);
	crosshair_argb = video::SColor(cross_a, cross_r, cross_g, cross_b);

	v3f selectionbox_color = g_settings->getV3F("selectionbox_color").value_or(v3f());
	u32 sbox_r = rangelim(myround(selectionbox_color.X), 0, 255);
	u32 sbox_g = rangelim(myround(selectionbox_color.Y), 0, 255);
	u32 sbox_b = rangelim(myround(selectionbox_color.Z), 0, 255);
	selectionbox_argb = video::SColor(255, sbox_r, sbox_g, sbox_b);

	use_crosshair_image = tsrc->isKnownSourceImage("crosshair.png");
	use_object_crosshair_image = tsrc->isKnownSourceImage("object_crosshair.png");

	m_selection_boxes.clear();
	m_halo_boxes.clear();

	std::string mode_setting = g_settings->get("node_highlighting");

	if (mode_setting == "halo") {
		m_mode = HIGHLIGHT_HALO;
	} else if (mode_setting == "none") {
		m_mode = HIGHLIGHT_NONE;
	} else {
		m_mode = HIGHLIGHT_BOX;
	}

	// Initialize m_selection_material
	IShaderSource *shdrsrc = client->getShaderSource();
	if (m_mode == HIGHLIGHT_HALO) {
		auto shader_id = shdrsrc->getShaderRaw("selection_shader", true);
		m_selection_material.MaterialType = shdrsrc->getShaderInfo(shader_id).material;
	} else {
		m_selection_material.MaterialType = video::EMT_SOLID;
	}

	if (m_mode == HIGHLIGHT_BOX) {
		m_selection_material.Thickness =
			rangelim(g_settings->getS16("selectionbox_width"), 1, 5);
	} else if (m_mode == HIGHLIGHT_HALO) {
		m_selection_material.setTexture(0, tsrc->getTextureForMesh("halo.png"));
		m_selection_material.BackfaceCulling = true;
	} else {
		m_selection_material.MaterialType = video::EMT_SOLID;
	}

	// Initialize m_block_bounds_material
	m_block_bounds_material.MaterialType = video::EMT_SOLID;
	m_block_bounds_material.Thickness =
		rangelim(g_settings->getS16("selectionbox_width"), 1, 5);

	// Prepare mesh for compass drawing
	m_rotation_mesh_buffer.reset(new scene::SMeshBuffer());
	auto *b = m_rotation_mesh_buffer.get();
	auto &vertices = b->Vertices->Data;
	auto &indices = b->Indices->Data;
	vertices.resize(4);
	indices.resize(6);

	video::SColor white(255, 255, 255, 255);
	v3f normal(0.f, 0.f, 1.f);

	vertices[0] = video::S3DVertex(v3f(-1.f, -1.f, 0.f), normal, white, v2f(0.f, 1.f));
	vertices[1] = video::S3DVertex(v3f(-1.f,  1.f, 0.f), normal, white, v2f(0.f, 0.f));
	vertices[2] = video::S3DVertex(v3f( 1.f,  1.f, 0.f), normal, white, v2f(1.f, 0.f));
	vertices[3] = video::S3DVertex(v3f( 1.f, -1.f, 0.f), normal, white, v2f(1.f, 1.f));

	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 2;
	indices[4] = 3;
	indices[5] = 0;

	b->getMaterial().MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	b->setHardwareMappingHint(scene::EHM_STATIC);
}

void Hud::readScalingSetting()
{
	m_hud_scaling      = g_settings->getFloat("hud_scaling", 0.5f, 20.0f);
	m_scale_factor     = m_hud_scaling * RenderingEngine::getDisplayDensity();
	m_hotbar_imagesize = std::floor(HOTBAR_IMAGE_SIZE *
		RenderingEngine::getDisplayDensity() + 0.5f);
	m_hotbar_imagesize *= m_hud_scaling;
	m_padding = m_hotbar_imagesize / 12;
}

Hud::~Hud()
{
	g_settings->deregisterAllChangedCallbacks(this);

	if (m_selection_mesh)
		m_selection_mesh->drop();
}

void Hud::drawItem(const ItemStack &item, const core::rect<s32>& rect,
		bool selected)
{
	if (selected) {
		/* draw highlighting around selected item */
		if (use_hotbar_selected_image) {
			core::rect<s32> imgrect2 = rect;
			imgrect2.UpperLeftCorner.X  -= (m_padding*2);
			imgrect2.UpperLeftCorner.Y  -= (m_padding*2);
			imgrect2.LowerRightCorner.X += (m_padding*2);
			imgrect2.LowerRightCorner.Y += (m_padding*2);
				video::ITexture *texture = tsrc->getTexture(hotbar_selected_image);
				core::dimension2di imgsize(texture->getOriginalSize());
				draw2DImageFilterScaled(driver, texture, imgrect2,
					core::rect<s32>(core::position2d<s32>(0,0), imgsize),
					NULL, hbar_colors, true);
		} else {
			video::SColor c_outside(255,255,0,0);
			//video::SColor c_outside(255,0,0,0);
			//video::SColor c_inside(255,192,192,192);
			s32 x1 = rect.UpperLeftCorner.X;
			s32 y1 = rect.UpperLeftCorner.Y;
			s32 x2 = rect.LowerRightCorner.X;
			s32 y2 = rect.LowerRightCorner.Y;
			// Black base borders
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y1 - m_padding),
				v2s32(x2 + m_padding, y1)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y2),
				v2s32(x2 + m_padding, y2 + m_padding)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y1),
				v2s32(x1, y2)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x2, y1),
				v2s32(x2 + m_padding, y2)
				), NULL);
			/*// Light inside borders
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
				v2s32(x1 - padding/2, y1 - padding/2),
				v2s32(x2 + padding/2, y1)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
				v2s32(x1 - padding/2, y2),
				v2s32(x2 + padding/2, y2 + padding/2)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
				v2s32(x1 - padding/2, y1),
				v2s32(x1, y2)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
				v2s32(x2, y1),
				v2s32(x2 + padding/2, y2)
				), NULL);
			*/
		}
	}

	video::SColor bgcolor2(128, 0, 0, 0);
	if (!use_hotbar_image)
		driver->draw2DRectangle(bgcolor2, rect, NULL);
	drawItemStack(driver, g_fontengine->getFont(), item, rect, NULL,
		client, selected ? IT_ROT_SELECTED : IT_ROT_NONE);
}

// NOTE: selectitem = 0 -> no selected; selectitem is 1-based
// mainlist can be NULL, but draw the frame anyway.
void Hud::drawItems(v2s32 screen_pos, v2s32 screen_offset, s32 itemcount, v2f alignment,
		s32 inv_offset, InventoryList *mainlist, u16 selectitem, u16 direction,
		bool is_hotbar)
{
	s32 height  = m_hotbar_imagesize + m_padding * 2;
	s32 width   = (itemcount - inv_offset) * (m_hotbar_imagesize + m_padding * 2);

	if (direction == HUD_DIR_TOP_BOTTOM || direction == HUD_DIR_BOTTOM_TOP) {
		s32 tmp = height;
		height = width;
		width = tmp;
	}

	// Position: screen_pos + screen_offset + alignment
	v2s32 pos(screen_offset.X * m_scale_factor, screen_offset.Y * m_scale_factor);
	pos += screen_pos;
	pos.X += (alignment.X - 1.0f) * (width * 0.5f);
	pos.Y += (alignment.Y - 1.0f) * (height * 0.5f);

	// Store hotbar_image in member variable, used by drawItem()
	if (hotbar_image != player->hotbar_image) {
		hotbar_image = player->hotbar_image;
		use_hotbar_image = !hotbar_image.empty();
	}

	// Store hotbar_selected_image in member variable, used by drawItem()
	if (hotbar_selected_image != player->hotbar_selected_image) {
		hotbar_selected_image = player->hotbar_selected_image;
		use_hotbar_selected_image = !hotbar_selected_image.empty();
	}

	// draw customized item background
	if (use_hotbar_image) {
		core::rect<s32> imgrect2(-m_padding/2, -m_padding/2,
			width+m_padding/2, height+m_padding/2);
		core::rect<s32> rect2 = imgrect2 + pos;
		video::ITexture *texture = tsrc->getTexture(hotbar_image);
		core::dimension2di imgsize(texture->getOriginalSize());
		draw2DImageFilterScaled(driver, texture, rect2,
			core::rect<s32>(core::position2d<s32>(0,0), imgsize),
			NULL, hbar_colors, true);
	}

	// Draw items
	core::rect<s32> imgrect(0, 0, m_hotbar_imagesize, m_hotbar_imagesize);
	const s32 list_max = std::min(itemcount, (s32) (mainlist ? mainlist->getSize() : 0 ));
	for (s32 i = inv_offset; i < list_max; i++) {
		s32 fullimglen = m_hotbar_imagesize + m_padding * 2;

		v2s32 steppos;
		switch (direction) {
		case HUD_DIR_RIGHT_LEFT:
			steppos = v2s32(m_padding + (list_max - 1 - i - inv_offset) * fullimglen, m_padding);
			break;
		case HUD_DIR_TOP_BOTTOM:
			steppos = v2s32(m_padding, m_padding + (i - inv_offset) * fullimglen);
			break;
		case HUD_DIR_BOTTOM_TOP:
			steppos = v2s32(m_padding, m_padding + (list_max - 1 - i - inv_offset) * fullimglen);
			break;
		default:
			steppos = v2s32(m_padding + (i - inv_offset) * fullimglen, m_padding);
			break;
		}

		core::rect<s32> item_rect = imgrect + pos + steppos;

		drawItem(mainlist->getItem(i), item_rect, (i + 1) == selectitem);

		if (is_hotbar && g_touchcontrols)
			g_touchcontrols->registerHotbarRect(i, item_rect);
	}
}

bool Hud::hasElementOfType(HudElementType type)
{
	for (size_t i = 0; i != player->maxHudId(); i++) {
		HudElement *e = player->getHud(i);
		if (!e)
			continue;
		if (e->type == type)
			return true;
	}
	return false;
}

// Calculates screen position of waypoint. Returns true if waypoint is visible (in front of the player), else false.
bool Hud::calculateScreenPos(const v3s16 &camera_offset, HudElement *e, v2s32 *pos)
{
	v3f w_pos = e->world_pos * BS;
	scene::ICameraSceneNode* camera =
		client->getSceneManager()->getActiveCamera();
	w_pos -= intToFloat(camera_offset, BS);
	core::matrix4 trans = camera->getProjectionMatrix();
	trans *= camera->getViewMatrix();
	f32 transformed_pos[4] = { w_pos.X, w_pos.Y, w_pos.Z, 1.0f };
	trans.multiplyWith1x4Matrix(transformed_pos);
	if (transformed_pos[3] < 0)
		return false;
	f32 zDiv = transformed_pos[3] == 0.0f ? 1.0f :
		core::reciprocal(transformed_pos[3]);
	pos->X = m_screensize.X * (0.5 * transformed_pos[0] * zDiv + 0.5);
	pos->Y = m_screensize.Y * (0.5 - transformed_pos[1] * zDiv * 0.5);
	return true;
}

void Hud::drawLuaElements(const v3s16 &camera_offset)
{
	const u32 text_height = g_fontengine->getTextHeight();
	gui::IGUIFont *const font = g_fontengine->getFont();

	// Reorder elements by z_index
	std::vector<HudElement*> elems;
	elems.reserve(player->maxHudId());

	// Add builtin elements if the server doesn't send them.
	// Declared here such that they have the same lifetime as the elems vector
	HudElement minimap;
	HudElement hotbar;
	if (client->getProtoVersion() < 44 && (player->hud_flags & HUD_FLAG_MINIMAP_VISIBLE)) {
		minimap = {HUD_ELEM_MINIMAP, v2f(1, 0), "", v2f(), "", 0 , 0, 0, v2f(-1, 1),
				v2f(-10, 10), v3f(), v2s32(256, 256), 0, "", 0};
		elems.push_back(&minimap);
	}
	if (client->getProtoVersion() < 46 && player->hud_flags & HUD_FLAG_HOTBAR_VISIBLE) {
		hotbar = {HUD_ELEM_HOTBAR, v2f(0.5, 1), "", v2f(), "", 0 , 0, 0, v2f(0, -1),
				v2f(0, -4), v3f(), v2s32(), 0, "", 0};
		elems.push_back(&hotbar);
	}

	for (size_t i = 0; i != player->maxHudId(); i++) {
		HudElement *e = player->getHud(i);
		if (!e)
			continue;

		auto it = elems.begin();
		while (it != elems.end() && (*it)->z_index <= e->z_index)
			++it;

		elems.insert(it, e);
	}

	for (HudElement *e : elems) {

		v2s32 pos(floor(e->pos.X * (float) m_screensize.X + 0.5),
				floor(e->pos.Y * (float) m_screensize.Y + 0.5));
		switch (e->type) {
			case HUD_ELEM_TEXT: {
				unsigned int font_size = g_fontengine->getDefaultFontSize();

				if (e->size.X > 0)
					font_size *= e->size.X;

#ifdef __ANDROID__
				// The text size on Android is not proportional with the actual scaling
				// FIXME: why do we have such a weird unportable hack??
				if (font_size > 3 && e->offset.X < -20)
					font_size -= 3;
#endif
				auto textfont = g_fontengine->getFont(FontSpec(font_size,
					(e->style & HUD_STYLE_MONO) ? FM_Mono : FM_Unspecified,
					e->style & HUD_STYLE_BOLD, e->style & HUD_STYLE_ITALIC));

				irr::gui::CGUITTFont *ttfont = nullptr;
				if (textfont->getType() == irr::gui::EGFT_CUSTOM)
					ttfont = static_cast<irr::gui::CGUITTFont *>(textfont);

				video::SColor color(255, (e->number >> 16) & 0xFF,
											 (e->number >> 8)  & 0xFF,
											 (e->number >> 0)  & 0xFF);
				EnrichedString text(unescape_string(utf8_to_wide(e->text)), color);
				core::dimension2d<u32> textsize = textfont->getDimension(text.c_str());

				v2s32 offset(0, (e->align.Y - 1.0) * (textsize.Height / 2));
				core::rect<s32> size(0, 0, e->scale.X * m_scale_factor,
						text_height * e->scale.Y * m_scale_factor);
				v2s32 offs(e->offset.X * m_scale_factor,
						e->offset.Y * m_scale_factor);

				// Draw each line
				// See also: GUIFormSpecMenu::parseLabel
				size_t str_pos = 0;
				while (str_pos < text.size()) {
					EnrichedString line = text.getNextLine(&str_pos);

					core::dimension2d<u32> linesize = textfont->getDimension(line.c_str());
					v2s32 line_offset((e->align.X - 1.0) * (linesize.Width / 2), 0);
					if (ttfont)
						ttfont->draw(line, size + pos + offset + offs + line_offset);
					else
						textfont->draw(line.c_str(), size + pos + offset + offs + line_offset, color);
					offset.Y += linesize.Height;
				}
				break; }
			case HUD_ELEM_STATBAR: {
				v2s32 offs(e->offset.X, e->offset.Y);
				drawStatbar(pos, HUD_CORNER_UPPER, e->dir, e->text, e->text2,
					e->number, e->item, offs, e->size);
				break; }
			case HUD_ELEM_INVENTORY: {
				InventoryList *inv = inventory->getList(e->text);
				if (!inv)
					warningstream << "HUD: Unknown inventory list. name=" << e->text << std::endl;
				drawItems(pos, v2s32(e->offset.X, e->offset.Y), e->number, e->align, 0,
					inv, e->item, e->dir, false);
				break; }
			case HUD_ELEM_WAYPOINT: {
				if (!calculateScreenPos(camera_offset, e, &pos))
					break;

				pos += v2s32(e->offset.X, e->offset.Y);
				video::SColor color(255, (e->number >> 16) & 0xFF,
											 (e->number >> 8)  & 0xFF,
											 (e->number >> 0)  & 0xFF);
				std::wstring text = unescape_translate(utf8_to_wide(e->name));
				const std::string &unit = e->text;
				// Waypoints reuse the item field to store precision,
				// item = precision + 1 and item = 0 <=> precision = 10 for backwards compatibility.
				// Also see `push_hud_element`.
				u32 item = e->item;
				float precision = (item == 0) ? 10.0f : (item - 1.f);
				bool draw_precision = precision > 0;

				core::rect<s32> bounds(0, 0, font->getDimension(text.c_str()).Width, (draw_precision ? 2:1) * text_height);
				pos.Y += (e->align.Y - 1.0) * bounds.getHeight() / 2;
				bounds += pos;
				font->draw(text.c_str(), bounds + v2s32((e->align.X - 1.0) * bounds.getWidth() / 2, 0), color);
				if (draw_precision) {
					std::ostringstream os;
					v3f p_pos = player->getPosition() / BS;
					float distance = std::floor(precision * p_pos.getDistanceFrom(e->world_pos)) / precision;
					os << distance << unit;
					text = unescape_translate(utf8_to_wide(os.str()));
					bounds.LowerRightCorner.X = bounds.UpperLeftCorner.X + font->getDimension(text.c_str()).Width;
					font->draw(text.c_str(), bounds + v2s32((e->align.X - 1.0f) * bounds.getWidth() / 2, text_height), color);
				}
				break; }
			case HUD_ELEM_IMAGE_WAYPOINT: {
				if (!calculateScreenPos(camera_offset, e, &pos))
					break;
				[[fallthrough]];
			}
			case HUD_ELEM_IMAGE: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;

				const video::SColor color(255, 255, 255, 255);
				const video::SColor colors[] = {color, color, color, color};
				core::dimension2di imgsize(texture->getOriginalSize());
				v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
				              imgsize.Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);
				draw2DImageFilterScaled(driver, texture, rect,
					core::rect<s32>(core::position2d<s32>(0,0), imgsize),
					NULL, colors, true);
				break; }
			case HUD_ELEM_COMPASS: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;

				core::rect<s32> rect_src(texture->getOriginalSize());
				drawCompassRotate(e, texture, core::rect<s32>(0, 0,
					texture->getOriginalSize().Width, texture->getOriginalSize().Height), 0);
				break; }
			case HUD_ELEM_WIDGET: {
				// Draw text (name) on top of image
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture) {
					warningstream << "HUD: Unknown widget texture: " << e->text << std::endl;
					continue;
				}

				v2s32 dstsize(texture->getOriginalSize().Width * e->scale.X * m_scale_factor,
				              texture->getOriginalSize().Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);

				const video::SColor color(255, 255, 255, 255);
				const video::SColor colors[] = {color, color, color, color};
				draw2DImageFilterScaled(driver, texture, rect,
					core::rect<s32>(core::position2d<s32>(0,0), texture->getOriginalSize()),
					NULL, colors, true);

				if (!e->name.empty()) {
					video::SColor text_color(255, (e->number >> 16) & 0xFF,
											 (e->number >> 8)  & 0xFF,
											 (e->number >> 0)  & 0xFF);
					EnrichedString text_widget(unescape_string(utf8_to_wide(e->name)), text_color);
					core::dimension2d<u32> textsize = font->getDimension(text_widget.c_str());
					v2s32 text_offs(rect.UpperLeftCorner.X + (rect.getWidth() - textsize.Width) / 2,
							rect.UpperLeftCorner.Y + (rect.getHeight() - textsize.Height) / 2);
					font->draw(text_widget.c_str(), core::rect<s32>(text_offs, textsize), text_color);
				}
				break; }
			case HUD_ELEM_BUTTON: {
				if (g_touchcontrols) {
					v2s32 offs(e->offset.X * m_scale_factor,
							e->offset.Y * m_scale_factor);
					v2s32 btn_pos = pos + offs;
					g_touchcontrols->drawButton(e->text, e->name, e->item, btn_pos, e->size.X, e->dir);
				}
				break; }
			case HUD_ELEM_TEXTURE_ANIM: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;

				const video::SColor color(255, 255, 255, 255);
				const video::SColor colors[] = {color, color, color, color};
				core::dimension2di imgsize(texture->getOriginalSize());
				v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
				              imgsize.Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);
				draw2DImageFilterScaled(driver, texture, rect,
					core::rect<s32>(e->uv_offset.X, e->uv_offset.Y,
						texture->getOriginalSize().Width,
						texture->getOriginalSize().Height),
					NULL, colors, true);
				break; }
			case HUD_ELEM_MODEL: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;
				// Draw text (name) on top of image
				core::dimension2di imgsize(texture->getOriginalSize());
				v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
				              imgsize.Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);

				draw2DImageFilterScaled(driver, texture, rect,
					core::rect<s32>(core::position2d<s32>(0,0), imgsize),
					NULL, video::SColor(e->number), true);
				break; }
			case HUD_ELEM_TEXT_ROTATE: {
				unsigned int font_size = g_fontengine->getDefaultFontSize();

				if (e->size.X > 0)
					font_size *= e->size.X;

#ifdef __ANDROID__
				// The text size on Android is not proportional with the actual scaling
				// FIXME: why do we have such a weird unportable hack??
				if (font_size > 3 && e->offset.X < -20)
					font_size -= 3;
#endif
				auto textfont = g_fontengine->getFont(FontSpec(font_size,
					(e->style & HUD_STYLE_MONO) ? FM_Mono : FM_Unspecified,
					e->style & HUD_STYLE_BOLD, e->style & HUD_STYLE_ITALIC));

				video::SColor color(255, (e->number >> 16) & 0xFF,
											 (e->number >> 8)  & 0xFF,
											 (e->number >> 0)  & 0xFF);
				EnrichedString text(unescape_string(utf8_to_wide(e->text)), color);
				core::dimension2d<u32> textsize = textfont->getDimension(text.c_str());

				v2s32 offset(0, (e->align.Y - 1.0) * (textsize.Height / 2));
				core::rect<s32> size(0, 0, e->scale.X * m_scale_factor,
						text_height * e->scale.Y * m_scale_factor);
				v2s32 offs(e->offset.X * m_scale_factor,
						e->offset.Y * m_scale_factor);

				// Draw each line
				// See also: GUIFormSpecMenu::parseLabel
				size_t str_pos = 0;
				while (str_pos < text.size()) {
					EnrichedString line = text.getNextLine(&str_pos);

					core::dimension2d<u32> linesize = textfont->getDimension(line.c_str());
					v2s32 line_offset((e->align.X - 1.0) * (linesize.Width / 2), 0);
					textfont->draw(line.c_str(), size + pos + offset + offs + line_offset, color);
					offset.Y += linesize.Height;
				}
				break; }
			case HUD_ELEM_IMAGE_ROTATE: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;

				const video::SColor color(255, 255, 255, 255);
				const video::SColor colors[] = {color, color, color, color};
				core::dimension2di imgsize(texture->getOriginalSize());
				v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
				              imgsize.Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);
				draw2DImageFilterScaled(driver, texture, rect,
					core::rect<s32>(e->uv_offset.X, e->uv_offset.Y,
						texture->getOriginalSize().Width,
						texture->getOriginalSize().Height),
					NULL, colors, true);
				break; }
			case HUD_ELEM_COMPASS_ROTATE: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;
				drawCompassRotate(e, texture, core::rect<s32>(0, 0,
					texture->getOriginalSize().Width, texture->getOriginalSize().Height), e->number);
				break; }
		}
	}
	// Draw hotbar if it exists and is visible
	if (player->hud_flags & HUD_FLAG_HOTBAR_VISIBLE) {
		HudElement hotbar_elem;
		hotbar_elem = {HUD_ELEM_HOTBAR, v2f(0.5, 1), "", v2f(), "", 0 , 0, 0, v2f(0, -1),
				v2f(0, -4), v3f(), v2s32(), 0, "", 0};
		if (!player->hasHudElement(HUD_ELEM_HOTBAR))
			hotbar = hotbar_elem;
		drawItems(pos, v2s32(hotbar.offset.X, hotbar.offset.Y), hotbar.number, hotbar.align, 0,
			inventory->getList("main"), player->getHotbarItemIndex(), hotbar.dir, true);
	}

	// Draw voice chat indicator
	if (m_is_voice_chatting) {
		video::SColor vc_color(255, 0, 255, 0); // Green color
		core::rect<s32> vc_rect(m_screensize.Width - 30, 10, m_screensize.Width - 10, 30);
		driver->draw2DRectangle(vc_color, vc_rect, NULL);
	}
}

void Hud::drawSelectionMesh()
{
	driver->setMaterial(m_selection_material);
	driver->setTransform(video::ETS_WORLD, core::IdentityMatrix);

	if (m_selection_mesh)
		driver->drawMeshBuffer(m_selection_mesh->getMeshBuffer(0));
}

void Hud::updateSelectionMesh(const v3s16 &camera_offset)
{
	if (m_mode == HIGHLIGHT_NONE)
		return;

	// Update selection mesh if necessary
	if (m_selection_mesh)
		m_selection_mesh->drop();
	m_selection_mesh = nullptr;

	if (!client->getHighlights().empty()) {
		m_selection_mesh = Mesh::createSelectionMesh(client->getHighlights(),
				selectionbox_argb, m_selection_mesh_color, m_mode);
		if (m_selection_mesh)
			m_selection_mesh->grab();
	}
}

void Hud::setSelectionPos(const v3f &pos, const v3s16 &camera_offset)
{
	m_selection_pos = pos;
	m_selection_pos_with_offset = pos - intToFloat(camera_offset, BS);
	// Clear selection mesh if position has changed
	if (m_selection_mesh)
		m_selection_mesh->drop();
	m_selection_mesh = nullptr;
}

float RealInputHandler::getJoystickSpeed()
{
	if (g_touchcontrols && g_touchcontrols->getJoystickSpeed())
		return g_touchcontrols->getJoystickSpeed();
	return joystick.getMovementSpeed();
}

float RealInputHandler::getJoystickDirection()
{
	// `getJoystickDirection() == 0` means forward, so we cannot use
	// `getJoystickDirection()` as a condition.
	if (g_touchcontrols && g_touchcontrols->getJoystickSpeed())
		return g_touchcontrols->getJoystickDirection();
	return joystick.getMovementDirection();
}

v2s32 RealInputHandler::getMousePos()
{
	auto control = RenderingEngine::get_raw_device()->getCursorControl();
	if (control) {
		return control->getPosition();
	}

	return m_mousepos;
}

void RealInputHandler::setMousePos(s32 x, s32 y)
{
	auto control = RenderingEngine::get_raw_device()->getCursorControl();
	if (control) {
		control->setPosition(x, y);
	} else {
		m_mousepos = v2s32(x, y);
	}
}

/*
 * RandomInputHandler
 */
s32 RandomInputHandler::Rand(s32 min, s32 max)
{
	return (myrand() % (max - min + 1)) + min;
}

struct RandomInputHandlerSimData {
	GameKeyType key;
	float counter;
	int time_max;
};

void RandomInputHandler::step(float dtime)
{
	static RandomInputHandlerSimData rnd_data[] = {
		{ KeyType::JUMP, 0.0f, 40 },
		{ KeyType::AUX1, 0.0f, 40 },
		{ KeyType::FORWARD, 0.0f, 40 },
		{ KeyType::LEFT, 0.0f, 40 },
		{ KeyType::DIG, 0.0f, 30 },
		{ KeyType::PLACE, 0.0f, 15 }
	};

	for (auto &i : rnd_data) {
		i.counter -= dtime;
		if (i.counter < 0.0) {
			i.counter = 0.1 * Rand(1, i.time_max);
			keydown.flip(i.key);
		}
	}
	{
		static float counter1 = 0;
		counter1 -= dtime;
		if (counter1 < 0.0) {
			counter1 = 0.1 * Rand(1, 20);
			mousespeed = v2s32(Rand(-20, 20), Rand(-15, 20));
		}
	}
	mousepos += mousespeed;
	static bool useJoystick = false;
	{
		static float counterUseJoystick = 0;
		counterUseJoystick -= dtime;
		if (counterUseJoystick < 0.0) {
			counterUseJoystick = 5.0; // switch between joystick and keyboard direction input
			useJoystick = !useJoystick;
		}
	}
	if (useJoystick) {
		static float counterMovement = 0;
		counterMovement -= dtime;
		if (counterMovement < 0.0) {
			counterMovement = 0.1 * Rand(1, 40);
			joystickSpeed = Rand(0,100)*0.01;
			joystickDirection = Rand(-100, 100)*0.01 * M_PI;
		}
	} else {
		joystickSpeed = 0.0f;
		joystickDirection = 0.0f;
	}
}
