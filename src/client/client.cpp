// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#include <iostream>
#include <algorithm>
#include <sstream>
#include <cmath>
#include <IFileSystem.h>
#include <json/json.h>
#include "client.h"
#include "client/fontengine.h"
#include "network/clientopcodes.h"
#include "network/connection.h"
#include "network/networkpacket.h"
#include "threading/mutex_auto_lock.h"
#include "client/clientevent.h"
#include "client/renderingengine.h"
#include "client/sound.h"
#include "client/texturepaths.h"
#include "client/texturesource.h"
#include "client/mesh_generator_thread.h"
#include "client/particles.h"
#include "client/localplayer.h"
#include "util/auth.h"
#include "util/directiontables.h"
#include "util/pointedthing.h"
#include "util/serialize.h"
#include "util/string.h"
#include "util/srp.h"
#include "filesys.h"
#include "mapblock_mesh.h"
#include "mapblock.h"
#include "mapsector.h"
#include "minimap.h"
#include "modchannels.h"
#include "content/mods.h"
#include "profiler.h"
#include "shader.h"
#include "gettext.h"
#include "gettime.h"
#include "clientdynamicinfo.h"
#include "clientmap.h"
#include "clientmedia.h"
#include "version.h"
#include "database/database-files.h"
#include "database/database-sqlite3.h"
#include "serialization.h"
#include "guiscalingfilter.h"
#include "script/scripting_client.h"
#include "game.h"
#include "chatmessage.h"
#include "translation.h"
#include "content/mod_configuration.h"
#include "mapnode.h"
#include "item_visuals_manager.h"
#include "hud.h"

extern gui::IGUIEnvironment* guienv;

/*
    Utility classes
*/

u32 PacketCounter::sum() const
{
    u32 n = 0;
    for (const auto &it : m_packets)
        n += it.second;
    return n;
}

void PacketCounter::print(std::ostream &o) const
{
    for (const auto &it : m_packets) {
        auto name = it.first >= TOCLIENT_NUM_MSG_TYPES ? nullptr
            : toClientCommandTable[it.first].name;
        if (!name)
            name = "?";
        o << "cmd " << it.first << " (" << name << ") count "
            << it.second << std::endl;
    }
}

/*
    Client
*/

Client::Client(
        const char *playername,
        const std::string &password,
        MapDrawControl &control,
        IWritableTextureSource *tsrc,
        IWritableShaderSource *shsrc,
        IWritableItemDefManager *itemdef,
        NodeDefManager *nodedef,
        ISoundManager *sound,
        MtEventManager *event,
        RenderingEngine *rendering_engine,
        ItemVisualsManager *item_visuals_manager,
        ELoginRegister allow_login_or_register
):
    m_tsrc(tsrc),
    m_shsrc(shsrc),
    m_itemdef(itemdef),
    m_nodedef(nodedef),
    m_sound(sound),
    m_event(event),
    m_rendering_engine(rendering_engine),
    m_item_visuals_manager(item_visuals_manager),
    m_mesh_update_manager(std::make_unique<MeshUpdateManager>(this)),
    m_env(
        make_irr<ClientMap>(this, rendering_engine, control, 666),
        tsrc, this
    ),
    m_hud(new Hud(this, m_env.getLocalPlayer(), this)),
    m_particle_manager(std::make_unique<ParticleManager>(&m_env)),
    m_allow_login_or_register(allow_login_or_register),
    m_server_ser_ver(SER_FMT_VER_INVALID),
    m_last_chat_message_sent(time(NULL)),
    m_password(password),
    m_chosen_auth_mech(AUTH_MECHANISM_NONE),
    m_media_downloader(new ClientMediaDownloader()),
    m_state(LC_Created),
    m_modchannel_mgr(new ModChannelMgr())
{
    // Add local player
    m_env.setLocalPlayer(new LocalPlayer(this, playername));

    // Make the mod storage database and begin the save for later
    m_mod_storage_database =
            new ModStorageDatabaseSQLite3(porting::path_user + DIR_DELIM + "client");
    m_mod_storage_database->beginSave();

    if (g_settings->getBool("enable_minimap")) {
        m_minimap = new Minimap(this);
    }

    m_cache_save_interval = g_settings->getU16("server_map_save_interval");
    m_mesh_grid = { g_settings->getU16("client_mesh_chunk") };
}

void Client::migrateModStorage()
{
    std::string mod_storage_dir = porting::path_user + DIR_DELIM + "client";
    std::string old_mod_storage = mod_storage_dir + DIR_DELIM + "mod_storage";
    if (fs::IsDir(old_mod_storage)) {
        infostream << "Migrating client mod storage to SQLite3 database" << std::endl;
        {
            ModStorageDatabaseFiles files_db(mod_storage_dir);
            std::vector<std::string> mod_list;
            files_db.listMods(&mod_list);
            for (const std::string &modname : mod_list) {
                infostream << "Migrating client mod storage for mod " << modname << std::endl;
                StringMap meta;
                files_db.getModEntries(modname, &meta);
                for (const auto &pair : meta) {
                    m_mod_storage_database->setModEntry(modname, pair.first, pair.second);
                }
            }
        }
        if (!fs::Rename(old_mod_storage, old_mod_storage + ".bak")) {
            // Execution cannot move forward if the migration does not complete.
            throw BaseException("Could not finish migrating client mod storage");
        }
        infostream << "Finished migration of client mod storage" << std::endl;
    }
}

