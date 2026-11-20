#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include "playfield_test.hpp"
#include "platform_input.hpp"
#include "scene.hpp"
#include "table_model.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pinball::runtime::Action;
using pinball::runtime::Tick;
const pinball::runtime::InputSource keyboard_source = 1;
const pinball::runtime::InputSource controller_source = 2;
const int footer_height = 76;

struct Options {
    std::string path;
    bool self_check;
    std::uint64_t frames;
    Tick step;
    Tick render;
    pinball::scene::SceneLayout layout;
    Options() : self_check(false), frames(0), step(8333333), render(16666667), layout() {}
};

std::uint64_t number(const std::string& value, std::uint64_t maximum) {
    if (value.empty()) throw std::runtime_error("empty numeric option");
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') throw std::runtime_error("options require decimal integers");
        const std::uint64_t digit = static_cast<std::uint64_t>(value[i] - '0');
        if (result > (maximum - digit) / 10 || digit > maximum) {
            throw std::runtime_error("numeric option exceeds supported range");
        }
        result = result * 10 + digit;
    }
    if (!result) throw std::runtime_error("numeric options must be positive");
    return result;
}

Options options(int argc, char** argv) {
    if (argc < 2) throw std::runtime_error("a local PINBALL.DAT path is required");
    Options result;
    result.path = argv[1];
    // The DAT's table_size is 600x416; full asset extent remains a debug choice.
    result.layout.width = 600;
    result.layout.height = 416;
    for (int a = 2; a < argc; ++a) {
        const std::string option = argv[a];
        if (option == "--self-check") result.self_check = true;
        else if (option == "--full-assets") { result.layout.width = 600; result.layout.height = 480; }
        else if (option == "--frames" && a + 1 < argc) result.frames = number(argv[++a], 1000000);
        else if (option == "--step-ns" && a + 1 < argc) result.step = static_cast<Tick>(number(argv[++a], 1000000000));
        else if (option == "--render-ns" && a + 1 < argc) result.render = static_cast<Tick>(number(argv[++a], 1000000000));
        else if (option == "--canvas" && a + 2 < argc) {
            result.layout.width = static_cast<std::uint32_t>(number(argv[++a], 16384));
            result.layout.height = static_cast<std::uint32_t>(number(argv[++a], 16384));
        } else throw std::runtime_error("unrecognized or incomplete viewer option: " + option);
    }
    return result;
}

std::uint64_t frame_hash(const pinball::scene::Surface& surface) {
    std::uint64_t result = 14695981039346656037ULL;
    for (std::size_t i = 0; i < surface.pixels.size(); ++i) {
        result ^= surface.pixels[i];
        result *= 1099511628211ULL;
    }
    return result;
}

class XInputReader {
public:
    XInputReader() : module_(NULL), get_state_(NULL) {
        wchar_t directory[MAX_PATH + 1] = {};
        const UINT size = GetSystemDirectoryW(directory, MAX_PATH + 1);
        if (!size || size > MAX_PATH) return;
        const wchar_t* dlls[] = {L"xinput1_4.dll", L"xinput9_1_0.dll"};
        for (std::size_t i = 0; i < 2; ++i) {
            const std::wstring path = std::wstring(directory) + L"\\" + dlls[i];
            HMODULE candidate = LoadLibraryExW(path.c_str(), NULL, 0);
            if (!candidate) continue;
            GetState entry = reinterpret_cast<GetState>(GetProcAddress(candidate, "XInputGetState"));
            if (entry) { module_ = candidate; get_state_ = entry; break; }
            FreeLibrary(candidate);
        }
    }
    ~XInputReader() { if (module_) FreeLibrary(module_); }
    pinball::platform::ControllerSnapshot snapshot() const {
        pinball::platform::ControllerSnapshot result;
        if (!get_state_) return result;
        XINPUT_STATE state = {};
        if (get_state_(0, &state) != ERROR_SUCCESS) return result;
        result.connected = true;
        result.left_trigger = state.Gamepad.bLeftTrigger;
        result.right_trigger = state.Gamepad.bRightTrigger;
        result.left_stick_x = state.Gamepad.sThumbLX;
        result.left_stick_y = state.Gamepad.sThumbLY;
        const WORD native[] = {XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
            XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_LEFT_SHOULDER,
            XINPUT_GAMEPAD_RIGHT_SHOULDER, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB};
        const std::uint32_t portable[] = {pinball::platform::PadA, pinball::platform::PadB,
            pinball::platform::PadX, pinball::platform::PadY, pinball::platform::PadStart,
            pinball::platform::PadBack, pinball::platform::PadLeftShoulder,
            pinball::platform::PadRightShoulder, pinball::platform::PadDpadLeft,
            pinball::platform::PadDpadRight, pinball::platform::PadDpadUp,
            pinball::platform::PadDpadDown, pinball::platform::PadLeftThumb,
            pinball::platform::PadRightThumb};
        for (std::size_t i = 0; i < 14; ++i) {
            if (state.Gamepad.wButtons & native[i]) result.buttons |= portable[i];
        }
        return result;
    }
private:
    typedef DWORD (WINAPI* GetState)(DWORD, XINPUT_STATE*);
    HMODULE module_;
    GetState get_state_;
    XInputReader(const XInputReader&);
    XInputReader& operator=(const XInputReader&);
};

