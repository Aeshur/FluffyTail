/**
 * FluffyTail - shared actor and Direct3D8 render runtime.
 *
 * Copyright (c) 2026 Aeshur. GNU LGPL v3. See LICENSE.md.
 */

#pragma once

#include <Windows.h>
#include <d3d8.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace fluffytail
{

struct indexed_draw_args
{
    D3DPRIMITIVETYPE primitive_type;
    UINT             min_index;
    UINT             num_vertices;
    UINT             start_index;
    UINT             primitive_count;
};

using indexed_draw_fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*,
                                                    D3DPRIMITIVETYPE,
                                                    UINT,
                                                    UINT,
                                                    UINT,
                                                    UINT);

struct draw_result
{
    bool    handled;
    HRESULT result;
};

class render_runtime final
{
    using actor_draw_fn = void(__thiscall*)(void*);

    struct actor_snapshot
    {
        bool    valid;
        uint8_t race;
        uint8_t hair;
    };

    static std::atomic<render_runtime*> instance_;
    static std::atomic_uint             hook_entries_;
    static thread_local uint32_t        actor_hook_depth_;
    static thread_local uint32_t        draw_depth_;
    static thread_local actor_snapshot  actor_snapshot_;

    uintptr_t        actor_draw_address_;
    uint8_t          actor_draw_original_[6];
    uint8_t          actor_draw_patch_[6];
    void*            actor_draw_trampoline_;
    std::atomic_bool stopping_;

    IDirect3DDevice8* device_;
    IDirect3DDevice8* device_identity_;
    DWORD             owner_thread_id_;
    bool              reset_pending_;

    static constexpr size_t                                   REPLACEMENT_TEXTURE_COUNT = 6;
    std::array<IDirect3DTexture8*, REPLACEMENT_TEXTURE_COUNT> replacement_textures_;
    uint32_t                                                  detection_frame_;
    std::unordered_set<IDirect3DBaseTexture8*>                tail_textures_;
    std::unordered_set<IDirect3DBaseTexture8*>                rejected_textures_;

    auto                   release_textures() -> void;
    auto                   release_tail_texture_cache() -> void;
    auto                   is_tail_texture(IDirect3DBaseTexture8* texture) -> bool;
    auto                   owner_thread() const noexcept -> bool;
    auto                   replacement_texture(const char* name) const noexcept -> IDirect3DTexture8*;
    static void __fastcall actor_draw_hook(void* actor, void* edx) noexcept;

public:
    render_runtime();
    ~render_runtime();

    render_runtime(const render_runtime&)                    = delete;
    render_runtime(render_runtime&&)                         = delete;
    auto operator=(const render_runtime&) -> render_runtime& = delete;
    auto operator=(render_runtime&&) -> render_runtime&      = delete;

    auto install_actor_draw_hook(uintptr_t address) -> bool;
    // Hook installation, removal, and device lifecycle are serialized on the
    // owner thread recorded by the first successful lifecycle operation.
    auto remove_actor_draw_hook() -> bool;
    auto owns_actor_draw_hook() const -> bool;

    auto initialize_device(IDirect3DDevice8* device) -> bool;
    // Device lifecycle and draw calls are render-thread-owned. A reset makes
    // the runtime unavailable until initialize_device succeeds again.
    auto before_device_reset() -> void;
    auto release_device() -> void;
    auto device_ready() const -> bool;
    auto begin_frame() -> void;

    auto try_draw_indexed(const indexed_draw_args& args, indexed_draw_fn downstream) noexcept
        -> draw_result;
    // Use this overload when the host supplies its device explicitly; identity
    // is checked against the device accepted by initialize_device.
    auto try_draw_indexed(IDirect3DDevice8*        device,
                          const indexed_draw_args& args,
                          indexed_draw_fn          downstream) noexcept -> draw_result;
    auto shutdown() -> bool;
};

} // namespace fluffytail