void Client::loadMods()
{
    // Don't load mods twice.
    // If client scripting is disabled by the client, don't load builtin or
    // client-provided mods.
    if (m_mods_loaded || !g_settings->getBool("enable_client_modding"))
        return;

    // If client scripting is disabled by the server, don't load builtin or
    // client-provided mods.
    // TODO Delete this code block when server-sent CSM and verifying of builtin are
    // complete.
    if (checkCSMRestrictionFlag(CSMRestrictionFlags::CSM_RF_LOAD_CLIENT_MODS)) {
        warningstream << "Client-provided mod loading is disabled by server." <<
            std::endl;
        return;
    }

    m_script = new ClientScripting(this);
    m_env.setScript(m_script);
    m_script->setEnv(&m_env);

    // Load builtin
    scanModIntoMemory(BUILTIN_MOD_NAME, getBuiltinLuaPath());
    m_script->loadModFromMemory(BUILTIN_MOD_NAME);
    m_script->checkSetByBuiltin();

    ModConfiguration modconf;
    {
        std::unordered_map<std::string, std::string> paths;
        std::string path_user = porting::path_user + DIR_DELIM + "clientmods";
        const auto modsPath = getClientModsLuaPath();
        if (modsPath != path_user) {
            paths["share"] = modsPath;
        }
        paths["mods"] = path_user;

        std::string settings_path = path_user + DIR_DELIM + "mods.conf";
        modconf.addModsFromConfig(settings_path, paths);
        modconf.checkConflictsAndDeps();
    }

    m_mods = modconf.getMods();

    // complain about mods with unsatisfied dependencies
    if (!modconf.isConsistent()) {
        errorstream << modconf.getUnsatisfiedModsError() << std::endl;
        return;
    }

    // Print mods
    infostream << "Client loading mods: ";
    for (const ModSpec &mod : m_mods)
        infostream << mod.name << " ";
    infostream << std::endl;

    // Load "mod" scripts
    for (const ModSpec &mod : m_mods) {
        mod.checkAndLog();
        scanModIntoMemory(mod.name, mod.path);
    }

    // Run them
    for (const ModSpec &mod : m_mods)
        m_script->loadModFromMemory(mod.name);

    // Mods are done loading. Unlock callbacks
    m_mods_loaded = true;

    // Run a callback when mods are loaded
    m_script->on_mods_loaded();

    // Create objects if they're ready
    if (m_state == LC_Ready)
        m_script->on_client_ready(m_env.getLocalPlayer());
    if (m_camera)
        m_script->on_camera_ready(m_camera);
    if (m_minimap)
        m_script->on_minimap_ready(m_minimap);
}

void Client::scanModSubfolder(const std::string &mod_name, const std::string &mod_path,
            std::string mod_subpath)
{
    std::string full_path = mod_path + DIR_DELIM + mod_subpath;
    std::vector<fs::DirListNode> mod = fs::GetDirListing(full_path);
    for (const fs::DirListNode &j : mod) {
        if (j.name[0] == '.')
            continue;

        if (j.dir) {
            scanModSubfolder(mod_name, mod_path, mod_subpath + j.name + DIR_DELIM);
            continue;
        }
        std::replace(mod_subpath.begin(), mod_subpath.end(), DIR_DELIM_CHAR, '/');

        std::string real_path = full_path + j.name;
        std::string vfs_path = mod_name + ":" + mod_subpath + j.name;
        infostream << "Client::scanModSubfolder(): Loading \"" << real_path
                << "\" as \"" << vfs_path << "\"." << std::endl;

        std::string contents;
        if (!fs::ReadFile(real_path, contents, true)) {
            continue;
        }

        m_mod_vfs.emplace(vfs_path, contents);
    }
}

const std::string &Client::getBuiltinLuaPath()
{
    static const std::string builtin_dir = porting::path_share + DIR_DELIM + "builtin";
    return builtin_dir;
}

const std::string &Client::getClientModsLuaPath()
{
    static const std::string clientmods_dir = porting::path_share + DIR_DELIM + "clientmods";
    return clientmods_dir;
}

const std::vector<ModSpec>& Client::getMods() const
{
    static std::vector<ModSpec> client_modspec_temp;
    return client_modspec_temp;
}

const ModSpec* Client::getModSpec(const std::string &modname) const
{
    return NULL;
}

void Client::Stop()
{
    m_shutdown = true;
    if (m_mods_loaded)
        m_script->on_shutdown();
    //request all client managed threads to stop
    m_mesh_update_manager->stop();
    // Save local server map
    if (m_localdb) {
        infostream << "Local map saving ended." << std::endl;
        m_localdb->endSave();
    }

    if (m_mods_loaded)
        delete m_script;
}

bool Client::isShutdown()
{
    return m_shutdown || !m_mesh_update_manager->isRunning();
}

Client::~Client()
{
    m_shutdown = true;
    if (m_con)
        m_con->Disconnect();

    deleteAuthData();

    m_mesh_update_manager->stop();
    m_mesh_update_manager->wait();

    MeshUpdateResult r;
    while (m_mesh_update_manager->getNextResult(r)) {
        for (auto block : r.map_blocks)
            if (block)
                block->refDrop();
        delete r.mesh;
    }

    delete m_inventory_from_server;

    // Delete detached inventories
    for (auto &m_detached_inventorie : m_detached_inventories) {
        delete m_detached_inventorie.second;
    }

    // cleanup 3d model meshes on client shutdown
    m_rendering_engine->cleanupMeshCache();

    m_item_visuals_manager->clear();

    guiScalingCacheClear();

    delete m_minimap;
    m_minimap = nullptr;

    delete m_media_downloader;

    // Write the changes and delete
    if (m_mod_storage_database)
        m_mod_storage_database->endSave();
    delete m_mod_storage_database;

    // Free sound ids
    for (auto &csp : m_sounds_client_to_server)
        m_sound->freeId(csp.first);
    m_sounds_client_to_server.clear();
}

void Client::connect(const Address &address, const std::string &address_name)
{
    if (m_con) {
        // can't do this if the connection has entered auth phase
        sanity_check(m_state == LC_Created && m_proto_ver == 0);
        infostream << "Client connection will be recreated" << std::endl;

        m_access_denied = false;
        m_access_denied_reconnect = false;
        m_access_denied_reason.clear();
    }

    m_address_name = address_name;
    m_con.reset(con::createMTP(CONNECTION_TIMEOUT, address.isIPv6(), this));

    infostream << "Connecting to server at ";
    address.print(infostream);
    infostream << std::endl;

    m_con->Connect(address);

    initLocalMapSaving(address, m_address_name);
}

