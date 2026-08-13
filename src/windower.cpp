/**
 * FluffyTail - Windower 4.7.9 native host adapter.
 *
 * ABI provenance includes Shirk/Nameplate Windozer.cpp and Windozer.h,
 * Copyright (c) 2024 BunnyBox Productions, GNU GPL v3. See LICENSE.GPL.txt and
 * LICENSES.md. FluffyTail implementation copyright (c) 2026 Aeshur.
 */

#include "windower.hpp"

#include "hook_utils.hpp"
#include "render_runtime.hpp"

#include <Windows.h>
#include <d3d8.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace
{

constexpr size_t RESET_VTABLE_INDEX = 14;
constexpr size_t DRAW_VTABLE_INDEX  = 71;

using reset_fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice8*, D3DPRESENT_PARAMETERS*);

thread_local uint32_t callback_depth_ = 0;

constexpr uint8_t ACTOR_DRAW_SIGNATURE[] = {
    0x81,
    0xEC,
    0x2C,
    0x01,
    0x00,
    0x00,
    0x53,
    0x55,
    0x56,
    0x8B,
    0xF1,
    0x57,
    0x33,
    0xFF,
    0x8B,
    0x46,
    0x70,
    0x89,
    0x7C,
    0x24,
    0x14,
    0x3B,
    0xC7,
    0x89,
    0x7C,
    0x24,
    0x1C,
    0xBB,
    0x01,
    0x00,
    0x00,
    0x00,
    0x74,
    0x1B,
    0x8B,
    0x88,
    0x2C,
    0x01,
    0x00,
    0x00,
    0x8A,
    0x80,
    0x30,
    0x01,
    0x00,
    0x00,
    0xC1,
    0xE9,
    0x11,
    0x23,
    0xCB,
    0x23,
    0xC3,
    0x89,
    0x4C,
    0x24,
    0x14,
    0x89,
    0x44,
    0x24,
    0x1C,
    0x8D,
    0x8E,
    0x74,
    0x06,
    0x00,
    0x00,
};

struct callback_scope final
{
    std::atomic_uint& callbacks;
    bool              acquired;

    explicit callback_scope(std::atomic_uint& value, bool already_acquired = false)
    : callbacks{ value }
    , acquired{ already_acquired }
    {
        if (!this->acquired)
            callbacks.fetch_add(1, std::memory_order_acq_rel);
        ++callback_depth_;
    }

    ~callback_scope()
    {
        --callback_depth_;
        callbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
};

struct draw_call_context final
{
    fluffytail::indexed_draw_fn previous;
    bool                        called;
};

} // namespace

namespace fluffytail
{

class windower_plugin final : public windower::PluginBase
{
    static std::atomic<windower_plugin*> instance_;
    static std::mutex                    instance_mutex_;

    windower::PluginManager* manager_;
    render_runtime           runtime_;
    IDirect3DDevice8*        device_;
    void**                   device_vtable_;
    reset_fn                 previous_reset_;
    indexed_draw_fn          previous_draw_;
    std::atomic_uint         callbacks_;
    std::atomic_bool         unloading_;
    DWORD                    owner_thread_;
    bool                     owner_thread_set_;
    bool                     device_hooks_installed_;
    bool                     reset_owned_;
    bool                     draw_owned_;
    bool                     ownership_lost_;
    bool                     unload_pinned_;
    bool                     loaded_;

    static thread_local draw_call_context* draw_context_;

    static auto reset_hook_address() -> void*
    {
        return reinterpret_cast<void*>(&windower_plugin::reset_hook);
    }

    static auto draw_hook_address() -> void*
    {
        return reinterpret_cast<void*>(&windower_plugin::draw_hook);
    }

    auto on_owner_thread() const -> bool
    {
        return this->owner_thread_set_ && this->owner_thread_ == GetCurrentThreadId();
    }

    auto wait_for_callbacks() -> void
    {
        while (this->callbacks_.load(std::memory_order_acquire) != 0)
            SwitchToThread();
    }

    static auto enter_callback() noexcept -> windower_plugin*
    {
        try
        {
            std::lock_guard<std::mutex> lock{ instance_mutex_ };
            auto* const                 self = instance_.load(std::memory_order_acquire);
            if (self != nullptr)
                self->callbacks_.fetch_add(1, std::memory_order_acq_rel);
            return self;
        }
        catch (...)
        {
            return nullptr;
        }
    }

