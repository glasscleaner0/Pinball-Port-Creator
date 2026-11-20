#include "cheat_menu.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace pinball::rules;
const std::uint32_t a=1,b=2,c=4,d=8,up=16,down=32,back=64,close_button=128;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
CheatMenuConfig config(const std::vector<std::uint32_t>& sequence) {
    return CheatMenuConfig(sequence,up,down,a,close_button,back);
}
CheatMenuEvents press(CheatMenu& menu,std::uint32_t held) {
    menu.update(0);return menu.update(held);
}
void disabled_and_validation() {
    CheatMenu disabled;
    for(unsigned i=0;i<32;++i) {
        const CheatMenuEvents e=press(disabled,1U<<i);
        check(!e.opened&&!e.closed&&!e.action,"disabled model intercepted fallback input");
    }
    check(!disabled.configured()&&!disabled.isUnlocked()&&!disabled.isOpen(),"empty sequence enabled menu");
    try {CheatMenu bad(config(std::vector<std::uint32_t>(1,0)));}
    catch(const std::invalid_argument&) {return;}
    throw std::runtime_error("zero unlock token accepted");
}
void distinct_edges_and_completion_consumed() {
    CheatMenu repeated(config(std::vector<std::uint32_t>(2,a)));
    repeated.update(a);repeated.update(a);repeated.update(a);
    check(!repeated.isUnlocked(),"held button consumed repeated tokens");
    repeated.update(0);const CheatMenuEvents unlocked=repeated.update(a);
    check(unlocked.opened&&!unlocked.action&&repeated.isOpen(),"unlock accept was not suppressed");
    check(!repeated.update(a).action,"held unlock-completing button activated menu");
    const CheatMenuEvents accepted=press(repeated,a);
    check(accepted.action&&accepted.selected_action==CheatAction::HiddenTest&&repeated.isOpen(),"fresh Accept must return action without closing");
    check(!repeated.update(a).action,"held Accept repeated action");
}
void failed_overlap_and_chords() {
    CheatMenu failed(config(std::vector<std::uint32_t>{a,b}));
    press(failed,a);press(failed,d);press(failed,b);
    check(!failed.isUnlocked(),"bad rising press did not reject sequence");
    press(failed,a);check(press(failed,b).opened,"retry after failure did not match");
    CheatMenu overlapping(config(std::vector<std::uint32_t>{a,b,a,b,c}));
    press(overlapping,a);press(overlapping,b);press(overlapping,a);press(overlapping,b);
    press(overlapping,a);press(overlapping,b);
    check(!overlapping.isUnlocked(),"overlapping prefix unlocked too early");
    check(press(overlapping,c).opened,"overlapping prefix was lost");
    CheatMenu chord(config(std::vector<std::uint32_t>{a|b,c}));
    chord.update(a);check(!chord.isUnlocked(),"partial chord completed token");
    chord.update(a|b);chord.update(0);check(chord.update(c).opened,"partial chord press was rejected");
    CheatMenu extra(config(std::vector<std::uint32_t>{a|b,c}));
    extra.update(a|b|d);press(extra,c);check(!extra.isUnlocked(),"unlock masks were not exact");
    press(extra,b);extra.update(a|b);check(press(extra,c).opened,"reverse chord press order failed");
    CheatMenu wide(config(std::vector<std::uint32_t>{0x80000000U}));
    check(wide.update(0x80000000U).opened,"high-bit controller mask was treated as signed");
}
void navigation_actions_and_persistence() {
    CheatMenu menu(config(std::vector<std::uint32_t>{c}));
    check(press(menu,c).opened,"fixture unlock failed");
    for(std::size_t row=0;row<5;++row) {
        const CheatMenuEvents selected=press(menu,a);
        check(selected.action&&static_cast<std::size_t>(selected.selected_action)==row,"action row order");
        check(menu.isOpen(),"selecting action closed menu");
        press(menu,down);menu.update(down);
        check(menu.selection()==row+1,"navigation held repeat or missing advance");
    }
    check(menu.selection()==5,"Close row not sixth");
    check(press(menu,a).closed&&!menu.isOpen()&&menu.isUnlocked(),"Close row revoked unlock or failed");
    menu.new_game();check(menu.isUnlocked()&&!menu.isOpen(),"new round revoked unlock");
    check(press(menu,back).opened,"Back failed to reopen unlocked menu");
    press(menu,down);check(menu.selection()==0,"down wrap failed");
    press(menu,up);check(menu.selection()==5,"up wrap failed");
    press(menu,up|down);check(menu.selection()==5,"opposite directions changed selection");
    menu.new_game();check(menu.isOpen()&&menu.selection()==5,"new round unexpectedly mutated app UI state");
    check(press(menu,close_button).closed&&!menu.isOpen(),"B/Close did not close");
    check(!press(menu,close_button).opened,"Close opened closed menu");
    press(menu,back);check(press(menu,back).closed,"Back did not toggle close");
    check(!menu.close().closed,"close emitted duplicate closed event");
    press(menu,back);check(menu.close().closed&&menu.isUnlocked(),"explicit close did not preserve unlock");
}
void staggered_opposite_directions() {
    CheatMenu menu(config(std::vector<std::uint32_t>{c}));press(menu,c);
    menu.update(0);menu.update(up);check(menu.selection()==5,"up fixture failed");
    menu.update(up|down);check(menu.selection()==5,"held Up plus fresh Down moved selection");
    check(!menu.update(up|down|a).action,"opposed held directions activated selection");
    menu.update(down);check(menu.selection()==5,"direction release synthesized navigation");
    press(menu,down);check(menu.selection()==0,"down fixture failed");
    menu.update(up|down);check(menu.selection()==0,"held Down plus fresh Up moved selection");
}
void attempt_capture_and_timeout_reset() {
    CheatMenu menu(config(std::vector<std::uint32_t>{up,up,down}));
    check(!menu.matching(),"idle menu reported partial unlock");
    menu.update(up);check(menu.matching(),"first proper prefix was not reported");
    menu.reset_attempt();check(!menu.matching(),"timeout did not clear prefix candidates");
    menu.update(up);check(!menu.matching(),"timeout synthesized a repeated held edge");
    press(menu,up);check(menu.matching(),"new edge after timeout did not restart code");
    press(menu,d);check(!menu.matching(),"failed attempt remained captured");
    press(menu,up);press(menu,up);check(menu.matching(),"second prefix was not reported");
    check(press(menu,down).opened&&!menu.matching(),"completed code remained matching");
    menu.reset_attempt();menu.new_game();check(menu.isUnlocked(),"attempt reset revoked app unlock");
    CheatMenu disabled;check(!disabled.matching(),"disabled model reported matching");
    CheatMenu chord(config(std::vector<std::uint32_t>{a|b,c}));
    chord.update(a);check(!chord.matching(),"incomplete first chord counted as a completed token");
    chord.update(a|b);check(chord.matching(),"completed chord prefix was not reported");
}
void priorities_baseline_and_copy() {
    CheatMenu menu(config(std::vector<std::uint32_t>{c}));
    menu.synchronize_input(c);check(!menu.update(c).opened,"reconnect baseline synthesized unlock");
    press(menu,c);const CheatMenuEvents closing=press(menu,back|a);
    check(closing.closed&&!closing.action,"Back+Accept was not consumed by close");
    const CheatMenuEvents opening=press(menu,back|a);
    check(opening.opened&&!opening.action,"Back+Accept activated upon opening");
    const CheatMenuEvents navigate=press(menu,down|a);
    check(!navigate.action&&menu.selection()==1,"navigation also activated a row");
    menu.synchronize_input(a);check(!menu.update(a).action,"reconnect baseline synthesized Accept");
    check(press(menu,a).selected_action==CheatAction::ExtraBall,"real edge after baseline lost");
    CheatMenu prefix(config(std::vector<std::uint32_t>{a,b,c}));
    press(prefix,a);CheatMenu copied=prefix;
    press(prefix,d);press(copied,b);check(press(copied,c).opened,"value copy lost prefix state");
    press(prefix,b);press(prefix,c);check(!prefix.isUnlocked(),"copy changed original candidates");
    const CheatMenuSnapshot snapshot=copied.snapshot();
    check(snapshot.configured&&snapshot.unlocked&&snapshot.open&&snapshot.selection==0,"snapshot mismatch");
}
}
int main() {
    try {
        disabled_and_validation();distinct_edges_and_completion_consumed();failed_overlap_and_chords();
        navigation_actions_and_persistence();staggered_opposite_directions();attempt_capture_and_timeout_reset();priorities_baseline_and_copy();
        std::cout<<"cheat_menu_tests PASS: explicit input configuration, chord/prefix edges, persistent unlock and six rows\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"cheat_menu_tests FAIL: "<<e.what()<<'\n';return 1;}
}