bool keyboard_action(WPARAM key, Action& action) {
    switch (key) {
    case 'Z': action = Action::LeftFlipper; return true;
    case VK_OEM_2: action = Action::RightFlipper; return true;
    case VK_SPACE: action = Action::Plunger; return true;
    case VK_LEFT: action = Action::NudgeLeft; return true;
    case VK_RIGHT: action = Action::NudgeRight; return true;
    case VK_F2: action = Action::NewGame; return true;
    case 'P': action = Action::Pause; return true;
    case VK_TAB: action = Action::Menu; return true;
    default: return false;
    }
}

class Viewer {
public:
    explicit Viewer(const Options& settings)
        : model_(pinball::table::TableModel::load(settings.path)),
          scene_(pinball::scene::static_resource_scene(model_, settings.layout)), game_(model_),
          input_(), controller_(input_, controller_source),
          scheduler_(pinball::runtime::FixedStepConfig(settings.step, settings.render, 8)),
          epoch_(std::chrono::steady_clock::now()), keys_(), window_(NULL), running_(true),
          help_(false), events_(0), frames_(0), frame_limit_(settings.frames) {
        render();
    }

    Tick now() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - epoch_).count();
    }

    void process_events(Tick time) {
        for (;;) {
            const std::vector<pinball::runtime::InputTransition> transitions = input_.drain_transitions();
            if (transitions.empty()) break;
            events_ += transitions.size();
            bool reset = false;
            // Global actions take precedence over gameplay releases.
            for (std::size_t i = 0; i < transitions.size(); ++i) {
                const pinball::runtime::InputTransition& edge = transitions[i];
                if (!edge.pressed || edge.cause != pinball::runtime::TransitionCause::Physical) continue;
                if (edge.action == Action::Pause) {
                    const bool paused = !scheduler_.paused();
                    scheduler_.set_paused(paused, time); input_.set_paused(paused);
                } else if (edge.action == Action::Menu) help_ = !help_;
                else if (edge.action == Action::NewGame) {
                    input_.release_all(); input_.set_paused(false);
                    scheduler_.reset(time); game_.reset(); help_ = false; reset = true;
                }
            }
            game_.set_suspended(input_.paused() || !input_.focused());
            if (!reset) for (std::size_t i = 0; i < transitions.size(); ++i) game_.input(transitions[i]);
        }
        game_.set_suspended(input_.paused() || !input_.focused());
    }
    void render() {
        game_.render(scene_, input_);
        bgra_.resize(scene_.surface.pixels.size());
        for (std::size_t i = 0; i < bgra_.size(); i += 4) {
            bgra_[i] = scene_.surface.pixels[i + 2];
            bgra_[i + 1] = scene_.surface.pixels[i + 1];
            bgra_[i + 2] = scene_.surface.pixels[i];
            bgra_[i + 3] = 0;
        }
    }

    void title() {
        std::wostringstream text;
        text << L"Pinball Port - Basic Gameplay Test | "
             << (scheduler_.paused() ? L"Paused" : game_.snapshot().game_over ? L"Game over - Y/F2 starts a new game" : game_.snapshot().hidden_test ? L"Hidden test ON - left stick to steer" : L"Plunger and ball test")
             << L" | Controller " << (controller_.connected() ? L"connected" : L"disconnected")
             << L" | Input edges " << events_;
        const std::wstring next = text.str();
        if (next != title_) { SetWindowTextW(window_, next.c_str()); title_ = next; }
    }

    void paint() {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window_, &paint);
        RECT client = {};
        GetClientRect(window_, &client);
        FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const int width = client.right;
        const int height = client.bottom > footer_height ? client.bottom - footer_height : 0;
        if (width > 0 && height > 0) {
            int draw_width = width;
            int draw_height = static_cast<int>(static_cast<std::int64_t>(width) * scene_.surface.height /
                                               scene_.surface.width);
            if (draw_height > height) {
                draw_height = height;
                draw_width = static_cast<int>(static_cast<std::int64_t>(height) * scene_.surface.width /
                                              scene_.surface.height);
            }
            BITMAPINFO bitmap = {};
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmap.bmiHeader.biWidth = static_cast<LONG>(scene_.surface.width);
            bitmap.bmiHeader.biHeight = -static_cast<LONG>(scene_.surface.height);
            bitmap.bmiHeader.biPlanes = 1;
            bitmap.bmiHeader.biBitCount = 32;
            bitmap.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, (width - draw_width) / 2, (height - draw_height) / 2,
                draw_width, draw_height, 0, 0, static_cast<int>(scene_.surface.width),
                static_cast<int>(scene_.surface.height), bgra_.data(), &bitmap,
                DIB_RGB_COLORS, SRCCOPY);
        }
        RECT footer = {8, height + 5, client.right - 8, client.bottom - 2};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(225, 225, 225));
        const wchar_t* label = help_
            ? L"THREE-BALL GAME: hold and release the plunger. Full pull takes about 2.475 seconds.\nPad: LT/RT flippers, A/Down plunger, LB/RB shake, Start pause, Back hidden test, left stick steer, Y reset.\nBasic scoring and three-ball flow; full missions and audio are unfinished."
            : L"PINBALL PORT - hold Space/A to pull, release to launch.\nZ /: flippers | Space: plunger | Arrows: shake | F2: reset | P: pause | Tab: help\nResize preserves the table aspect ratio. Controller 0 uses the selected bindings.";
        DrawTextW(dc, label, -1, &footer, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        EndPaint(window_, &paint);
    }

    LRESULT message(UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_PAINT) { paint(); return 0; }
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_SIZE) { InvalidateRect(window_, NULL, FALSE); return 0; }
        if (message == WM_ACTIVATEAPP) {
            input_.set_focused(wparam != 0);
            if (!wparam) keys_.fill(false);
            return 0;
        }
        if (message == WM_KEYDOWN || message == WM_KEYUP) {
            Action action = Action::Count;
            if (keyboard_action(wparam, action)) {
                const bool pressed = message == WM_KEYDOWN;
                const bool repeat = pressed && (static_cast<std::uint64_t>(lparam) & (1ULL << 30)) != 0;
                const std::size_t a = static_cast<std::size_t>(action);
                if (!repeat && keys_[a] != pressed) {
                    keys_[a] = pressed;
                    input_.set_source(keyboard_source, action, pressed);
                }
                return 0;
            }
        }
        if (message == WM_CLOSE) { DestroyWindow(window_); return 0; }
        if (message == WM_DESTROY) { running_ = false; PostQuitMessage(0); return 0; }
        return DefWindowProcW(window_, message, wparam, lparam);
    }

    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        Viewer* viewer = reinterpret_cast<Viewer*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            viewer = static_cast<Viewer*>(create->lpCreateParams);
            viewer->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(viewer));
        }
        if (!viewer) return DefWindowProcW(window, message, wparam, lparam);
        try { return viewer->message(message, wparam, lparam); }
        catch (const std::exception& error) {
            viewer->failure_ = error.what();
            viewer->running_ = false;
            DestroyWindow(window);
            return 0;
        }
    }

    int run() {
        const HINSTANCE instance = GetModuleHandleW(NULL);
        const wchar_t* class_name = L"PinballRewriteTableViewer";
        WNDCLASSEXW window_class = {};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = procedure;
        window_class.hInstance = instance;
        window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
        window_class.lpszClassName = class_name;
        if (!RegisterClassExW(&window_class)) throw std::runtime_error("cannot register viewer window");
        RECT rectangle = {0, 0, static_cast<LONG>(scene_.surface.width),
                          static_cast<LONG>(scene_.surface.height) + footer_height};
        if (!AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE)) {
            UnregisterClassW(class_name, instance);
            throw std::runtime_error("cannot calculate viewer window size");
        }
        HWND window = CreateWindowExW(0, class_name, L"Pinball Port - Basic Gameplay Test",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
            rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
            NULL, NULL, instance, this);
        if (!window) {
            UnregisterClassW(class_name, instance);
            throw std::runtime_error("cannot create viewer window");
        }
        XInputReader reader;
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        while (running_) {
            MSG message = {};
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) { running_ = false; break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running_) break;
            const pinball::platform::ControllerSnapshot pad = reader.snapshot();
            controller_.update(pad);
            const Tick time = now();
            process_events(time);
            const pinball::platform::StickState stick = pinball::platform::left_stick(pad);
            const bool allow_stick = input_.focused() && !input_.paused();
            game_.set_test_stick(allow_stick ? stick.x : 0, allow_stick ? stick.y : 0);
            const pinball::runtime::FrameSchedule frame = scheduler_.advance(time);
            for (std::size_t s = 0; s < frame.simulation_steps; ++s) game_.advance(frame.simulation_step);
            if (frame.render_due) {
                render();
                title();
                InvalidateRect(window_, NULL, FALSE);
                ++frames_;
                if (frame_limit_ && frames_ >= frame_limit_) DestroyWindow(window_);
            }
            if (running_) MsgWaitForMultipleObjects(0, NULL, FALSE, 2, QS_ALLINPUT);
        }
        controller_.disconnect();
        input_.release_all();
        if (IsWindow(window)) DestroyWindow(window);
        UnregisterClassW(class_name, instance);
        if (!failure_.empty()) throw std::runtime_error(failure_);
        std::cout << "Table viewer closed: " << frames_ << " rendered frames, " << events_
                  << " logical input edges; development physics and basic three-ball rounds.\n";
        return 0;
    }

    void settle() {
        for(unsigned i=0;i<600 && !game_.snapshot().seated;++i)game_.advance(8333333);
        if(!game_.snapshot().seated)throw std::runtime_error("reference feed did not settle on the plunger");
    }
    int self_check(std::uint64_t requested_frames) {
        settle();render();
        const pinball::scene::Surface background=scene_.background;
        const Action actions[]={Action::LeftFlipper,Action::RightFlipper,Action::Plunger};
        for(std::size_t a=0;a<3;++a) {
            input_.set_source(keyboard_source,actions[a],true);process_events(0);
            game_.advance(a==2?250000000:game_.flipper_raise_time());
            render();
            if(a==0 && game_.snapshot().left_flipper<=0)throw std::runtime_error("left flipper control");
            if(a==1 && game_.snapshot().right_flipper<=0)throw std::runtime_error("right flipper control");
            if(a==2 && game_.snapshot().charge<=0)throw std::runtime_error("plunger charge");
            input_.set_source(keyboard_source,actions[a],false);process_events(0);
            game_.advance(a==2?25000000:game_.flipper_return_time());
            if(a==2 && game_.snapshot().launches==0)throw std::runtime_error("plunger collision window did not launch");
            game_.reset();settle();render();
        }
        input_.set_source(keyboard_source,Action::Pause,true);process_events(0);
        if(!game_.snapshot().suspended)throw std::runtime_error("pause input");
        input_.set_source(keyboard_source,Action::NewGame,true);process_events(0);
        if(game_.snapshot().suspended)throw std::runtime_error("new game did not resume");
        const std::uint64_t count=requested_frames?requested_frames:8;
        for(std::uint64_t i=0;i<count;++i){game_.advance(16666666);render();}
        if(background.pixels!=scene_.background.pixels || background.depth!=scene_.background.depth)
            throw std::runtime_error("rendering mutated scene background");
        std::cout<<"{\"mode\":\"reference-viewer-self-check\",\"frames\":"<<count
            <<",\"window_created\":false,\"source_behavior_reference\":true,\"xp_fidelity_verified\":false}\n";
        return 0;
    }
private:
    pinball::table::TableModel model_;
    pinball::scene::ResourceScene scene_;
    pinball::development::PlayfieldTest game_;
    pinball::runtime::InputState input_;
    pinball::platform::ControllerAdapter controller_;
    pinball::runtime::FixedStepScheduler scheduler_;
    std::chrono::steady_clock::time_point epoch_;
    std::array<bool, pinball::runtime::action_count> keys_;
    HWND window_;
    bool running_;
    bool help_;
    std::uint64_t events_;
    std::uint64_t frames_;
    std::uint64_t frame_limit_;
    std::vector<std::uint8_t> bgra_;
    std::wstring title_;
    std::string failure_;
};

} // namespace

int main(int argc, char** argv) {
    try {
        const Options settings = options(argc, argv);
        Viewer viewer(settings);
        return settings.self_check ? viewer.self_check(settings.frames) : viewer.run();
    } catch (const std::exception& error) {
        std::cerr << "Table viewer failed: " << error.what() << "\nUsage: pinball_viewer <PINBALL.DAT> "
            << "[--self-check] [--frames N] [--full-assets | --canvas W H] "
            << "[--step-ns N] [--render-ns N]\n";
        return 1;
    }
}
