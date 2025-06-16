// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include "settings.h"
#include "porting.h"
#include "filesys.h"
#include "config.h"
#include "constants.h"
#include "porting.h"
#include "mapgen/mapgen.h" // Mapgen::setDefaultSettings
#include "util/string.h"
#include "server.h"


/*
 * inspired by https://github.com/systemd/systemd/blob/7aed43437175623e0f3ae8b071bbc500c13ce893/src/hostname/hostnamed.c#L406
 * this could be done in future with D-Bus using query:
 * busctl get-property org.freedesktop.hostname1 /org/freedesktop/hostname1 org.freedesktop.hostname1 Chassis
 */
static bool detect_touch()
{
#if defined(__ANDROID__)
    return true;
#elif defined(__linux__)
    std::string chassis_type;

    // device-tree platforms (non-X86)
    std::ifstream dtb_file("/proc/device-tree/chassis-type");
    if (dtb_file.is_open()) {
        std::getline(dtb_file, chassis_type);
        chassis_type.pop_back();

        if (chassis_type == "tablet" ||
            chassis_type == "handset" ||
            chassis_type == "watch")
            return true;

        if (!chassis_type.empty())
            return false;
    }
    // SMBIOS
    std::ifstream dmi_file("/sys/class/dmi/id/chassis_type");
    if (dmi_file.is_open()) {
        std::getline(dmi_file, chassis_type);

        if (chassis_type == "11" /* Handheld */ ||
            chassis_type == "30" /* Tablet */)
            return true;

        return false;
    }

    // ACPI-based platforms
    std::ifstream acpi_file("/sys/firmware/acpi/pm_profile");
    if (acpi_file.is_open()) {
        std::getline(acpi_file, chassis_type);

        if (chassis_type == "8" /* Tablet */)
            return true;

        return false;
    }

    return false;
#else
    // we don't know, return default
    return false;
#endif
}