void Client::step(float dtime)
{
    // Limit a bit
    if (dtime > DTIME_LIMIT)
        dtime = DTIME_LIMIT;

    m_animation_time = fmodf(m_animation_time + dtime, 60.0f);

    ReceiveAll();

    /*
        Packet counter
    */
    {
        float &counter = m_packetcounter_timer;
        counter -= dtime;
        if(counter <= 0.0f)
        {
            counter = 30.0f;
            u32 sum = m_packetcounter.sum();
            float avg = sum / counter;

            infostream << "Client packetcounter (" << counter << "s): "
                    << "sum=" << sum << " avg=" << avg << "/s" << std::endl;
            m_packetcounter.print(infostream);
            m_packetcounter.clear();
        }
    }

    // The issue that made this workaround necessary was fixed in August 2024, but
    // it's not like we can remove this code - ever.
    if (m_state == LC_Created) {
        float &counter = m_connection_reinit_timer;
        counter -= dtime;
        if (counter <= 0) {
            counter = 1.5f;

            LocalPlayer *myplayer = m_env.getLocalPlayer();
            FATAL_ERROR_IF(!myplayer, "Local player not found in environment");

            sendInit(myplayer->getName());
        }

        // Not connected, return
        return;
    }

    /*
        Do stuff if connected
    */

    /*
        Run Map's timers and unload unused data
    */
    constexpr float map_timer_and_unload_dtime = 5.25f;
    constexpr s32 mapblock_limit_enforce_distance = 200;
    if(m_map_timer_and_unload_interval.step(dtime, map_timer_and_unload_dtime)) {
        std::vector<v3s16> deleted_blocks;

        // Determine actual block limit to use
        const s32 configured_limit = g_settings->getS32("client_mapblock_limit");
        s32 mapblock_limit;
        if (configured_limit < 0) {
            mapblock_limit = -1;
        } else {
            s32 view_range = g_settings->getS16("viewing_range");
            // Up to a certain limit we want to guarantee that the client can keep
            // a full 360° view loaded in memory without blocks vanishing behind
            // the players back.
            // We use a sphere volume to approximate this. In practice far less
            // blocks will be needed due to occlusion/culling.
            float blocks_range = ceilf(std::min(mapblock_limit_enforce_distance, view_range)
                / (float) MAP_BLOCKSIZE);
            mapblock_limit = (4.f/3.f) * M_PI * powf(blocks_range, 3);
            assert(mapblock_limit > 0);
            mapblock_limit = std::max(mapblock_limit, configured_limit);
            if (mapblock_limit > std::max(configured_limit, m_mapblock_limit_logged)) {
                infostream << "Client: using block limit of " << mapblock_limit
                    << " rather than configured " << configured_limit
                    << " due to view range." << std::endl;
                m_mapblock_limit_logged = mapblock_limit;
            }
        }

        m_env.getMap().timerUpdate(map_timer_and_unload_dtime,
            std::max(g_settings->getFloat("client_unload_unused_data_timeout"), 0.0f),
            mapblock_limit, &deleted_blocks);

        // Send info to server

        auto i = deleted_blocks.begin();
        std::vector<v3s16> sendlist;
        for(;;) {
            if(sendlist.size() == 255 || i == deleted_blocks.end()) {
                if(sendlist.empty())
                    break;
                /*
                    [0] u16 command
                    [2] u8 count
                    [3] v3s16 pos_0
                    [3+6] v3s16 pos_1
                    ...
                */

                sendDeletedBlocks(sendlist);

                sendlist.clear();
            } else {
                sendlist.push_back(*i);
                ++i;
            }
        }
    }

    // Audio engine cleanup
    m_sound->cull();

    // Send input
    sendPlayerPos();

    // Save local db (periodically)
    if (m_localdb) {
        float &interval = m_cache_save_interval;
        interval -= dtime;
        if (interval <= 0.0f) {
            interval = g_settings->getU16("server_map_save_interval");
            infostream << "Local map saving..." << std::endl;
            m_localdb->save();
        }
    }

    // Check if server replied
    /*
        avg_rtt is used to scale some timers. It's not used for anything too
        criticial, so we don't care that it won't be up-to-date while the client
        is loading media.
    */

    if (!m_media_downloader->isFinished() || m_media_downloader->isDownloading()) {
        // Only update if not loaded yet
        if (m_avg_rtt_timer <= 0.0f) {
            m_avg_rtt_timer = 1.0f;
            m_con->setAvgRTT(0.f);
        }
    } else {
        if (m_avg_rtt_timer <= 0.0f) {
            m_avg_rtt_timer = 1.0f;
            m_con->setAvgRTT(m_rtt);
        }
    }
    m_avg_rtt_timer -= dtime;

    /*
        Run scripting
    */
    if (m_mods_loaded) {
        m_script->on_player_receive_fields();
        m_script->on_node_receive_fields();
        m_script->on_chat_message();
        m_script->on_hud_data();
        m_script->on_media_fetch_event();
        m_script->on_media_push_event();
        m_script->on_client_event();
        m_script->on_chat_history_change();
        m_script->on_hud_flags_change();
        m_script->on_player_hp_change();
        m_script->on_player_breath_change();
        m_script->on_player_movement();
        m_script->on_player_fov_change();
        m_script->on_respawn_explicit();
        m_script->on_privs_change();
        m_script->on_animation_frame();
        m_script->on_update_player_list();
        m_script->on_player_eye_offset_change();
        m_script->on_minimap_modes_change();
        m_script->on_light_change();
        m_script->on_camera_change();
    }

    // Handle some of the client events here, especially formspec related ones,
    // as `on_player_receive_fields` might require them.
    // Events should only ever be consumed by one handler.
    while (m_event->hasEvent()) {
        ClientEvent *e = m_event->getEvent();
        bool consumed = false;
        switch (e->type) {
            case CLIENT_EVENT_ACTIVATE_INVENTORY:
                // Inventory activation is also a formspec
                if (!g_menumgr.isMenuOpen())
                    g_menumgr.showInventoryFormspec();
                consumed = true;
                break;
            case CLIENT_EVENT_SEND_INVENTORY_ACTION: {
                InventoryAction *a = (InventoryAction *)e->data;
                sendInventoryAction(a);
                delete a;
                consumed = true;
                break; }
            case CLIENT_EVENT_SEND_INVENTORY_FIELDS: {
                auto fields = (StringMap *)e->data;
                sendInventoryFields(e->name, *fields);
                delete fields;
                consumed = true;
                break; }
            case CLIENT_EVENT_SEND_NODEMETA_FIELDS: {
                auto data = (struct ClientEventNodeMetaFields *)e->data;
                sendNodemetaFields(data->pos, data->formname, data->fields);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_SEND_CHAT_MESSAGE: {
                auto msg = (std::wstring *)e->data;
                sendChatMessage(*msg);
                delete msg;
                consumed = true;
                break; }
            case CLIENT_EVENT_CHANGE_PASSWORD: {
                auto pass_change_data = (struct ClientEventChangePassword *)e->data;
                sendChangePassword(pass_change_data->oldpassword,
                    pass_change_data->newpassword);
                delete pass_change_data;
                consumed = true;
                break; }
            case CLIENT_EVENT_CHANGE_SUBSCRIBE: {
                auto change_subscribe_data = (struct ClientEventChangeSubscribe *)e->data;
                sendSubscribe(change_subscribe_data->subscribe);
                delete change_subscribe_data;
                consumed = true;
                break; }
            case CLIENT_EVENT_RESPAWN: {
                sendRespawnLegacy();
                consumed = true;
                break; }
            case CLIENT_EVENT_READY: {
                sendReady();
                consumed = true;
                break; }
            case CLIENT_EVENT_SET_PLAYER_ITEM: {
                sendPlayerItem(e->item);
                consumed = true;
                break; }
            case CLIENT_EVENT_SET_PLAYER_CONTROL: {
                PlayerControl *pc = (PlayerControl *)e->data;
                setPlayerControl(*pc);
                delete pc;
                consumed = true;
                break; }
            case CLIENT_EVENT_SEND_HAVE_MEDIA: {
                auto tokens = (std::vector<u32> *)e->data;
                sendHaveMedia(*tokens);
                delete tokens;
                consumed = true;
                break; }
            case CLIENT_EVENT_UPDATE_CLIENT_INFO: {
                auto info = (ClientDynamicInfo *)e->data;
                sendUpdateClientInfo(*info);
                delete info;
                consumed = true;
                break; }
            case CLIENT_EVENT_INTERACT: {
                auto data = (struct ClientEventInteract *)e->data;
                interact(data->action, data->pointed_thing);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_SHOW_FORMSPEC: {
                auto fs_data = (struct ClientEventShowFormspec *)e->data;
                g_menumgr.showFormspec(fs_data->formname,
                    fs_data->formspec, fs_data->escapable);
                delete fs_data;
                consumed = true;
                break; }
            case CLIENT_EVENT_DETACHED_INVENTORY: {
                auto di_data = (struct ClientEventDetachedInventory *)e->data;
                g_menumgr.showDetachedInventory(
                    di_data->type, di_data->name, di_data->formspec,
                    *di_data->inv, *di_data->inv_list, di_data->escapable,
                    di_data->player_inv_readonly);
                delete di_data->inv;
                delete di_data->inv_list;
                delete di_data;
                consumed = true;
                break; }
            case CLIENT_EVENT_PLAY_SOUND: {
                ClientEventPlaySound *data = (ClientEventPlaySound *)e->data;
                m_sound->play(data->sound,
                    data->forced_pos ? &data->pos : nullptr,
                    data->gain, data->pitch, data->loop, data->id, data->fade,
                    data->force_relative, data->is_attached, data->attached_to_local_player,
                    data->listener_relative_pos);
                delete data->sound;
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_STOP_SOUND: {
                ClientEventStopSound *data = (ClientEventStopSound *)e->data;
                m_sound->stop(data->id);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_FADE_SOUND: {
                ClientEventFadeSound *data = (ClientEventFadeSound *)e->data;
                m_sound->fade(data->id, data->gain, data->fade_time);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_SCREENCAP: {
                makeScreenshot();
                consumed = true;
                break; }
            case CLIENT_EVENT_DISCONNECT: {
                g_gamecallback->disconnect();
                consumed = true;
                break; }
            case CLIENT_EVENT_SET_CRACK: {
                ClientEventSetCrack *data = (ClientEventSetCrack *)e->data;
                setCrack(data->level, data->pos);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_SET_HUD_PARAM: {
                ClientEventSetHudParam *data = (ClientEventSetHudParam *)e->data;
                m_env.getLocalPlayer()->setHudParam(data->hud_id, data->name, data->value);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_SET_PLAYER_SPRINT: {
                ClientEventSetPlayerSprint *data = (ClientEventSetPlayerSprint *)e->data;
                m_env.getLocalPlayer()->setSprint(data->is_sprinting, data->sprint_status_only);
                delete data;
                consumed = true;
                break; }
            case CLIENT_EVENT_UPDATE_PLAYER_ANIMATION_SPEED: {
                ClientEventUpdatePlayerAnimationSpeed *data = (ClientEventUpdatePlayerAnimationSpeed *)e->data;
                m_env.getLocalPlayer()->setAnimationSpeed(data->speed_factor);
                delete data;
                consumed = true;
                break; }
            default:
                break;
        }
        delete e;
    }

    m_particle_manager->update(dtime, m_env.getLocalPlayer()->getPosition());
    m_rendering_engine->update(dtime);

    // Check if chat queue has some messages to send
    clearOutChatQueue();

    // Check for mesh update results
    MeshUpdateResult result;
    while (m_mesh_update_manager->getNextResult(result)) {
        if (result.ack_to_server)
            sendGotBlocks({result.blockpos});
        ClientMap *map = static_cast<ClientMap*>(&m_env.getMap());
        map->updateMeshes(result);
    }

    // Update map and node timers
    m_env.getMap().update(dtime);

    // Update meshes that need it
    m_env.getMap().remesh(m_rendering_engine->getSceneManager());

    // Update players
    m_env.updatePlayers(dtime);

    // Update crack animation
    m_env.getLocalPlayer()->updateCrack(dtime);

    // Update minimap
    if (m_minimap)
        m_minimap->update(dtime);

    // Update item visuals
    m_item_visuals_manager->update(dtime);

    // Process commands.
    // `m_con->Receive()` adds received packets to `m_received_packets`
    if (m_con)
        m_con->Receive();
    for (NetworkPacket *pkt : m_received_packets)
    {
        ProcessData(pkt);
        delete pkt;
    }
    m_received_packets.clear();

    // Send any pending packets.
    // This implicitly calls Peer::step(dtime) for all peers and sends the
    // packets to them.
    if (m_con)
        m_con->Send();

    // Prune dead peers and timeout old ones.
    if (m_con)
        m_con->prunePeers();

    // This will remove old packets from Peer and clear acked packets
    if (m_con)
        m_con->step(dtime);
}

void Client::sendPlayerPos()
{
    // If the position hasn't changed or there's nothing else to send, don't send.
    // Only send player position if there is something new or if it's time for an update
    LocalPlayer *player = m_env.getLocalPlayer();
    bool pos_changed = player->positionChanged();

    float &interval = m_playerpos_send_timer;
    interval -= m_con->getAvgRTT() > 0 ? m_con->getAvgRTT() : 0.0f;
    bool interval_passed = false;
    if (interval <= 0.0f) {
        interval = player->getPositionSendInterval();
        interval_passed = true;
    }

    if (player->getUpdateOnlyIfMoved() && !pos_changed && !interval_passed)
        return;

    // Position, look direction etc. is stored in player control
    // This is sent every 0.25 seconds by default.
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_PLAYER_POSITION);
    player->writeControl(*pkt, false);
    send(pkt);

    player->resetPositionChanged();
}

void Client::ProcessData(NetworkPacket *pkt)
{
    m_packetcounter.add(pkt->getCommand());
    //infostream << "Client::ProcessData(cmd=" << (int)pkt->getCommand() << ")" << std::endl;

    switch (pkt->getCommand()) {
    case TOCLIENT_NULL: handleCommand_Null(pkt); break;
    case TOCLIENT_DEPRECATED: handleCommand_Deprecated(pkt); break;
    case TOCLIENT_HELLO: handleCommand_Hello(pkt); break;
    case TOCLIENT_AUTH_ACCEPT: handleCommand_AuthAccept(pkt); break;
    case TOCLIENT_ACCEPT_SUDO_MODE: handleCommand_AcceptSudoMode(pkt); break;
    case TOCLIENT_DENY_SUDO_MODE: handleCommand_DenySudoMode(pkt); break;
    case TOCLIENT_ACCESS_DENIED: handleCommand_AccessDenied(pkt); break;
    case TOCLIENT_REMOVE_NODE: handleCommand_RemoveNode(pkt); break;
    case TOCLIENT_ADD_NODE: handleCommand_AddNode(pkt); break;
    case TOCLIENT_NODEMETA_CHANGED: handleCommand_NodemetaChanged(pkt); break;
    case TOCLIENT_BLOCK_DATA: handleCommand_BlockData(pkt); break;
    case TOCLIENT_INVENTORY: handleCommand_Inventory(pkt); break;
    case TOCLIENT_TIME_OF_DAY: handleCommand_TimeOfDay(pkt); break;
    case TOCLIENT_CHAT_MESSAGE: handleCommand_ChatMessage(pkt); break;
    case TOCLIENT_ACTIVE_OBJECT_REMOVE_ADD: handleCommand_ActiveObjectRemoveAdd(pkt); break;
    case TOCLIENT_ACTIVE_OBJECT_MESSAGES: handleCommand_ActiveObjectMessages(pkt); break;
    case TOCLIENT_MOVEMENT: handleCommand_Movement(pkt); break;
    case TOCLIENT_FOV: handleCommand_Fov(pkt); break;
    case TOCLIENT_HP: handleCommand_HP(pkt); break;
    case TOCLIENT_BREATH: handleCommand_Breath(pkt); break;
    case TOCLIENT_MOVE_PLAYER: handleCommand_MovePlayer(pkt); break;
    case TOCLIENT_MOVE_PLAYER_REL: handleCommand_MovePlayerRel(pkt); break;
    case TOCLIENT_DEATH_SCREEN_LEGACY: handleCommand_DeathScreenLegacy(pkt); break;
    case TOCLIENT_ANNOUNCE_MEDIA: handleCommand_AnnounceMedia(pkt); break;
    case TOCLIENT_MEDIA: handleCommand_Media(pkt); break;
    case TOCLIENT_NODE_DEF: handleCommand_NodeDef(pkt); break;
    case TOCLIENT_ITEM_DEF: handleCommand_ItemDef(pkt); break;
    case TOCLIENT_PLAY_SOUND: handleCommand_PlaySound(pkt); break;
    case TOCLIENT_STOP_SOUND: handleCommand_StopSound(pkt); break;
    case TOCLIENT_FADE_SOUND: handleCommand_FadeSound(pkt); break;
    case TOCLIENT_PRIVILEGES: handleCommand_Privileges(pkt); break;
    case TOCLIENT_INVENTORY_FORM_SPEC: handleCommand_InventoryFormSpec(pkt); break;
    case TOCLIENT_DETACHED_INVENTORY: handleCommand_DetachedInventory(pkt); break;
    case TOCLIENT_SHOW_FORMSPEC: handleCommand_ShowFormSpec(pkt); break;
    case TOCLIENT_SPAWN_PARTICLE: handleCommand_SpawnParticle(pkt); break;
    case TOCLIENT_ADD_PARTICLE_SPAWNER: handleCommand_AddParticleSpawner(pkt); break;
    case TOCLIENT_DELETE_PARTICLE_SPAWNER: handleCommand_DeleteParticleSpawner(pkt); break;
    case TOCLIENT_HUD_ADD: handleCommand_HudAdd(pkt); break;
    case TOCLIENT_HUD_REMOVE: handleCommand_HudRemove(pkt); break;
    case TOCLIENT_HUD_CHANGE: handleCommand_HudChange(pkt); break;
    case TOCLIENT_HUD_SET_FLAGS: handleCommand_HudSetFlags(pkt); break;
    case TOCLIENT_HUD_SET_PARAM: handleCommand_HudSetParam(pkt); break;
    case TOCLIENT_HUD_SET_SKY: handleCommand_HudSetSky(pkt); break;
    case TOCLIENT_HUD_SET_SUN: handleCommand_HudSetSun(pkt); break;
    case TOCLIENT_HUD_SET_MOON: handleCommand_HudSetMoon(pkt); break;
    case TOCLIENT_HUD_SET_STARS: handleCommand_HudSetStars(pkt); break;
    case TOCLIENT_CLOUD_PARAMS: handleCommand_CloudParams(pkt); break;
    case TOCLIENT_OVERRIDE_DAY_NIGHT_RATIO: handleCommand_OverrideDayNightRatio(pkt); break;
    case TOCLIENT_LOCAL_PLAYER_ANIMATIONS: handleCommand_LocalPlayerAnimations(pkt); break;
    case TOCLIENT_EYE_OFFSET: handleCommand_EyeOffset(pkt); break;
    case TOCLIENT_UPDATE_PLAYER_LIST: handleCommand_UpdatePlayerList(pkt); break;
    case TOCLIENT_MOD_CHANNEL_MSG: handleCommand_ModChannelMsg(pkt); break;
    case TOCLIENT_MOD_CHANNEL_SIGNAL: handleCommand_ModChannelSignal(pkt); break;
    case TOCLIENT_SRP_BYTES_S_AND_B: handleCommand_SrpBytesSandB(pkt); break;
    case TOCLIENT_FORMSPEC_PREPEND: handleCommand_FormspecPrepend(pkt); break;
    case TOCLIENT_CSM_RESTRICTION_FLAGS: handleCommand_CSMRestrictionFlags(pkt); break;
    case TOCLIENT_PLAYER_SPEED: handleCommand_PlayerSpeed(pkt); break;
    case TOCLIENT_MEDIA_PUSH: handleCommand_MediaPush(pkt); break;
    case TOCLIENT_MINIMAP_MODES: handleCommand_MinimapModes(pkt); break;
    case TOCLIENT_SET_LIGHTING: handleCommand_SetLighting(pkt); break;
    case TOCLIENT_CAMERA: handleCommand_Camera(pkt); break;
    default: {
        warningstream << "Client::ProcessData(): Unknown command (0x" << std::hex
                << (int)pkt->getCommand() << ")" << std::dec << std::endl;
        break; }
    }
}

void Client::Send(NetworkPacket* pkt)
{
    sanity_check(m_con != nullptr);
    m_con->Send(pkt);
}

void Client::interact(InteractAction action, const PointedThing &pointed)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_INTERACT);
    pkt->writeU8(action);

    if (pointed.type == PT_NOTHING) {
        pkt->writeU8(0);
    } else if (pointed.type == PT_NODE) {
        pkt->writeU8(1);
        pkt->writeU32(pointed.version);
        pkt->writeV3s16(pointed.pos);
        pkt->writeU8(pointed.face);
        pkt->writeV3f(pointed.intersect);
        pkt->writeBool(pointed.controls);
        pkt->writeS16(pointed.wield_item);
    } else if (pointed.type == PT_OBJECT) {
        pkt->writeU8(2);
        pkt->writeU32(pointed.version);
        pkt->writeU16(pointed.id);
        pkt->writeV3f(pointed.intersect);
        pkt->writeBool(pointed.controls);
        pkt->writeS16(pointed.wield_item);
    } else {
        // Invalid pointer type
        delete pkt;
        return;
    }
    send(pkt);
}

void Client::sendNodemetaFields(v3s16 p, const std::string &formname,
        const StringMap &fields)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_NODEMETA_FIELDS);
    pkt->writeV3s16(p);
    pkt->writeString(formname);
    pkt->writeStringMap(fields);
    send(pkt);
}

void Client::sendInventoryFields(const std::string &formname,
        const StringMap &fields)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_INVENTORY_FIELDS);
    pkt->writeString(formname);
    pkt->writeStringMap(fields);
    send(pkt);
}

