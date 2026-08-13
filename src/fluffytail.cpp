/**
 * FluffyTail - Ashita v4.3 host adapter.
 *
 * Derived from the Ashita v4 example plugin (src/exampleplugin.cpp),
 * Copyright (c) 2025 Ashita Development Team, GNU LGPL v3. Modifications for
 * per-face Mithra tail colour by Aeshur, under the same licence.
 * See LICENSE.md / LICENSES.md.
 */

#include "fluffytail.hpp"

#include "hook_utils.hpp"

#include <cstddef>
#include <sstream>

static_assert(offsetof(Ashita::FFXI::entity_t, Race) == 0xEF,
              "retail entity race offset changed");
static_assert(offsetof(Ashita::FFXI::entity_t, Look) == 0xFC,
              "retail entity look offset changed");
static_assert(offsetof(Ashita::FFXI::look_t, Hair) == 0,
              "retail look hair offset changed");

namespace
{

auto STDMETHODCALLTYPE submit_ashita_draw(IDirect3DDevice8* device,
                                          D3DPRIMITIVETYPE  primitive_type,
                                          UINT              min_index,
                                          UINT              num_vertices,
                                          UINT              start_index,
                                          UINT              primitive_count) -> HRESULT
{
    return device->DrawIndexedPrimitive(
        primitive_type, min_index, num_vertices, start_index, primitive_count);
}

} // namespace

namespace fluffytail
{

ashita_plugin::ashita_plugin()
: core_{ nullptr }
, runtime_{ std::make_unique<render_runtime>() }
{
}

ashita_plugin::~ashita_plugin()
{
    if (this->runtime_ != nullptr && !this->runtime_->shutdown())
    {
        // Ashita's void Release contract cannot refuse destruction. The module
        // was pinned before hook installation, so retain the runtime when a
        // foreign owner or thread mismatch makes unhooking unprovable.
        (void)this->runtime_.release();
    }
}

auto ashita_plugin::GetName() const -> const char*
{
    return "fluffytail";
}

auto ashita_plugin::GetAuthor() const -> const char*
{
    return "Aeshur";
}

auto ashita_plugin::GetDescription() const -> const char*
{
    return "Per-face Mithra tail colour.";
}

auto ashita_plugin::GetLink() const -> const char*
{
    return "https://github.com/Aeshur/FluffyTail";
}

auto ashita_plugin::GetVersion() const -> double
{
    return 1.1;
}

auto ashita_plugin::GetPriority() const -> int32_t
{
    return 0;
}

auto ashita_plugin::GetInterfaceVersion() const -> double
{
    return ASHITA_INTERFACE_VERSION;
}

auto ashita_plugin::GetFlags() const -> uint32_t
{
    return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands) |
           static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D);
}

auto ashita_plugin::Initialize(IAshitaCore* core, ILogManager* logger, uint32_t id) -> bool
{
    UNREFERENCED_PARAMETER(logger);
    UNREFERENCED_PARAMETER(id);
    if (core == nullptr)
        return false;
    this->core_ = core;

    // Retail FFXI PE timestamp 2026-07-07: the actor virtual draw entry calls this core.
    const auto actorDraw = core->GetPointerManager()->Add(
        "fluffytail_actor_draw",
        "FFXiMain.dll",
        "81EC2C0100005355568BF15733FF8B4670897C24143BC7897C241C"
        "BB01000000741B8B882C0100008A8030010000C1E91123CB23C3"
        "894C24148944241C8D8E74060000",
        0,
        0);

    if (actorDraw == 0)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("sig scan FAILED: actor draw not found (client update?)");
        core->GetChatManager()->Write(1, false, message.str().c_str());
        return false;
    }
    if (!pin_current_module() || this->runtime_ == nullptr ||
        !this->runtime_->install_actor_draw_hook(actorDraw))
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("actor draw hook FAILED at 0x%08X");
        core->GetChatManager()->Writef(1, false, message.str().c_str(), actorDraw);
        return false;
    }
    return true;
}

