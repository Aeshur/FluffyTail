/**
 * FluffyTail - per-face Mithra tail colour plugin for Ashita v4.3.
 *
 * Derived from the Ashita v4 example plugin (src/exampleplugin.cpp),
 * Copyright (c) 2025 Ashita Development Team, GNU LGPL v3. Modifications for
 * per-face Mithra tail colour by Aeshur, under the same licence.
 * See LICENSE.md / LICENSES.md.
 */

#include "fluffytail.hpp"

namespace
{

// FFXI race identifier for Mithra.
constexpr uint8_t RACE_MITHRA = 7;

// The XiPivot overlay uses this fixed neutral only as a marker during drawing.
constexpr uint32_t BASELINE_RGB = 0x241C1A;

// Established face -> tail colour map, keyed by the low 16 hair values ('1A'..'8B').
constexpr const char* FACE_COLOURS[16] = {
    "red",
    "brunette",
    "white",
    "silver",
    "silver",
    "red",
    "blonde",
    "red",
    "rose",
    "brunette",
    "white",
    "red",
    "blonde",
    "brunette",
    "brunette",
    "blonde",
};

auto face_colour(uint8_t hair) -> const char*
{
    return hair < (sizeof(FACE_COLOURS) / sizeof(FACE_COLOURS[0])) ? FACE_COLOURS[hair]
                                                                   : nullptr;
}

// These solid tail colours were calibrated manually against the face references.
// Do not change a value without checking it again in game, where lighting brightens it.
struct colour_t
{
    const char* name;
    uint32_t    rgb;
};

constexpr colour_t COLOURS[] = {
    { "white", 0xD8D8CA },
    { "silver", 0xCDC2D4 },
    { "blonde", 0xC3A261 },
    { "red", 0x652708 },
    { "rose", 0x814231 },
    { "brunette", 0x3E2412 },
};

constexpr auto rgb565(uint32_t rgb) -> uint16_t
{
    return static_cast<uint16_t>((((rgb >> 19) & 0x1F) << 11) | (((rgb >> 10) & 0x3F) << 5) |
                                 ((rgb >> 3) & 0x1F));
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
    if (other != 0xFFFF)
        return false;

    return endpoint == rgb565(BASELINE_RGB);
}

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

    // Every DXT3 block must match the neutral marker, preventing false positives.
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
    texture->UnlockRect(0);
    return matches;
}

// rgb is 0xRRGGBB. Generated pixels are opaque.
auto make_texture(IDirect3DDevice8* device, uint32_t rgb) -> IDirect3DTexture8*
{
    constexpr uint32_t TEXTURE_SIZE = 8;
    IDirect3DTexture8* texture      = nullptr;
    if (FAILED(device->CreateTexture(TEXTURE_SIZE, TEXTURE_SIZE, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture)))
        return nullptr;

    D3DLOCKED_RECT rect{};
    if (FAILED(texture->LockRect(0, &rect, nullptr, 0)))
    {
        safe_release(texture);
        return nullptr;
    }

    const uint32_t argb   = 0xFF000000u | (rgb & 0x00FFFFFFu);
    auto*          pixels = static_cast<uint8_t*>(rect.pBits);
    for (uint32_t y = 0; y < TEXTURE_SIZE; ++y)
    {
        auto* pixel_row = reinterpret_cast<uint32_t*>(pixels + (y * rect.Pitch));
        for (uint32_t x = 0; x < TEXTURE_SIZE; ++x)
            pixel_row[x] = argb;
    }
    if (FAILED(texture->UnlockRect(0)))
    {
        safe_release(texture);
        return nullptr;
    }
    return texture;
}

auto write_code(uintptr_t address, const void* bytes, size_t size) -> bool
{
    DWORD oldProtect{};
    if (!VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    CopyMemory(reinterpret_cast<void*>(address), bytes, size);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), size);

    DWORD ignored{};
    VirtualProtect(reinterpret_cast<void*>(address), size, oldProtect, &ignored);
    return true;
}

} // namespace