void Client::sendInventoryAction(InventoryAction *a)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_INVENTORY_ACTION);
    a->writeToPacket(pkt);
    send(pkt);
}

void Client::sendChatMessage(const std::wstring &message)
{
    // Protect against sending too many chat messages
    if (!canSendChatMessage()) {
        std::string str = gettext("Too many messages sent, ignoring.");
        ChatMessage *msg = new ChatMessage(CHAT_MESSAGE_TYPE_SYSTEM,
                str, false, false);
        g_chat_logger.log(LL_INFO, str);
        pushToChatQueue(msg);
        return;
    }

    // Only send new message if message content changed (not when input is repeated with arrow up/down)
    // Note that previous_chat_message is wide string and message might be utf8
    if (m_env.getLocalPlayer()->previous_chat_message.length() > 0 && m_env.getLocalPlayer()->previous_chat_message == message)
        return;

    // Cut message if it's too long
    std::wstring cut_message = message;
    unsigned max_size = g_settings->getU16("chat_message_max_size");
    if (cut_message.length() > max_size) {
        cut_message.resize(max_size);
        std::string str = gettext("Chat message too long, cutting.");
        ChatMessage *msg = new ChatMessage(CHAT_MESSAGE_TYPE_SYSTEM,
                str, false, false);
        g_chat_logger.log(LL_INFO, str);
        pushToChatQueue(msg);
    }

    NetworkPacket *pkt = new NetworkPacket(TOSERVER_CHAT_MESSAGE);
    pkt->writeString(wide_to_utf8(cut_message));
    send(pkt);

    m_last_chat_message_sent = time(NULL);
    m_chat_messages_sent.add(1);
}