void set_default_settings()
{
    Settings *settings = Settings::createLayer(SL_DEFAULTS);
    bool has_touch = detect_touch();

    // Client and server
    settings->setDefault("language", "");
    settings->setDefault("name", "");
    settings->setDefault("bind_address", "");
    settings->setDefault("serverlist_url", "https://servers.luanti.org");

    // Client
    settings->setDefault("address", "");
    settings->setDefault("remote_port", "30000");
#if defined(__unix__) && !defined(__APPLE__) && !defined (__ANDROID__)
    // On Linux+X11 (not Linux+Wayland or Linux+XWayland), I've encountered a bug
    // where fake mouse events were generated from touch events if in relative
    // mouse mode, resulting in the touchscreen controls being instantly disabled
    // again and thus making them unusable.
    // => We can't switch based on the last input method used.
    // => Fall back to hardware detection.
    settings->setDefault("touch_controls", bool_to_cstr(has_touch));
#else
    settings->setDefault("touch_controls", "auto");
#endif
    // Since GUI scaling shouldn't suddenly change during a session, we use
    // hardware detection for "touch_gui" instead of switching based on the last
    // input method used.
    settings->setDefault("touch_gui", bool_to_cstr(has_touch));
    settings->setDefault("sound_volume", "0.8");
    settings->setDefault("sound_volume_unfocused", "0.3");
    settings->setDefault("mute_sound", "false");
    settings->setDefault("sound_extensions_blacklist", "");
    settings->setDefault("mesh_generation_interval", "0");
    settings->setDefault("mesh_generation_threads", "0");
    settings->setDefault("mesh_buffer_min_vertices", "300");
    settings->setDefault("free_move", "false");
    settings->setDefault("pitch_move", "false");
    settings->setDefault("fast_move", "false");
    settings->setDefault("noclip", "false");
    settings->setDefault("screenshot_path", "screenshots");
    settings->setDefault("screenshot_format", "png");
    settings->setDefault("screenshot_quality", "0");
    settings->setDefault("client_unload_unused_data_timeout", "600");
    settings->setDefault("client_mapblock_limit", "7500"); // about 120 MB
    settings->setDefault("enable_build_where_you_stand", "false");
    settings->setDefault("curl_timeout", "20000");
    settings->setDefault("curl_parallel_limit", "8");
    settings->setDefault("curl_file_download_timeout", "300000");
    settings->setDefault("curl_verify_cert", "true");
    settings->setDefault("enable_remote_media_server", "true");
    settings->setDefault("enable_client_modding", "false");
    settings->setDefault("max_out_chat_queue_size", "20");
    settings->setDefault("pause_on_lost_focus", "false");
    settings->setDefault("enable_split_login_register", "true");
    settings->setDefault("occlusion_culler", "bfs");
    settings->setDefault("enable_raytraced_culling", "true");
    settings->setDefault("chat_weblink_color", "#8888FF");

    // Keymap
#if USE_SDL2
#define USEKEY2(name, value, _) settings->setDefault(name, value)
#else
#define USEKEY2(name, _, value) settings->setDefault(name, value)
#endif
    USEKEY2("keymap_forward", "SYSTEM_SCANCODE_26", "KEY_KEY_W");
    settings->setDefault("keymap_autoforward", "");
    USEKEY2("keymap_backward", "SYSTEM_SCANCODE_22", "KEY_KEY_S");
    USEKEY2("keymap_left", "SYSTEM_SCANCODE_4", "KEY_KEY_A");
    USEKEY2("keymap_right", "SYSTEM_SCANCODE_7", "KEY_KEY_D");
    USEKEY2("keymap_jump", "SYSTEM_SCANCODE_44", "KEY_SPACE");
#if !USE_SDL2 && defined(__MACH__) && defined(__APPLE__)
    // Altered settings for CIrrDeviceOSX
    settings->setDefault("keymap_sneak", "KEY_SHIFT");
#else
    USEKEY2("keymap_sneak", "SYSTEM_SCANCODE_225", "KEY_LSHIFT");
#endif
    settings->setDefault("keymap_dig", "KEY_LBUTTON");
    settings->setDefault("keymap_place", "KEY_RBUTTON");
    USEKEY2("keymap_drop", "SYSTEM_SCANCODE_20", "KEY_KEY_Q");
    USEKEY2("keymap_zoom", "SYSTEM_SCANCODE_29", "KEY_KEY_Z");
    USEKEY2("keymap_inventory", "SYSTEM_SCANCODE_12", "KEY_KEY_I");
    USEKEY2("keymap_aux1", "SYSTEM_SCANCODE_8", "KEY_KEY_E");
    USEKEY2("keymap_chat", "SYSTEM_SCANCODE_23", "KEY_KEY_T");
    USEKEY2("keymap_cmd", "SYSTEM_SCANCODE_56", "/");
    USEKEY2("keymap_cmd_local", "SYSTEM_SCANCODE_55", ".");
    USEKEY2("keymap_minimap", "SYSTEM_SCANCODE_25", "KEY_KEY_V");
    USEKEY2("keymap_voicechat", "SYSTEM_SCANCODE_25", "KEY_KEY_V");
    USEKEY2("keymap_console", "SYSTEM_SCANCODE_67", "KEY_F10");

    // see <https://github.com/luanti-org/luanti/issues/12792>
    USEKEY2("keymap_rangeselect", has_touch ? "SYSTEM_SCANCODE_21" : "", has_touch ? "KEY_KEY_R" : "");

    USEKEY2("keymap_freemove", "SYSTEM_SCANCODE_14", "KEY_KEY_K");
    settings->setDefault("keymap_pitchmove", "");
    USEKEY2("keymap_fastmove", "SYSTEM_SCANCODE_13", "KEY_KEY_J");
    USEKEY2("keymap_noclip", "SYSTEM_SCANCODE_11", "KEY_KEY_H");
    USEKEY2("keymap_hotbar_next", "SYSTEM_SCANCODE_17", "KEY_KEY_N");
    USEKEY2("keymap_hotbar_previous", "SYSTEM_SCANCODE_5", "KEY_KEY_B");
    USEKEY2("keymap_mute", "SYSTEM_SCANCODE_16", "KEY_KEY_M");
    settings->setDefault("keymap_increase_volume", "");
    settings->setDefault("keymap_decrease_volume", "");
    settings->setDefault("keymap_cinematic", "");
    settings->setDefault("keymap_toggle_block_bounds", "");
    USEKEY2("keymap_toggle_hud", "SYSTEM_SCANCODE_58", "KEY_F1");
    USEKEY2("keymap_toggle_chat", "SYSTEM_SCANCODE_59", "KEY_F2");
    USEKEY2("keymap_toggle_fog", "SYSTEM_SCANCODE_60", "KEY_F3");
#ifndef NDEBUG
    USEKEY2("keymap_toggle_update_camera", "SYSTEM_SCANCODE_61", "KEY_F4");
#else
    settings->setDefault("keymap_toggle_update_camera", "");
#endif
    USEKEY2("keymap_toggle_debug", "SYSTEM_SCANCODE_62", "KEY_F5");
    USEKEY2("keymap_toggle_profiler", "SYSTEM_SCANCODE_63", "KEY_F6");
    USEKEY2("keymap_camera_mode", "SYSTEM_SCANCODE_6", "KEY_KEY_C");
    USEKEY2("keymap_screenshot", "SYSTEM_SCANCODE_69", "KEY_F12");
    USEKEY2("keymap_fullscreen", "SYSTEM_SCANCODE_68", "KEY_F11");
    USEKEY2("keymap_increase_viewing_range_min", "SYSTEM_SCANCODE_46", "+");
    USEKEY2("keymap_decrease_viewing_range_min", "SYSTEM_SCANCODE_45", "-");
    USEKEY2("keymap_slot1", "SYSTEM_SCANCODE_30", "KEY_KEY_1");
    USEKEY2("keymap_slot2", "SYSTEM_SCANCODE_31", "KEY_KEY_2");
    USEKEY2("keymap_slot3", "SYSTEM_SCANCODE_32", "KEY_KEY_3");
    USEKEY2("keymap_slot4", "SYSTEM_SCANCODE_33", "KEY_KEY_4");
    USEKEY2("keymap_slot5", "SYSTEM_SCANCODE_34", "KEY_KEY_5");
    USEKEY2("keymap_slot6", "SYSTEM_SCANCODE_35", "KEY_KEY_6");
    USEKEY2("keymap_slot7", "SYSTEM_SCANCODE_36", "KEY_KEY_7");
    USEKEY2("keymap_slot8", "SYSTEM_SCANCODE_37", "KEY_KEY_8");
    USEKEY2("keymap_slot9", "SYSTEM_SCANCODE_38", "KEY_KEY_9");
    USEKEY2("keymap_slot10", "SYSTEM_SCANCODE_39", "KEY_KEY_0");
    settings->setDefault("keymap_slot11", "");
    settings->setDefault("keymap_slot12", "");
    settings->setDefault("keymap_slot13", "");
    settings->setDefault("keymap_slot14", "");
    settings->setDefault("keymap_slot15", "");
    settings->setDefault("keymap_slot16", "");
    settings->setDefault("keymap_slot17", "");
    settings->setDefault("keymap_slot18", "");
    settings->setDefault("keymap_slot19", "");
    settings->setDefault("keymap_slot20", "");
    settings->setDefault("keymap_slot21", "");
    settings->setDefault("keymap_slot22", "");
    settings->setDefault("keymap_slot23", "");
    settings->setDefault("keymap_slot24", "");
    settings->setDefault("keymap_slot25", "");
    settings->setDefault("keymap_slot26", "");
    settings->setDefault("keymap_slot27", "");
    settings->setDefault("keymap_slot28", "");
    settings->setDefault("keymap_slot29", "");
    settings->setDefault("keymap_slot30", "");
    settings->setDefault("keymap_slot31", "");
    settings->setDefault("keymap_slot32", "");

#ifndef NDEBUG
    // Default keybinds for quicktune in debug builds
    USEKEY2("keymap_quicktune_prev", "SYSTEM_SCANCODE_74", "KEY_HOME");
    USEKEY2("keymap_quicktune_next", "SYSTEM_SCANCODE_77", "KEY_END");
    USEKEY2("keymap_quicktune_dec", "SYSTEM_SCANCODE_81", "KEY_NEXT");
    USEKEY2("keymap_quicktune_inc", "SYSTEM_SCANCODE_82", "KEY_PRIOR");
#else
    settings->setDefault("keymap_quicktune_prev", "");
    settings->setDefault("keymap_quicktune_next", "");
    settings->setDefault("keymap_quicktune_dec", "");
    settings->setDefault("keymap_quicktune_inc", "");
#endif
#undef USEKEY2

    // Visuals
#ifdef NDEBUG
    settings->setDefault("show_debug", "false");
    settings->setDefault("opengl_debug", "false");
#else
    settings->setDefault("show_debug", "true");
    settings->setDefault("opengl_debug", "true");
#endif
    settings->setDefault("fsaa", "2");
    settings->setDefault("undersampling", "1");
    settings->setDefault("world_aligned_mode", "enable");
    settings->setDefault("autoscale_mode", "disable");
    settings->setDefault("texture_min_size", std::to_string(TEXTURE_FILTER_MIN_SIZE));
    settings->setDefault("enable_fog", "true");
    settings->setDefault("fog_start", "0.4");
    settings->setDefault("3d_mode", "none"); // Valid options: "none", "anaglyph", "interlaced", "sidebyside", "topbottom", "crossview", "cartoon"
    settings->setDefault("3d_paralax_strength", "0.025");
    settings->setDefault("tooltip_show_delay", "400");
    settings->setDefault("tooltip_append_itemname", "false");
    settings->setDefault("fps_max", "60");
    settings->setDefault("fps_max_unfocused", "10");
    settings->setDefault("viewing_range", "190");
    settings->setDefault("client_mesh_chunk", "1");
    settings->setDefault("screen_w", "1024");
    settings->setDefault("screen_h", "600");
    settings->setDefault("window_maximized", "false");
    settings->setDefault("autosave_screensize", "true");
    settings->setDefault("fullscreen", bool_to_cstr(has_touch));
    settings->setDefault("vsync", "false");
    settings->setDefault("fov", "72");
    settings->setDefault("leaves_style", "fancy");
    settings->setDefault("connected_glass", "false");
    settings->setDefault("smooth_lighting", "true");
    settings->setDefault("performance_tradeoffs", "false");
    settings->setDefault("lighting_alpha", "0.0");
    settings->setDefault("lighting_beta", "1.5");
    settings->setDefault("display_gamma", "1.0");
    settings->setDefault("lighting_boost", "0.2");
    settings->setDefault("lighting_boost_center", "0.5");
    settings->setDefault("lighting_boost_spread", "0.2");
    settings->setDefault("texture_path", "");
    settings->setDefault("shader_path", "");
    settings->setDefault("video_driver", "");
    settings->setDefault("cinematic", "false");
    settings->setDefault("camera_smoothing", "0.0");
    settings->setDefault("cinematic_camera_smoothing", "0.7");
    settings->setDefault("view_bobbing_amount", "1.0");
    settings->setDefault("enable_3d_clouds", "true");
    settings->setDefault("soft_clouds", "false");
    settings->setDefault("cloud_radius", "12");
    settings->setDefault("menu_clouds", "true");
    settings->setDefault("translucent_liquids", "true");
    settings->setDefault("console_height", "0.6");
    settings->setDefault("console_color", "(0,0,0)");
    settings->setDefault("console_alpha", "200");
    settings->setDefault("formspec_fullscreen_bg_color", "(0,0,0)");
    settings->setDefault("formspec_fullscreen_bg_opacity", "140");
    settings->setDefault("selectionbox_color", "(0,0,0)");
    settings->setDefault("selectionbox_width", "2");
    settings->setDefault("node_highlighting", "box");
    settings->setDefault("crosshair_color", "(255,255,255)");
    settings->setDefault("crosshair_alpha", "255");
    settings->setDefault("recent_chat_messages", "6");
    settings->setDefault("hud_scaling", "1.0");
    settings->setDefault("gui_scaling", "1.0");
    settings->setDefault("gui_scaling_filter", "false");
    settings->setDefault("smooth_scrolling", "true");
    settings->setDefault("hud_hotbar_max_width", "1.0");
    settings->setDefault("enable_local_map_saving", "false");
    settings->setDefault("show_entity_selectionbox", "false");
    settings->setDefault("ambient_occlusion_gamma", "1.8");
    settings->setDefault("arm_inertia", "true");
    settings->setDefault("show_nametag_backgrounds", "true");
    settings->setDefault("show_block_bounds_radius_near", "4");
    settings->setDefault("transparency_sorting_group_by_buffers", "true");
    settings->setDefault("transparency_sorting_distance", "16");

    settings->setDefault("enable_minimap", "true");
    settings->setDefault("minimap_shape_round", "true");
    settings->setDefault("minimap_double_scan_height", "true");

    // Effects
    settings->setDefault("enable_post_processing", "true");
    settings->setDefault("post_processing_texture_bits", "16");
    settings->setDefault("directional_colored_fog", "true");
    settings->setDefault("inventory_items_animations", "false");
    settings->setDefault("mip_map", "false");
    settings->setDefault("bilinear_filter", "false");
    settings->setDefault("trilinear_filter", "false");
    settings->setDefault("anisotropic_filter", "false");
    settings->setDefault("tone_mapping", "false");
    settings->setDefault("enable_waving_water", "false");
    settings->setDefault("water_wave_height", "1.0");
    settings->setDefault("water_wave_length", "20.0");
    settings->setDefault("water_wave_speed", "5.0");
    settings->setDefault("enable_waving_leaves", "false");
    settings->setDefault("enable_waving_plants", "false");
    settings->setDefault("exposure_compensation", "0.0");
    settings->setDefault("enable_auto_exposure", "false");
    settings->setDefault("debanding", "true");
    settings->setDefault("antialiasing", "none");
    settings->setDefault("enable_bloom", "false");
    settings->setDefault("enable_bloom_debug", "false");
    settings->setDefault("enable_volumetric_lighting", "false");
    settings->setDefault("enable_water_reflections", "false");
    settings->setDefault("enable_translucent_foliage", "false");

    // Effects Shadows
    settings->setDefault("enable_dynamic_shadows", "false");
    settings->setDefault("shadow_strength_gamma", "1.0");
    settings->setDefault("shadow_map_max_distance", "140.0");
    settings->setDefault("shadow_map_texture_size", "2048");
    settings->setDefault("shadow_map_texture_32bit", "true");
    settings->setDefault("shadow_map_color", "false");
    settings->setDefault("shadow_filters", "1");
    settings->setDefault("shadow_poisson_filter", "true");
    settings->setDefault("shadow_update_frames", "16");
    settings->setDefault("shadow_soft_radius", "5.0");
    settings->setDefault("shadow_sky_body_orbit_tilt", "0.0");

    // Input
    settings->setDefault("invert_mouse", "false");
    settings->setDefault("enable_hotbar_mouse_wheel", "true");
    settings->setDefault("invert_hotbar_mouse_wheel", "false");
    settings->setDefault("mouse_sensitivity", "0.2");
    settings->setDefault("repeat_place_time", "0.25");
    settings->setDefault("repeat_dig_time", "0.0");
    settings->setDefault("safe_dig_and_place", "false");
    settings->setDefault("random_input", "false");
    settings->setDefault("aux1_descends", "false");
    settings->setDefault("doubletap_jump", "false");
    settings->setDefault("always_fly_fast", "true");
    settings->setDefault("toggle_sneak_key", "false");
    settings->setDefault("toggle_aux1_key", "false");
    settings->setDefault("autojump", bool_to_cstr(has_touch));
    settings->setDefault("continuous_forward", "false");
    settings->setDefault("enable_joysticks", "false");
    settings->setDefault("joystick_id", "0");
    settings->setDefault("joystick_type", "auto");
    settings->setDefault("repeat_joystick_button_time", "0.17");
    settings->setDefault("joystick_frustum_sensitivity", "170");
    settings->setDefault("joystick_deadzone", "2048");

    // Main menu
    settings->setDefault("main_menu_path", "");
    settings->setDefault("serverlist_file", "favoriteservers.json");

    // General font settings
    settings->setDefault("font_path", porting::getDataPath("fonts" DIR_DELIM "Arimo-Regular.ttf"));
    settings->setDefault("font_path_italic", porting::getDataPath("fonts" DIR_DELIM "Arimo-Italic.ttf"));
    settings->setDefault("font_path_bold", porting::getDataPath("fonts" DIR_DELIM "Arimo-Bold.ttf"));
    settings->setDefault("font_path_bold_italic", porting::getDataPath("fonts" DIR_DELIM "Arimo-BoldItalic.ttf"));
    settings->setDefault("font_bold", "false");
    settings->setDefault("font_italic", "false");
    settings->setDefault("font_shadow", "1");
    settings->setDefault("font_shadow_alpha", "127");
    settings->setDefault("font_size_divisible_by", "1");
    settings->setDefault("mono_font_path", porting::getDataPath("fonts" DIR_DELIM "Cousine-Regular.ttf"));
    settings->setDefault("mono_font_path_italic", porting::getDataPath("fonts" DIR_DELIM "Cousine-Italic.ttf"));
    settings->setDefault("mono_font_path_bold", porting::getDataPath("fonts" DIR_DELIM "Cousine-Bold.ttf"));
    settings->setDefault("mono_font_path_bold_italic", porting::getDataPath("fonts" DIR_DELIM "Cousine-BoldItalic.ttf"));
    settings->setDefault("mono_font_size_divisible_by", "1");
    settings->setDefault("fallback_font_path", porting::getDataPath("fonts" DIR_DELIM "DroidSansFallbackFull.ttf"));

    std::string font_size_str = std::to_string(TTF_DEFAULT_FONT_SIZE);
    settings->setDefault("font_size", font_size_str);
    settings->setDefault("mono_font_size", font_size_str);
    settings->setDefault("chat_font_size", "0"); // Default "font_size"

    // ContentDB
    settings->setDefault("contentdb_url", "https://content.luanti.org");
    settings->setDefault("contentdb_enable_updates_indicator", "true");
    settings->setDefault("contentdb_max_concurrent_downloads", "3");

#ifdef __ANDROID__
    settings->setDefault("contentdb_flag_blacklist", "nonfree, android_default");
#else
    settings->setDefault("contentdb_flag_blacklist", "nonfree, desktop_default");
#endif

#if ENABLE_UPDATE_CHECKER
    settings->setDefault("update_information_url", "https://www.luanti.org/release_info.json");
#else
    settings->setDefault("update_information_url", "");
#endif

    // Server
    settings->setDefault("strip_color_codes", "false");
#ifndef NDEBUG
    settings->setDefault("random_mod_load_order", "true");
#else
    settings->setDefault("random_mod_load_order", "false");
#endif
#if USE_PROMETHEUS
    settings->setDefault("prometheus_listener_address", "127.0.0.1:30000");
#endif

    // Network
    settings->setDefault("enable_ipv6", "true");
    settings->setDefault("ipv6_server", "true");
    settings->setDefault("max_packets_per_iteration", "1024");
    settings->setDefault("port", "30000");
    settings->setDefault("strict_protocol_version_checking", "false");
    settings->setDefault("protocol_version_min", "1");
    settings->setDefault("player_transfer_distance", "0");
    settings->setDefault("max_simultaneous_block_sends_per_client", "40");

    settings->setDefault("motd", "");
    settings->setDefault("max_users", "15");
    settings->setDefault("creative_mode", "false");
    settings->setDefault("enable_damage", "true");
    settings->setDefault("default_password", "");
    settings->setDefault("default_privs", "interact, shout");
    settings->setDefault("enable_pvp", "true");
    settings->setDefault("enable_mod_channels", "false");
    settings->setDefault("disallow_empty_password", "false");
    settings->setDefault("anticheat_flags", flagdesc_anticheat,
        AC_DIGGING | AC_INTERACTION | AC_MOVEMENT);
    settings->setDefault("anticheat_movement_tolerance", "1.0");
    settings->setDefault("enable_rollback_recording", "false");
    settings->setDefault("deprecated_lua_api_handling", "log");

    settings->setDefault("kick_msg_shutdown", "Server shutting down.");
    settings->setDefault("kick_msg_crash", "This server has experienced an internal error. You will now be disconnected.");
    settings->setDefault("ask_reconnect_on_crash", "false");

    settings->setDefault("chat_message_format", "<@name> @message");
    settings->setDefault("profiler_print_interval", "0");
    settings->setDefault("active_object_send_range_blocks", "8");
    settings->setDefault("active_block_range", "4");
    //settings->setDefault("max_simultaneous_block_sends_per_client", "1");
    // This causes frametime jitter on client side, or does it?
    settings->setDefault("max_block_send_distance", "12");
    settings->setDefault("block_send_optimize_distance", "4");
    settings->setDefault("block_cull_optimize_distance", "25");
    settings->setDefault("server_side_occlusion_culling", "true");
    settings->setDefault("csm_restriction_flags", "62");
    settings->setDefault("csm_restriction_noderange", "0");
    settings->setDefault("max_clearobjects_extra_loaded_blocks", "4096");
    settings->setDefault("time_speed", "72");
    settings->setDefault("world_start_time", "6125");
    settings->setDefault("server_unload_unused_data_timeout", "29");
    settings->setDefault("max_objects_per_block", "256");
    settings->setDefault("server_map_save_interval", "5.3");
    settings->setDefault("chat_message_max_size", "500");
    settings->setDefault("chat_message_limit_per_10sec", "8.0");
    settings->setDefault("chat_message_limit_trigger_kick", "50");
    settings->setDefault("sqlite_synchronous", "2");
    settings->setDefault("map_compression_level_disk", "-1");
    settings->setDefault("map_compression_level_net", "-1");
    settings->setDefault("full_block_send_enable_min_time_from_building", "2.0");
    settings->setDefault("dedicated_server_step", "0.09");
    settings->setDefault("active_block_mgmt_interval", "2.0");
    settings->setDefault("abm_interval", "1.0");
    settings->setDefault("abm_time_budget", "0.2");
    settings->setDefault("nodetimer_interval", "0.2");
    settings->setDefault("ignore_world_load_errors", "false");
    settings->setDefault("remote_media", "");
    settings->setDefault("debug_log_level", "action");
    settings->setDefault("debug_log_size_max", "50");
    settings->setDefault("chat_log_level", "error");
    settings->setDefault("emergequeue_limit_total", "1024");
    settings->setDefault("emergequeue_limit_diskonly", "128");
    settings->setDefault("emergequeue_limit_generate", "128");
    settings->setDefault("node_timer_intervall_min", "1.0");
    settings->setDefault("node_timer_intervall_max", "1.0");
    settings->setDefault("player_physics_interval", "0.05");
    settings->setDefault("liquid_loop_interval", "1");
    settings->setDefault("item_entity_ttl", "300"); // 5 minutes
    settings->setDefault("item_entity_ttl_random_add", "0");
    settings->setDefault("item_entity_collect", "false");
    settings->setDefault("damage_enabled", "true");
    settings->setDefault("dropping_items_disables_handling_player_inventory", "false");
    settings->setDefault("max_forceloaded_blocks", "1000");
    settings->setDefault("chunk_emerge_queue_limit", "8");
    settings->setDefault("chunk_emerge_queue_level", "1");
    settings->setDefault("load_node_timers_on_emerge", "true");
    settings->setDefault("client_authentication_timeout", "10");
    settings->setDefault("chat_history_size", "256");
    settings->setDefault("enable_rollback_debug", "false");
    settings->setDefault("item_stack_limit", "99");
    settings->setDefault("liquid_flow_time", "5");
    settings->setDefault("secure.http_mods", "false");
    settings->setDefault("http_server_enabled", "false");
    settings->setDefault("http_server_port", "8080");
    settings->setDefault("http_server_trusted_proxies", "");
    settings->setDefault("http_server_allow_ip_overrides", "false");
    settings->setDefault("enable_bones_system", "true");
    settings->setDefault("bones_do_drop", "true");
    settings->setDefault("bones_light_level", "14");
    settings->setDefault("sprint_speed_factor", "1.2");
    settings->setDefault("fast_dig_key", "false");
    settings->setDefault("enable_climb", "true");
    settings->setDefault("enable_jump_decay", "true");
    settings->setDefault("player_look_horizontal_limit", "0");
    settings->setDefault("player_look_vertical_limit", "0");
    settings->setDefault("walk_speed", "4.6");
    settings->setDefault("fast_speed", "10");
    settings->setDefault("climb_speed", "4.0");
    settings->setDefault("jump_height", "1.2");
    settings->setDefault("gravity", "9.81");
    settings->setDefault("node_damage_cooldown", "0.2");
    settings->setDefault("max_hp", "20");
    settings->setDefault("fall_damage_add_hp", "1");
    settings->setDefault("fall_damage_level", "3");
    settings->setDefault("suffocation_damage_per_second", "2");
    settings->setDefault("clear_objects_min_range", "30");
    settings->setDefault("clear_objects_max_range", "200");
    settings->setDefault("clear_objects_min_time", "600");
    settings->setDefault("clear_objects_max_time", "900");
    settings->setDefault("area_forceloading_blocks_per_player", "0");
    settings->setDefault("allow_sprint", "true");
    settings->setDefault("force_enable_damage", "false");
    settings->setDefault("shutdown_timeout", "10");
    settings->setDefault("player_move_speed_factor", "1.0");
    settings->setDefault("enable_item_drop", "true");
    settings->setDefault("camera_max_height", "3");
    settings->setDefault("area_forceloading_max_blocks", "256");
    settings->setDefault("time_send_interval", "0.25");
    settings->setDefault("client_entity_map_limit", "5000");
    settings->setDefault("csm_debug", "false");
    settings->setDefault("texture_path_override", "");
    settings->setDefault("enable_shaders", "false");
    settings->setDefault("shader_standard_caps_level", "0");
    settings->setDefault("enable_damage_for_creative_mode", "false");
    settings->setDefault("enable_old_liquid_behavior", "false");
    settings->setDefault("max_forceloaded_blocks_per_area", "0");
    settings->setDefault("forceload_min_block_per_area", "1");
    settings->setDefault("enable_anti_player_lag", "false");
    settings->setDefault("player_lag_factor", "0.5");
    settings->setDefault("player_lag_threshold", "0.0");
    settings->setDefault("max_forceloaded_blocks_total", "25000");
    settings->setDefault("player_move_velocity_factor", "1.0");
    settings->setDefault("player_punch_interval", "0.2");
    settings->setDefault("player_fast_punch_interval", "0.15");
    settings->setDefault("disable_item_decay", "false");
    settings->setDefault("show_chat_input_text_length", "true");
    settings->setDefault("chat_input_text_length_color", "#888");
    settings->setDefault("chat_input_text_length_color_limit", "#F00");
    settings->setDefault("rollback_time_period", "86400");
    settings->setDefault("rollback_purge_interval", "3600");
    settings->setDefault("rollback_max_size", "0");
    settings->setDefault("rollback_alt_database_path", "");
    settings->setDefault("rollback_store_block_data", "true");
    settings->setDefault("rollback_block_data_ttl", "604800");
    settings->setDefault("enable_console", "true");
    settings->setDefault("player_animation_speed", "1.0");
    settings->setDefault("active_objects_send_range", "200");
    settings->setDefault("active_objects_receive_range", "200");
    settings->setDefault("connection_timeout_time", "60");
    settings->setDefault("network_throttle", "0");
    settings->setDefault("network_throttle_send_interval", "1.0");
    settings->setDefault("network_send_queue_size", "200");
    settings->setDefault("network_max_send_queues_per_client", "16");
    settings->setDefault("enable_sprint", "true");
    settings->setDefault("show_sprint_status", "false");
    settings->setDefault("sprint_status_color", "#FFFF00");
    settings->setDefault("sprint_status_color_off", "#FF0000");
    settings->setDefault("sprint_status_display_duration", "1.0");
    settings->setDefault("fall_speed", "9.81");
    settings->setDefault("client_texture_cache_size", "500");
    settings->setDefault("server_announce", "false");
    settings->setDefault("server_name", "Luanti Server");
    settings->setDefault("server_description", "A Luanti server");
    settings->setDefault("server_address", "");
    settings->setDefault("server_url", "");
    settings->setDefault("server_autoshutdown_timeout", "0");
    settings->setDefault("server_autoshutdown_message", "Server is shutting down due to inactivity.");
    settings->setDefault("server_autoshutdown_warning_message", "Server will shut down in %i minutes due to inactivity.");
    settings->setDefault("server_autoshutdown_warning_timeout", "120");
    settings->setDefault("server_autoshutdown_warning_interval", "60");
    settings->setDefault("server_autoshutdown_warning_messages_count", "0");
    settings->setDefault("server_autoshutdown_warning_messages_interval", "0");
    settings->setDefault("server_autoshutdown_players_threshold", "0");
    settings->setDefault("server_autoshutdown_players_threshold_message", "Server will shut down because there are too few players.");
    settings->setDefault("server_autoshutdown_players_threshold_warning_message", "Server will shut down in %i minutes because there are too few players.");
    settings->setDefault("server_autoshutdown_players_threshold_warning_timeout", "0");
    settings->setDefault("server_autoshutdown_players_threshold_warning_interval", "0");
    settings->setDefault("server_autoshutdown_players_threshold_warning_messages_count", "0");
    settings->setDefault("server_autoshutdown_players_threshold_warning_messages_interval", "0");
    settings->setDefault("player_get_static_contact_info", "true");
    settings->setDefault("load_mod_specific_textures_first", "false");
    settings->setDefault("max_transfer_per_second", "0");
    settings->setDefault("default_player_model", "character.b3d");
    settings->setDefault("texture_path_override_server", "");
    settings->setDefault("show_auth_info_on_connect", "true");
    settings->setDefault("node_physics_steps_per_second", "10");
    settings->setDefault("player_default_speed", "1.0");
    settings->setDefault("player_default_fast_speed", "1.0");
    settings->setDefault("player_default_climb_speed", "1.0");
    settings->setDefault("player_default_jump_height", "1.0");
    settings->setDefault("player_default_fall_speed", "1.0");
    settings->setDefault("player_default_max_hp", "1.0");
    settings->setDefault("player_default_punch_interval", "1.0");
    settings->setDefault("player_default_fast_punch_interval", "1.0");
    settings->setDefault("player_default_node_damage_cooldown", "1.0");
    settings->setDefault("disable_legacy_client_version_string", "false");
    settings->setDefault("enable_fallback_fonts", "true");
    settings->setDefault("enable_touchscreen_editor_debug_output", "false");
    settings->setDefault("disable_texture_filtering", "false");
    Mapgen::setDefaultSettings(settings);
}