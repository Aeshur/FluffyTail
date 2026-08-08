/**
 * FluffyTail - per-face Mithra tail colour plugin for Ashita v4.3.
 *
 * Derived from the Ashita v4 example plugin (src/exampleplugin.hpp),
 * Copyright (c) 2025 Ashita Development Team, GNU LGPL v3. Modifications for
 * per-face Mithra tail colour by Aeshur, under the same licence.
 * See LICENSE.md / LICENSES.md.
 */

#pragma once

#include "defines.hpp"

namespace fluffytail
{

class plugin final : public IPlugin
{
    IAshitaCore*      core_;
    IDirect3DDevice8* device_;

    using actor_draw_fn = void(__thiscall*)(void*);

    static plugin*          instance_;
    uintptr_t               actor_draw_address_;
    uint8_t                 actor_draw_original_[6];
    void*                   actor_draw_trampoline_;
    Ashita::FFXI::entity_t* current_entity_;
    uint32_t                detection_frame_;
    bool                    submitting_tail_draw_;

    std::unordered_map<std::string, IDirect3DTexture8*> textures_;
    std::unordered_set<IDirect3DBaseTexture8*>          tail_textures_;
    std::unordered_set<IDirect3DBaseTexture8*>          rejected_textures_;

    auto                   release_textures(void) -> void;
    auto                   release_tail_texture_cache(void) -> void;
    auto                   is_tail_texture(IDirect3DBaseTexture8* texture) -> bool;
    auto                   install_actor_draw_hook(uintptr_t address) -> bool;
    auto                   remove_actor_draw_hook(void) -> void;
    static void __fastcall actor_draw_hook(void* actor, void* edx);

public:
    plugin(void);
    ~plugin(void) override;

    auto GetName(void) const -> const char* override;
    auto GetAuthor(void) const -> const char* override;
    auto GetDescription(void) const -> const char* override;
    auto GetLink(void) const -> const char* override;
    auto GetVersion(void) const -> double override;
    auto GetInterfaceVersion(void) const -> double override;
    auto GetPriority(void) const -> int32_t override;
    auto GetFlags(void) const -> uint32_t override;

    auto Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool override;
    auto Release(void) -> void override;

    auto HandleEvent(const char* event_name, const void* event_data, const uint32_t event_size)
        -> void override;

    auto HandleCommand(int32_t mode, const char* command, bool injected) -> bool override;
    auto HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modified_mode, bool* modified_indent, char* modified_message, bool injected, bool blocked)
        -> bool override;
    auto HandleOutgoingText(int32_t mode, const char* message, int32_t* modified_mode, char* modified_message, bool injected, bool blocked)
        -> bool override;

    auto HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t size_chunk, const uint8_t* data_chunk, bool injected, bool blocked) -> bool override;
    auto HandleOutgoingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t size_chunk, const uint8_t* data_chunk, bool injected, bool blocked) -> bool override;

    auto Direct3DInitialize(IDirect3DDevice8* device) -> bool override;
    auto Direct3DBeginScene(bool is_rendering_back_buffer) -> void override;
    auto Direct3DEndScene(bool is_rendering_back_buffer) -> void override;
    auto Direct3DPresent(const RECT* p_source_rect, const RECT* p_dest_rect, HWND h_dest_window_override, const RGNDATA* p_dirty_region)
        -> void override;
    auto Direct3DSetRenderState(D3DRENDERSTATETYPE state, DWORD* value) -> bool override;
    auto Direct3DDrawPrimitive(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count) -> bool override;
    auto Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, UINT min_index, UINT num_vertices, UINT start_index, UINT primitive_count)
        -> bool override;
    auto Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, CONST void* vertex_stream_zero_data, UINT vertex_stream_zero_stride) -> bool override;
    auto Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT min_vertex_index, UINT num_vertex_indices, UINT primitive_count, CONST void* index_data, D3DFORMAT index_data_format, CONST void* vertex_stream_zero_data, UINT vertex_stream_zero_stride) -> bool override;
};

} // namespace fluffytail
