// Original rewrite frontend. No supplied game source or SDK sample code is
// used here. External API contracts consulted: xsdk public headers and
// docs/GUIDE.md, docs/HARDWARE.md at ade2c81d914634834d77394c5d9f8c4575f395bc;
// Xenia's public VdSwap argument contract and xe_gpu_texture_fetch_t layout:
// https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc
// https://github.com/xenia-project/xenia/blob/master/src/xenia/gpu/xenos.h
// Presentation is a new backend requiring actual console and Xenia validation.

#include "playfield_test.hpp"
#include "platform_input.hpp"
#include "runtime.hpp"
#include "scene.hpp"
#include "score_display.hpp"
#include "message_display.hpp"
#include "game_sound_bank.hpp"
#include "cheat_menu.hpp"
#include "startup_sequence.hpp"
#include "xbox_audio_output.hpp"
#include "table_model.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pinball {
namespace xbox_backend {

enum class PresentMode { Automatic, Console, Xenia };

struct Config {
    std::string dat_path,cheat_unlock;
    PresentMode present;
    std::uint32_t canvas_width;
    std::uint32_t canvas_height;
    std::uint32_t controller;
    std::uint32_t buffer_count;
    std::uint32_t swap_interval;
    std::uint32_t max_frames;
    std::uint32_t motion_smoke;
    std::uint32_t ramp_smoke;
    std::uint32_t hidden_test_smoke;
    std::uint32_t components_smoke;
    std::uint32_t round_smoke;
    std::uint32_t wormhole_smoke;
    std::uint32_t bank_smoke,menu_smoke;
    std::uint32_t sound,sound_volume,startup,startup_smoke;
    platform::ControllerBindings bindings;
    Config() : dat_path("game:\\PINBALL.DAT"), cheat_unlock("DPAD_UP,DPAD_UP,DPAD_DOWN,DPAD_DOWN,DPAD_LEFT,DPAD_RIGHT,DPAD_LEFT,DPAD_RIGHT,B,A"), present(PresentMode::Xenia),
        canvas_width(600), canvas_height(416), controller(0), buffer_count(3),
        swap_interval(1), max_frames(0), motion_smoke(0), ramp_smoke(0), hidden_test_smoke(0), components_smoke(0), round_smoke(0), wormhole_smoke(0), bank_smoke(0), menu_smoke(0), sound(1),sound_volume(100),startup(1),startup_smoke(0),bindings(platform::ControllerBindings::viewer_defaults()) {}
};

std::string trim(const std::string& value)
{
    const std::size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1U);
}

std::uint32_t number(const std::string& text, std::uint32_t minimum,
                     std::uint32_t maximum)
{
    if (text.empty()) throw std::runtime_error("empty numeric configuration value");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9')
            throw std::runtime_error("configuration number must use decimal digits");
        const unsigned digit = static_cast<unsigned>(text[i] - '0');
        if (value > maximum / 10U || (value == maximum / 10U && digit > maximum % 10U))
            throw std::runtime_error("configuration number exceeds its supported range");
        value = value * 10U + digit;
    }
    if (value < minimum) throw std::runtime_error("configuration number is below its supported range");
    return value;
}

std::uint32_t button(const std::string& token)
{
    using namespace platform;
    if (token == "A") return PadA;
    if (token == "B") return PadB;
    if (token == "X") return PadX;
    if (token == "Y") return PadY;
    if (token == "START") return PadStart;
    if (token == "BACK") return PadBack;
    if (token == "LB") return PadLeftShoulder;
    if (token == "RB") return PadRightShoulder;
    if (token == "DPAD_LEFT") return PadDpadLeft;
    if (token == "DPAD_RIGHT") return PadDpadRight;
    if (token == "DPAD_UP") return PadDpadUp;
    if (token == "DPAD_DOWN") return PadDpadDown;
    if (token == "LEFT_THUMB") return PadLeftThumb;
    if (token == "RIGHT_THUMB") return PadRightThumb;
    throw std::runtime_error("unknown controller button: " + token);
}

// User-selected console unlock sequence. Commas separate exact held-mask
// steps; '+' combines buttons within a chord. No Start suffix is implied.
std::vector<std::uint32_t> cheat_sequence(const std::string& value)
{
    std::vector<std::uint32_t> result;
    if(value=="NONE")return result;
    if(value.empty()||value[value.size()-1]==',')throw std::runtime_error("cheat_unlock has an empty step");
    std::istringstream steps(value);std::string step;
    while(std::getline(steps,step,',')) {
        step=trim(step);
        if(step.empty()||step[step.size()-1]=='+')throw std::runtime_error("cheat_unlock has an empty chord button");
        std::istringstream chord(step);std::string token;std::uint32_t mask=0;
        while(std::getline(chord,token,'+')) {
            token=trim(token);if(token.empty())throw std::runtime_error("cheat_unlock has an empty chord button");
            const std::uint32_t part=button(token);
            if(mask&part)throw std::runtime_error("cheat_unlock repeats a button within one chord");
            mask|=part;
        }
        result.push_back(mask);
        if(result.size()>64)throw std::runtime_error("cheat_unlock supports at most64 steps");
    }
    return result;
}
void bind(platform::ControllerBindings& bindings, runtime::Action action,
          const std::string& value)
{
    const std::size_t index = static_cast<std::size_t>(action);
    std::uint32_t buttons = 0;
    bool left = false, right = false;
    if (value != "NONE") {
        std::istringstream tokens(value);
        std::string token;
        bool any = false;
        while (std::getline(tokens, token, ',')) {
            token = trim(token);
            if (token.empty()) throw std::runtime_error("empty controller binding");
            if (token == "LT") left = true;
            else if (token == "RT") right = true;
            else buttons |= button(token);
            any = true;
        }
        if (!any || (!value.empty() && value[value.size() - 1U] == ','))
            throw std::runtime_error("empty controller binding");
    }
    bindings.buttons[index] = buttons;
    bindings.left_trigger[index] = left;
    bindings.right_trigger[index] = right;
}

void setting(Config& config, const std::string& key, const std::string& value)
{
    using runtime::Action;
    if (key == "dat") {
        if (value.empty()) throw std::runtime_error("DAT path is empty");
        config.dat_path = value;
    } else if (key == "present") {
        if (value == "auto") config.present = PresentMode::Automatic;
        else if (value == "console") config.present = PresentMode::Console;
        else if (value == "xenia") config.present = PresentMode::Xenia;
        else throw std::runtime_error("present must be auto, console or xenia");
    } else if (key == "canvas_width") config.canvas_width = number(value, 1, 4096);
    else if (key == "canvas_height") config.canvas_height = number(value, 1, 4096);
    else if (key == "controller") config.controller = number(value, 0, 3);
    else if (key == "buffer_count") config.buffer_count = number(value, 2, 4);
    else if (key == "swap_interval") config.swap_interval = number(value, 1, 4);
    else if (key == "max_frames") config.max_frames = number(value, 0, 10000000);
    else if (key == "motion_smoke") config.motion_smoke = number(value, 0, 1);
    else if (key == "ramp_smoke") config.ramp_smoke = number(value, 0, 1);
    else if (key == "hidden_test_smoke") config.hidden_test_smoke = number(value, 0, 1);
    else if (key == "components_smoke") config.components_smoke = number(value, 0, 1);
    else if (key == "round_smoke") config.round_smoke = number(value, 0, 1);
    else if (key == "wormhole_smoke") config.wormhole_smoke = number(value, 0, 1);
    else if (key == "bank_smoke") config.bank_smoke = number(value, 0, 1);
    else if (key == "menu_smoke") config.menu_smoke = number(value, 0, 1);
    else if (key == "cheat_unlock") { cheat_sequence(value);config.cheat_unlock=value; }
    else if (key == "sound") config.sound=number(value,0,1);
    else if (key == "sound_volume") config.sound_volume=number(value,0,100);
    else if (key == "startup") config.startup=number(value,0,1);
    else if (key == "startup_smoke") config.startup_smoke=number(value,0,1);
    else if (key == "trigger_press") config.bindings.trigger_press = static_cast<std::uint8_t>(number(value, 1, 255));
    else if (key == "trigger_release") config.bindings.trigger_release = static_cast<std::uint8_t>(number(value, 0, 254));
    else if (key == "left_flipper") bind(config.bindings, Action::LeftFlipper, value);
    else if (key == "right_flipper") bind(config.bindings, Action::RightFlipper, value);
    else if (key == "plunger") bind(config.bindings, Action::Plunger, value);
    else if (key == "nudge_left") bind(config.bindings, Action::NudgeLeft, value);
    else if (key == "nudge_right") bind(config.bindings, Action::NudgeRight, value);
    else if (key == "nudge_up") bind(config.bindings, Action::NudgeUp, value);
    else if (key == "new_game") bind(config.bindings, Action::NewGame, value);
    else if (key == "pause") bind(config.bindings, Action::Pause, value);
    else if (key == "exit") bind(config.bindings, Action::Menu, value);
    else if (key == "hidden_test") bind(config.bindings, Action::HiddenTest, value);
    else throw std::runtime_error("unknown configuration setting: " + key);
}

void read_settings(std::istream& input, Config& config)
{
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
            throw std::runtime_error("configuration line lacks '=' at line " + std::to_string(line_number));
        try { setting(config, trim(line.substr(0, equals)), trim(line.substr(equals + 1U))); }
        catch (const std::exception& error) {
            throw std::runtime_error("configuration line " + std::to_string(line_number) + ": " + error.what());
        }
    }
    if (input.bad()) throw std::runtime_error("failed while reading configuration");
}

void validate_config(const Config& config)
{
    if(config.motion_smoke + config.ramp_smoke + config.hidden_test_smoke + config.components_smoke + config.round_smoke + config.wormhole_smoke + config.bank_smoke + config.menu_smoke + config.startup_smoke > 1)
        throw std::runtime_error("choose one scripted smoke scenario");
    if(config.startup_smoke&&!config.startup)throw std::runtime_error("startup_smoke requires startup=1");
    if(config.menu_smoke&&cheat_sequence(config.cheat_unlock).empty())throw std::runtime_error("menu_smoke requires a configured unlock sequence");
    if (config.bindings.trigger_release >= config.bindings.trigger_press)
        throw std::runtime_error("trigger_release must be smaller than trigger_press");
}

// A display/resolve texel is B,G,R,A bytes on the big-endian CPU. Using the
// public B8G8R8A8 BYTE format avoids an ambiguous ARGB-unit conversion.
std::uint32_t display_word(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    return (std::uint32_t(b) << 24) | (std::uint32_t(g) << 16) |
           (std::uint32_t(r) << 8) | a;
}

struct Fit {
    std::uint32_t x, y, width, height;
};

Fit fit(std::uint32_t source_width, std::uint32_t source_height,
        std::uint32_t target_width, std::uint32_t target_height, std::uint32_t top)
{
    if (!source_width || !source_height || !target_width || target_height <= top)
        throw std::runtime_error("cannot fit an empty image or viewport");
    const std::uint32_t available_height = target_height - top;
    Fit result = {0, 0, target_width, available_height};
    if (std::uint64_t(target_width) * source_height > std::uint64_t(available_height) * source_width)
        result.width = static_cast<std::uint32_t>(std::uint64_t(available_height) * source_width / source_height);
    else
        result.height = static_cast<std::uint32_t>(std::uint64_t(target_width) * source_height / source_width);
    result.width = (std::max)(std::uint32_t(1), result.width);
    result.height = (std::max)(std::uint32_t(1), result.height);
    result.x = (target_width - result.width) / 2U;
    result.y = top + (available_height - result.height) / 2U;
    return result;
}

void scale_frame(const scene::Surface& source, std::uint32_t width, std::uint32_t height,
                 std::uint32_t top, std::vector<std::uint32_t>& output)
{
    if (source.pixels.size() != std::uint64_t(source.width) * source.height * 4U)
        throw std::runtime_error("source surface has invalid color storage");
    const Fit bounds = fit(source.width, source.height, width, height, top);
    if (std::uint64_t(width) * height > output.max_size())
        throw std::runtime_error("display staging frame exceeds container capacity");
    output.assign(static_cast<std::size_t>(width) * height, display_word(0, 0, 0, 255));
    for (std::uint32_t y = 0; y < bounds.height; ++y) {
        const std::uint32_t sy = static_cast<std::uint32_t>(std::uint64_t(y) * source.height / bounds.height);
        for (std::uint32_t x = 0; x < bounds.width; ++x) {
            const std::uint32_t sx = static_cast<std::uint32_t>(std::uint64_t(x) * source.width / bounds.width);
            const std::size_t sample = (static_cast<std::size_t>(sy) * source.width + sx) * 4U;
            output[(static_cast<std::size_t>(bounds.y) + y) * width + bounds.x + x] =
                display_word(source.pixels[sample], source.pixels[sample + 1U],
                             source.pixels[sample + 2U], source.pixels[sample + 3U]);
        }
    }
}

