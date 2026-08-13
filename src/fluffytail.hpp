/**
 * FluffyTail - Ashita v4.3 host adapter.
 *
 * Derived from the Ashita v4 example plugin (src/exampleplugin.hpp),
 * Copyright (c) 2025 Ashita Development Team, GNU LGPL v3. Modifications for
 * per-face Mithra tail colour by Aeshur, under the same licence.
 * See LICENSE.md / LICENSES.md.
 */

#pragma once

#include "defines.hpp"
#include "render_runtime.hpp"

#include <memory>

namespace fluffytail
{

class ashita_plugin final : public IPlugin
{
    IAshitaCore*                    core_;
    std::unique_ptr<render_runtime> runtime_;

public:
    ashita_plugin();
    ~ashita_plugin() override;

    auto GetName() const -> const char* override;
    auto GetAuthor() const -> const char* override;
    auto GetDescription() const -> const char* override;
    auto GetLink() const -> const char* override;
    auto GetVersion() const -> double override;
    auto GetInterfaceVersion() const -> double override;
    auto GetPriority() const -> int32_t override;
    auto GetFlags() const -> uint32_t override;

    auto Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool override;
    auto Release() -> void override;

    auto HandleEvent(const char* event_name,
                     const void* event_data,
                     uint32_t    event_size) -> void override;
    auto HandleCommand(int32_t mode, const char* command, bool injected) -> bool override;
    auto HandleIncomingText(int32_t     mode,
                            bool        indent,
                            const char* message,
                            int32_t*    modified_mode,
                            bool*       modified_indent,
                            char*       modified_message,
                            bool        injected,
                            bool        blocked) -> bool override;
    auto HandleOutgoingText(int32_t     mode,
                            const char* message,
                            int32_t*    modified_mode,
                            char*       modified_message,
                            bool        injected,
                            bool        blocked) -> bool override;
    auto HandleIncomingPacket(uint16_t       id,
                              uint32_t       size,
                              const uint8_t* data,
                              uint8_t*       modified,
                              uint32_t       size_chunk,
                              const uint8_t* data_chunk,
                              bool           injected,
                              bool           blocked) -> bool override;
    auto HandleOutgoingPacket(uint16_t       id,
                              uint32_t       size,
                              const uint8_t* data,
                              uint8_t*       modified,
                              uint32_t       size_chunk,
                              const uint8_t* data_chunk,
                              bool           injected,
                              bool           blocked) -> bool override;

    auto Direct3DInitialize(IDirect3DDevice8* device) -> bool override;
    auto Direct3DBeginScene(bool is_rendering_back_buffer) -> void override;
    auto Direct3DEndScene(bool is_rendering_back_buffer) -> void override;
    auto Direct3DPresent(const RECT*    source_rect,
                         const RECT*    dest_rect,
                         HWND           dest_window_override,
                         const RGNDATA* dirty_region) -> void override;
    auto Direct3DSetRenderState(D3DRENDERSTATETYPE state, DWORD* value) -> bool override;
    auto Direct3DDrawPrimitive(D3DPRIMITIVETYPE primitive_type,
                               UINT             start_vertex,
                               UINT             primitive_count) -> bool override;
    auto Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type,
                                      UINT             min_index,
                                      UINT             num_vertices,
                                      UINT             start_index,
                                      UINT             primitive_count) -> bool override;
    auto Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type,
                                 UINT             primitive_count,
                                 const void*      vertex_stream_zero_data,
                                 UINT             vertex_stream_zero_stride) -> bool override;
    auto Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type,
                                        UINT             min_vertex_index,
                                        UINT             num_vertex_indices,
                                        UINT             primitive_count,
                                        const void*      index_data,
                                        D3DFORMAT        index_data_format,
                                        const void*      vertex_stream_zero_data,
                                        UINT             vertex_stream_zero_stride) -> bool override;
};

} // namespace fluffytail