namespace fluffytail
{

plugin* plugin::instance_ = nullptr;

plugin::plugin(void)
: core_{ nullptr }
, device_{ nullptr }
, actor_draw_address_{ 0 }
, actor_draw_original_{}
, actor_draw_trampoline_{ nullptr }
, current_entity_{ nullptr }
, detection_frame_{ 0 }
, submitting_tail_draw_{ false }
{
}

plugin::~plugin(void)
{
    this->remove_actor_draw_hook();
    this->release_tail_texture_cache();
    this->release_textures();
}

auto plugin::GetName(void) const -> const char*
{
    return "fluffytail";
}

auto plugin::GetAuthor(void) const -> const char*
{
    return "Aeshur";
}

auto plugin::GetDescription(void) const -> const char*
{
    return "Per-face Mithra tail colour.";
}

auto plugin::GetLink(void) const -> const char*
{
    return "https://github.com/Aeshur/FluffyTail";
}

auto plugin::GetVersion(void) const -> double
{
    return 1.0;
}

auto plugin::GetPriority(void) const -> int32_t
{
    return 0;
}

auto plugin::GetInterfaceVersion(void) const -> double
{
    return ASHITA_INTERFACE_VERSION;
}

auto plugin::GetFlags(void) const -> uint32_t
{
    return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands) |
           static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D);
}

auto plugin::Initialize(IAshitaCore* core, ILogManager* logger, const uint32_t id) -> bool
{
    UNREFERENCED_PARAMETER(logger);
    UNREFERENCED_PARAMETER(id);
    this->core_ = core;

    // Retail FFXI PE timestamp 2026-07-07: the actor virtual draw entry calls this core.
    // The pattern covers its setup through the first stable actor/entity accesses.
    const auto actorDraw =
        core->GetPointerManager()->Add("fluffytail_actor_draw", "FFXiMain.dll", "81EC2C0100005355568BF15733FF8B4670897C24143BC7897C241C"
                                                                                "BB01000000741B8B882C0100008A8030010000C1E91123CB23C3"
                                                                                "894C24148944241C8D8E74060000",
                                       0,
                                       0);

    if (actorDraw == 0)
    {
        std::ostringstream actorMsg;
        actorMsg << Ashita::Chat::Header("fluffytail");
        actorMsg << Ashita::Chat::Error(
            "sig scan FAILED: actor draw not found (client update?)");
        core->GetChatManager()->Write(1, false, actorMsg.str().c_str());
        return false;
    }
    if (!this->install_actor_draw_hook(actorDraw))
    {
        std::ostringstream actorMsg;
        actorMsg << Ashita::Chat::Header("fluffytail");
        actorMsg << Ashita::Chat::Error("actor draw hook FAILED at 0x%08X");
        core->GetChatManager()->Writef(1, false, actorMsg.str().c_str(), actorDraw);
        return false;
    }

    return true;
}

auto plugin::Release(void) -> void
{
    this->remove_actor_draw_hook();
    this->release_tail_texture_cache();
    this->release_textures();
    this->device_ = nullptr;
}

auto plugin::release_textures(void) -> void
{
    for (auto& [name, texture] : this->textures_)
        safe_release(texture);
    this->textures_.clear();
}

auto plugin::release_tail_texture_cache(void) -> void
{
    for (auto* texture : this->tail_textures_)
        texture->Release();
    this->tail_textures_.clear();
    this->rejected_textures_.clear();
    this->detection_frame_ = 0;
}

auto plugin::is_tail_texture(IDirect3DBaseTexture8* texture) -> bool
{
    if (this->tail_textures_.find(texture) != this->tail_textures_.end())
        return true;
    if (this->rejected_textures_.find(texture) != this->rejected_textures_.end())
        return false;

    if (has_tail_fingerprint(texture))
    {
        texture->AddRef();
        this->tail_textures_.insert(texture);
        return true;
    }

    this->rejected_textures_.insert(texture);
    return false;
}