// Normal gameplay uses the whole display height. Optional margins remain a
// generic scale_frame contract, rather than a production diagnostic header.
void scale_gameplay_frame(const scene::Surface& source,std::uint32_t width,std::uint32_t height,
                          std::vector<std::uint32_t>& output)
{
    scale_frame(source,width,height,0,output);
}
// Tracking an unlock prefix never captures gameplay. The opening and closing
// samples belong to the menu, including the final A that completes the code.
bool menu_captures_gameplay(bool was_open,bool is_open) {return was_open||is_open;}
bool tick_before(runtime::Tick a,runtime::Tick b)
{
    volatile std::uint32_t ah=static_cast<std::uint32_t>(static_cast<std::uint64_t>(a)>>32U),al=static_cast<std::uint32_t>(a);
    volatile std::uint32_t bh=static_cast<std::uint32_t>(static_cast<std::uint64_t>(b)>>32U),bl=static_cast<std::uint32_t>(b);
    return ah==bh?al<bl:ah<bh;
}
bool same_tick(runtime::Tick a,runtime::Tick b) {return !tick_before(a,b)&&!tick_before(b,a);}

// VdSwap takes a D3D texture-header fetch constant, whose base address is a
// CPU virtual mapping. SDK texture fetch constants instead hold GPU physical
// pages. Only word 1 bits12..31 are the address; preserve all other API fields.
std::array<std::uint32_t, 6> swap_fetch(const std::uint32_t* texture_fetch,
                                     std::uint32_t virtual_address)
{
    if (!texture_fetch || (virtual_address & 4095U))
        throw std::runtime_error("swap texture base must be page aligned");
    std::array<std::uint32_t, 6> result;
    std::copy(texture_fetch, texture_fetch + 6, result.begin());
    result[1] = (result[1] & 4095U) | virtual_address;
    return result;
}

struct PrimaryRingWrite {
    std::uint32_t begin;
    std::uint32_t end;
    std::uint32_t padding_begin;
    std::uint32_t padding_count;
};

// A VdSwap reservation must be contiguous. At a wrap, consume the unused tail
// with one-word type2 NOPs before the new transaction at index0. Never fill an
// entire ring at once: equal read/write indices mean no pending commands.
PrimaryRingWrite primary_ring_write(std::uint32_t cursor, std::uint32_t capacity,
                                   std::uint32_t dwords)
{
    if (!capacity || (capacity & (capacity - 1U)) || cursor >= capacity ||
        !dwords || dwords >= capacity)
        throw std::runtime_error("invalid primary ring extent");
    PrimaryRingWrite result = {cursor, 0, cursor, 0};
    if (dwords > capacity - cursor) {
        result.begin = 0;
        result.padding_count = capacity - cursor;
    }
    result.end = result.begin + dwords;
    if (result.end == capacity) result.end = 0;
    return result;
}

// Public PM4 ABI: EVENT_WRITE_SHD opcode0x58, payload3, PS_DONE_TS event3;
// address bits0..1 select endianness,2 making a CPU-native uint32 writeback.
std::array<std::uint32_t, 4> primary_completion_packet(std::uint32_t physical_address,
                                                     std::uint32_t value)
{
    if ((physical_address & 3U) || !value)
        throw std::runtime_error("invalid primary ring completion address/value");
    return {{0xc0025800U, 3U, physical_address | 2U, value}};
}

} // namespace xbox_backend
} // namespace pinball

#ifndef PINBALL_XBOX_BACKEND_CONTRACT_TEST
#ifndef __XSDK__
#error "Build this native frontend with the xsdk PowerPC toolchain."
#endif

#include <xsdk/gpu.h>
#include <xsdk/kernel.h>
#include <xsdk/log.h>
#include <xsdk/present.h>
#include <xsdk/texture.h>
#include <xsdk/thread.h>
#include <xsdk/xam.h>
#include <time.h>

namespace pinball {
namespace xbox_backend {
namespace {

const std::uint32_t wait_timeout_ms = 5000;

void status_ok(NTSTATUS status, const char* call)
{
    if (NT_SUCCESS(status)) return;
    char text[128];
    std::snprintf(text, sizeof(text), "%s failed: 0x%08lX", call, static_cast<unsigned long>(status));
    throw std::runtime_error(text);
}

bool xenia_kernel()
{
    // This is a capability guard, not a proof the emulator can present.
    // The exact signature is documented in the SDK's public kernel.h.
    return XboxKrnlVersion && XboxKrnlVersion->major == 2 &&
        XboxKrnlVersion->minor == 65535U && XboxKrnlVersion->build == 65535U &&
        XboxKrnlVersion->qfe == 32768U;
}

// Explicit emulator mode owns only a primary ring. It deliberately does not
// call xsdk_gpu_open: that API submits hardware warm-up draws before returning,
// and the observed Canary run timed out at its initial completion fence.
// Pinned emulator API behavior: InitializeRingBuffer clears the ring/read index
// but not an already-submitted write index; VdSwap writes64dwords without a kick;
// MMIO0x7FC80714 takes a DWORD write index. A swap-only frame is supported by its
// D3D12 backend. These are API/data-format bindings, not copied driver routines.
// https://github.com/xenia-canary/xenia-canary/blob/28bc50db0923e5fe2a7770e1be80da5fb7b78605/src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc
// https://github.com/xenia-canary/xenia-canary/blob/28bc50db0923e5fe2a7770e1be80da5fb7b78605/src/xenia/gpu/graphics_system.cc
class XeniaPrimaryRing {
public:
    XeniaPrimaryRing() : ring_(0), writeback_(0), engines_(false), pending_(false),
        cursor_(0), sequence_(0), last_status_(STATUS_SUCCESS) {}
    ~XeniaPrimaryRing() { close(); }
    void open()
    {
        ring_ = xsdk_gpu_alloc(ring_dwords * 4U);
        writeback_ = static_cast<volatile std::uint32_t*>(MmAllocatePhysicalMemoryEx(0, 65536,
            PAGE_READWRITE | PAGE_NOCACHE | MEM_LARGE_PAGES, 0,
            (std::numeric_limits<std::uint32_t>::max)(), 65536));
        if (!ring_ || !writeback_) throw std::runtime_error("cannot allocate minimal Xenia ring/writeback");
        for (std::size_t i = 0; i < 3; ++i) writeback_[i] = 0;
        if (!VdInitializeEngines(0x4f810000U, reinterpret_cast<void*>(engines_stopped), 0, 0, 0))
            throw std::runtime_error("VdInitializeEngines failed for minimal Xenia ring");
        engines_ = true;
        VdInitializeRingBuffer(MmGetPhysicalAddress(const_cast<const std::uint32_t*>(ring_)), 12);
        VdEnableRingBufferRPtrWriteBack(MmGetPhysicalAddress(const_cast<const std::uint32_t*>(writeback_)), 6);
        VdSetSystemCommandBufferGpuIdentifierAddress(writeback_ + 1);
        // Write commands only AFTER initialization, which zeroes this memory.
        const PrimaryRingWrite transaction = primary_ring_write(cursor_, ring_dwords, 4);
        const std::uint32_t value = next_value();
        write_completion(transaction.begin, value);
        submit(transaction.end);
        status_ok(wait_complete(), "minimal Xenia ring probe");
        xsdk_log_write("presentation: minimal Xenia primary-ring probe fence%lu landed, RPTR%lu; hardware GPU warm-up bypassed\n",
            static_cast<unsigned long>(sequence_), static_cast<unsigned long>(writeback_[0]));
    }
    void swap(const std::array<std::uint32_t, 6>& fetch, std::uint32_t front,
              std::uint32_t width, std::uint32_t height)
    {
        status_ok(wait_complete(), "minimal Xenia ring reuse");
        const PrimaryRingWrite transaction = primary_ring_write(cursor_, ring_dwords, 68);
        for (std::uint32_t i = 0; i < transaction.padding_count; ++i)
            ring_[transaction.padding_begin + i] = 0x80000000U; // PM4 type2 NOP.
        const std::uint32_t value = next_value();
        std::array<std::uint32_t, 0x94 / 4> system = {};
        std::uint32_t system_value = 0;
        VdGetSystemCommandBuffer(system.data(), &system_value);
        std::uint32_t format = 6, color_space = 0;
        VdSwap(ring_ + transaction.begin, fetch.data(), writeback_ + 1, system.data(), system_value,
            &front, &format, &color_space, &width, &height);
        write_completion(transaction.begin + 64U, value);
        submit(transaction.end);
        status_ok(wait_complete(), "minimal Xenia swap completion");
    }
    bool close()
    {
        if (!ring_ && !writeback_) return true;
        if (pending_ && !NT_SUCCESS(last_status_)) {
            // A timed-out command may still read these allocations. Retain them
            // for title teardown rather than freeing memory under the worker.
            xsdk_log_write("presentation: minimal Xenia ring did not drain; retaining its physical memory for title teardown\n");
            ring_ = writeback_ = 0;
            engines_ = false;
            return false;
        }
        if (pending_ && !NT_SUCCESS(wait_complete())) {
            ring_ = writeback_ = 0;
            engines_ = false;
            return false;
        }
        if (engines_) {
            VdSetSystemCommandBufferGpuIdentifierAddress(0);
            VdShutdownEngines();
            engines_ = false;
        }
        xsdk_gpu_free(ring_);
        xsdk_gpu_free(writeback_);
        ring_ = writeback_ = 0;
        return true;
    }
private:
    XeniaPrimaryRing(const XeniaPrimaryRing&);
    XeniaPrimaryRing& operator=(const XeniaPrimaryRing&);
    static const std::uint32_t ring_dwords = 8192;
    volatile std::uint32_t* ring_;
    volatile std::uint32_t* writeback_;
    bool engines_, pending_;
    std::uint32_t cursor_, sequence_;
    NTSTATUS last_status_;
    static void engines_stopped() {}
    std::uint32_t next_value()
    {
        if (++sequence_ == 0) ++sequence_;
        writeback_[2] = 0;
        return sequence_;
    }
    void write_completion(std::uint32_t offset, std::uint32_t value)
    {
        const std::uint32_t address = MmGetPhysicalAddress(const_cast<const std::uint32_t*>(writeback_ + 2));
        const std::array<std::uint32_t, 4> packet = primary_completion_packet(address, value);
        for (std::size_t i = 0; i < packet.size(); ++i) ring_[offset + i] = packet[i];
    }
    void submit(std::uint32_t next_cursor)
    {
        cursor_ = next_cursor;
        pending_ = true;
        last_status_ = STATUS_SUCCESS;
        __asm__ volatile("sync" ::: "memory");
        *reinterpret_cast<volatile std::uint32_t*>(std::uintptr_t(0x7fc80714U)) = cursor_;
    }
    NTSTATUS wait_complete()
    {
        if (!pending_) return STATUS_SUCCESS;
        if (!NT_SUCCESS(last_status_)) return last_status_;
        timespec started = {};
        if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) return last_status_ = STATUS_UNSUCCESSFUL;
        for (;;) {
            if (writeback_[2] == sequence_ && writeback_[0] == cursor_) {
                pending_ = false;
                return last_status_ = STATUS_SUCCESS;
            }
            timespec now = {};
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < started.tv_sec)
                return last_status_ = STATUS_UNSUCCESSFUL;
            const std::int64_t seconds = static_cast<std::int64_t>(now.tv_sec - started.tv_sec);
            if (seconds > static_cast<std::int64_t>(wait_timeout_ms / 1000U) + 1) break;
            const std::int64_t elapsed_ms = seconds * 1000 + (now.tv_nsec - started.tv_nsec) / 1000000;
            if (elapsed_ms >= wait_timeout_ms) break;
            xsdk_thread_sleep(1);
        }
        xsdk_log_write("presentation: minimal ring timeout, WPTR%lu RPTR%lu fence%lu expected%lu\n",
            static_cast<unsigned long>(cursor_), static_cast<unsigned long>(writeback_[0]),
            static_cast<unsigned long>(writeback_[2]), static_cast<unsigned long>(sequence_));
        return last_status_ = STATUS_IO_TIMEOUT;
    }
};

class Presentation {
public:
    Presentation() : gpu_open_(false), console_open_(false), emulator_(false), minimal_xenia_(false),
        width_(0), height_(0), next_(0), frames_(), fences_(), writeback_(0), swaps_(0) {}
    ~Presentation() { close(); }
    void open(const Config& config)
    {
        if (config.present == PresentMode::Xenia) {
            emulator_ = true;
            minimal_xenia_ = true;
            width_ = 1280;
            height_ = 720;
            xsdk_log_write("presentation: explicit Xenia mode; opening fresh minimal primary ring BEFORE any physical GPU setup\n");
            primary_.open();
            create_textures();
            xsdk_log_write("presentation: Xenia-only VdSwap,1280x720,two CPU-filled textures; visible presentation awaiting validation\n");
            return;
        }
        // The console display requires the GPU first. Preserve the SDK path for
        // console/auto modes; never reinitialize a failed ring as a fresh one.
        status_ok(xsdk_gpu_open(), "xsdk_gpu_open");
        gpu_open_ = true;
        const NTSTATUS result = xsdk_present_open(XSDK_DISPLAY_720P, config.buffer_count,
            config.swap_interval, &width_, &height_);
        if (NT_SUCCESS(result)) {
            console_open_ = true;
            xsdk_log_write("presentation: xsdk_present,%lux%lu,CPU tiled BGRA upload; console execution is not yet validated\n",
                static_cast<unsigned long>(width_), static_cast<unsigned long>(height_));
            return;
        }
        if (result != STATUS_NOT_SUPPORTED || config.present == PresentMode::Console || !xenia_kernel())
            status_ok(result, "xsdk_present_open");
        xsdk_log_write("presentation: physical display unavailable; retaining initialized SDK queue for guarded legacy Xenia fallback\n");
        emulator_ = true;
        width_ = 1280;
        height_ = 720;
        create_textures();
        writeback_ = xsdk_gpu_alloc(128);
        if (!writeback_) throw std::runtime_error("cannot allocate swap writeback memory");
        writeback_[0] = 0;
        VdSetSystemCommandBufferGpuIdentifierAddress(writeback_);
    }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    bool emulator() const { return emulator_; }
    void show(const std::vector<std::uint32_t>& words)
    {
        if (words.size() != static_cast<std::size_t>(width_) * height_)
            throw std::runtime_error("display frame size mismatch");
        if (console_open_) {
            std::uint32_t address = 0;
            status_ok(xsdk_present_begin(&address, wait_timeout_ms), "xsdk_present_begin");
            XsdkTexture frame = {};
            status_ok(xsdk_texture_init(&frame, XSDK_FORMAT_B8G8R8A8,
                width_, height_, 1, true), "xsdk_texture_init(display frame)");
            xsdk_texture_base_at_corner(&frame);
            xsdk_texture_attach(&frame, xsdk_present_pixels(address));
            xsdk_texture_upload(&frame, 0, words.data(), width_ * 4U);
            xsdk_gpu_invalidate(address, frame.size_bytes);
            // No resolve: eDRAM would overwrite this CPU-filled image.
            xsdk_present_end();
        } else {
            XsdkTexture& frame = frames_[next_];
            if (!minimal_xenia_ && fences_[next_])
                status_ok(xsdk_gpu_wait(fences_[next_], wait_timeout_ms), "xsdk_gpu_wait(reuse)");
            xsdk_texture_upload(&frame, 0, words.data(), width_ * 4U);
            const std::uintptr_t cpu_address = reinterpret_cast<std::uintptr_t>(frame.texels);
            if (cpu_address > (std::numeric_limits<std::uint32_t>::max)())
                throw std::runtime_error("swap CPU address exceeds 32 bits");
            std::uint32_t front = static_cast<std::uint32_t>(cpu_address);
            const std::array<std::uint32_t, 6> fetch = swap_fetch(frame.fetch, front);
            if (minimal_xenia_) {
                // Upload drains CPU stores; Canary's swap reads this supplied
                // texture directly. No render-engine state/resolve is needed.
                primary_.swap(fetch, front, width_, height_);
            } else {
                xsdk_gpu_invalidate(frame.address, frame.size_bytes);
                std::array<std::uint32_t, 0x94 / 4> system = {};
                std::uint32_t system_value = 0;
                VdGetSystemCommandBuffer(system.data(), &system_value);
                std::uint32_t format = 6, color_space = 0, width = width_, height = height_;
                VdSwap(xsdk_gpu_reserve(64), fetch.data(), writeback_, system.data(), system_value,
                    &front, &format, &color_space, &width, &height);
                fences_[next_] = xsdk_gpu_fence();
                status_ok(xsdk_gpu_wait(fences_[next_], wait_timeout_ms), "xsdk_gpu_wait(swap)");
            }
            next_ = (next_ + 1U) % 2U;
        }
        ++swaps_;
    }
    void close()
    {
        if (!gpu_open_ && !minimal_xenia_) return;
        if (minimal_xenia_) {
            const bool drained = primary_.close();
            if (drained) free_textures();
            else {
                for (std::size_t i = 0; i < 2; ++i) frames_[i].texels = 0;
                xsdk_log_write("presentation: retaining undrained Xenia texture memory for title teardown\n");
            }
            minimal_xenia_ = false;
        } else {
            if (console_open_) { xsdk_present_close(); console_open_ = false; }
            if (emulator_) {
                for (std::size_t i = 0; i < 2; ++i) {
                    if (fences_[i]) {
                        const NTSTATUS waited = xsdk_gpu_wait(fences_[i], wait_timeout_ms);
                        if (!NT_SUCCESS(waited)) xsdk_log_write("presentation close: fence wait0x%08lX\n", static_cast<unsigned long>(waited));
                    }
                }
                VdSetSystemCommandBufferGpuIdentifierAddress(0);
                free_textures();
                if (writeback_) { xsdk_gpu_free(writeback_); writeback_ = 0; }
            }
            const NTSTATUS closed = xsdk_gpu_close();
            if (!NT_SUCCESS(closed)) xsdk_log_write("xsdk_gpu_close failed:0x%08lX\n", static_cast<unsigned long>(closed));
            gpu_open_ = false;
        }
        xsdk_log_write("presentation close:%lu submitted frames\n", static_cast<unsigned long>(swaps_));
    }
private:
    Presentation(const Presentation&);
    Presentation& operator=(const Presentation&);
    bool gpu_open_, console_open_, emulator_, minimal_xenia_;
    std::uint32_t width_, height_, next_;
    XsdkTexture frames_[2];
    std::uint32_t fences_[2];
    volatile std::uint32_t* writeback_;
    std::uint32_t swaps_;
    XeniaPrimaryRing primary_;
    void create_textures()
    {
        for (std::size_t i = 0; i < 2; ++i) {
            status_ok(xsdk_texture_create(&frames_[i], XSDK_FORMAT_B8G8R8A8,
                width_, height_, 1, true), "xsdk_texture_create(Xenia front buffer)");
            xsdk_texture_base_at_corner(&frames_[i]);
        }
    }
    void free_textures()
    {
        for (std::size_t i = 0; i < 2; ++i) {
            if (frames_[i].texels) xsdk_texture_free(&frames_[i]);
            frames_[i].texels = 0;
        }
    }
};

struct NativeClockTrace {
    std::uint32_t sequence;
    std::array<std::uint32_t,2> seconds;
    std::array<std::uint32_t,2> nanoseconds;
    std::array<std::uint32_t,2> computed;
    NativeClockTrace() : sequence(0), seconds(), nanoseconds(), computed() {}
};
NativeClockTrace clock_trace;

std::array<std::uint32_t,2> raw_clock_words(const void* value, std::size_t bytes)
{
    std::array<std::uint32_t,2> result = {};
    // Native PowerPC memory is big-endian. Copy bytes, without shifts or
    // 64-bit varargs, to observe the actual stored high/low words.
    std::memcpy(result.data(), value, (std::min)(bytes, sizeof(result)));
    return result;
}

void trace_clock_at(const char* point, const runtime::Tick& received, std::uint32_t frame)
{
    const std::array<std::uint32_t,2> value = raw_clock_words(&received, sizeof(received));
    xsdk_log_write("clock trace %s frame%lu sample%lu: sec[%luB]=%08lX,%08lX nsec[%luB]=%08lX,%08lX computed=%08lX,%08lX received=%08lX,%08lX\n",
        point, static_cast<unsigned long>(frame), static_cast<unsigned long>(clock_trace.sequence),
        static_cast<unsigned long>(sizeof(time_t)), static_cast<unsigned long>(clock_trace.seconds[0]),
        static_cast<unsigned long>(clock_trace.seconds[1]), static_cast<unsigned long>(sizeof(long)),
        static_cast<unsigned long>(clock_trace.nanoseconds[0]), static_cast<unsigned long>(clock_trace.nanoseconds[1]),
        static_cast<unsigned long>(clock_trace.computed[0]), static_cast<unsigned long>(clock_trace.computed[1]),
        static_cast<unsigned long>(value[0]), static_cast<unsigned long>(value[1]));
}

runtime::Tick monotonic_time()
{
    timespec now = {};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1000000000)
        throw std::runtime_error("CLOCK_MONOTONIC is unavailable or invalid");
    if (static_cast<std::uint64_t>(now.tv_sec) >
        static_cast<std::uint64_t>(((std::numeric_limits<runtime::Tick>::max)() - now.tv_nsec) / runtime::ticks_per_second))
        throw std::runtime_error("monotonic clock exceeds runtime range");
    const runtime::Tick result = static_cast<runtime::Tick>(now.tv_sec) * runtime::ticks_per_second + now.tv_nsec;
    const std::array<std::uint32_t,2> sec = raw_clock_words(&now.tv_sec, sizeof(now.tv_sec));
    const std::array<std::uint32_t,2> nsec = raw_clock_words(&now.tv_nsec, sizeof(now.tv_nsec));
    const std::array<std::uint32_t,2> computed = raw_clock_words(&result, sizeof(result));
    const bool changed = sec != clock_trace.seconds ||
        ((computed[1] ^ clock_trace.computed[1]) & 0x80000000U) != 0;
    ++clock_trace.sequence;
    clock_trace.seconds = sec;
    clock_trace.nanoseconds = nsec;
    clock_trace.computed = computed;
    // Use the stored high word for this diagnostic decision: a broken native
    // record-form comparison must not turn tracing into a per-poll log flood.
    if (clock_trace.sequence <= 3 || changed || (computed[0] & 0x80000000U) != 0)
        trace_clock_at("monotonic conversion", result, 0);
    return result;
}