void Client::clearOutChatQueue()
{
    while (!m_chat_queue.empty()) {
        ChatMessage *msg = m_chat_queue.front();
        g_menumgr.addChatMessage(msg);
        m_chat_queue.pop();
    }
}

void Client::sendChangePassword(const std::string &oldpassword,
        const std::string &newpassword)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_CHANGE_PASSWORD);
    pkt->writeString(oldpassword);
    pkt->writeString(newpassword);
    send(pkt);
}

void Client::sendDamage(u16 damage)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_DAMAGE);
    pkt->writeU16(damage);
    send(pkt);
}

void Client::sendRespawnLegacy()
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_RESPAWN_LEGACY);
    send(pkt);
}

void Client::sendReady()
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_CLIENT_READY);
    send(pkt);
}

void Client::sendHaveMedia(const std::vector<u32> &tokens)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_HAVE_MEDIA);
    pkt->writeU16(tokens.size());
    for (u32 token : tokens) {
        pkt->writeU32(token);
    }
    send(pkt);
}

void Client::sendUpdateClientInfo(const ClientDynamicInfo &info)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_CLIENT_INFO);
    pkt->writeU8(info.client_version);
    pkt->writeU16(info.node_highlighting_mode);
    pkt->writeU16(info.min_viewing_range);
    pkt->writeFloat(info.fov);
    pkt->writeBool(info.enable_sound);
    pkt->writeBool(info.enable_music);
    pkt->writeBool(info.new_style_water);
    pkt->writeBool(info.new_style_lava);
    pkt->writeU16(info.movement_speed_factor);
    pkt->writeU16(info.jump_speed_factor);
    pkt->writeU16(info.sneak_speed_factor);
    pkt->writeU16(info.dig_time_factor);
    pkt->writeU16(info.build_time_factor);
    pkt->writeU16(info.place_distance_factor);
    pkt->writeU16(info.drop_distance_factor);
    pkt->writeU16(info.reach_distance_factor);
    send(pkt);
}