auto plugin::install_actor_draw_hook(uintptr_t address) -> bool
{
    constexpr size_t  PROLOGUE_SIZE        = sizeof(this->actor_draw_original_);
    constexpr size_t  RELATIVE_JUMP_SIZE   = 5;
    constexpr uint8_t RELATIVE_JUMP_OPCODE = 0xE9;
    constexpr size_t  NOP_SIZE             = 1;
    constexpr uint8_t NOP_INSTRUCTION      = 0x90;
    constexpr size_t  TRAMPOLINE_SIZE      = PROLOGUE_SIZE + RELATIVE_JUMP_SIZE;

    if (instance_ != nullptr || address == 0)
        return false;

    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
        nullptr, TRAMPOLINE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr)
        return false;

    CopyMemory(this->actor_draw_original_, reinterpret_cast<const void*>(address), PROLOGUE_SIZE);
    CopyMemory(trampoline, this->actor_draw_original_, PROLOGUE_SIZE);
    trampoline[PROLOGUE_SIZE] = RELATIVE_JUMP_OPCODE;
    const auto resumeDisplacement =
        static_cast<int32_t>((address + PROLOGUE_SIZE) -
                             (reinterpret_cast<uintptr_t>(trampoline) + TRAMPOLINE_SIZE));
    CopyMemory(trampoline + PROLOGUE_SIZE + 1, &resumeDisplacement, sizeof(resumeDisplacement));
    FlushInstructionCache(GetCurrentProcess(), trampoline, TRAMPOLINE_SIZE);

    uint8_t patch[PROLOGUE_SIZE]    = { RELATIVE_JUMP_OPCODE, 0, 0, 0, 0, 0 };
    patch[PROLOGUE_SIZE - NOP_SIZE] = NOP_INSTRUCTION;
    const auto hookDisplacement     = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(&plugin::actor_draw_hook) - (address + RELATIVE_JUMP_SIZE));
    CopyMemory(patch + 1, &hookDisplacement, sizeof(hookDisplacement));

    // Keep the trampoline until removal restores all six bytes of the game prologue.
    this->actor_draw_address_    = address;
    this->actor_draw_trampoline_ = trampoline;
    instance_                    = this;
    if (!write_code(address, patch, sizeof(patch)))
    {
        instance_                    = nullptr;
        this->actor_draw_address_    = 0;
        this->actor_draw_trampoline_ = nullptr;
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    return true;
}

auto plugin::remove_actor_draw_hook(void) -> void
{
    if (this->actor_draw_address_ != 0)
        write_code(this->actor_draw_address_, this->actor_draw_original_, sizeof(this->actor_draw_original_));

    instance_                 = nullptr;
    this->current_entity_     = nullptr;
    this->actor_draw_address_ = 0;
    if (this->actor_draw_trampoline_ != nullptr)
    {
        VirtualFree(this->actor_draw_trampoline_, 0, MEM_RELEASE);
        this->actor_draw_trampoline_ = nullptr;
    }
}

void __fastcall plugin::actor_draw_hook(void* actor, void* edx)
{
    UNREFERENCED_PARAMETER(edx);

    // Save and restore this draw's entity so nested draws see their own actor.
    auto*            self                = instance_;
    auto*            previous            = self->current_entity_;
    constexpr size_t ACTOR_ENTITY_OFFSET = 0x70;
    self->current_entity_                = *reinterpret_cast<Ashita::FFXI::entity_t**>(
        static_cast<uint8_t*>(actor) + ACTOR_ENTITY_OFFSET);

    const auto original = reinterpret_cast<actor_draw_fn>(self->actor_draw_trampoline_);
    original(actor);
    self->current_entity_ = previous;
}

auto plugin::HandleEvent(const char* event_name, const void* event_data, const uint32_t event_size) -> void
{
    UNREFERENCED_PARAMETER(event_name);
    UNREFERENCED_PARAMETER(event_data);
    UNREFERENCED_PARAMETER(event_size);
}

auto plugin::HandleCommand(int32_t mode, const char* command, bool injected) -> bool
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(injected);

    if (command == nullptr)
        return false;

    std::istringstream input(command);
    std::string        root;
    std::string        action;
    input >> root >> action;
    if (_stricmp(root.c_str(), "/fluffytail") != 0)
        return false;

    if (_stricmp(action.c_str(), "inspect") != 0)
    {
        std::ostringstream msg;
        msg << Ashita::Chat::Header("fluffytail")
            << Ashita::Chat::Message("usage: /fluffytail inspect");
        this->core_->GetChatManager()->Write(1, false, msg.str().c_str());
        return true;
    }

    const auto  index  = this->core_->GetMemoryManager()->GetTarget()->GetTargetIndex(0);
    const auto* entity = this->core_->GetMemoryManager()->GetEntity()->GetRawEntity(index);
    if (index == 0 || entity == nullptr)
    {
        std::ostringstream msg;
        msg << Ashita::Chat::Header("fluffytail")
            << Ashita::Chat::Error("inspect failed: no target");
        this->core_->GetChatManager()->Write(1, false, msg.str().c_str());
        return true;
    }

    const auto*        name = this->core_->GetMemoryManager()->GetEntity()->GetName(index);
    std::ostringstream msg;
    msg << Ashita::Chat::Header("fluffytail")
        << Ashita::Chat::Message("inspect %s idx=%u sid=0x%08X type=%u race=%u actor=0x%08X");
    this->core_->GetChatManager()->Writef(1, false, msg.str().c_str(), name ? name : "?", index, entity->ServerId, entity->Type, entity->Race, static_cast<uint32_t>(entity->ActorPointer));

    std::ostringstream lookMsg;
    lookMsg << Ashita::Chat::Header("fluffytail")
            << Ashita::Chat::Message(
                   "look hair=%04X head=%04X body=%04X hands=%04X legs=%04X feet=%04X");
    this->core_->GetChatManager()->Writef(
        1, false, lookMsg.str().c_str(), entity->Look.Hair, entity->Look.Head, entity->Look.Body, entity->Look.Hands, entity->Look.Legs, entity->Look.Feet);
    return true;
}