platform::ControllerSnapshot controller_snapshot(std::uint32_t index)
{
    platform::ControllerSnapshot result;
    INPUT_STATE raw = {};
    if (XamInputGetState(index, 0, &raw) != 0) return result;
    result.connected = true;
    struct Mapping { std::uint16_t native; std::uint32_t portable; };
    static const Mapping mapping[] = {
        {INPUT_GAMEPAD_A, platform::PadA}, {INPUT_GAMEPAD_B, platform::PadB},
        {INPUT_GAMEPAD_X, platform::PadX}, {INPUT_GAMEPAD_Y, platform::PadY},
        {INPUT_GAMEPAD_START, platform::PadStart}, {INPUT_GAMEPAD_BACK, platform::PadBack},
        {INPUT_GAMEPAD_LEFT_SHOULDER, platform::PadLeftShoulder},
        {INPUT_GAMEPAD_RIGHT_SHOULDER, platform::PadRightShoulder},
        {INPUT_GAMEPAD_DPAD_LEFT, platform::PadDpadLeft}, {INPUT_GAMEPAD_DPAD_RIGHT, platform::PadDpadRight},
        {INPUT_GAMEPAD_DPAD_UP, platform::PadDpadUp}, {INPUT_GAMEPAD_DPAD_DOWN, platform::PadDpadDown},
        {INPUT_GAMEPAD_LEFT_THUMB, platform::PadLeftThumb}, {INPUT_GAMEPAD_RIGHT_THUMB, platform::PadRightThumb}
    };
    for (std::size_t i = 0; i < sizeof(mapping) / sizeof(mapping[0]); ++i)
        if (raw.gamepad.buttons & mapping[i].native) result.buttons |= mapping[i].portable;
    result.left_trigger = raw.gamepad.left_trigger;
    result.right_trigger = raw.gamepad.right_trigger;
    result.left_stick_x = raw.gamepad.thumb_lx;
    result.left_stick_y = raw.gamepad.thumb_ly;
    return result;
}

// Original minimal diagnostic glyphs; these are not original game assets or
// copied SDK font data. Each row is five on/off cells.
std::array<std::uint8_t, 7> glyph(char character)
{
    switch (character) {
    case 'A': return {{14,17,17,31,17,17,17}};
    case 'B': return {{30,17,17,30,17,17,30}};
    case 'C': return {{14,17,16,16,16,17,14}};
    case 'D': return {{30,17,17,17,17,17,30}};
    case 'E': return {{31,16,16,30,16,16,31}};
    case 'F': return {{31,16,16,30,16,16,16}};
    case 'G': return {{14,17,16,23,17,17,14}};
    case 'H': return {{17,17,17,31,17,17,17}};
    case 'I': return {{14,4,4,4,4,4,14}};
    case 'J': return {{7,2,2,2,18,18,12}};
    case 'K': return {{17,18,20,24,20,18,17}};
    case 'L': return {{16,16,16,16,16,16,31}};
    case 'M': return {{17,27,21,21,17,17,17}};
    case 'N': return {{17,25,21,19,17,17,17}};
    case 'O': return {{14,17,17,17,17,17,14}};
    case 'P': return {{30,17,17,30,16,16,16}};
    case 'Q': return {{14,17,17,17,21,18,13}};
    case 'R': return {{30,17,17,30,20,18,17}};
    case 'S': return {{15,16,16,14,1,1,30}};
    case 'T': return {{31,4,4,4,4,4,4}};
    case 'U': return {{17,17,17,17,17,17,14}};
    case 'V': return {{17,17,17,17,17,10,4}};
    case 'W': return {{17,17,17,21,21,21,10}};
    case 'X': return {{17,17,10,4,10,17,17}};
    case 'Y': return {{17,17,10,4,4,4,4}};
    case 'Z': return {{31,1,2,4,8,16,31}};
    case '/': return {{1,1,2,4,8,16,16}};
    case '-': return {{0,0,0,31,0,0,0}};
    case '|': return {{4,4,4,4,4,4,4}};
    default: return {{0,0,0,0,0,0,0}};
    }
}

