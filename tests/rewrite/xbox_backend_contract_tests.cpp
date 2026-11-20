// Exercise portable native-backend contracts without substituting a host
// window for an Xbox executable or pretending SDK calls ran successfully.
#define PINBALL_XBOX_BACKEND_CONTRACT_TEST
#include "../../src/rewrite/xbox360_main.cpp"

#include <functional>
#include <iostream>

namespace {
using namespace pinball::xbox_backend;
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& operation)
{
    try { operation(); } catch (const std::exception&) { return; }
    throw std::runtime_error("expected invalid contract input to be rejected");
}

void packed_display_bytes()
{
    require(display_word(0x12, 0x34, 0x56, 0x78) == 0x56341278U,
        "native display word must become B,G,R,A bytes on the big-endian CPU");
    require(display_word(255, 0, 0, 255) == 0x0000ffffU,
        "red must match the public console display-word observation");
    require(display_word(0, 255, 0, 255) == 0x00ff00ffU, "green channel order is wrong");
    require(display_word(0, 0, 255, 255) == 0xff0000ffU, "blue channel order is wrong");
}

void letterbox_and_sample_edges()
{
    const Fit wide = fit(600, 480, 1280, 720, 0);
    require(wide.x == 190 && wide.y == 0 && wide.width == 900 && wide.height == 720,
        "wide display must preserve the table aspect ratio");
    const Fit header = fit(600, 416, 1280, 720, 40);
    require(header.width == 980 && header.height == 680 && header.x == 150 && header.y == 40,
        "a caller-supplied generic margin must stay outside the asset viewport");
    const Fit tall = fit(600, 416, 640, 480, 0);
    require(tall.width == 640 && tall.height == 443 && tall.y == 18,
        "4:3 fitting must letterbox vertically and retain integer bounds");
    pinball::scene::Surface source(2, 2);
    source.pixels = {255,0,0,255, 0,255,0,255, 0,0,255,255, 18,52,86,120};
    std::vector<std::uint32_t> frame;
    scale_frame(source, 6, 4, 0, frame);
    require(frame.size() == 24, "scaled staging size mismatch");
    for (unsigned y = 0; y < 4; ++y) {
        require(frame[y * 6] == 0x000000ffU && frame[y * 6 + 5] == 0x000000ffU,
            "both horizontal bars must remain opaque black");
    }
    require(frame[1] == 0x0000ffffU && frame[2] == 0x0000ffffU && frame[3] == 0x00ff00ffU,
        "nearest scaling must duplicate first-row samples exactly");
    require(frame[13] == 0xff0000ffU && frame[16] == 0x56341278U,
        "last-row/last-column sample or BGRA packing changed");
    rejects([] { fit(0, 2, 3, 4, 0); });
    rejects([] { fit(2, 2, 3, 4, 4); });
    source.pixels.pop_back();
    rejects([&] { scale_frame(source, 6, 4, 0, frame); });
}