ClientEnvironment& Client::getEnv() { return m_env; }

ITextureSource *Client::tsrc() { return m_tsrc; }

ISoundManager *Client::sound() { return m_sound; }

scene::ISceneManager *Client::getSceneManager()
{
    return m_rendering_engine->getSceneManager();
}

IItemDefManager* Client::getItemDefManager() { return m_itemdef; }

const NodeDefManager* Client::getNodeDefManager() { return m_nodedef; }

ICraftDefManager* Client::getCraftDefManager() { return m_itemdef; }

ITextureSource* Client::getTextureSource() { return m_tsrc; }

IWritableShaderSource* Client::getShaderSource() { return m_shsrc; }

u16 Client::allocateUnknownNodeId(const std::string &name)
{
    return m_nodedef->allocateUnknownNodeId(name);
}

ISoundManager* Client::getSoundManager() { return m_sound; }

MtEventManager* Client::getEventManager() { return m_event; }

ParticleManager* Client::getParticleManager() { return m_particle_manager.get(); }

scene::IAnimatedMesh* Client::getMesh(const std::string &filename, bool cache)
{
    return m_rendering_engine->getMesh(filename, cache);
}

const std::string* Client::getModFile(std::string filename)
{
    return m_mod_vfs.get(filename);
}

void Client::migrateModStorage()
{
    std::string mod_storage_dir = porting::path_user + DIR_DELIM + "client";
    std::string old_mod_storage = mod_storage_dir + DIR_DELIM + "mod_storage";
    if (fs::IsDir(old_mod_storage)) {
        infostream << "Migrating client mod storage to SQLite3 database" << std::endl;
        {
            ModStorageDatabaseFiles files_db(mod_storage_dir);
            std::vector<std::string> mod_list;
            files_db.listMods(&mod_list);
            for (const std::string &modname : mod_list) {
                infostream << "Migrating client mod storage for mod " << modname << std::endl;
                StringMap meta;
                files_db.getModEntries(modname, &meta);
                for (const auto &pair : meta) {
                    m_mod_storage_database->setModEntry(modname, pair.first, pair.second);
                }
            }
        }
        if (!fs::Rename(old_mod_storage, old_mod_storage + ".bak")) {
            // Execution cannot move forward if the migration does not complete.
            throw BaseException("Could not finish migrating client mod storage");
        }
        infostream << "Finished migration of client mod storage" << std::endl;
    }
}