void text(std::vector<std::uint32_t>& frame, std::uint32_t width, std::uint32_t height,
          std::uint32_t x, std::uint32_t y, std::uint32_t scale, const std::string& value,
          std::uint32_t color)
{
    for (std::size_t c = 0; c < value.size(); ++c) {
        const std::array<std::uint8_t, 7> rows = glyph(value[c]);
        for (std::uint32_t gy = 0; gy < 7; ++gy) {
            for (std::uint32_t gx = 0; gx < 5; ++gx) {
                if (!(rows[gy] & (1U << (4U - gx)))) continue;
                for (std::uint32_t dy = 0; dy < scale; ++dy) {
                    for (std::uint32_t dx = 0; dx < scale; ++dx) {
                        const std::uint64_t px = std::uint64_t(x) + c * 6U * scale + gx * scale + dx;
                        const std::uint64_t py = std::uint64_t(y) + gy * scale + dy;
                        if (px < width && py < height) frame[static_cast<std::size_t>(py) * width + static_cast<std::size_t>(px)] = color;
                    }
                }
            }
        }
    }
}

// Real-clock timeout is kept outside the paused simulation clock. Compare
// unsigned words explicitly, avoiding the old native64-bit record-form issue.
bool menu_attempt_expired(runtime::Tick now,runtime::Tick activity)
{
    const std::uint64_t elapsed=static_cast<std::uint64_t>(now)-static_cast<std::uint64_t>(activity);
    const std::uint32_t high=static_cast<std::uint32_t>(elapsed>>32U),low=static_cast<std::uint32_t>(elapsed);
    return high>1U||(high==1U&&low>=705032704U); // Five billion nanoseconds.
}

platform::ControllerSnapshot menu_smoke_pad(std::uint32_t frame,const std::vector<std::uint32_t>& sequence)
{
    platform::ControllerSnapshot pad;pad.connected=true;
    if(frame>=2&&frame<42) {
        const std::uint32_t step=(frame-2)/4;
        if(step<sequence.size()&&(frame-2)%4<2)pad.buttons=sequence[step];
    }
    struct Command {std::uint32_t frame,button;};
    const Command commands[]={
        {50,platform::PadA},{60,platform::PadB},{70,platform::PadBack},
        {80,platform::PadDpadDown},{90,platform::PadA},{100,platform::PadDpadDown},
        {110,platform::PadA},{120,platform::PadDpadDown},{130,platform::PadA},
        {140,platform::PadDpadDown},{150,platform::PadA},{160,platform::PadB},
        {170,platform::PadY},{180,platform::PadBack},{190,platform::PadB},
        {200,platform::PadBack},{210,platform::PadB}};
    for(std::size_t i=0;i<sizeof(commands)/sizeof(commands[0]);++i)
        if(frame>=commands[i].frame&&frame<commands[i].frame+2)pad.buttons=commands[i].button;
    return pad;
}

void menu_overlay(std::vector<std::uint32_t>& frame,std::uint32_t width,std::uint32_t height,
                  const rules::CheatMenu& menu,const development::MotionSnapshot& state,const std::string& feedback)
{
    if(!menu.isOpen())return;
    const std::uint32_t panel_width=(std::min)(width,600U),panel_height=(std::min)(height,330U);
    const std::uint32_t x=(width-panel_width)/2,y=(height-panel_height)/2;
    const std::uint32_t dark=display_word(12,18,28,255),bar=display_word(44,66,94,255);
    const std::uint32_t white=display_word(240,240,240,255),yellow=display_word(255,210,80,255);
    for(std::uint32_t py=y;py<y+panel_height;++py)
        for(std::uint32_t px=x;px<x+panel_width;++px)frame[std::size_t(py)*width+px]=dark;
    text(frame,width,height,x+20,y+16,2,"CHEAT MENU - GAME PAUSED",yellow);
    const std::string rows[]={std::string("HIDDEN TEST - ")+(state.hidden_test?"ON":"OFF"),
        "EXTRA BALL",std::string("UNLIMITED BALLS - ")+(state.unlimited_balls?"ON":"OFF"),
        std::string("GRAVITY WELL - ")+(state.gravity_well_active?"ON":"OFF"),"RANK UP","CLOSE"};
    for(std::size_t i=0;i<6;++i) {
        const std::uint32_t row_y=y+50+static_cast<std::uint32_t>(i)*32;
        if(menu.selection()==i)for(std::uint32_t py=row_y-5;py<(std::min)(row_y+22,height);++py)
            for(std::uint32_t px=x+12;px<(std::min)(x+panel_width-12,width);++px)frame[std::size_t(py)*width+px]=bar;
        text(frame,width,height,x+24,row_y,2,rows[i],menu.selection()==i?yellow:white);
    }
    text(frame,width,height,x+20,y+250,1,"DPAD UP AND DOWN SELECT - A ACTIVATE",white);
    text(frame,width,height,x+20,y+267,1,"B OR BACK CLOSE - Y NEW GAME",white);
    text(frame,width,height,x+20,y+288,1,feedback.empty()?"UNLOCK STAYS UNTIL THE APP CLOSES":feedback,yellow);
    text(frame,width,height,x+20,y+307,1,"LEFT STICK STEERS IN HIDDEN TEST",white);
}
startup::AudioState startup_audio_state(const audio::XboxAudioOutput& output)
{
    const audio::AudioOutputStats state=output.stats();
    return startup::AudioState(state.running,state.mixer.active_voices,state.submitted,state.played);
}
struct StartupPlayback {
    startup::Sequence sequence;
    bool enabled,has_feed;
    std::vector<audio::SoundCue> deferred;
    audio::SoundCue feed;
    std::uint32_t intro_requests,intro_accepted,feed_requests,feed_accepted,flushes,playing;
    StartupPlayback(bool on,runtime::Tick feed_delay,runtime::Tick now)
        :sequence(startup::SequenceConfig(5020408164LL,feed_delay),now),enabled(on),has_feed(false),deferred(),feed(),
         intro_requests(0),intro_accepted(0),feed_requests(0),feed_accepted(0),flushes(0),playing(0) {deferred.reserve(16);}
    void flush(audio::XboxAudioOutput& output,const audio::GameSoundBank* bank,bool external_pause)
    {
        output.stop();if(bank)output.start();output.set_paused(external_pause);++flushes;
    }
    void begin(development::PlayfieldTest& game,audio::XboxAudioOutput& output,const audio::GameSoundBank* bank,
               runtime::Tick now,bool external_pause,bool restarting)
    {
        if(!enabled){sequence.bypass(now);return;}
        deferred.clear();has_feed=false;
        if(restarting)flush(output,bank,external_pause);else{output.stop_all();output.set_paused(external_pause);}
        std::vector<audio::SoundCue> cues;game.drain_sounds(cues);audio::VoiceId intro=0;bool requested=false;
        for(const audio::SoundCue& cue:cues) {
            if(cue.role==audio::SoundRole::TableStart) {
                if(!requested){requested=true;++intro_requests;if(bank)intro=output.play(bank->clip(cue),cue.gain,cue.pan);}
            } else if(cue.role==audio::SoundRole::PlungerFeed) {if(!has_feed){feed=cue;has_feed=true;}}
            else deferred.push_back(cue);
        }
        if(intro)++intro_accepted;
        sequence.restart(now,intro!=0,startup_audio_state(output));
        xsdk_log_write("startup: intro generation%lu accepted%lu feed_deferred%lu\n",
            static_cast<unsigned long>(sequence.snapshot().generation),static_cast<unsigned long>(intro!=0),static_cast<unsigned long>(has_feed));
    }
    startup::Events advance(runtime::Tick now,bool external_pause,audio::XboxAudioOutput& output,const audio::GameSoundBank* bank)
    {
        output.set_paused(external_pause); // Startup itself freezes only gameplay.
        const startup::Events event=sequence.advance(now,external_pause,startup_audio_state(output));
        if(event.play_feed) {
            if(event.flush_audio)flush(output,bank,external_pause);
            if(has_feed){++feed_requests;if(bank&&output.play(bank->clip(feed),feed.gain,feed.pan))++feed_accepted;}
            for(const audio::SoundCue& cue:deferred)if(bank)output.play(bank->clip(cue),cue.gain,cue.pan);
            deferred.clear();has_feed=false;
            xsdk_log_write("startup: intro finished generation%lu feed_requested%lu fallback_flush%lu\n",
                static_cast<unsigned long>(sequence.snapshot().generation),static_cast<unsigned long>(feed_requests),static_cast<unsigned long>(event.flush_audio));
        }
        if(event.became_playing){++playing;xsdk_log_write("startup: feed delay finished generation%lu ball visible\n",static_cast<unsigned long>(sequence.snapshot().generation));}
        return event;
    }
};
platform::ControllerSnapshot startup_smoke_pad(std::uint32_t frame,std::uint32_t visible_frame)
{
    platform::ControllerSnapshot pad;pad.connected=true;
    const bool visible=visible_frame!=(std::numeric_limits<std::uint32_t>::max)();
    if((frame>=10&&frame<30)||(frame>=160&&(!visible||frame<visible_frame+30))||
       (visible&&frame>=visible_frame+50&&frame<visible_frame+210))pad.buttons=platform::PadA;
    if(frame>=40&&frame<60)pad.buttons=platform::PadDpadDown;
    if((frame>=80&&frame<82)||(frame>=110&&frame<112)||
       (visible&&frame>=visible_frame+250&&frame<visible_frame+252))pad.buttons=platform::PadStart;
    if(frame>=150&&frame<152)pad.buttons=platform::PadY;
    return pad;
}
Config configuration(int argc, char** argv)
{
    Config config;
    std::string path = "game:\\pinball360.ini";
    bool explicit_path = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.compare(0, 9, "--config=") == 0) { path = arg.substr(9); explicit_path = true; }
    }
    std::ifstream input(path.c_str());
    if (input) read_settings(input, config);
    else if (explicit_path) throw std::runtime_error("cannot open requested configuration: " + path);
    else xsdk_log_write("configuration: no game:\\pinball360.ini; using temporary viewer defaults\n");
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg.compare(0, 9, "--config=") == 0) continue;
        const std::size_t equal = arg.find('=');
        if (arg.compare(0, 2, "--") != 0 || equal == std::string::npos)
            throw std::runtime_error("arguments must use --setting=value");
        setting(config, arg.substr(2, equal - 2U), arg.substr(equal + 1U));
    }
    validate_config(config);
    return config;
}