    auto console() const -> windower::Console*
    {
        return this->manager_ != nullptr ? this->manager_->GetConsole() : nullptr;
    }

    auto write(const char* message) const -> void
    {
        try
        {
            auto* output = this->console();
            if (output == nullptr)
                return;

            char line[512]{};
            std::snprintf(line, sizeof(line), "[FluffyTail] %s", message ? message : "");
            output->Write(line);
        }
        catch (...)
        {
            // Host callbacks must not receive a C++ exception.
        }
    }

    auto owns_device_hooks() const -> bool
    {
        if (this->ownership_lost_)
            return false;
        if (!this->device_hooks_installed_ || this->device_vtable_ == nullptr)
            return true;

        if (this->reset_owned_ &&
            this->device_vtable_[RESET_VTABLE_INDEX] != reset_hook_address())
            return false;
        if (this->draw_owned_ &&
            this->device_vtable_[DRAW_VTABLE_INDEX] != draw_hook_address())
            return false;
        return true;
    }

    auto attach_device() -> bool
    {
        if (this->device_hooks_installed_)
            return this->reset_owned_ && this->draw_owned_ && this->owns_device_hooks();
        if (this->manager_ == nullptr || !this->on_owner_thread() || this->unload_pinned_)
            return false;

        auto* device = static_cast<IDirect3DDevice8*>(this->manager_->GetDirect3D8Device());
        if (device == nullptr)
            return false;

        auto** vtable = *reinterpret_cast<void***>(device);
        if (vtable == nullptr)
            return false;

        const auto previousReset = reinterpret_cast<reset_fn>(vtable[RESET_VTABLE_INDEX]);
        const auto previousDraw  = reinterpret_cast<indexed_draw_fn>(vtable[DRAW_VTABLE_INDEX]);
        if (previousReset == nullptr || previousDraw == nullptr ||
            !this->runtime_.initialize_device(device))
            return false;

        this->device_         = device;
        this->device_vtable_  = vtable;
        this->previous_reset_ = previousReset;
        this->previous_draw_  = previousDraw;
        this->unloading_.store(false, std::memory_order_release);

        const auto reset = replace_vtable_slot(&vtable[RESET_VTABLE_INDEX],
                                               reinterpret_cast<void*>(previousReset),
                                               reset_hook_address());
        if (!reset.exchanged)
        {
            this->runtime_.release_device();
            this->device_         = nullptr;
            this->device_vtable_  = nullptr;
            this->previous_reset_ = nullptr;
            this->previous_draw_  = nullptr;
            return false;
        }

        this->reset_owned_            = true;
        this->device_hooks_installed_ = true;
        if (!reset.protection_restored)
        {
            this->unload_pinned_ = true;
            return false;
        }

        const auto draw = replace_vtable_slot(&vtable[DRAW_VTABLE_INDEX],
                                              reinterpret_cast<void*>(previousDraw),
                                              draw_hook_address());
        if (!draw.exchanged)
        {
            const auto rollback = restore_vtable_slot(&vtable[RESET_VTABLE_INDEX],
                                                      reset_hook_address(),
                                                      reinterpret_cast<void*>(previousReset));
            if (rollback.exchanged && rollback.protection_restored)
            {
                this->reset_owned_            = false;
                this->device_hooks_installed_ = false;
                this->runtime_.release_device();
                this->device_         = nullptr;
                this->device_vtable_  = nullptr;
                this->previous_reset_ = nullptr;
                this->previous_draw_  = nullptr;
            }
            else
            {
                // The reset slot may still call us (or have a foreign owner). Keep
                // all callable state and pin the instance rather than unloading
                // code that remains reachable from the device vtable.
                this->unload_pinned_ = true;
                if (rollback.exchanged)
                    this->reset_owned_ = false;
                if (rollback.observed != reset_hook_address())
                {
                    this->reset_owned_    = false;
                    this->ownership_lost_ = true;
                }
            }
            return false;
        }

        this->draw_owned_             = true;
        this->device_hooks_installed_ = true;
        if (!draw.protection_restored)
        {
            this->unload_pinned_ = true;
            return false;
        }
        return true;
    }

