/**
 * FluffyTail - shared actor and Direct3D8 render runtime.
 *
 * Copyright (c) 2026 Aeshur. GNU LGPL v3. See LICENSE.md.
 */

#include "render_runtime.hpp"

#include "hook_utils.hpp"
#include "tail_policy.hpp"

#include <cstring>
#include <limits>

namespace
{

constexpr size_t ACTOR_ENTITY_OFFSET = 0x70;
constexpr size_t ENTITY_RACE_OFFSET  = 0xEF;
constexpr size_t ENTITY_LOOK_OFFSET  = 0xFC;

struct raw_actor_snapshot
{
    bool    valid;
    uint8_t race;
    uint8_t hair;
};

auto read_raw_actor_snapshot(void* actor, raw_actor_snapshot& out) noexcept -> bool
{
    out = { false, 0, 0 };
    if (actor == nullptr)
        return false;

    __try
    {
        auto* entity = *reinterpret_cast<void**>(
            static_cast<uint8_t*>(actor) + ACTOR_ENTITY_OFFSET);
        if (entity == nullptr)
            return false;

        const auto* bytes = static_cast<const uint8_t*>(entity);
        uint16_t    look{};
        CopyMemory(&look, bytes + ENTITY_LOOK_OFFSET, sizeof(look));
        out = { true, bytes[ENTITY_RACE_OFFSET], static_cast<uint8_t>(look & 0xFF) };
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        out = { false, 0, 0 };
        return false;
    }
}

template <typename T>
auto safe_release(T*& resource) -> void
{
    if (resource != nullptr)
    {
        auto* owned = resource;
        resource    = nullptr;
        try
        {
            owned->Release();
        }
        catch (...)
        {
        }
    }
}

auto is_tail_block(const uint8_t* block) -> bool
{
    for (size_t index = 0; index < 8; ++index)
    {
        if (block[index] != 0xFF)
            return false;
    }
    for (size_t index = 12; index < 16; ++index)
    {
        if (block[index] != 0)
            return false;
    }

    uint16_t endpoint{};
    uint16_t other{};
    CopyMemory(&endpoint, block + 8, sizeof(endpoint));
    CopyMemory(&other, block + 10, sizeof(other));
    return other == 0xFFFF && endpoint == fluffytail::rgb565(fluffytail::BASELINE_RGB);
}

struct texture_lock_scope final
{
    IDirect3DTexture8* texture;
    bool               locked;

    auto unlock() noexcept -> HRESULT
    {
        if (!locked)
            return D3D_OK;
        HRESULT result = D3DERR_DRIVERINTERNALERROR;
        try
        {
            result = texture->UnlockRect(0);
            if (SUCCEEDED(result))
                locked = false;
        }
        catch (...)
        {
        }
        return result;
    }