// Development-only replay: normal event/physics/rules APIs, no user controls.
// Fast simulation removes wall-clock waiting while preserving all rule durations.
struct BankReplay {
    std::vector<motion::BallState> shots;
    motion::BallState bumper,gate,ramp,drain;
    std::array<std::size_t,3> target_groups;
    std::array<std::size_t,4> lamp_groups;
    std::uint32_t stage,hits,cycles,award_mask,drop_mask,raise_mask,lamp_on,lamp_off,lamp_steady,wait_frames;
    bool saved_preserved,loss_reset,exhausted;
    BankReplay(const table::TableModel& model,bool enabled)
        :bumper(physics::Vec2(),physics::Vec2(),.3f),gate(bumper),ramp(bumper),drain(bumper),
         target_groups(),lamp_groups(),stage(0),hits(0),cycles(0),award_mask(0),drop_mask(0),raise_mask(0),
         lamp_on(0),lamp_off(0),lamp_steady(0),wait_frames(0),saved_preserved(false),loss_reset(false),exhausted(false) {
        if(!enabled)return;
        const reference::TablePhysics p(model);
        for(unsigned i=0;i<3;++i) {
            target_groups[i]=p.multiplier_bank.targets[i].group;
            // Second-face approaches were checked against the complete profile.
            shots.push_back(approach(p,p.multiplier_bank.targets[i].collider_indices.at(1),true));
        }
        for(unsigned i=0;i<4;++i)lamp_groups[i]=p.multiplier_bank.lamps[i].group;
        const motion::StaticCollider& circle=p.static_colliders.at(p.components.at(0).collider_indices.at(0));
        const physics::Vec2 radial(.9238795f,.3826834f);
        bumper=motion::BallState(physics::add(circle.first,physics::multiply(radial,circle.radius+p.ball_radius+.02f)),
            physics::multiply(radial,-20),p.ball_radius);
        gate=approach(p,p.rules.skill_gate_collider_index,false);
        ramp=approach(p,p.rules.ramp_score_collider_index,false);ramp.height=1.360704f;
        drain=approach(p,p.rules.drain_collider_index,true);
    }
    static motion::BallState approach(const reference::TablePhysics& p,std::size_t index,bool inflated) {
        const motion::StaticCollider& line=p.static_colliders.at(index);
        const physics::Vec2 axis=physics::subtract(line.second,line.first);
        const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
        const physics::Vec2 center=physics::multiply(physics::add(line.first,line.second),.5f);
        motion::BallState state(physics::add(center,physics::multiply(front,.02f+(inflated?p.ball_radius:0))),
            physics::multiply(front,-20),p.ball_radius);state.collision_mask=line.collision_mask;return state;
    }
    void hidden(development::PlayfieldTest& game,bool enabled) {
        if(game.snapshot().hidden_test!=enabled) {
            const runtime::InputTransition edge={1,0x50494e47U,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};
            game.input(edge);
        }
        game.set_test_stick(0,0);
    }
    void shot(development::PlayfieldTest& game,const motion::BallState& state) {
        hidden(game,false);game.set_replay_ball(state);game.advance(runtime::ticks_per_second/120);hidden(game,true);
    }
    void advance(development::PlayfieldTest& game) {
        static const std::uint32_t scores[]={500,1000,2500,3500,4500,7500,9000,10500,15000,17500,20000,27500,32500,37500,52500};
        static const std::uint32_t factors[]={2,3,5,10,10};
        if(stage==0) {
            shot(game,shots.at(hits%3));const development::MotionSnapshot state=game.snapshot();
            if(state.target_hits!=hits+1 || static_cast<std::uint32_t>(state.score)!=scores[hits])
                throw std::runtime_error("bank replay hit/old-multiplier award mismatch");
            award_mask|=1U<<hits;++hits;
            if(hits%3==0) {
                if(state.bank_completions!=cycles+1 || state.score_multiplier!=factors[cycles])
                    throw std::runtime_error("bank replay completion/factor mismatch");
                ++cycles;stage=1;
            }
        } else if(stage==1){game.advance(100000000);stage=2;}
        else if(stage==2){game.advance(100000000);stage=3;}
        else if(stage==3) {
            game.advance(50000000);const rules::TargetBankSnapshot bank=game.target_bank_snapshot();
            if(!bank.raised[0]||!bank.raised[1]||!bank.raised[2])throw std::runtime_error("bank targets did not rearm after250ms");
            stage=hits<15?0:4;
        } else if(stage==4) {
            shot(game,bumper);if(static_cast<std::uint32_t>(game.snapshot().score)!=57500)
                throw std::runtime_error("ordinary bumper score did not use10x");stage=5;
        } else if(stage==5){shot(game,gate);stage=6;}
        else if(stage==6) {
            shot(game,ramp);if(static_cast<std::uint32_t>(game.snapshot().score)!=82500)
                throw std::runtime_error("reflex special score was multiplied");stage=7;
        } else if(stage==7) {
            hidden(game,false);game.set_replay_ball(drain);game.advance(runtime::ticks_per_second/120);
            const development::MotionSnapshot state=game.snapshot();
            saved_preserved=state.saved_drains==1&&state.balls_remaining==3&&state.score_multiplier==10&&
                static_cast<std::uint32_t>(state.score)==82500;
            if(!saved_preserved)throw std::runtime_error("protected drain reset multiplier or consumed ball/bonus");stage=8;
        } else if(stage==8) {
            game.advance(runtime::ticks_per_second);
            if(game.snapshot().ball_active){shot(game,gate);wait_frames=0;stage=9;}
        } else if(stage==9) {
            game.advance(runtime::ticks_per_second);++wait_frames;
            if(!game.snapshot().shoot_again){hidden(game,false);game.set_replay_ball(drain);stage=10;}
            else if(wait_frames>15)throw std::runtime_error("bank replay save timer did not expire");
        } else if(stage==10) {
            game.advance(runtime::ticks_per_second/120);const development::MotionSnapshot state=game.snapshot();
            const rules::TargetBankSnapshot bank=game.target_bank_snapshot();
            loss_reset=state.drains==2&&state.saved_drains==1&&state.balls_remaining==2&&state.score_multiplier==1&&
                static_cast<std::uint32_t>(state.score)==92500&&bank.tier==0&&bank.raised[0]&&bank.raised[1]&&bank.raised[2];
            if(!loss_reset)throw std::runtime_error("charged drain bonus/reset was multiplied or lost");stage=11;
        } else if(stage==11){game.advance(runtime::ticks_per_second);if(game.snapshot().ball_active)stage=12;}
        const development::MotionSnapshot state=game.snapshot();
        exhausted=exhausted||state.exhausted_steps||state.moving_exhausted;
    }
    void inspect(const development::PlayfieldTest& game,const scene::ResourceScene& scene) {
        const rules::TargetBankSnapshot bank=game.target_bank_snapshot();
        for(unsigned t=0;t<3;++t) {
            bool found=false;
            for(std::size_t i=0;i<scene.instances.size();++i)if(scene.instances[i].resource_group==target_groups[t]) {
                found=true;if(scene.instances[i].visible!=bank.raised[t])throw std::runtime_error("target image/drop visibility mismatch");
                if(bank.raised[t])raise_mask|=1U<<t;else drop_mask|=1U<<t;
            }
            if(!found)throw std::runtime_error("target sprite missing from scene");
        }
        for(unsigned l=0;l<4;++l) {
            bool found=false;
            for(std::size_t i=0;i<scene.instances.size();++i)if(scene.instances[i].resource_group==lamp_groups[l]) {
                found=true;if(scene.instances[i].visible!=bank.lamp_visible[l])throw std::runtime_error("multiplier lamp wiring mismatch");
                if(bank.lamp_modes[l]==rules::BankLampMode::Flashing) {
                    if(bank.lamp_visible[l])lamp_on|=1U<<l;else lamp_off|=1U<<l;
                } else if(bank.lamp_modes[l]==rules::BankLampMode::Steady&&bank.lamp_visible[l])lamp_steady|=1U<<l;
            }
            if(!found)throw std::runtime_error("multiplier lamp missing from scene");
        }
    }
};

struct WormholeReplay {
    std::vector<motion::BallState> starts;
    std::vector<physics::Vec2> exits;
    std::uint32_t selected,phase,pass_mask,wait_frames,flight_frames;
    bool exhausted;
    WormholeReplay(const table::TableModel& model,bool enabled)
        :selected(6),phase(0),pass_mask(0),wait_frames(0),flight_frames(0),exhausted(false) {
        if(!enabled)return;
        const reference::TablePhysics p(model);
        for(std::size_t i=0;i<p.sinks.size();++i) {
            const motion::StaticCollider& gate=p.static_colliders.at(p.sinks[i].collider_index);
            const physics::Vec2 axis=physics::subtract(gate.second,gate.first);
            const physics::Vec2 front=physics::unit(physics::Vec2(-axis.y,axis.x));
            const physics::Vec2 center=physics::multiply(physics::add(gate.first,gate.second),.5f);
            starts.push_back(motion::BallState(physics::add(center,physics::multiply(front,p.ball_radius+.08f)),
                physics::multiply(front,-20),p.ball_radius));
            exits.push_back(p.sinks[i].position);
        }
        if(starts.size()!=3)throw std::runtime_error("wormhole replay requires three default routes");
    }
    void advance(development::PlayfieldTest& game,const table::TableModel& model,std::uint32_t frame) {
        const std::uint32_t index=(std::min)(frame/20,5U);
        if(index!=selected) {
            selected=index;phase=wait_frames=flight_frames=0;
            game=development::PlayfieldTest(model,starts.at(index%3));
        }
        const runtime::Tick tick=runtime::ticks_per_second/120;
        if(phase==0 || phase==1) {
            for(unsigned step=0;step<60;++step) {
                game.advance(tick);
                const development::MotionSnapshot state=game.snapshot();
                if(phase==0 && state.captured) {
                    if(state.sink_captures!=1 || static_cast<std::uint32_t>(state.score)!=2500)
                        throw std::runtime_error("wormhole replay initial capture mismatch");
                    if(selected>=3) {
                        const runtime::InputTransition edge={1,0x50494e46U,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};
                        game.input(edge);game.set_test_stick(0,0);
                    }
                    phase=1;break;
                }
                if(phase==1 && state.sink_ejections!=0) {
                    if(state.captured || state.sink_captures!=1 || state.sink_ejections!=1)
                        throw std::runtime_error("wormhole recaptured on its ejection frame");
                    phase=2;break;
                }
            }
            if(++wait_frames>10)throw std::runtime_error("wormhole replay capture/release timed out");
        } else if(phase==2) {
            game.advance(selected<3?250000000:runtime::ticks_per_second);++flight_frames;
            const development::MotionSnapshot state=game.snapshot();
            if(state.captured || state.sink_captures!=1 || state.sink_ejections!=1)
                throw std::runtime_error("wormhole could not leave its own entrance");
            if(selected>=3 && static_cast<std::uint32_t>(state.score)!=2500)
                throw std::runtime_error("neutral wormhole flight repeated its score");
            if(selected<3 || flight_frames==10) {
                if(physics::length(physics::subtract(state.ball.position,exits.at(selected%3)))<=state.ball.radius)
                    throw std::runtime_error("wormhole did not clear release position");
                pass_mask|=1U<<selected;phase=3;
            }
        }
        const development::MotionSnapshot state=game.snapshot();
        exhausted=exhausted || state.exhausted_steps || state.moving_exhausted;
        if(frame%20==19 && phase!=3)throw std::runtime_error("wormhole case incomplete");
    }
};

struct RoundReplay {
    motion::BallState bumper,gate,drain;
    std::uint32_t stage,losses,loss_mask,ball_mask,wait_frames;
    bool score_seen,hud_seen;
    explicit RoundReplay(const table::TableModel& model,bool enabled)
        :bumper(physics::Vec2(),physics::Vec2(),.3f),gate(bumper),drain(bumper),
         stage(0),losses(0),loss_mask(0),ball_mask(0),wait_frames(0),score_seen(false),hud_seen(false) {
        if(!enabled)return;
        const reference::TablePhysics p(model);
        const motion::StaticCollider& circle=p.static_colliders.at(p.components.at(0).collider_indices.at(0));
        const physics::Vec2 radial(.9238795f,.3826834f);
        bumper=motion::BallState(physics::add(circle.first,physics::multiply(radial,circle.radius+p.ball_radius+.02f)),
            physics::multiply(radial,-20),p.ball_radius);
        const std::size_t indices[]={p.rules.skill_gate_collider_index,p.rules.drain_collider_index};
        for(unsigned i=0;i<2;++i) {
            const motion::StaticCollider& line=p.static_colliders.at(indices[i]);
            const physics::Vec2 edge=physics::subtract(line.second,line.first);
            const physics::Vec2 normal=physics::unit(physics::Vec2(-edge.y,edge.x));
            const physics::Vec2 center=physics::multiply(physics::add(line.first,line.second),.5f);
            motion::BallState start(physics::add(center,physics::multiply(normal,.02f+(i?p.ball_radius:0))),
                physics::multiply(normal,-20),p.ball_radius);
            start.collision_mask=line.collision_mask;
            if(i)drain=start;else gate=start;
        }
    }
    void hidden(development::PlayfieldTest& game,bool enabled) {
        if(game.snapshot().hidden_test!=enabled) {
            const runtime::InputTransition edge={1,0x50494e45U,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};
            game.input(edge);
        }
        game.set_test_stick(0,0);
    }
    void advance(development::PlayfieldTest& game) {
        const runtime::Tick tick=runtime::ticks_per_second/120;
        if(stage==0) {
            game.advance(tick);
            if(game.snapshot().bumper_hits) {
                if(static_cast<std::uint32_t>(game.snapshot().score)!=500)throw std::runtime_error("round replay bumper award mismatch");
                score_seen=true;game.set_replay_ball(gate);stage=1;
            }
        } else if(stage==1) {
            game.advance(tick);hidden(game,true);stage=2;wait_frames=0;
        } else if(stage==2) {
            game.advance(runtime::ticks_per_second);++wait_frames;
            if(!game.snapshot().shoot_again) {
                hidden(game,false);game.set_replay_ball(drain);stage=3;
            } else if(wait_frames>15)throw std::runtime_error("round replay launch gate did not start save timer");
        } else if(stage==3) {
            game.advance(tick);
            const development::MotionSnapshot state=game.snapshot();
            if(state.drains>losses) {
                if(state.saved_drains!=0 || state.balls_remaining!=2-losses)
                    throw std::runtime_error("round replay lost a protected ball or wrong count");
                loss_mask|=1U<<losses;++losses;
                const std::uint32_t expected=losses==1?10500U:losses==2?35500U:60500U;
                if(static_cast<std::uint32_t>(state.score)!=expected)throw std::runtime_error("round replay drain bonus mismatch");
                stage=4;
            }
        } else if(stage==4) {
            game.advance(runtime::ticks_per_second);
            const development::MotionSnapshot state=game.snapshot();
            if(state.game_over)stage=5;
            else if(state.ball_active){game.set_replay_ball(gate);stage=1;}
        } else game.advance(tick);
        const development::MotionSnapshot state=game.snapshot();
        if(state.ball_number>=1 && state.ball_number<=3)ball_mask|=1U<<(state.ball_number-1);
    }
    void inspect(const table::TableModel& model,const scene::ResourceScene& scene) {
        if(stage!=5||hud_seen)return;
        hud::ScoreDisplay painter(model);hud::HudState expected;expected.score=60500;expected.ball_number=0;
        scene::Surface reference=scene.background;painter.render(reference,scene.background,expected);
        const scene::Rect boxes[]={painter.layout().score_restore,painter.layout().ball,painter.layout().player};
        for(unsigned b=0;b<3;++b)for(int y=boxes[b].y;y<boxes[b].y+boxes[b].height;++y)
            for(int x=boxes[b].x;x<boxes[b].x+boxes[b].width;++x) {
                if(x<0||y<0||x>=int(scene.surface.width)||y>=int(scene.surface.height))continue;
                const std::size_t pixel=(std::size_t(y)*scene.surface.width+x)*4;
                for(unsigned c=0;c<4;++c)if(scene.surface.pixels[pixel+c]!=reference.pixels[pixel+c])
                    throw std::runtime_error("round replay numeric HUD mismatch");
            }
        hud_seen=true;
    }
};