auto plugin::HandleIncomingText(int32_t mode, bool indent, const char* message, int32_t* modified_mode, bool* modified_indent, char* modified_message, bool injected, bool blocked) -> bool
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(indent);
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(modified_mode);
    UNREFERENCED_PARAMETER(modified_indent);
    UNREFERENCED_PARAMETER(modified_message);
    UNREFERENCED_PARAMETER(injected);
    UNREFERENCED_PARAMETER(blocked);
    return false;
}

auto plugin::HandleOutgoingText(int32_t mode, const char* message, int32_t* modified_mode, char* modified_message, bool injected, bool blocked) -> bool
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(modified_mode);
    UNREFERENCED_PARAMETER(modified_message);
    UNREFERENCED_PARAMETER(injected);
    UNREFERENCED_PARAMETER(blocked);
    return false;
}

auto plugin::HandleIncomingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t size_chunk, const uint8_t* data_chunk, bool injected, bool blocked)
    -> bool
{
    UNREFERENCED_PARAMETER(id);
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(modified);
    UNREFERENCED_PARAMETER(size_chunk);
    UNREFERENCED_PARAMETER(data_chunk);
    UNREFERENCED_PARAMETER(injected);
    UNREFERENCED_PARAMETER(blocked);
    return false;
}

auto plugin::HandleOutgoingPacket(uint16_t id, uint32_t size, const uint8_t* data, uint8_t* modified, uint32_t size_chunk, const uint8_t* data_chunk, bool injected, bool blocked)
    -> bool
{
    UNREFERENCED_PARAMETER(id);
    UNREFERENCED_PARAMETER(size);
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(modified);
    UNREFERENCED_PARAMETER(size_chunk);
    UNREFERENCED_PARAMETER(data_chunk);
    UNREFERENCED_PARAMETER(injected);
    UNREFERENCED_PARAMETER(blocked);
    return false;
}

auto plugin::Direct3DInitialize(IDirect3DDevice8* device) -> bool
{
    this->release_tail_texture_cache();
    this->device_ = device;

    // Direct3DInitialize may run again after a device reset.
    this->release_textures();
    for (const auto& c : COLOURS)
    {
        if (auto* tex = make_texture(device, c.rgb); tex != nullptr)
            this->textures_[c.name] = tex;
    }

    constexpr size_t COLOUR_COUNT = sizeof(COLOURS) / sizeof(COLOURS[0]);
    if (this->textures_.size() != COLOUR_COUNT)
    {
        std::ostringstream msg;
        msg << Ashita::Chat::Header("fluffytail")
            << Ashita::Chat::Error("texture creation FAILED: loaded %zu/%zu colours");
        this->core_->GetChatManager()->Writef(1, false, msg.str().c_str(), this->textures_.size(), COLOUR_COUNT);
        return false;
    }

    return true;
}

auto plugin::Direct3DBeginScene(bool is_rendering_back_buffer) -> void
{
    constexpr uint32_t REJECTED_TEXTURE_EXPIRY_FRAMES = 600;
    if (is_rendering_back_buffer && ++this->detection_frame_ >= REJECTED_TEXTURE_EXPIRY_FRAMES)
    {
        // Negative entries do not own a COM reference, so periodically discard them
        // in case FFXI reused an address after unloading a model texture.
        this->rejected_textures_.clear();
        this->detection_frame_ = 0;
    }
}