auto ashita_plugin::Release() -> void
{
    if (this->runtime_ != nullptr && !this->runtime_->shutdown() && this->core_ != nullptr)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("unload blocked: actor hook ownership changed");
        this->core_->GetChatManager()->Write(1, false, message.str().c_str());
    }
    this->core_ = nullptr;
}

auto ashita_plugin::HandleEvent(const char* event_name,
                                const void* event_data,
                                uint32_t    event_size) -> void
{
    UNREFERENCED_PARAMETER(event_name);
    UNREFERENCED_PARAMETER(event_data);
    UNREFERENCED_PARAMETER(event_size);
}

auto ashita_plugin::HandleCommand(int32_t mode, const char* command, bool injected) -> bool
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(injected);
    if (command == nullptr || this->core_ == nullptr)
        return false;

    std::istringstream input(command);
    std::string        root;
    std::string        action;
    input >> root >> action;
    if (_stricmp(root.c_str(), "/fluffytail") != 0)
        return false;

    if (_stricmp(action.c_str(), "inspect") != 0)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Message("usage: /fluffytail inspect");
        this->core_->GetChatManager()->Write(1, false, message.str().c_str());
        return true;
    }

    const auto index = this->core_->GetMemoryManager()->GetTarget()->GetTargetIndex(0);
    if (index == 0)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("inspect failed: no target");
        this->core_->GetChatManager()->Write(1, false, message.str().c_str());
        return true;
    }

    const auto* entity = this->core_->GetMemoryManager()->GetEntity()->GetRawEntity(index);
    if (entity == nullptr)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("inspect failed: no target");
        this->core_->GetChatManager()->Write(1, false, message.str().c_str());
        return true;
    }

    const auto*        name = this->core_->GetMemoryManager()->GetEntity()->GetName(index);
    std::ostringstream message;
    message << Ashita::Chat::Header("fluffytail")
            << Ashita::Chat::Message(
                   "inspect %s idx=%u sid=0x%08X type=%u race=%u actor=0x%08X");
    this->core_->GetChatManager()->Writef(1,
                                          false,
                                          message.str().c_str(),
                                          name ? name : "?",
                                          index,
                                          entity->ServerId,
                                          entity->Type,
                                          entity->Race,
                                          static_cast<uint32_t>(entity->ActorPointer));

    std::ostringstream lookMessage;
    lookMessage << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Message(
                       "look hair=%04X head=%04X body=%04X hands=%04X legs=%04X feet=%04X");
    this->core_->GetChatManager()->Writef(1,
                                          false,
                                          lookMessage.str().c_str(),
                                          entity->Look.Hair,
                                          entity->Look.Head,
                                          entity->Look.Body,
                                          entity->Look.Hands,
                                          entity->Look.Legs,
                                          entity->Look.Feet);
    return true;
}

auto ashita_plugin::HandleIncomingText(int32_t     mode,
                                       bool        indent,
                                       const char* message,
                                       int32_t*    modified_mode,
                                       bool*       modified_indent,
                                       char*       modified_message,
                                       bool        injected,
                                       bool        blocked) -> bool
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

auto ashita_plugin::HandleOutgoingText(int32_t     mode,
                                       const char* message,
                                       int32_t*    modified_mode,
                                       char*       modified_message,
                                       bool        injected,
                                       bool        blocked) -> bool
{
    UNREFERENCED_PARAMETER(mode);
    UNREFERENCED_PARAMETER(message);
    UNREFERENCED_PARAMETER(modified_mode);
    UNREFERENCED_PARAMETER(modified_message);
    UNREFERENCED_PARAMETER(injected);
    UNREFERENCED_PARAMETER(blocked);
    return false;
}

auto ashita_plugin::HandleIncomingPacket(uint16_t       id,
                                         uint32_t       size,
                                         const uint8_t* data,
                                         uint8_t*       modified,
                                         uint32_t       size_chunk,
                                         const uint8_t* data_chunk,
                                         bool           injected,
                                         bool           blocked) -> bool
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

auto ashita_plugin::HandleOutgoingPacket(uint16_t       id,
                                         uint32_t       size,
                                         const uint8_t* data,
                                         uint8_t*       modified,
                                         uint32_t       size_chunk,
                                         const uint8_t* data_chunk,
                                         bool           injected,
                                         bool           blocked) -> bool
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