    ~texture_lock_scope() noexcept
    {
        for (size_t attempt = 0; locked && attempt < 2; ++attempt)
            (void)this->unlock();
    }
};

auto has_tail_fingerprint(IDirect3DBaseTexture8* source_texture) -> bool
{
    constexpr size_t TEXTURE_WIDTH   = 256;
    constexpr size_t TEXTURE_HEIGHT  = 256;
    constexpr size_t BLOCK_DIMENSION = 4;
    constexpr size_t BLOCK_SIZE      = 16;
    constexpr size_t BLOCKS_PER_ROW  = TEXTURE_WIDTH / BLOCK_DIMENSION;
    constexpr size_t BLOCK_ROWS      = TEXTURE_HEIGHT / BLOCK_DIMENSION;

    if (source_texture == nullptr || source_texture->GetType() != D3DRTYPE_TEXTURE)
        return false;

    auto*           texture = static_cast<IDirect3DTexture8*>(source_texture);
    D3DSURFACE_DESC desc{};
    if (FAILED(texture->GetLevelDesc(0, &desc)) || desc.Width != TEXTURE_WIDTH ||
        desc.Height != TEXTURE_HEIGHT || desc.Format != D3DFMT_DXT3)
        return false;

    D3DLOCKED_RECT rect{};
    if (FAILED(texture->LockRect(0, &rect, nullptr, D3DLOCK_READONLY)))
        return false;
    texture_lock_scope lockScope{ texture, true };

    const auto* first   = static_cast<const uint8_t*>(rect.pBits);
    bool        matches = first != nullptr &&
                          rect.Pitch >= static_cast<int32_t>(BLOCK_SIZE * BLOCKS_PER_ROW) &&
                          is_tail_block(first);
    for (size_t row = 0; matches && row < BLOCK_ROWS; ++row)
    {
        const auto* scan = first + (row * rect.Pitch);
        for (size_t column = 0; column < BLOCKS_PER_ROW; ++column)
        {
            if (std::memcmp(scan + (column * BLOCK_SIZE), first, BLOCK_SIZE) != 0)
            {
                matches = false;
                break;
            }
        }
    }
    return SUCCEEDED(lockScope.unlock()) && matches;
}

auto make_texture(IDirect3DDevice8* device, uint32_t rgb) -> IDirect3DTexture8*
{
    constexpr uint32_t TEXTURE_SIZE = 8;
    if (device == nullptr)
        return nullptr;

    IDirect3DTexture8* texture = nullptr;
    if (FAILED(device->CreateTexture(TEXTURE_SIZE,
                                     TEXTURE_SIZE,
                                     1,
                                     0,
                                     D3DFMT_A8R8G8B8,
                                     D3DPOOL_MANAGED,
                                     &texture)))
        return nullptr;

    D3DLOCKED_RECT rect{};
    if (FAILED(texture->LockRect(0, &rect, nullptr, 0)))
    {
        safe_release(texture);
        return nullptr;
    }
    texture_lock_scope lockScope{ texture, true };

    const uint32_t argb   = 0xFF000000u | (rgb & 0x00FFFFFFu);
    auto*          pixels = static_cast<uint8_t*>(rect.pBits);
    for (uint32_t y = 0; y < TEXTURE_SIZE; ++y)
    {
        auto* pixelRow = reinterpret_cast<uint32_t*>(pixels + (y * rect.Pitch));
        for (uint32_t x = 0; x < TEXTURE_SIZE; ++x)
            pixelRow[x] = argb;
    }
    if (FAILED(lockScope.unlock()))
    {
        // The lock guard must finish all retries while the COM object is live.
        // If the driver still refuses to unlock, retain the texture rather than
        // releasing an object whose locked surface may still be in use.
        (void)lockScope.unlock();
        if (lockScope.locked)
        {
            lockScope.locked = false;
            return nullptr;
        }
        safe_release(texture);
        return nullptr;
    }
    return texture;
}

struct draw_scope final
{
    uint32_t& depth;

    ~draw_scope() noexcept
    {
        --depth;
    }
};

struct actor_entry_scope final
{
    std::atomic_uint& entries;
    uint32_t&         depth;

    ~actor_entry_scope() noexcept
    {
        --depth;
        entries.fetch_sub(1, std::memory_order_acq_rel);
    }
};

struct texture_scope final
{
    IDirect3DBaseTexture8* original;

    ~texture_scope() noexcept
    {
        if (original != nullptr)
        {
            try
            {
                original->Release();
            }
            catch (...)
            {
            }
        }
    }
};

auto restore_texture(IDirect3DDevice8* device, IDirect3DBaseTexture8* texture) noexcept -> HRESULT
{
    HRESULT result = D3DERR_DRIVERINTERNALERROR;
    for (size_t attempt = 0; attempt < 2; ++attempt)
    {
        try
        {
            result = device->SetTexture(0, texture);
            if (SUCCEEDED(result))
                return result;
        }
        catch (...)
        {
        }
    }
    return result;
}

} // namespace