LocalClientState Client::getState() { return m_state; }

float Client::mediaReceiveProgress()
{
    return m_media_downloader->get_progress();
}

void Client::drawLoadScreen(const std::wstring &text, float dtime, int percent)
{
    m_rendering_engine->drawLoadScreen(text, dtime, percent);
}

void Client::afterContentReceived()
{
    m_state = LC_Ready;

    // Init scripting here if the client is ready
    if (m_mods_loaded)
        m_script->on_client_ready(m_env.getLocalPlayer());
    if (m_camera)
        m_script->on_camera_ready(m_camera);
    if (m_minimap)
        m_script->on_minimap_ready(m_minimap);

    // Call client scripting callback
    // (this cannot be done while loading media due to script callbacks
    // calling functions like get_map_node, which only works if the
    // server is sending maps)
    if (m_script)
        m_script->after_content_received();
}

void Client::showUpdateProgressTexture(void *args, u32 progress, u32 max_progress)
{
    Client *client = static_cast<Client*>(args);
    client->drawLoadScreen(L"Downloading media...", 0.0, 0);
}

float Client::getRTT() { return m_rtt; }

float Client::getCurRate() { return m_cur_rate; }

u64 Client::getMapSeed() const { return m_map_seed; }

void Client::addUpdateMeshTask(v3s16 blockpos, bool ack_to_server, bool urgent)
{
    m_mesh_update_manager->add(blockpos, ack_to_server, urgent);
}

void Client::addUpdateMeshTaskWithEdge(v3s16 blockpos, bool ack_to_server, bool urgent)
{
    for (s32 y_offset = -1; y_offset <= 1; y_offset++) {
        for (s32 x_offset = -1; x_offset <= 1; x_offset++) {
            for (s32 z_offset = -1; z_offset <= 1; z_offset++) {
                // Make sure that center block itself is urgent, while surrounding is not
                // for avoiding long lags in game
                addUpdateMeshTask(blockpos + v3s16(x_offset, y_offset, z_offset),
                        false, (x_offset == 0 && y_offset == 0 && z_offset == 0) && urgent);
            }
        }
    }
}

void Client::addUpdateMeshTaskForNode(v3s16 nodepos, bool ack_to_server, bool urgent)
{
    addUpdateMeshTask(getMapBlockPos(nodepos), ack_to_server, urgent);
}

ClientEvent *Client::getClientEvent()
{
    sanity_check(hasClientEvents());
    ClientEvent *e = m_client_event_queue.front();
    m_client_event_queue.pop();
    return e;
}

void Client::setPlayerControl(PlayerControl &control)
{
    m_env.getLocalPlayer()->setControl(control);
}