    auto detach_device() -> bool
    {
        if (!this->device_hooks_installed_)
        {
            this->runtime_.release_device();
            return true;
        }
        if (callback_depth_ != 0)
        {
            this->unload_pinned_ = true;
            return false;
        }
        if (!this->on_owner_thread())
        {
            this->unload_pinned_ = true;
            return false;
        }

        // Close callback admission while publishing the unhook. Existing
        // callbacks are counted already; do not hold this mutex while waiting
        // for them, since a nested callback may need to reacquire it.
        std::unique_lock<std::mutex> callback_lock{ instance_mutex_ };
        this->unloading_.store(true, std::memory_order_release);

        bool restored = true;
        if (this->draw_owned_)
        {
            if (this->device_vtable_[DRAW_VTABLE_INDEX] != draw_hook_address())
            {
                this->draw_owned_     = false;
                this->ownership_lost_ = true;
                this->unload_pinned_  = true;
                restored              = false;
            }
            else
            {
                const auto result = restore_vtable_slot(
                    &this->device_vtable_[DRAW_VTABLE_INDEX],
                    draw_hook_address(),
                    reinterpret_cast<void*>(this->previous_draw_));
                if (result.exchanged && result.protection_restored)
                    this->draw_owned_ = false;
                else
                {
                    if (result.exchanged)
                        this->draw_owned_ = false;
                    if (result.observed != draw_hook_address())
                    {
                        this->draw_owned_     = false;
                        this->ownership_lost_ = true;
                    }
                    this->unload_pinned_ = true;
                    restored             = false;
                }
            }
        }

        if (this->reset_owned_)
        {
            if (this->device_vtable_[RESET_VTABLE_INDEX] != reset_hook_address())
            {
                this->reset_owned_    = false;
                this->ownership_lost_ = true;
                this->unload_pinned_  = true;
                restored              = false;
            }
            else
            {
                const auto result = restore_vtable_slot(
                    &this->device_vtable_[RESET_VTABLE_INDEX],
                    reset_hook_address(),
                    reinterpret_cast<void*>(this->previous_reset_));
                if (result.exchanged && result.protection_restored)
                    this->reset_owned_ = false;
                else
                {
                    if (result.exchanged)
                        this->reset_owned_ = false;
                    if (result.observed != reset_hook_address())
                    {
                        this->reset_owned_    = false;
                        this->ownership_lost_ = true;
                    }
                    this->unload_pinned_ = true;
                    restored             = false;
                }
            }
        }

        if (!restored || this->ownership_lost_ || this->unload_pinned_ ||
            this->reset_owned_ || this->draw_owned_)
        {
            callback_lock.unlock();
            return false;
        }

        instance_.store(nullptr, std::memory_order_release);
        callback_lock.unlock();
        this->wait_for_callbacks();
        this->runtime_.release_device();
        this->device_                 = nullptr;
        this->device_vtable_          = nullptr;
        this->previous_reset_         = nullptr;
        this->previous_draw_          = nullptr;
        this->device_hooks_installed_ = false;
        this->unloading_.store(false, std::memory_order_release);
        return true;
    }

    auto unload() -> bool
    {
        if (!this->loaded_)
            return true;
        if (!this->on_owner_thread())
        {
            this->unload_pinned_ = true;
            return false;
        }
        if (!this->detach_device())
        {
            this->unload_pinned_ = true;
            return false;
        }
        if (!this->runtime_.shutdown())
        {
            this->unload_pinned_ = true;
            return false;
        }

        this->loaded_  = false;
        this->manager_ = nullptr;
        instance_.store(nullptr, std::memory_order_release);
        return true;
    }

    static auto invoke_reset(reset_fn               previous,
                             IDirect3DDevice8*      device,
                             D3DPRESENT_PARAMETERS* parameters) -> HRESULT
    {
        if (previous == nullptr)
            return D3DERR_INVALIDCALL;
        try
        {
            return previous(device, parameters);
        }
        catch (...)
        {
            return D3DERR_DRIVERINTERNALERROR;
        }
    }