// Presentation messages are a temporary event summary, not the complete XP
// text queue. They use the exact extracted XP glyph asset and DAT text boxes.
struct GameMessages {
    bool initialized;
    development::MotionSnapshot previous;
    runtime::Tick changed;
    std::string info;
    explicit GameMessages(const development::MotionSnapshot& initial):initialized(false),previous(initial),changed(0),info(){}
    static bool before(runtime::Tick a,runtime::Tick b) {
        const std::uint64_t x=static_cast<std::uint64_t>(a),y=static_cast<std::uint64_t>(b);
        const std::uint32_t xh=static_cast<std::uint32_t>(x>>32U),yh=static_cast<std::uint32_t>(y>>32U);
        return xh==yh?static_cast<std::uint32_t>(x)<static_cast<std::uint32_t>(y):xh<yh;
    }
    static std::string points(std::uint64_t value) {
        std::ostringstream stream;stream<<value;std::string result=stream.str();
        for(std::size_t offset=result.size();offset>3;){offset-=3;result.insert(offset,",");}
        return result;
    }
    hud::MessageState update(const development::MotionSnapshot& now,const std::string& objective=std::string()) {
        const bool reset=!initialized||before(now.time,previous.time)||now.serves<previous.serves;
        if(reset){info.clear();changed=now.time;}
        else {
            std::string next;
            if(now.total_score!=previous.total_score)next=points(now.total_score-previous.total_score)+" Points";
            if(now.score_multiplier!=previous.score_multiplier)next="Bonus Multiplier\n"+points(now.score_multiplier)+"X";
            if(now.ground_bumper_level!=previous.ground_bumper_level)next="Attack Bumpers\nLevel "+points(now.ground_bumper_level+1);
            if(now.raised_bumper_level!=previous.raised_bumper_level)next="Launch Bumpers\nLevel "+points(now.raised_bumper_level+1);
            if(now.medal_tier!=previous.medal_tier&&now.medal_tier>previous.medal_tier)next=now.medal_tier==1?"Bronze Medal":now.medal_tier==2?"Silver Medal":"Gold Medal";
            if(now.extra_balls_pending>previous.extra_balls_pending)next="Extra Ball\nAwarded";
            if(!next.empty()){info=next;changed=now.time;}
        }
        const std::uint64_t elapsed=static_cast<std::uint64_t>(now.time-changed);
        if((elapsed>>32U)!=0||static_cast<std::uint32_t>(elapsed)>=2000000000U)info.clear();
        hud::MessageState state;state.info=info;
        if(!objective.empty())state.mission=objective;
        else if(now.game_over)state.mission="Game Over\nPlayer 1\n"+points(now.score);
        else if(!now.ball_active)state.mission=now.serve_pending?"Awaiting Deployment":"Ball Lost";
        else if(now.hidden_test)state.mission="Hidden Test\nLeft Stick\nMoves Ball";
        else if(now.shoot_again)state.mission="Shoot Again";
        if(now.tilted){state.info="Tilt";state.mission.clear();}
        else if(now.tilt_pressure>.5)state.info="Careful...";
        previous=now;initialized=true;return state;
    }
};
int run(int argc, char** argv)
{
    Config config = configuration(argc, argv);
    if(config.motion_smoke||config.ramp_smoke||config.hidden_test_smoke||config.components_smoke||config.round_smoke||config.wormhole_smoke||config.bank_smoke||config.menu_smoke)config.startup=0;
    if(config.hidden_test_smoke)config.cheat_unlock="NONE"; // Retained legacy pad harness.
    const std::vector<std::uint32_t> unlock_sequence=cheat_sequence(config.cheat_unlock);
    if(config.menu_smoke&&unlock_sequence.size()!=10)throw std::runtime_error("menu smoke expects the selected ten-step Konami code");
    xsdk_log_write("Pinball Port: BASIC SCORE AND ROUND TEST; source-guided physics/default single-player rules; optional original WAV audio\n");
    xsdk_log_write("asset placement and base states remain inferences; 600x416 DAT canvas is default, canvas_height=480 inspects complete table asset\n");
    xsdk_log_write("controller: LT/RT flippers, A/Down plunger, LB/RB shake, Start pause, Konami unlock then Back cheat menu, left stick Hidden Test steering, Y new game\n");
    const table::TableModel model = table::TableModel::load(config.dat_path);
    scene::SceneLayout layout;
    layout.width = config.canvas_width;
    layout.height = config.canvas_height;
    scene::ResourceScene scene = scene::static_resource_scene(model, layout);
    development::PlayfieldTest game(model);
    const std::size_t asset_separator=config.dat_path.find_last_of("/\\");
    const std::string font_path=(asset_separator==std::string::npos?std::string():config.dat_path.substr(0,asset_separator+1))+"PBMSG_FT.bin";
    const hud::MessageDisplay messages(model,font::BitmapFont::load(font_path));
    GameMessages message_state(game.snapshot());
    audio::XboxAudioOutput sound_output;
    std::unique_ptr<audio::GameSoundBank> sound_bank;
    std::vector<audio::SoundCue> sound_cues;sound_cues.reserve(256);
    if(config.sound){
        try{
            const std::string directory=(asset_separator==std::string::npos?std::string():config.dat_path.substr(0,asset_separator+1))+"SOUNDS";
            sound_bank.reset(new audio::GameSoundBank(model,directory));
            sound_output.set_master_gain(static_cast<float>(config.sound_volume)/100.0f);
            const bool started=sound_output.start();const audio::SoundBankStats clips=sound_bank->stats();
            xsdk_log_write("audio init: ready%lu loaded%lu missing%lu budget%lu errors%lu bytes%lu\n",
                static_cast<unsigned long>(started),static_cast<unsigned long>(clips.loaded),static_cast<unsigned long>(clips.missing),
                static_cast<unsigned long>(clips.over_budget),static_cast<unsigned long>(clips.mapping_errors),static_cast<unsigned long>(clips.cached_bytes));
        }catch(const std::exception& error){xsdk_log_write("optional audio unavailable:%s\n",error.what());sound_bank.reset();sound_output.stop();}
    }

    BankReplay bank_replay(model,config.bank_smoke!=0);
    WormholeReplay wormhole_replay(model,config.wormhole_smoke!=0);
    RoundReplay round_replay(model,config.round_smoke!=0);
    if(config.round_smoke)game.set_replay_ball(round_replay.bumper);
    std::vector<motion::BallState> component_starts;
    std::vector<reference::ComponentProfile> component_cases;
    if(config.components_smoke) {
        const reference::TablePhysics profile(model);
        component_cases=profile.components;
        for(std::size_t i=0;i<component_cases.size();++i) {
            // Whole-profile clearance probes are retained under work/physics-v7-components.
            const std::size_t segment=(component_cases[i].group==469 || component_cases[i].group==470)?1:0;
            const motion::StaticCollider& body=profile.static_colliders.at(component_cases[i].collider_indices.at(segment));
            physics::Vec2 center=body.first,front(1,0);
            if(component_cases[i].group==372 || component_cases[i].group==413)
                front=physics::Vec2(.9238795f,.3826834f);
            float offset=profile.ball_radius+.02f;
            if(body.kind==motion::ColliderKind::Disk)offset+=body.radius;
            else {
                center=physics::multiply(physics::add(body.first,body.second),.5f);
                const physics::Vec2 edge=physics::subtract(body.second,body.first);
                front=physics::unit(physics::Vec2(-edge.y,edge.x));
            }
            motion::BallState start(physics::add(center,physics::multiply(front,offset)),
                physics::multiply(front,-20),profile.ball_radius);
            start.collision_mask=body.collision_mask;
            // Explicit raised replay height from ramp101's preserved upper exit.
            // This is a test start, not an inferred bumper geometry attribute.
            if(start.collision_mask==4)start.height=1.360704f;
            component_starts.push_back(start);
        }
        if(component_cases.size()!=11)throw std::runtime_error("component smoke requires seven bumpers/four rebounds");
    }
    if(config.ramp_smoke) {
        const reference::TablePhysics profile(model);
        const reference::RampPlaneProfile& mouth=profile.ramp_planes.at(17);
        const physics::Vec2 middle=physics::multiply(physics::add(mouth.vertices[0],mouth.vertices[1]),.5f);
        const physics::Vec2 edge=physics::subtract(mouth.vertices[1],mouth.vertices[0]);
        const physics::Vec2 front=physics::unit(physics::Vec2(edge.y,-edge.x));
        game=development::PlayfieldTest(model,physics::add(middle,physics::multiply(front,.15f)),physics::multiply(front,-40));
    }
    xsdk_log_write("motion: source-derived response; %lu candidate static colliders, incomplete geometry and rules; no XP equivalence claim\n", static_cast<unsigned long>(game.collider_count()));
    for (std::size_t i = 0; i < scene.limitations.size(); ++i) xsdk_log_write("scene: %s\n", scene.limitations[i].c_str());
    Presentation display;
    display.open(config);
    runtime::InputState input;
    platform::ControllerAdapter adapter(input, 0x36000000U + config.controller, config.bindings);
    rules::CheatMenu cheat_menu(rules::CheatMenuConfig(unlock_sequence,platform::PadDpadUp,platform::PadDpadDown,
        platform::PadA,platform::PadB,platform::PadBack));
    bool menu_ready=false,menu_captured=false;
    std::uint32_t menu_previous_buttons=0,menu_opened=0,menu_closed=0,menu_actions=0;
    runtime::Tick menu_activity=0,menu_pause_time=0,menu_prefix_time=0;
    bool menu_freeze_seen=false,menu_freeze_ok=true,menu_a_consumed=false,menu_reset_seen=false,menu_reopened=false;
    bool menu_prefix_seen=false,menu_prefix_advanced=false,menu_prefix_unpaused=true,menu_prefix_plunger=false;
    const std::uint32_t menu_initial_rank=game.snapshot().rank;
    std::string menu_feedback;
    const runtime::Tick initial_feed_delay=reference::TablePhysics(model).rules.feed_delay;
    const runtime::Tick intro_started=monotonic_time();
    StartupPlayback startup_audio(config.startup!=0,initial_feed_delay,intro_started);
    startup_audio.begin(game,sound_output,sound_bank.get(),intro_started,xsdk_system_ui_open(),false);
    const runtime::Tick started = monotonic_time();
    trace_clock_at("scheduler constructor", started, 0);
    runtime::FixedStepScheduler scheduler(runtime::FixedStepConfig(
        runtime::ticks_per_second / 120, runtime::ticks_per_second * config.swap_interval / 60, 8), started);
    bool startup_hidden_ok=true,startup_input_ok=true,startup_clock_ok=true,startup_pause_seen=false,startup_reset_seen=false;
    bool startup_playing_seen=false,startup_fresh_launch=false,startup_full_charge=false,startup_pause_clock_ok=true;
    runtime::Tick startup_paused_elapsed=0;
    std::uint32_t startup_phase_mask=0;
    std::uint32_t startup_visible_frame=(std::numeric_limits<std::uint32_t>::max)();
    bool pause_requested = false, exit_requested = false;
    std::uint32_t frame_count = 0, shake_frames = 0, test_toggles = 0;
    std::uint32_t layers_seen=game.snapshot().ball.collision_mask;
    double peak_height=game.snapshot().ball.height;
    physics::Vec2 test_checkpoint=game.snapshot().ball.position;
    double test_hover=0, test_steer=0, test_neutral=0, test_fall=0;
    std::uint32_t component_case=11, component_hits=0, component_frames=0, component_resets=0;
    std::uint32_t component_bumpers=0, component_rebounds=0;
    bool component_frozen=false, component_exhausted=false;
    std::vector<std::uint32_t> staging;
    while (!exit_requested) {
        const runtime::Tick now = monotonic_time();
        const bool system_ui = xsdk_system_ui_open();
        input.set_focused(!system_ui);
        if(config.components_smoke) {
            const std::uint32_t selected=(std::min)(frame_count/30,10U);
            if(selected!=component_case) {
                component_case=selected;
                game=development::PlayfieldTest(model,component_starts.at(selected));
                component_frozen=false;
            }
        }
        platform::ControllerSnapshot pad;
        const bool normal_pad=!config.motion_smoke&&!config.ramp_smoke&&!config.hidden_test_smoke&&!config.components_smoke&&!config.round_smoke&&!config.wormhole_smoke&&!config.bank_smoke&&!config.menu_smoke&&!config.startup_smoke;
        if (normal_pad) {
            pad = controller_snapshot(config.controller);
        }
        if (config.hidden_test_smoke) {
            // Exercise the same adapter/deadzone/frontend path as a real pad.
            // Hold Back for several frames so a repeated toggle is detectable.
            pad.connected = true;
            if (frame_count < 5 || (frame_count >= 60 && frame_count < 65)) pad.buttons = platform::PadBack;
            if (frame_count >= 10 && frame_count < 40) pad.left_stick_y = 20000;
        }
        if(config.menu_smoke)pad=menu_smoke_pad(frame_count,unlock_sequence);
        if(config.startup_smoke)pad=startup_smoke_pad(frame_count,startup_visible_frame);
        const bool was_menu_open=cheat_menu.isOpen();
        const runtime::Tick menu_clock_before=game.snapshot().time;
        bool menu_baseline=false;
        rules::CheatMenuEvents menu_events;
        if(cheat_menu.configured()) {
            if(!pad.connected||system_ui) {
                cheat_menu.reset_attempt();cheat_menu.synchronize_input(pad.buttons);
                menu_ready=false;menu_previous_buttons=pad.buttons;
            } else if(!menu_ready) {
                cheat_menu.synchronize_input(pad.buttons);menu_previous_buttons=pad.buttons;
                menu_activity=now;menu_ready=true;menu_baseline=true;
            } else {
                if(cheat_menu.matching()&&menu_attempt_expired(now,menu_activity)) {
                    cheat_menu.reset_attempt();xsdk_log_write("cheat menu: unlock attempt timed out\n");
                }
                menu_events=cheat_menu.update(pad.buttons);
                if((pad.buttons&~menu_previous_buttons)!=0)menu_activity=now;
                menu_previous_buttons=pad.buttons;
            }
        }
        menu_captured=menu_captures_gameplay(was_menu_open,cheat_menu.isOpen());
        if(menu_events.opened)menu_pause_time=menu_clock_before;
        if(menu_events.opened) {++menu_opened;xsdk_log_write("cheat menu: opened unlocked%lu\n",static_cast<unsigned long>(cheat_menu.isUnlocked()));}
        if(menu_events.closed) {++menu_closed;xsdk_log_write("cheat menu: closed\n");}
        if(menu_events.action) {
            bool applied=false;
            if(startup_audio.sequence.blocks_gameplay())menu_feedback="WAIT FOR GAME TO START";
            else if(menu_events.selected_action==rules::CheatAction::HiddenTest&&game.snapshot().hidden_test) {
                const runtime::InputTransition toggle={1,0x43484d4eU,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};
                game.input(toggle);applied=true;
            } else applied=game.activate_cheat(menu_events.selected_action);
            const char* names[]={"HIDDEN TEST TOGGLED","EXTRA BALL ADDED","UNLIMITED BALLS ON","GRAVITY WELL ENABLED","RANK INCREASED"};
            if(!startup_audio.sequence.blocks_gameplay())
                menu_feedback=applied?names[static_cast<std::size_t>(menu_events.selected_action)]:"ACTION NEEDS AN ACTIVE BALL";
            if(applied)menu_actions|=1U<<static_cast<unsigned>(menu_events.selected_action);
            xsdk_log_write("cheat menu: action%lu applied%lu\n",static_cast<unsigned long>(menu_events.selected_action),static_cast<unsigned long>(applied));
        }
        input.set_paused(pause_requested||system_ui||menu_captured||startup_audio.sequence.blocks_gameplay());
        if(normal_pad||config.hidden_test_smoke||config.menu_smoke||config.startup_smoke)adapter.update(pad);
        if(menu_baseline)input.release_all(runtime::TransitionCause::FocusLost);
        if (config.motion_smoke && (frame_count == 15 || frame_count == 190)) {
            input.set_source(0x50494e42U, runtime::Action::Plunger, frame_count == 15);
        }
        if (config.motion_smoke) {
            const bool flip=frame_count>=200 && (frame_count%30)<12;
            input.set_source(0x50494e43U,runtime::Action::LeftFlipper,flip);
            input.set_source(0x50494e43U,runtime::Action::RightFlipper,flip);
            input.set_source(0x50494e43U,runtime::Action::NudgeLeft,frame_count>=240 && frame_count<250);
            input.set_source(0x50494e43U,runtime::Action::NudgeRight,frame_count>=300 && frame_count<310);
        }
        const std::vector<runtime::InputTransition> transitions = input.drain_transitions();
        bool reset_motion = false;
        for (std::size_t i = 0; i < transitions.size(); ++i) {
            const runtime::InputTransition& edge = transitions[i];
            xsdk_log_write("input: %s %s cause%lu\n", runtime::action_name(edge.action), edge.pressed ? "down" : "up",
                static_cast<unsigned long>(edge.cause));
            if (!edge.pressed || edge.cause != runtime::TransitionCause::Physical) continue;
            if(menu_baseline)continue;
            if(menu_captured&&edge.action!=runtime::Action::NewGame)continue;
            if(startup_audio.sequence.blocks_gameplay()&&edge.action!=runtime::Action::NewGame&&
               edge.action!=runtime::Action::Pause&&edge.action!=runtime::Action::Menu)continue;
            if(cheat_menu.configured()&&edge.action==runtime::Action::HiddenTest)continue;
            if (edge.action == runtime::Action::Menu) exit_requested = true;
            if (edge.action == runtime::Action::HiddenTest) ++test_toggles;
            if (edge.action == runtime::Action::Pause) pause_requested = !pause_requested;
            if (edge.action == runtime::Action::NewGame) {
                pause_requested = false;
                input.release_all();
                game.reset();sound_output.stop_all();cheat_menu.new_game();menu_feedback="NEW GAME - UNLOCK RETAINED";
                startup_audio.begin(game,sound_output,sound_bank.get(),now,system_ui||menu_captured,true);
                scheduler.reset(now); reset_motion = true;
                xsdk_log_write("NewGame: started a fresh three-ball round; full missions/rules remain unfinished\n");
            }
        }
        if(reset_motion&&cheat_menu.isOpen())menu_pause_time=game.snapshot().time;
        const bool external_paused=pause_requested||system_ui||menu_captured;
        startup_audio.advance(now,external_paused,sound_output,sound_bank.get());
        const bool startup_blocked=startup_audio.sequence.blocks_gameplay();
        const bool frontend_paused=external_paused||startup_blocked;
        input.set_paused(frontend_paused);
        game.set_suspended(frontend_paused);
        if(!reset_motion&&!menu_captured&&!menu_baseline&&!startup_blocked)for(std::size_t i=0;i<transitions.size();++i) {
            if(cheat_menu.configured()&&transitions[i].action==runtime::Action::HiddenTest)continue;
            game.input(transitions[i]);
        }
        const platform::StickState stick = platform::left_stick(pad);
        game.set_test_stick((frontend_paused || reset_motion) ? 0 : stick.x,
                            (frontend_paused || reset_motion) ? 0 : stick.y);
        try { scheduler.set_paused(frontend_paused, now); }
        catch (...) { trace_clock_at("set_paused failed", now, frame_count); throw; }
        runtime::FrameSchedule schedule;
        try { schedule = scheduler.advance(now); }
        catch (...) { trace_clock_at("advance failed", now, frame_count); throw; }
        if (exit_requested) break;
        if(config.bank_smoke && schedule.render_due)bank_replay.advance(game);
        if(config.wormhole_smoke && schedule.render_due)wormhole_replay.advance(game,model,frame_count);
        if(config.round_smoke && schedule.render_due)round_replay.advance(game);
        for (std::size_t s = 0; !config.round_smoke && !config.wormhole_smoke && !config.bank_smoke && s < schedule.simulation_steps; ++s) {
            game.advance(schedule.simulation_step);
            const development::MotionSnapshot sample=game.snapshot();
            layers_seen|=sample.ball.collision_mask;
            peak_height=(std::max)(peak_height,double(sample.ball.height));
            if(config.components_smoke) {
                component_exhausted=component_exhausted || sample.exhausted_steps || sample.moving_exhausted;
                const reference::ComponentProfile& target=component_cases.at(component_case);
                if(!component_frozen && game.component_visual(target.group).active) {
                    component_hits|=1U<<component_case;
                    if(target.kind==reference::HitComponentKind::Bumper)++component_bumpers;else ++component_rebounds;
                    const runtime::InputTransition freeze={1,0x50494e44U,runtime::Action::HiddenTest,true,runtime::TransitionCause::Physical};
                    game.input(freeze);game.set_test_stick(0,0);component_frozen=true;
                }
            }
        }
        // Input edges may be polled before this presentation frame is due.
        // Preserve the actual opening-A result instead of sampling a later
        // poll where the one-shot opened event has already been consumed.
        if(config.menu_smoke&&frame_count==38&&menu_events.opened) {
            menu_a_consumed=!menu_events.action&&menu_captured&&
                !input.down(runtime::Action::Plunger)&&same_tick(game.snapshot().time,menu_clock_before);
        }
        if (!schedule.render_due) { xsdk_thread_sleep(1); continue; }
        sound_output.set_paused(external_paused);
        game.drain_sounds(sound_cues);
        if(sound_bank)for(const audio::SoundCue& cue:sound_cues)sound_output.play(sound_bank->clip(cue),cue.gain,cue.pan);
        game.render(scene, input, startup_audio.sequence.ball_visible());
        if(config.round_smoke)round_replay.inspect(model,scene);
        if(config.bank_smoke)bank_replay.inspect(game,scene);
        const development::MotionSnapshot rendered = game.snapshot();
        if(config.startup_smoke) {
            const startup::Snapshot gate=startup_audio.sequence.snapshot();
            startup_phase_mask|=1U<<static_cast<unsigned>(gate.phase);
            if(startup_blocked) {
                startup_hidden_ok=startup_hidden_ok&&!startup_audio.sequence.ball_visible();
                startup_clock_ok=startup_clock_ok&&same_tick(rendered.time,0);
                startup_input_ok=startup_input_ok&&rendered.charge==0&&rendered.launches==0&&!input.down(runtime::Action::Plunger);
            }
            if(frame_count==82){startup_pause_seen=pause_requested&&gate.phase==startup::Phase::Intro;startup_paused_elapsed=gate.elapsed;}
            if(frame_count>=82&&frame_count<110)startup_pause_clock_ok=startup_pause_clock_ok&&same_tick(gate.elapsed,startup_paused_elapsed);
            if(frame_count==152)startup_reset_seen=gate.generation==2&&gate.phase==startup::Phase::Intro&&startup_audio.flushes>=1&&startup_audio.feed_requests==0;
            if(gate.phase==startup::Phase::Playing) {
                if(!startup_playing_seen){startup_visible_frame=frame_count;startup_playing_seen=true;}
                if(frame_count<startup_visible_frame+30)startup_input_ok=startup_input_ok&&rendered.charge==0&&rendered.launches==0&&!input.down(runtime::Action::Plunger);
                if(frame_count==startup_visible_frame+209)startup_full_charge=rendered.charge==100;
                if(frame_count==startup_visible_frame+240)startup_fresh_launch=rendered.launches==1;
                if(frame_count==startup_visible_frame+49||frame_count==startup_visible_frame+209||
                   frame_count==startup_visible_frame+211||frame_count==startup_visible_frame+240)
                    xsdk_log_write("startup launch checkpoint: offset%lu charge%lu launches%lu seated%lu px_micro%ld py_micro%ld vx_micro%ld vy_micro%ld\n",
                        static_cast<unsigned long>(frame_count-startup_visible_frame),static_cast<unsigned long>(rendered.charge),
                        static_cast<unsigned long>(rendered.launches),static_cast<unsigned long>(rendered.seated),
                        static_cast<long>(rendered.ball.position.x*1000000),static_cast<long>(rendered.ball.position.y*1000000),
                        static_cast<long>(rendered.ball.velocity.x*1000000),static_cast<long>(rendered.ball.velocity.y*1000000));
            }
        }
        if(config.menu_smoke) {
            if(cheat_menu.matching()&&!cheat_menu.isOpen()) {
                if(!menu_prefix_seen){menu_prefix_time=rendered.time;menu_prefix_seen=true;}
                else if(tick_before(menu_prefix_time,rendered.time))menu_prefix_advanced=true;
                menu_prefix_unpaused=menu_prefix_unpaused&&!schedule.paused;
                if((pad.buttons&platform::PadDpadDown)&&input.down(runtime::Action::Plunger))menu_prefix_plunger=true;
            }
            if(cheat_menu.isOpen()) {
                menu_freeze_seen=true;
                menu_freeze_ok=menu_freeze_ok&&same_tick(rendered.time,menu_pause_time);
            }
            if(frame_count==39)menu_a_consumed=menu_a_consumed&&cheat_menu.isUnlocked()&&cheat_menu.isOpen()&&
                !rendered.hidden_test&&!rendered.cheats_used&&rendered.extra_balls_pending==0&&menu_actions==0&&rendered.charge==0;
            if(frame_count==52&&!rendered.hidden_test)throw std::runtime_error("menu smoke Hidden Test did not activate");
            if(frame_count==92&&rendered.extra_balls_pending!=1)throw std::runtime_error("menu smoke extra ball did not activate once");
            if(frame_count==112&&!rendered.unlimited_balls)throw std::runtime_error("menu smoke unlimited balls did not activate");
            if(frame_count==132&&!rendered.gravity_well_active)throw std::runtime_error("menu smoke gravity well did not activate");
            if(frame_count==152&&rendered.rank<=menu_initial_rank)throw std::runtime_error("menu smoke rank did not increase");
            if(frame_count==172)menu_reset_seen=cheat_menu.isUnlocked()&&!rendered.hidden_test&&!rendered.unlimited_balls&&
                !rendered.gravity_well_active&&!rendered.cheats_used&&rendered.extra_balls_pending==0&&rendered.rank==menu_initial_rank;
            if(frame_count==182)menu_reopened=menu_reset_seen&&cheat_menu.isOpen();
        }
        messages.render(scene.surface,scene.background,message_state.update(rendered,game.mission_text()));
        if (config.hidden_test_smoke) {
            const double distance=physics::length(physics::subtract(rendered.ball.position,test_checkpoint));
            if(frame_count==9) {test_hover=distance;test_checkpoint=rendered.ball.position;}
            if(frame_count==39) {test_steer=distance;test_checkpoint=rendered.ball.position;}
            if(frame_count==59) {test_neutral=distance;test_checkpoint=rendered.ball.position;}
            if(frame_count==99) test_fall=distance;
        }
        if(config.components_smoke) {
            const reference::ComponentProfile& target=component_cases.at(component_case);
            const development::ComponentVisualState state=game.component_visual(target.group);
            for(std::size_t i=0;i<scene.instances.size();++i)if(scene.instances[i].resource_group==target.group) {
                const scene::SpriteInstance& image=scene.instances[i];
                if(state.active && image.visible && image.state_index==target.fired_frame)
                    component_frames|=1U<<component_case;
                if(frame_count%30==29 && !state.active && image.visible==(target.idle_frame>=0) &&
                   (target.idle_frame<0 || image.state_index==static_cast<std::size_t>(target.idle_frame)))
                    component_resets|=1U<<component_case;
            }
        }
        if (rendered.shake_x || rendered.shake_y) ++shake_frames;
        scale_gameplay_frame(scene.surface,display.width(),display.height(),staging);
        menu_overlay(staging,display.width(),display.height(),cheat_menu,rendered,menu_feedback);
        display.show(staging);
        ++frame_count;
        if (frame_count == 1) xsdk_log_write("frame: first CPU scene submitted; visible output still requires observation\n");
        if (config.max_frames && frame_count >= config.max_frames) exit_requested = true;
    }
    adapter.disconnect();
    input.release_all();
    const INPUT_VIBRATION stop = {};
    XamInputSetState(config.controller, 0, &stop);
    const development::MotionSnapshot result = game.snapshot();
    xsdk_log_write("motion: launches%lu returns%lu contacts%lu exhausted%lu\n",
        static_cast<unsigned long>(result.launches), static_cast<unsigned long>(result.returns),
        static_cast<unsigned long>(result.contacts), static_cast<unsigned long>(result.exhausted_steps));
    xsdk_log_write("controls: flipper_contacts%lu nudges%lu moving_exhausted%lu shake_frames%lu\n",
        static_cast<unsigned long>(result.flipper_contacts),static_cast<unsigned long>(result.nudges),
        static_cast<unsigned long>(result.moving_exhausted),static_cast<unsigned long>(shake_frames));
    xsdk_log_write("sinks: captures%lu ejections%lu pending%lu\n",
        static_cast<unsigned long>(result.sink_captures),static_cast<unsigned long>(result.sink_ejections),
        static_cast<unsigned long>(result.captured));
    xsdk_log_write("kickouts: captures%lu ejections%lu pending%lu\n",
        static_cast<unsigned long>(result.kickout_captures),static_cast<unsigned long>(result.kickout_ejections),
        static_cast<unsigned long>(result.captured_kickout));
    xsdk_log_write("ramps: transitions%lu layers%lu peak_height_micro%ld holes%lu landings%lu\n",
        static_cast<unsigned long>(result.ramp_transitions),static_cast<unsigned long>(layers_seen),
        static_cast<long>(std::floor(peak_height*1000000.0+.5)),
        static_cast<unsigned long>(result.hole_captures),static_cast<unsigned long>(result.hole_landings));
    xsdk_log_write("components: bumper_hits%lu rebound_hits%lu active%lu\n",
        static_cast<unsigned long>(result.bumper_hits),static_cast<unsigned long>(result.rebound_hits),
        static_cast<unsigned long>(result.active_flashes));
    xsdk_log_write("round: score%lu total%llu ball%lu remaining%lu active%lu pending%lu game_over%lu drains%lu saved%lu serves%lu\n",
        static_cast<unsigned long>(result.score),static_cast<unsigned long long>(result.total_score),
        static_cast<unsigned long>(result.ball_number),static_cast<unsigned long>(result.balls_remaining),
        static_cast<unsigned long>(result.ball_active),static_cast<unsigned long>(result.serve_pending),
        static_cast<unsigned long>(result.game_over),static_cast<unsigned long>(result.drains),
        static_cast<unsigned long>(result.saved_drains),static_cast<unsigned long>(result.serves));
    xsdk_log_write("bank: hits%lu completions%lu multiplier%lu tier%lu\n",
        static_cast<unsigned long>(result.target_hits),static_cast<unsigned long>(result.bank_completions),
        static_cast<unsigned long>(result.score_multiplier),static_cast<unsigned long>(result.multiplier_tier));
    if(config.startup_smoke) {
        const bool passed=frame_count>=900&&startup_phase_mask==7&&startup_audio.intro_requests==2&&startup_audio.feed_requests==1&&
            startup_audio.playing==1&&startup_audio.sequence.snapshot().generation==2&&startup_hidden_ok&&startup_input_ok&&startup_clock_ok&&
            startup_pause_seen&&startup_pause_clock_ok&&startup_reset_seen&&startup_playing_seen&&startup_full_charge&&startup_fresh_launch&&!result.exhausted_steps&&!result.moving_exhausted;
        xsdk_log_write("startup smoke: phases%lu intros%lu accepted%lu feeds%lu feed_accepted%lu flushes%lu hidden%lu input%lu clock%lu paused%lu reset%lu playing%lu charged%lu launch%lu pass%lu\n",
            static_cast<unsigned long>(startup_phase_mask),static_cast<unsigned long>(startup_audio.intro_requests),static_cast<unsigned long>(startup_audio.intro_accepted),
            static_cast<unsigned long>(startup_audio.feed_requests),static_cast<unsigned long>(startup_audio.feed_accepted),static_cast<unsigned long>(startup_audio.flushes),
            static_cast<unsigned long>(startup_hidden_ok),static_cast<unsigned long>(startup_input_ok),static_cast<unsigned long>(startup_clock_ok),
            static_cast<unsigned long>(startup_pause_seen&&startup_pause_clock_ok),static_cast<unsigned long>(startup_reset_seen),static_cast<unsigned long>(startup_playing_seen),
            static_cast<unsigned long>(startup_full_charge),static_cast<unsigned long>(startup_fresh_launch),static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("startup intro/feed/input ordering scenario failed");
    }
    if(config.menu_smoke) {
        const bool passed=frame_count>=220&&cheat_menu.isUnlocked()&&menu_opened==4&&menu_closed==4&&menu_actions==31&&
            menu_prefix_seen&&menu_prefix_advanced&&menu_prefix_unpaused&&menu_prefix_plunger&&
            menu_a_consumed&&menu_freeze_seen&&menu_freeze_ok&&menu_reset_seen&&menu_reopened&&!result.exhausted_steps&&!result.moving_exhausted;
        xsdk_log_write("menu smoke: unlocked%lu opened%lu closed%lu actions%lu consumed%lu frozen%lu reset%lu reopened%lu prefix_advanced%lu prefix_unpaused%lu prefix_plunger%lu pass%lu\n",
            static_cast<unsigned long>(cheat_menu.isUnlocked()),static_cast<unsigned long>(menu_opened),static_cast<unsigned long>(menu_closed),
            static_cast<unsigned long>(menu_actions),static_cast<unsigned long>(menu_a_consumed),static_cast<unsigned long>(menu_freeze_seen&&menu_freeze_ok),
            static_cast<unsigned long>(menu_reset_seen),static_cast<unsigned long>(menu_reopened),
            static_cast<unsigned long>(menu_prefix_advanced),static_cast<unsigned long>(menu_prefix_unpaused),
            static_cast<unsigned long>(menu_prefix_plunger),static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("Konami cheat menu/controller scenario failed");
    }
    if(config.bank_smoke) {
        const bool passed=bank_replay.stage==12&&bank_replay.award_mask==32767&&bank_replay.drop_mask==7&&
            bank_replay.raise_mask==7&&bank_replay.lamp_on==15&&bank_replay.lamp_off==15&&bank_replay.lamp_steady==15&&
            bank_replay.saved_preserved&&bank_replay.loss_reset&&!bank_replay.exhausted&&result.target_hits==15&&
            result.bank_completions==5&&result.score_multiplier==1&&static_cast<std::uint32_t>(result.score)==92500;
        xsdk_log_write("bank smoke: awards%lu dropped%lu raised%lu lamp_on%lu lamp_off%lu steady%lu saved%lu reset%lu stage%lu exhausted%lu pass%lu\n",
            static_cast<unsigned long>(bank_replay.award_mask),static_cast<unsigned long>(bank_replay.drop_mask),
            static_cast<unsigned long>(bank_replay.raise_mask),static_cast<unsigned long>(bank_replay.lamp_on),
            static_cast<unsigned long>(bank_replay.lamp_off),static_cast<unsigned long>(bank_replay.lamp_steady),
            static_cast<unsigned long>(bank_replay.saved_preserved),static_cast<unsigned long>(bank_replay.loss_reset),
            static_cast<unsigned long>(bank_replay.stage),static_cast<unsigned long>(bank_replay.exhausted),static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("multiplier target bank replay failed");
    }
    if(config.wormhole_smoke) {
        const bool passed=frame_count>=120 && wormhole_replay.pass_mask==63 && !wormhole_replay.exhausted;
        xsdk_log_write("wormhole smoke: cases%lu exhausted%lu pass%lu\n",
            static_cast<unsigned long>(wormhole_replay.pass_mask),static_cast<unsigned long>(wormhole_replay.exhausted),
            static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("wormhole release replay failed");
    }
    if(config.round_smoke) {
        const bool passed=round_replay.stage==5 && round_replay.loss_mask==7 && round_replay.ball_mask==7 &&
            round_replay.score_seen && round_replay.hud_seen && result.game_over && !result.ball_active &&
            result.ball_number==0 && result.balls_remaining==0 && static_cast<std::uint32_t>(result.score)==60500 &&
            result.drains==3 && result.saved_drains==0 && result.serves==3 && !result.exhausted_steps && !result.moving_exhausted;
        xsdk_log_write("round smoke: losses%lu balls%lu hud%lu stage%lu pass%lu\n",
            static_cast<unsigned long>(round_replay.loss_mask),static_cast<unsigned long>(round_replay.ball_mask),
            static_cast<unsigned long>(round_replay.hud_seen),static_cast<unsigned long>(round_replay.stage),
            static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("finite three-ball round replay failed");
    }
    if(config.components_smoke) {
        const bool passed=frame_count>=330 && component_hits==2047 && component_frames==2047 &&
            component_resets==2047 && component_bumpers==7 && component_rebounds==4 && !component_exhausted;
        xsdk_log_write("component smoke: bumpers%lu rebounds%lu hits%lu frames%lu resets%lu exhausted%lu pass%lu\n",
            static_cast<unsigned long>(component_bumpers),static_cast<unsigned long>(component_rebounds),
            static_cast<unsigned long>(component_hits),static_cast<unsigned long>(component_frames),
            static_cast<unsigned long>(component_resets),static_cast<unsigned long>(component_exhausted),
            static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("component replay/frame/reset scenario failed");
    }
    if(config.hidden_test_smoke) {
        const bool passed=frame_count>=100 && test_toggles==2 && !result.hidden_test &&
            test_hover<.00001 && test_steer>.01 && test_neutral<.00001 && test_fall>.001 &&
            result.exhausted_steps==0 && result.moving_exhausted==0;
        xsdk_log_write("hidden test: toggles%lu hover_micro%ld steer_micro%ld neutral_micro%ld fall_micro%ld enabled%lu pass%lu\n",
            static_cast<unsigned long>(test_toggles),static_cast<long>(test_hover*1000000),
            static_cast<long>(test_steer*1000000),static_cast<long>(test_neutral*1000000),
            static_cast<long>(test_fall*1000000),static_cast<unsigned long>(result.hidden_test),
            static_cast<unsigned long>(passed));
        if(!passed)throw std::runtime_error("hidden-test pad scenario failed");
    }
    const audio::AudioOutputStats audio_state=sound_output.stats();
    xsdk_log_write("audio: running%lu submitted%lu played%lu voices%llu ignored%llu dropped%llu cue_drops%lu status%lu\n",
        static_cast<unsigned long>(audio_state.running),static_cast<unsigned long>(audio_state.submitted),static_cast<unsigned long>(audio_state.played),
        static_cast<unsigned long long>(audio_state.mixer.started),static_cast<unsigned long long>(audio_state.mixer.ignored),
        static_cast<unsigned long long>(audio_state.mixer.dropped),static_cast<unsigned long>(game.sound_cue_drops()),static_cast<unsigned long>(audio_state.last_status));
    sound_output.stop();
    display.close();
    xsdk_log_write("viewer exited cleanly after %lu frames\n", static_cast<unsigned long>(frame_count));
    return 0;
}

} // namespace
} // namespace xbox_backend
} // namespace pinball

int main(int argc, char** argv)
{
    try { return pinball::xbox_backend::run(argc, argv); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Pinball native viewer failed: %s\n", error.what());
        return 1;
    }
}
#endif // PINBALL_XBOX_BACKEND_CONTRACT_TEST

