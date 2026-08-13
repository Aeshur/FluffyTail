/**
 * FluffyTail - Windower 4.7.9 native plugin ABI declarations.
 *
 * The interface layout was derived from Windower's current first-party plugin
 * binaries; exact artifact hashes, vtable RVAs, and stack cleanup widths are in
 * tests/windower_abi_fixture.json. Earlier declarations were informed by
 * Shirk/Nameplate Windozer.h, Copyright (c) 2024 BunnyBox Productions, GNU GPL
 * v3. See LICENSE.GPL.txt and LICENSES.md.
 */

#pragma once

#include <Windows.h>

#include <cstdint>

namespace fluffytail::windower
{

inline constexpr uint32_t INTERFACE_VERSION = 0x04070300;

class TextHandler;
class PrimitiveHandler;
class PacketStreamHandler;
class FFXI;
class Settings;

class Console
{
public:
    virtual void __stdcall OpenConsole(bool open)                       = 0;
    virtual auto __stdcall IsVisible() -> bool                          = 0;
    virtual void __stdcall SetPosition(float x, float y)                = 0;
    virtual void __stdcall Write(const char* text)                      = 0;
    virtual void __stdcall Clear()                                      = 0;
    virtual void __stdcall SendCommand(const char* command, bool delay) = 0;
};

class PluginManager
{
public:
    virtual auto __stdcall  GetMMFSettingsHandler() -> Settings*             = 0;
    virtual auto __stdcall  GetHWND() -> HWND                                = 0;
    virtual auto __stdcall  GetDirect3D8Device() -> void*                    = 0;
    virtual auto __stdcall  GetConsole() -> Console*                         = 0;
    virtual auto __stdcall  GetTextHandler() -> TextHandler*                 = 0;
    virtual auto __stdcall  GetPrimitiveHandler() -> PrimitiveHandler*       = 0;
    virtual auto __stdcall  GetPacketStreamHandler() -> PacketStreamHandler* = 0;
    virtual auto __stdcall  GetFFXI() -> FFXI*                               = 0;
    virtual auto __thiscall Dtor(uint8_t flags) -> PluginManager*            = 0;
};

// This is the 18-slot IPlugin host interface. Windower's first-party
// WindowerPlugin helper derives from IPlugin and appends 16 private virtuals;
// those helper slots are not part of the object contract returned to Hook.
// Hook 4.7.9 uses Dealloc at host slot 3 where the archived 0x04070000 interface
// exposed Unload. Keep this declaration in the exact verified order.
class PluginBase
{
public:
    virtual auto __stdcall  GetPluginAuthor() -> const char*                           = 0;
    virtual auto __stdcall  GetPluginName() -> const char*                             = 0;
    virtual void __stdcall  Load(PluginManager* manager)                               = 0;
    virtual void __stdcall  Dealloc()                                                  = 0;
    virtual auto __stdcall  IgnoreUnload() -> bool                                     = 0;
    virtual void __stdcall  PreRender()                                                = 0;
    virtual void __stdcall  PostRender()                                               = 0;
    virtual void __stdcall  PluginCommand(const char* command)                         = 0;
    virtual auto __stdcall  UnhandledCommand(const char* command) -> bool              = 0;
    virtual void __stdcall  IncomingText(void* arg0, void* arg1, void* arg2)           = 0;
    virtual void __stdcall  OutgoingText(void* arg0, void* arg1, void* arg2)           = 0;
    virtual auto __stdcall  IncomingChunk(void* arg0,
                                          void* arg1,
                                          void* arg2,
                                          bool  modified) -> bool                      = 0;
    virtual auto __stdcall  OutgoingChunk(void* arg0,
                                          void* arg1,
                                          void* arg2,
                                          bool  modified) -> bool                      = 0;
    virtual auto __stdcall  Mouse(void* arg0,
                                  void* arg1,
                                  void* arg2,
                                  void* arg3,
                                  bool  modified) -> bool                              = 0;
    virtual auto __stdcall  Keyboard(void* arg0, void* arg1, bool modified) -> bool    = 0;
    virtual void __stdcall  AddItem(void* arg0, void* arg1, void* arg2, void* arg3)    = 0;
    virtual void __stdcall  RemoveItem(void* arg0, void* arg1, void* arg2, void* arg3) = 0;
    virtual auto __thiscall Dtor(uint8_t flags) -> PluginBase*                         = 0;

protected:
    ~PluginBase() = default;
};

static_assert(sizeof(void*) == 4, "Windower 4 plugins must be built for x86");
static_assert(sizeof(PluginBase) == 4, "Windower PluginBase ABI changed");

} // namespace fluffytail::windower

extern "C" auto GetInterfaceVersion() -> uint32_t;
extern "C" auto CreateInstance() -> fluffytail::windower::PluginBase*;