    static auto STDMETHODCALLTYPE reset_hook(IDirect3DDevice8*      device,
                                             D3DPRESENT_PARAMETERS* parameters) -> HRESULT
    {
        auto* self = enter_callback();
        if (self == nullptr)
            return D3DERR_INVALIDCALL;

        callback_scope scope{ self->callbacks_, true };
        if (self->previous_reset_ == nullptr)
            return D3DERR_INVALIDCALL;
        const auto previous = self->previous_reset_;
        bool       called   = false;
        try
        {
            if (!self->on_owner_thread() || device != self->device_ ||
                self->unloading_.load(std::memory_order_acquire))
            {
                called = true;
                return invoke_reset(previous, device, parameters);
            }

            self->runtime_.before_device_reset();
            called            = true;
            const auto result = previous(device, parameters);
            if (result == D3D_OK && !self->runtime_.initialize_device(device))
                self->write("texture recreation failed after device reset");
            return result;
        }
        catch (...)
        {
            if (!called)
            {
                called = true;
                return invoke_reset(previous, device, parameters);
            }
            return D3DERR_DRIVERINTERNALERROR;
        }
    }

    static auto invoke_draw(indexed_draw_fn   previous,
                            IDirect3DDevice8* device,
                            D3DPRIMITIVETYPE  primitive_type,
                            UINT              min_index,
                            UINT              num_vertices,
                            UINT              start_index,
                            UINT              primitive_count) -> HRESULT
    {
        if (previous == nullptr)
            return D3DERR_INVALIDCALL;
        try
        {
            return previous(
                device, primitive_type, min_index, num_vertices, start_index, primitive_count);
        }
        catch (...)
        {
            return D3DERR_DRIVERINTERNALERROR;
        }
    }

    static auto STDMETHODCALLTYPE downstream_draw(IDirect3DDevice8* device,
                                                  D3DPRIMITIVETYPE  primitive_type,
                                                  UINT              min_index,
                                                  UINT              num_vertices,
                                                  UINT              start_index,
                                                  UINT              primitive_count) -> HRESULT
    {
        auto* context = draw_context_;
        if (context == nullptr || context->previous == nullptr)
            return D3DERR_INVALIDCALL;
        context->called = true;
        return invoke_draw(context->previous,
                           device,
                           primitive_type,
                           min_index,
                           num_vertices,
                           start_index,
                           primitive_count);
    }

    static auto STDMETHODCALLTYPE draw_hook(IDirect3DDevice8* device,
                                            D3DPRIMITIVETYPE  primitive_type,
                                            UINT              min_index,
                                            UINT              num_vertices,
                                            UINT              start_index,
                                            UINT              primitive_count) -> HRESULT
    {
        auto* self = enter_callback();
        if (self == nullptr)
            return D3DERR_INVALIDCALL;

        callback_scope scope{ self->callbacks_, true };
        if (self->previous_draw_ == nullptr)
            return D3DERR_INVALIDCALL;
        const auto previous = self->previous_draw_;
        if (!self->on_owner_thread() || device != self->device_ ||
            self->unloading_.load(std::memory_order_acquire))
        {
            return invoke_draw(previous,
                               device,
                               primitive_type,
                               min_index,
                               num_vertices,
                               start_index,
                               primitive_count);
        }

        draw_call_context context{ previous, false };
        auto* const       prior_context = draw_context_;
        draw_context_                   = &context;
        try
        {
            const indexed_draw_args args{
                primitive_type,
                min_index,
                num_vertices,
                start_index,
                primitive_count,
            };
            const auto draw = self->runtime_.try_draw_indexed(
                device, args, &windower_plugin::downstream_draw);
            draw_context_ = prior_context;
            if (context.called)
                return draw.handled ? draw.result : D3DERR_DRIVERINTERNALERROR;
            return invoke_draw(previous,
                               device,
                               primitive_type,
                               min_index,
                               num_vertices,
                               start_index,
                               primitive_count);
        }
        catch (...)
        {
            draw_context_ = prior_context;
            if (context.called)
                return D3DERR_DRIVERINTERNALERROR;
            return invoke_draw(previous,
                               device,
                               primitive_type,
                               min_index,
                               num_vertices,
                               start_index,
                               primitive_count);
        }
    }

public:
    windower_plugin()
    : manager_{ nullptr }
    , device_{ nullptr }
    , device_vtable_{ nullptr }
    , previous_reset_{ nullptr }
    , previous_draw_{ nullptr }
    , callbacks_{ 0 }
    , unloading_{ false }
    , owner_thread_{ 0 }
    , owner_thread_set_{ false }
    , device_hooks_installed_{ false }
    , reset_owned_{ false }
    , draw_owned_{ false }
    , ownership_lost_{ false }
    , unload_pinned_{ false }
    , loaded_{ false }
    {
    }

    ~windower_plugin()
    {
        this->unload();
    }

    auto __stdcall GetPluginAuthor() -> const char* override
    {
        return "Aeshur";
    }