namespace fluffytail
{

static_assert((sizeof(COLOURS) / sizeof(COLOURS[0])) == 6,
              "replacement texture slots must match the calibrated colours");

std::atomic<render_runtime*>                render_runtime::instance_{ nullptr };
std::atomic_uint                            render_runtime::hook_entries_{ 0 };
thread_local uint32_t                       render_runtime::actor_hook_depth_ = 0;
thread_local uint32_t                       render_runtime::draw_depth_       = 0;
thread_local render_runtime::actor_snapshot render_runtime::actor_snapshot_{ false, 0, 0 };

render_runtime::render_runtime()
: actor_draw_address_{ 0 }
, actor_draw_original_{}
, actor_draw_patch_{}
, actor_draw_trampoline_{ nullptr }
, stopping_{ false }
, device_{ nullptr }
, device_identity_{ nullptr }
, owner_thread_id_{ 0 }
, reset_pending_{ false }
, replacement_textures_{}
, detection_frame_{ 0 }
{
}

render_runtime::~render_runtime()
{
    this->shutdown();
}

auto render_runtime::install_actor_draw_hook(uintptr_t address) -> bool
{
    constexpr size_t  PROLOGUE_SIZE                    = sizeof(this->actor_draw_original_);
    constexpr size_t  RELATIVE_JUMP_SIZE               = 5;
    constexpr uint8_t RELATIVE_JUMP_OPCODE             = 0xE9;
    constexpr uint8_t NOP_INSTRUCTION                  = 0x90;
    constexpr size_t  TRAMPOLINE_SIZE                  = PROLOGUE_SIZE + RELATIVE_JUMP_SIZE;
    constexpr uint8_t EXPECTED_PROLOGUE[PROLOGUE_SIZE] = { 0x81, 0xEC, 0x2C, 0x01, 0x00, 0x00 };

    if (instance_.load(std::memory_order_acquire) != nullptr || address == 0 ||
        (this->owner_thread_id_ != 0 && this->owner_thread_id_ != GetCurrentThreadId()) ||
        !compare_bytes(address, EXPECTED_PROLOGUE, sizeof(EXPECTED_PROLOGUE)))
        return false;

    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, TRAMPOLINE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr)
        return false;

    CopyMemory(this->actor_draw_original_, reinterpret_cast<const void*>(address), PROLOGUE_SIZE);
    CopyMemory(trampoline, this->actor_draw_original_, PROLOGUE_SIZE);
    trampoline[PROLOGUE_SIZE]     = RELATIVE_JUMP_OPCODE;
    const auto resumeDisplacement = static_cast<int32_t>(
        (address + PROLOGUE_SIZE) - (reinterpret_cast<uintptr_t>(trampoline) + TRAMPOLINE_SIZE));
    CopyMemory(trampoline + PROLOGUE_SIZE + 1, &resumeDisplacement, sizeof(resumeDisplacement));
    if (!FlushInstructionCache(GetCurrentProcess(), trampoline, TRAMPOLINE_SIZE))
    {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    this->actor_draw_patch_[0]  = RELATIVE_JUMP_OPCODE;
    this->actor_draw_patch_[5]  = NOP_INSTRUCTION;
    const auto hookDisplacement = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&render_runtime::actor_draw_hook) -
        (address + RELATIVE_JUMP_SIZE));
    CopyMemory(this->actor_draw_patch_ + 1, &hookDisplacement, sizeof(hookDisplacement));

    this->actor_draw_address_    = address;
    this->actor_draw_trampoline_ = trampoline;
    this->owner_thread_id_       = GetCurrentThreadId();
    this->stopping_.store(false, std::memory_order_release);
    render_runtime* expected = nullptr;
    if (!instance_.compare_exchange_strong(expected,
                                           this,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire))
    {
        this->actor_draw_address_    = 0;
        this->actor_draw_trampoline_ = nullptr;
        this->owner_thread_id_       = 0;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    const auto installed =
        write_executable(address, this->actor_draw_patch_, sizeof(this->actor_draw_patch_));
    const bool patchVisible =
        compare_bytes(address, this->actor_draw_patch_, sizeof(this->actor_draw_patch_));
    if (installed.bytes_written && installed.cache_flushed &&
        installed.protection_restored && patchVisible)
        return true;

    if (!installed.bytes_written &&
        compare_bytes(address, this->actor_draw_original_, sizeof(this->actor_draw_original_)))
    {
        render_runtime* owner = this;
        (void)instance_.compare_exchange_strong(owner,
                                                nullptr,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire);
        this->actor_draw_address_    = 0;
        this->actor_draw_trampoline_ = nullptr;
        this->owner_thread_id_       = 0;
        VirtualFree(trampoline, 0, MEM_RELEASE);
    }
    else
    {
        // Bytes may have changed without a coherent instruction-cache view.
        // Enter pass-through mode before the adapter retains this runtime.
        this->stopping_.store(true, std::memory_order_release);
    }
    // If executable bytes changed but cache coherency is unproved, retain all
    // state. The adapter's failed-load teardown will restore the prologue or
    // deliberately pin/leak this runtime rather than free callable code.
    return false;
}