bool Client::updateWieldedItem()
{
    return m_env.getLocalPlayer()->updateWieldedItem();
}

Inventory* Client::getInventory(const InventoryLocation &loc)
{
    return m_env.getInventory(loc);
}

void Client::inventoryAction(InventoryAction *a)
{
    // if it is a local event (i.e. from client side), just add it to queue and let it be processed later
    if (a->isLocal()) {
        m_event->addEvent(new ClientEvent(CLIENT_EVENT_SEND_INVENTORY_ACTION, a));
    } else {
        sendInventoryAction(a);
    }
}

void Client::setPlayerItem(u16 item)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_SET_Wielded_ITEM);
    pkt->writeU16(item);
    send(pkt);
}

int Client::getCrackLevel()
{
    return m_env.getLocalPlayer()->getCrackLevel();
}

v3s16 Client::getCrackPos()
{
    return m_env.getLocalPlayer()->getCrackPos();
}

void Client::setCrack(int level, v3s16 pos)
{
    m_env.getLocalPlayer()->setCrack(level, pos);
}

u16 Client::getHP()
{
    return m_env.getLocalPlayer()->getHP();
}

bool Client::canSendChatMessage() const
{
    /*
        A simple mechanism to prevent clients from flooding the server.
        This is client-side only.
    */
    const float limit = g_settings->getFloat("chat_message_limit_per_10sec");
    if (limit == 0)
        return true;

    const float time_since_last = time(NULL) - m_last_chat_message_sent;

    // Use integer math to prevent floating point inaccuracies from messing with the limit.
    const u32 time_in_10s_since_epoch = time(NULL) / 10;
    // Clear count of messages sent in last 10 seconds if a new 10 second period started.
    if (time_in_10s_since_epoch != m_chat_messages_sent_epoch)
    {
        m_chat_messages_sent.clear();
        m_chat_messages_sent_epoch = time_in_10s_since_epoch;
    }

    if (m_chat_messages_sent.sum() >= limit)
        return false;

    // This prevents new messages from appearing in chat when the last message sent
    // was very recent (so that `chat_message_limit_per_10sec` won't be violated)
    // when this message is sent.
    return time_since_last >= 10.0f / limit;
}

void Client::typeChatMessage(const std::wstring &message)
{
    m_env.getLocalPlayer()->previous_chat_message = message;
}

bool Client::getChatMessage(std::wstring &message)
{
    if (m_chat_input_queue.empty())
        return false;
    message = m_chat_input_queue.front();
    m_chat_input_queue.pop();
    return true;
}

bool Client::joinModChannel(const std::string &channel)
{
    return m_modchannel_mgr->join(channel);
}

bool Client::leaveModChannel(const std::string &channel)
{
    return m_modchannel_mgr->leave(channel);
}

bool Client::sendModChannelMessage(const std::string &channel,
            const std::string &message)
{
    return m_modchannel_mgr->send(channel, message);
}

ModChannel *Client::getModChannel(const std::string &channel)
{
    return m_modchannel_mgr->get(channel);
}

const std::string &Client::getFormspecPrepend() const
{
    return m_formspec_prepend;
}

void Client::pushToEventQueue(ClientEvent *event)
{
    m_client_event_queue.push(event);
}

const Address Client::getServerAddress()
{
    return m_con->getPeerAddress();
}

void Client::deleteAuthData()
{
    delete m_auth_srp_server_ephemeral_key;
    m_auth_srp_server_ephemeral_key = nullptr;
}

void Client::sendInit(const std::string &playerName)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_INIT);
    pkt->writeU16(PROTOCOL_VERSION);
    pkt->writeString(playerName);
    pkt->writeString(MINETEST_VERSION_STRING);
    pkt->writeString(MINETEST_APP_NAME);

    std::string mods_checksum = ModManager::getModsChecksum();
    if (!mods_checksum.empty())
        pkt->writeString(mods_checksum);

    send(pkt);
    m_state = LC_Init;
}

static void srp_salt_and_b_callback(void *user_data, const std::string &salt_string, const std::string &B_string)
{
    Client *client = static_cast<Client *>(user_data);

    NetworkPacket *pkt = new NetworkPacket(TOSERVER_AUTH_CONTINUE);
    pkt->writeU8(AUTH_MECHANISM_SRP);
    pkt->writeString(salt_string);
    pkt->writeString(B_string);
    client->Send(pkt);
}

void Client::startAuth(AuthMechanism chosen_auth_mechanism)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_AUTH_START);
    pkt->writeU8(chosen_auth_mechanism);
    m_chosen_auth_mech = chosen_auth_mechanism;

    send(pkt);
}

void Client::sendDeletedBlocks(std::vector<v3s16> &blocks)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_GOT_BLOCKS);
    pkt->writeU8(blocks.size());
    for (v3s16 p : blocks)
        pkt->writeV3s16(p);
    send(pkt);
}

void Client::sendGotBlocks(const std::vector<v3s16> &blocks)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_GOT_BLOCKS);
    pkt->writeU8(blocks.size());
    for (v3s16 p : blocks)
        pkt->writeV3s16(p);
    send(pkt);
}

void Client::sendRemovedSounds(const std::vector<s32> &soundList)
{
    NetworkPacket *pkt = new NetworkPacket(TOSERVER_SOUND_REMOVE);
    pkt->writeU16(soundList.size());
    for (s32 id : soundList)
        pkt->writeS32(id);
    send(pkt);
}

void Client::initLocalMapSaving(const Address &address, const std::string &hostname)
{
    // Only enable local map saving for localhost singleplayer
    if (!g_settings->getBool("enable_local_map_saving") ||
            !address.isLocalhost() || address.getPort() != g_settings->getU16("port")) {
        m_localdb = nullptr;
        return;
    }

    std::string db_name = hostname;
    if (db_name.empty()) {
        db_name = "localhost";
    }
    std::string db_path = porting::path_user + DIR_DELIM + "worlds" + DIR_DELIM + db_name;

    infostream << "Local map saving started to " << db_path << std::endl;
    m_localdb.reset(new ModStorageDatabaseSQLite3(db_path, "map.sqlite"));
    m_localdb->beginSave();
}