    auto __stdcall GetPluginName() -> const char* override
    {
        return "fluffytail";
    }

    void __stdcall Load(windower::PluginManager* manager) override
    {
        if (manager == nullptr || !pin_current_module())
            return;

        windower_plugin* expected = nullptr;
        if (!instance_.compare_exchange_strong(expected,
                                               this,
                                               std::memory_order_acq_rel,
                                               std::memory_order_acquire))
            return;

        this->manager_          = manager;
        this->owner_thread_     = GetCurrentThreadId();
        this->owner_thread_set_ = true;
        this->loaded_           = true;

        const auto actorDraw = find_module_signature(
            "FFXiMain.dll", ACTOR_DRAW_SIGNATURE, sizeof(ACTOR_DRAW_SIGNATURE));
        if (actorDraw == 0 || !this->runtime_.install_actor_draw_hook(actorDraw))
        {
            this->write("load failed: actor draw signature or hook unavailable");
            this->unload();
            return;
        }
        if (!this->attach_device())
            this->write("device not ready; will retry during rendering");
    }

    void __stdcall Dealloc() override
    {
        if (this->unload())
            this->Dtor(1);
    }

    auto __stdcall IgnoreUnload() -> bool override
    {
        return this->unload_pinned_ || !this->runtime_.owns_actor_draw_hook() ||
               !this->owns_device_hooks();
    }

    void __stdcall PreRender() override
    {
        try
        {
            if (!this->device_hooks_installed_)
                this->attach_device();
            if (this->runtime_.device_ready())
                this->runtime_.begin_frame();
        }
        catch (...)
        {
            this->write("render setup failed");
        }
    }

    void __stdcall PostRender() override
    {
    }

    void __stdcall PluginCommand(const char* command) override
    {
        if (command != nullptr && _stricmp(command, "inspect") == 0)
        {
            this->write("inspect is available through the Ashita adapter only");
            return;
        }
        this->write("usage: //fluffytail inspect");
    }

    auto __stdcall UnhandledCommand(const char* command) -> bool override
    {
        UNREFERENCED_PARAMETER(command);
        return false;
    }

    void __stdcall IncomingText(void* arg0, void* arg1, void* arg2) override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
    }

    void __stdcall OutgoingText(void* arg0, void* arg1, void* arg2) override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
    }

    auto __stdcall IncomingChunk(void* arg0, void* arg1, void* arg2, bool modified)
        -> bool override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        return modified;
    }

    auto __stdcall OutgoingChunk(void* arg0, void* arg1, void* arg2, bool modified)
        -> bool override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        return modified;
    }

    auto __stdcall Mouse(void* arg0, void* arg1, void* arg2, void* arg3, bool modified)
        -> bool override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        UNREFERENCED_PARAMETER(arg3);
        return modified;
    }

    auto __stdcall Keyboard(void* arg0, void* arg1, bool modified) -> bool override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        return modified;
    }

    void __stdcall AddItem(void* arg0, void* arg1, void* arg2, void* arg3) override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        UNREFERENCED_PARAMETER(arg3);
    }

    void __stdcall RemoveItem(void* arg0, void* arg1, void* arg2, void* arg3) override
    {
        UNREFERENCED_PARAMETER(arg0);
        UNREFERENCED_PARAMETER(arg1);
        UNREFERENCED_PARAMETER(arg2);
        UNREFERENCED_PARAMETER(arg3);
    }

    auto __thiscall Dtor(uint8_t flags) -> windower::PluginBase* override
    {
        auto* result = this;
        // Both scalar-destructor forms must honor the teardown gate. If a
        // foreign hook still chains through this DLL, preserve the live object.
        if (this->loaded_ && !this->unload())
            return result;
        if ((flags & 1) != 0)
            delete this;
        else
            this->~windower_plugin();
        return result;
    }
};

std::atomic<windower_plugin*>   windower_plugin::instance_{ nullptr };
std::mutex                      windower_plugin::instance_mutex_{};
thread_local draw_call_context* windower_plugin::draw_context_ = nullptr;

} // namespace fluffytail

extern "C" auto GetInterfaceVersion() -> uint32_t
{
    return fluffytail::windower::INTERFACE_VERSION;
}

extern "C" auto CreateInstance() -> fluffytail::windower::PluginBase*
{
    return new fluffytail::windower_plugin();
}