auto render_runtime::owns_actor_draw_hook() const -> bool
{
    return this->actor_draw_address_ == 0 ||
           compare_bytes(this->actor_draw_address_,
                         this->actor_draw_patch_,
                         sizeof(this->actor_draw_patch_));
}

auto render_runtime::remove_actor_draw_hook() -> bool
{
    if (this->actor_draw_address_ == 0)
        return true;
    if (actor_hook_depth_ != 0)
        return false;
    if (!this->owner_thread())
        return false;

    this->stopping_.store(true, std::memory_order_release);
    if (!this->owns_actor_draw_hook())
        return false;

    const auto restored       = write_executable(this->actor_draw_address_,
                                                 this->actor_draw_original_,
                                                 sizeof(this->actor_draw_original_));
    const bool bytes_restored = compare_bytes(this->actor_draw_address_,
                                              this->actor_draw_original_,
                                              sizeof(this->actor_draw_original_));
    // Do not free the trampoline until bytes, instruction-cache coherency, and
    // the original page protection are all proved restored.
    if (!restored.bytes_written || !restored.cache_flushed ||
        !restored.protection_restored || !bytes_restored)
        return false;

    // Publish the unhook only after the original bytes are verified. Any stale
    // entry into the hook is counted before loading instance_ and is therefore
    // covered by this drain before the trampoline can be released.
    render_runtime* owner = this;
    (void)instance_.compare_exchange_strong(owner,
                                            nullptr,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    while (hook_entries_.load(std::memory_order_acquire) != 0)
        SwitchToThread();

    actor_snapshot_           = { false, 0, 0 };
    this->actor_draw_address_ = 0;
    if (this->actor_draw_trampoline_ != nullptr)
    {
        VirtualFree(this->actor_draw_trampoline_, 0, MEM_RELEASE);
        this->actor_draw_trampoline_ = nullptr;
    }
    return true;
}

void __fastcall render_runtime::actor_draw_hook(void* actor, void* edx) noexcept
{
    UNREFERENCED_PARAMETER(edx);

    // Count before loading instance_: teardown can unpublish the runtime and
    // wait for this entry without racing a stale self pointer.
    hook_entries_.fetch_add(1, std::memory_order_acq_rel);
    ++actor_hook_depth_;
    actor_entry_scope entryScope{ hook_entries_, actor_hook_depth_ };
    auto*             self = instance_.load(std::memory_order_acquire);
    if (self == nullptr)
        return;

    const auto original        = reinterpret_cast<actor_draw_fn>(self->actor_draw_trampoline_);
    bool       original_called = false;
    try
    {
        if (original == nullptr || self->stopping_.load(std::memory_order_acquire) ||
            actor == nullptr)
        {
            if (original != nullptr)
            {
                original_called = true;
                original(actor);
            }
        }
        else
        {
            struct snapshot_scope final
            {
                actor_snapshot& slot;
                actor_snapshot  previous;

                explicit snapshot_scope(actor_snapshot& value) noexcept
                : slot{ value }
                , previous{ value }
                {
                }

                ~snapshot_scope() noexcept
                {
                    slot = previous;
                }
            } scope{ actor_snapshot_ };

            raw_actor_snapshot next{};
            (void)read_raw_actor_snapshot(actor, next);
            actor_snapshot_ = { next.valid, next.race, next.hair };
            original_called = true;
            original(actor);
        }
    }
    catch (...)
    {
        // Hooks must never let a C++ exception unwind into the game.
        if (original != nullptr && !original_called &&
            !self->stopping_.load(std::memory_order_acquire))
        {
            try
            {
                original(actor);
            }
            catch (...)
            {
            }
        }
    }
}

auto render_runtime::release_textures() -> void
{
    for (auto*& texture : this->replacement_textures_)
        safe_release(texture);
}

auto render_runtime::release_tail_texture_cache() -> void
{
    for (auto* texture : this->tail_textures_)
    {
        auto* owned = texture;
        safe_release(owned);
    }
    this->tail_textures_.clear();
    this->rejected_textures_.clear();
    this->detection_frame_ = 0;
}

auto render_runtime::is_tail_texture(IDirect3DBaseTexture8* texture) -> bool
{
    if (texture == nullptr)
        return false;
    if (this->tail_textures_.find(texture) != this->tail_textures_.end())
        return true;
    if (this->rejected_textures_.find(texture) != this->rejected_textures_.end())
        return false;

    if (has_tail_fingerprint(texture))
    {
        texture->AddRef();
        try
        {
            const auto result = this->tail_textures_.insert(texture);
            if (!result.second)
                texture->Release();
            return true;
        }
        catch (...)
        {
            texture->Release();
            return false;
        }
    }

    try
    {
        this->rejected_textures_.insert(texture);
    }
    catch (...)
    {
    }
    return false;
}

auto render_runtime::owner_thread() const noexcept -> bool
{
    return this->owner_thread_id_ == 0 || GetCurrentThreadId() == this->owner_thread_id_;
}

auto render_runtime::replacement_texture(const char* name) const noexcept -> IDirect3DTexture8*
{
    if (name == nullptr)
        return nullptr;
    for (size_t index = 0; index < this->replacement_textures_.size(); ++index)
    {
        if (std::strcmp(COLOURS[index].name, name) == 0)
            return this->replacement_textures_[index];
    }
    return nullptr;
}

auto render_runtime::initialize_device(IDirect3DDevice8* device) -> bool
{
    if (!this->owner_thread() || device == nullptr)
        return false;
    if (this->device_identity_ != nullptr && this->device_identity_ != device)
        return false;

    this->release_tail_texture_cache();
    this->release_textures();
    this->owner_thread_id_ = GetCurrentThreadId();
    this->device_identity_ = device;
    this->device_          = device;
    this->reset_pending_   = false;

    for (size_t index = 0; index < this->replacement_textures_.size(); ++index)
    {
        auto* texture = make_texture(device, COLOURS[index].rgb);
        if (texture == nullptr)
        {
            this->release_textures();
            this->device_        = nullptr;
            this->reset_pending_ = true;
            return false;
        }
        this->replacement_textures_[index] = texture;
    }
    return true;
}

auto render_runtime::before_device_reset() -> void
{
    if (!this->owner_thread())
        return;
    this->release_tail_texture_cache();
    this->release_textures();
    this->device_        = nullptr;
    this->reset_pending_ = true;
}

auto render_runtime::release_device() -> void
{
    if (!this->owner_thread())
        return;
    this->before_device_reset();
    this->device_identity_ = nullptr;
    this->reset_pending_   = false;
    if (this->actor_draw_address_ == 0)
        this->owner_thread_id_ = 0;
}

auto render_runtime::device_ready() const -> bool
{
    if (!this->owner_thread() || this->reset_pending_ || this->device_ == nullptr)
        return false;
    for (const auto* texture : this->replacement_textures_)
    {
        if (texture == nullptr)
            return false;
    }
    return true;
}

auto render_runtime::begin_frame() -> void
{
    if (!this->device_ready())
        return;
    constexpr uint32_t REJECTED_TEXTURE_EXPIRY_FRAMES = 600;
    if (++this->detection_frame_ >= REJECTED_TEXTURE_EXPIRY_FRAMES)
    {
        // Negative entries own no COM reference and may otherwise hide an address
        // reused for a later model texture.
        this->rejected_textures_.clear();
        this->detection_frame_ = 0;
    }
}

auto render_runtime::try_draw_indexed(const indexed_draw_args& args,
                                      indexed_draw_fn          downstream) noexcept -> draw_result
{
    return this->try_draw_indexed(this->device_, args, downstream);
}

auto render_runtime::try_draw_indexed(IDirect3DDevice8*        device,
                                      const indexed_draw_args& args,
                                      indexed_draw_fn          downstream) noexcept -> draw_result
{
    try
    {
        if (device != this->device_ || draw_depth_ != 0 || downstream == nullptr ||
            !this->device_ready() || !actor_snapshot_.valid ||
            actor_snapshot_.race != RACE_MITHRA)
            return { false, D3D_OK };

        const auto* colour_name = face_colour(actor_snapshot_.hair);
        auto*       replacement = this->replacement_texture(colour_name);
        if (replacement == nullptr)
            return { false, D3D_OK };

        IDirect3DBaseTexture8* bound     = nullptr;
        HRESULT                getResult = D3DERR_DRIVERINTERNALERROR;
        try
        {
            getResult = device->GetTexture(0, &bound);
        }
        catch (...)
        {
            safe_release(bound);
            return { false, D3D_OK };
        }
        if (FAILED(getResult))
        {
            safe_release(bound);
            return { false, D3D_OK };
        }
        if (bound == nullptr)
            return { false, D3D_OK };

        texture_scope textureScope{ bound };
        if (!this->is_tail_texture(bound))
            return { false, D3D_OK };

        HRESULT swapResult = D3DERR_DRIVERINTERNALERROR;
        try
        {
            swapResult = device->SetTexture(0, replacement);
        }
        catch (...)
        {
            (void)restore_texture(device, bound);
            // No downstream draw was submitted, so the adapter must retain its
            // normal pass-through path even if bind recovery also failed.
            return { false, D3D_OK };
        }
        if (FAILED(swapResult))
            return { false, D3D_OK };

        ++draw_depth_;
        HRESULT result = D3DERR_DRIVERINTERNALERROR;
        try
        {
            draw_scope drawScope{ draw_depth_ };
            result = downstream(device,
                                args.primitive_type,
                                args.min_index,
                                args.num_vertices,
                                args.start_index,
                                args.primitive_count);
        }
        catch (...)
        {
            // Preserve the internal-error result and restore the original bind.
        }

        const auto restoreResult = restore_texture(device, bound);
        // The replacement draw has already been submitted. Keep it consumed to
        // prevent a duplicate outer draw, but make a failed restoration visible.
        return { true, FAILED(restoreResult) ? restoreResult : result };
    }
    catch (...)
    {
        return { false, D3D_OK };
    }
}

auto render_runtime::shutdown() -> bool
{
    // Keep all runtime state alive while the code patch is restored. If
    // restoration is refused (ownership changed or wrong thread), do not free
    // textures or the trampoline behind a still-callable hook.
    if (!this->remove_actor_draw_hook())
        return false;
    if (!this->owner_thread())
        return false;
    this->release_device();
    return true;
}

} // namespace fluffytail