void gameplay_full_height_contract()
{
    const Fit bounds=fit(600,416,1280,720,0);
    require(bounds.x==121&&bounds.y==0&&bounds.width==1038&&bounds.height==720,
        "production gameplay must fit the original canvas using all720 display rows");
    pinball::scene::Surface source(2,2);
    source.pixels={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    std::vector<std::uint32_t> frame;
    scale_gameplay_frame(source,6,4,frame);
    require(frame.size()==24&&frame[1]==display_word(255,0,0,255)&&
        frame[3*6+4]==display_word(255,255,255,255),
        "gameplay compositor must paint the first and last display rows without a reserved header");
    require(frame[0]==display_word(0,0,0,255)&&frame[23]==display_word(0,0,0,255),
        "full-height gameplay must preserve aspect-fit black side bars");
}
void unlock_prefix_keeps_gameplay_contract()
{
    using namespace pinball;
    const Config config;
    const std::vector<std::uint32_t> sequence=cheat_sequence(config.cheat_unlock);
    rules::CheatMenu menu(rules::CheatMenuConfig(sequence,platform::PadDpadUp,platform::PadDpadDown,
        platform::PadA,platform::PadB,platform::PadBack));
    runtime::InputState input;
    platform::ControllerAdapter adapter(input,73,config.bindings);
    runtime::Tick now=0x100000000LL+0x7fffffffLL;
    runtime::FixedStepScheduler scheduler(runtime::FixedStepConfig(10000000,20000000,4),now);
    platform::ControllerSnapshot pad;pad.connected=true;
    rules::CheatMenuEvents events;runtime::FrameSchedule schedule={};bool captured=false;
    const auto sample=[&](std::uint32_t held){
        now+=20000000;
        const bool was_open=menu.isOpen();events=menu.update(held);
        captured=menu_captures_gameplay(was_open,menu.isOpen());
        input.set_paused(captured);pad.buttons=held;adapter.update(pad);
        scheduler.set_paused(captured,now);schedule=scheduler.advance(now);
    };
    sample(0);unsigned plunger_samples=0;
    for(std::size_t step=0;step+1<sequence.size();++step) {
        const runtime::Tick before=scheduler.simulation_time();sample(sequence[step]);
        require(!captured&&!schedule.paused&&!menu.isOpen()&&tick_before(before,scheduler.simulation_time()),
            "proper unlock prefix must not pause or capture normal gameplay");
        if(sequence[step]==platform::PadDpadDown){
            require(input.down(runtime::Action::Plunger),"D-pad Down prefix must remain a normal plunger press");++plunger_samples;
        }
        sample(0);
    }
    require(plunger_samples==2&&menu.matching(),"Konami prefix tracking did not survive normal input");
    const runtime::Tick before_open=scheduler.simulation_time();sample(sequence.back());
    require(events.opened&&!events.action&&captured&&schedule.paused&&menu.isOpen()&&
        same_tick(before_open,scheduler.simulation_time())&&!input.down(runtime::Action::Plunger),
        "final A must open the paused menu and be consumed instead of reaching the plunger or first action");
    sample(platform::PadA);sample(0);
    require(same_tick(before_open,scheduler.simulation_time()),"open menu must freeze simulation time");
    sample(platform::PadB);
    require(events.closed&&captured&&schedule.paused,"closing B sample must remain owned by the menu");
    sample(0);sample(0);
    require(!captured&&!schedule.paused&&tick_before(before_open,scheduler.simulation_time()),
        "gameplay did not resume after the closed-menu sample");
    require(tick_before(0x7fffffffLL,0x80000000LL)&&tick_before(0xffffffffLL,0x100000000LL)&&
        same_tick(0x100000000LL,0x100000000LL),"native timing word comparisons failed across31/32-bit boundaries");
}

void swap_api_address_contract()
{
    const std::uint32_t physical_fetch[6] = {0x91827364U,0x01234abcU,2,3,4,5};
    const std::array<std::uint32_t,6> adjusted = swap_fetch(physical_fetch, 0xa1234000U);
    require(adjusted[1] == 0xa1234abcU,
        "VdSwap fetch must carry the CPU virtual page and preserve low format/endian bits");
    require(adjusted[0] == physical_fetch[0] && adjusted[2] == 2 && adjusted[5] == 5,
        "swap address conversion must preserve all non-address fetch fields");
    require(physical_fetch[1] == 0x01234abcU, "swap conversion must not mutate SDK texture state");
    rejects([&] { swap_fetch(physical_fetch, 0xa1234001U); });
    rejects([] { swap_fetch(0, 0xa1234000U); });
}

void minimal_primary_ring_contract()
{
    const PrimaryRingWrite first = primary_ring_write(0, 8192, 4);
    require(first.begin == 0 && first.end == 4 && first.padding_count == 0,
        "minimal ring probe must begin at the fresh zero write index");
    const PrimaryRingWrite frame = primary_ring_write(4, 8192, 68);
    require(frame.begin == 4 && frame.end == 72 && frame.padding_count == 0,
        "swap reservation must remain contiguous with its trailing fence");
    const PrimaryRingWrite wrap = primary_ring_write(8164, 8192, 68);
    require(wrap.begin == 0 && wrap.end == 68 && wrap.padding_begin == 8164 && wrap.padding_count == 28,
        "ring wrap must consume its tail with NOPs before a contiguous swap");
    const PrimaryRingWrite exact = primary_ring_write(8124, 8192, 68);
    require(exact.begin == 8124 && exact.end == 0 && exact.padding_count == 0,
        "write index at the exact ring end must wrap to zero");
    rejects([] { primary_ring_write(0,8192,8192); });
    rejects([] { primary_ring_write(8192,8192,68); });
    rejects([] { primary_ring_write(0,8191,68); });
    rejects([] { primary_ring_write(0,8192,0); });
    const std::array<std::uint32_t,4> fence = primary_completion_packet(0x01234008U,0xa1b2c3d4U);
    require(fence[0] == 0xc0025800U && fence[1] == 3 && fence[2] == 0x0123400aU &&
        fence[3] == 0xa1b2c3d4U, "completion packet must carry physical address and CPU-native endian selection");
    rejects([] { primary_completion_packet(0x01234009U,1); });
    rejects([] { primary_completion_packet(0x01234008U,0); });
}

void konami_configuration_contract()
{
    using namespace pinball::platform;
    Config config;
    const std::vector<std::uint32_t> expected={PadDpadUp,PadDpadUp,PadDpadDown,PadDpadDown,
        PadDpadLeft,PadDpadRight,PadDpadLeft,PadDpadRight,PadB,PadA};
    require(cheat_sequence(config.cheat_unlock)==expected,"default unlock must be classic Konami with B,A and no Start suffix");
    require(config.menu_smoke==0,"menu scenario must be opt-in");
    setting(config,"cheat_unlock"," A + B , DPAD_UP ");
    const std::vector<std::uint32_t> chord=cheat_sequence(config.cheat_unlock);
    require(chord.size()==2&&chord[0]==(PadA|PadB)&&chord[1]==PadDpadUp,"configured exact chord sequence lost its masks/order");
    setting(config,"cheat_unlock","NONE");
    require(cheat_sequence(config.cheat_unlock).empty(),"NONE must explicitly disable the unlock model");
    for(const char* invalid:{"",",A","A,","A,,B","A+","A++B","A+A","MYSTERY"})
        rejects([&]{cheat_sequence(invalid);});
    std::string too_long="A";for(unsigned i=0;i<64;++i)too_long+=",A";
    rejects([&]{cheat_sequence(too_long);});
    config.cheat_unlock=Config().cheat_unlock;
    setting(config,"menu_smoke","1");validate_config(config);
    setting(config,"motion_smoke","1");rejects([&]{validate_config(config);});
    rejects([&]{setting(config,"menu_smoke","2");});
}
void startup_configuration_contract()
{
    Config config;
    require(config.startup==1&&config.startup_smoke==0,"production default must enable ordered intro while startup smoke stays opt-in");
    setting(config,"sound","0");
    require(config.startup==1,"disabled audio must not silently bypass production intro metadata timing");
    setting(config,"startup","0");validate_config(config);
    setting(config,"startup_smoke","1");
    rejects([&]{validate_config(config);});
    setting(config,"startup","1");validate_config(config);
    setting(config,"motion_smoke","1");rejects([&]{validate_config(config);});
    setting(config,"motion_smoke","0");setting(config,"menu_smoke","1");rejects([&]{validate_config(config);});
    setting(config,"menu_smoke","0");
    rejects([&]{setting(config,"startup","2");});rejects([&]{setting(config,"startup_smoke","2");});
}

void configurable_temporary_controls()
{
    Config config;
    require(config.bindings.buttons[static_cast<std::size_t>(pinball::runtime::Action::HiddenTest)] == pinball::platform::PadBack &&
        config.bindings.buttons[static_cast<std::size_t>(pinball::runtime::Action::Menu)] == 0, "Back must toggle hidden test without exiting");
    setting(config, "hidden_test", "X");
    require(config.bindings.buttons[static_cast<std::size_t>(pinball::runtime::Action::HiddenTest)] == pinball::platform::PadX, "hidden test binding must be configurable");
    require(config.motion_smoke == 0 && config.hidden_test_smoke == 0 && config.components_smoke == 0 && config.round_smoke == 0 && config.wormhole_smoke == 0 && config.bank_smoke == 0, "scripted input must be opt-in");
    setting(config, "hidden_test_smoke", "1");
    require(config.hidden_test_smoke == 1, "hidden test smoke option not parsed");
    setting(config, "ramp_smoke", "1");
    rejects([&] { validate_config(config); });
    setting(config, "ramp_smoke", "0");
    setting(config, "hidden_test_smoke", "0");
    setting(config, "bank_smoke", "1");
    require(config.bank_smoke==1,"bank smoke option not parsed");
    setting(config, "wormhole_smoke", "1");
    rejects([&]{validate_config(config);});
    setting(config, "bank_smoke", "0");
    require(config.wormhole_smoke==1,"wormhole smoke option not parsed");
    setting(config, "round_smoke", "1");
    rejects([&]{validate_config(config);});
    setting(config, "wormhole_smoke", "0");
    require(config.round_smoke==1,"round smoke option not parsed");
    setting(config, "components_smoke", "1");
    rejects([&]{validate_config(config);});
    setting(config, "round_smoke", "0");
    require(config.components_smoke==1,"component smoke option not parsed");
    setting(config, "hidden_test_smoke", "1");
    rejects([&] {validate_config(config);});
    setting(config, "hidden_test_smoke", "0");
    setting(config, "components_smoke", "0");
    setting(config, "motion_smoke", "1");
    require(config.motion_smoke == 1, "motion smoke option not parsed");
    rejects([&] { setting(config, "motion_smoke", "2"); });
    setting(config, "motion_smoke", "0");
    require(config.dat_path == "game:\\PINBALL.DAT" && config.canvas_height == 416,
        "native asset path and documented default canvas changed");
    std::istringstream input(
        "# user-adjustable native viewer configuration\n"
        "present=xenia\ncanvas_height=480\nmax_frames=120\n"
        "left_flipper=X,LT\nright_flipper=RB, RT\nplunger=NONE\n"
        "pause=A\nexit=Y\ntrigger_press=200\ntrigger_release=100\n");
    read_settings(input, config);
    validate_config(config);
    using pinball::runtime::Action;
    const std::size_t left = static_cast<std::size_t>(Action::LeftFlipper);
    const std::size_t right = static_cast<std::size_t>(Action::RightFlipper);
    const std::size_t plunger = static_cast<std::size_t>(Action::Plunger);
    require(config.present == PresentMode::Xenia && config.canvas_height == 480 && config.max_frames == 120,
        "presentation/debug overrides were not applied");
    require(config.bindings.buttons[left] == pinball::platform::PadX && config.bindings.left_trigger[left],
        "per-action mapping must replace temporary defaults");
    require(config.bindings.buttons[right] == pinball::platform::PadRightShoulder && config.bindings.right_trigger[right],
        "button and trigger must OR within a configurable action");
    require(config.bindings.buttons[plunger] == 0 && !config.bindings.left_trigger[plunger] &&
        !config.bindings.right_trigger[plunger], "NONE must unbind the action");
    rejects([&] { setting(config, "controller", "4"); });
    rejects([&] { setting(config, "buffer_count", "1"); });
    rejects([&] { setting(config, "max_frames", "999999999999999999"); });
    rejects([&] { setting(config, "present", "pretend"); });
    rejects([&] { setting(config, "exit", "UNKNOWN"); });
    rejects([&] { setting(config, "exit", "A,"); });
    rejects([&] { setting(config, "unknown", "A"); });
    config.bindings.trigger_release = config.bindings.trigger_press;
    rejects([&] { validate_config(config); });
}
}

int main()
{
    try {
        packed_display_bytes();
        letterbox_and_sample_edges();
        gameplay_full_height_contract();
        unlock_prefix_keeps_gameplay_contract();
        swap_api_address_contract();
        configurable_temporary_controls();
        konami_configuration_contract();
        startup_configuration_contract();
        minimal_primary_ring_contract();
        std::cout << "Xbox backend portable contracts passed: big-endian BGRA, aspect fit, nearest sampling,"
                     " full-height gameplay, nonpausing unlock input, VdSwap virtual-address fields and configurable controls. No native SDK execution is claimed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Xbox backend contract failed: " << error.what() << '\n';
        return 1;
    }
}