auto plugin::Direct3DEndScene(bool is_rendering_back_buffer) -> void
{
    UNREFERENCED_PARAMETER(is_rendering_back_buffer);
}

auto plugin::Direct3DPresent(const RECT* p_source_rect, const RECT* p_dest_rect, HWND h_dest_window_override, const RGNDATA* p_dirty_region) -> void
{
    UNREFERENCED_PARAMETER(p_source_rect);
    UNREFERENCED_PARAMETER(p_dest_rect);
    UNREFERENCED_PARAMETER(h_dest_window_override);
    UNREFERENCED_PARAMETER(p_dirty_region);
}

auto plugin::Direct3DSetRenderState(D3DRENDERSTATETYPE state, DWORD* value) -> bool
{
    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(value);
    return false;
}

auto plugin::Direct3DDrawPrimitive(D3DPRIMITIVETYPE primitive_type, UINT start_vertex, UINT primitive_count) -> bool
{
    UNREFERENCED_PARAMETER(primitive_type);
    UNREFERENCED_PARAMETER(start_vertex);
    UNREFERENCED_PARAMETER(primitive_count);
    return false;
}

auto plugin::Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type, UINT min_index, UINT num_vertices, UINT start_index, UINT primitive_count) -> bool
{
    if (this->submitting_tail_draw_ || this->device_ == nullptr ||
        this->current_entity_ == nullptr || this->current_entity_->Race != RACE_MITHRA)
        return false;

    const auto hair = static_cast<uint8_t>(this->current_entity_->Look.Hair & 0xFF);
    if (hair >= 16)
        return false;

    const auto* colour      = face_colour(hair);
    const auto  replacement = this->textures_.find(colour ? colour : "");
    if (replacement == this->textures_.end())
        return false;

    IDirect3DBaseTexture8* bound = nullptr;
    if (FAILED(this->device_->GetTexture(0, &bound)) || bound == nullptr)
        return false;
    if (!this->is_tail_texture(bound))
    {
        bound->Release();
        return false;
    }

    if (FAILED(this->device_->SetTexture(0, replacement->second)))
    {
        bound->Release();
        return false;
    }

    // Submit this one draw ourselves so the original texture can be restored before
    // any later game draw. The guard lets Ashita's recursive callback pass through.
    this->submitting_tail_draw_ = true;
    this->device_->DrawIndexedPrimitive(primitive_type, min_index, num_vertices, start_index, primitive_count);
    this->submitting_tail_draw_ = false;
    this->device_->SetTexture(0, bound);
    bound->Release();

    return true; // The replacement draw was already submitted.
}

auto plugin::Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type, UINT primitive_count, CONST void* vertex_stream_zero_data, UINT vertex_stream_zero_stride) -> bool
{
    UNREFERENCED_PARAMETER(primitive_type);
    UNREFERENCED_PARAMETER(primitive_count);
    UNREFERENCED_PARAMETER(vertex_stream_zero_data);
    UNREFERENCED_PARAMETER(vertex_stream_zero_stride);
    return false;
}

auto plugin::Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type,
                                            UINT             min_vertex_index,
                                            UINT             num_vertex_indices,
                                            UINT             primitive_count,
                                            CONST void*      index_data,
                                            D3DFORMAT        index_data_format,
                                            CONST void*      vertex_stream_zero_data,
                                            UINT             vertex_stream_zero_stride) -> bool
{
    UNREFERENCED_PARAMETER(primitive_type);
    UNREFERENCED_PARAMETER(min_vertex_index);
    UNREFERENCED_PARAMETER(num_vertex_indices);
    UNREFERENCED_PARAMETER(primitive_count);
    UNREFERENCED_PARAMETER(index_data);
    UNREFERENCED_PARAMETER(index_data_format);
    UNREFERENCED_PARAMETER(vertex_stream_zero_data);
    UNREFERENCED_PARAMETER(vertex_stream_zero_stride);
    return false;
}

} // namespace fluffytail

__declspec(dllexport) auto __stdcall expCreatePlugin(const char* args) -> IPlugin*
{
    UNREFERENCED_PARAMETER(args);
    return new fluffytail::plugin();
}

__declspec(dllexport) auto __stdcall expDestroyPlugin(void* instance) -> void
{
    delete static_cast<fluffytail::plugin*>(instance);
}

__declspec(dllexport) auto __stdcall expGetInterfaceVersion(void) -> double
{
    return ASHITA_INTERFACE_VERSION;
}