auto ashita_plugin::Direct3DInitialize(IDirect3DDevice8* device) -> bool
{
    try
    {
        if (this->runtime_ != nullptr && this->runtime_->initialize_device(device))
            return true;
    }
    catch (...)
    {
    }

    if (this->core_ != nullptr)
    {
        std::ostringstream message;
        message << Ashita::Chat::Header("fluffytail")
                << Ashita::Chat::Error("texture creation FAILED");
        this->core_->GetChatManager()->Write(1, false, message.str().c_str());
    }
    return false;
}

auto ashita_plugin::Direct3DBeginScene(bool is_rendering_back_buffer) -> void
{
    try
    {
        if (is_rendering_back_buffer && this->runtime_ != nullptr)
            this->runtime_->begin_frame();
    }
    catch (...)
    {
    }
}

auto ashita_plugin::Direct3DEndScene(bool is_rendering_back_buffer) -> void
{
    UNREFERENCED_PARAMETER(is_rendering_back_buffer);
}

auto ashita_plugin::Direct3DPresent(const RECT*    source_rect,
                                    const RECT*    dest_rect,
                                    HWND           dest_window_override,
                                    const RGNDATA* dirty_region) -> void
{
    UNREFERENCED_PARAMETER(source_rect);
    UNREFERENCED_PARAMETER(dest_rect);
    UNREFERENCED_PARAMETER(dest_window_override);
    UNREFERENCED_PARAMETER(dirty_region);
}

auto ashita_plugin::Direct3DSetRenderState(D3DRENDERSTATETYPE state, DWORD* value) -> bool
{
    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(value);
    return false;
}

auto ashita_plugin::Direct3DDrawPrimitive(D3DPRIMITIVETYPE primitive_type,
                                          UINT             start_vertex,
                                          UINT             primitive_count) -> bool
{
    UNREFERENCED_PARAMETER(primitive_type);
    UNREFERENCED_PARAMETER(start_vertex);
    UNREFERENCED_PARAMETER(primitive_count);
    return false;
}

auto ashita_plugin::Direct3DDrawIndexedPrimitive(D3DPRIMITIVETYPE primitive_type,
                                                 UINT             min_index,
                                                 UINT             num_vertices,
                                                 UINT             start_index,
                                                 UINT             primitive_count) -> bool
{
    const indexed_draw_args args{
        primitive_type,
        min_index,
        num_vertices,
        start_index,
        primitive_count,
    };
    return this->runtime_ != nullptr &&
           this->runtime_->try_draw_indexed(args, &submit_ashita_draw).handled;
}

auto ashita_plugin::Direct3DDrawPrimitiveUP(D3DPRIMITIVETYPE primitive_type,
                                            UINT             primitive_count,
                                            const void*      vertex_stream_zero_data,
                                            UINT             vertex_stream_zero_stride) -> bool
{
    UNREFERENCED_PARAMETER(primitive_type);
    UNREFERENCED_PARAMETER(primitive_count);
    UNREFERENCED_PARAMETER(vertex_stream_zero_data);
    UNREFERENCED_PARAMETER(vertex_stream_zero_stride);
    return false;
}

auto ashita_plugin::Direct3DDrawIndexedPrimitiveUP(D3DPRIMITIVETYPE primitive_type,
                                                   UINT             min_vertex_index,
                                                   UINT             num_vertex_indices,
                                                   UINT             primitive_count,
                                                   const void*      index_data,
                                                   D3DFORMAT        index_data_format,
                                                   const void*      vertex_stream_zero_data,
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

extern "C" __declspec(dllexport) auto __stdcall expCreatePlugin(const char* args) -> IPlugin*
{
    UNREFERENCED_PARAMETER(args);
    return new fluffytail::ashita_plugin();
}

extern "C" __declspec(dllexport) auto __stdcall expDestroyPlugin(void* instance) -> void
{
    delete static_cast<fluffytail::ashita_plugin*>(instance);
}

extern "C" __declspec(dllexport) auto __stdcall expGetInterfaceVersion() -> double
{
    return ASHITA_INTERFACE_VERSION;
}
