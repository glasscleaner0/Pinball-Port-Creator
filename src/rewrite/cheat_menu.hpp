#ifndef PINBALL_REWRITE_CHEAT_MENU_HPP
#define PINBALL_REWRITE_CHEAT_MENU_HPP
#include "cheat_action.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>
namespace pinball { namespace rules {
struct CheatMenuConfig {
    std::vector<std::uint32_t> sequence;
    std::uint32_t up,down,accept,close,back;
    explicit CheatMenuConfig(const std::vector<std::uint32_t>& unlock=std::vector<std::uint32_t>(),
                             std::uint32_t up_mask=0,std::uint32_t down_mask=0,
                             std::uint32_t accept_mask=0,std::uint32_t close_mask=0,
                             std::uint32_t back_mask=0);
};
struct CheatMenuEvents {
    bool opened,closed,action;
    CheatAction selected_action;
    CheatMenuEvents():opened(false),closed(false),action(false),selected_action(CheatAction::HiddenTest) {}
};
struct CheatMenuSnapshot {
    bool configured,unlocked,open;
    std::size_t selection;
};
// Console UI policy, independent of original keyboard cheat parsing.
// No sequence or button assignment is supplied by default. Empty sequence
// disables this model so callers can retain their existing Back behavior.
// Unlock tokens are EXACT held masks, consumed only on a rising press.
// Proper subsets of a pending token allow a chord to be pressed in parts.
// Release-only samples neither advance nor reject a sequence; other rising
// presses reject mismatched candidates while preserving overlapping prefixes.
// Menu bindings require all their bits, permit extra held bits, and fire once
// on their own rising edge. Back, Close, navigation, Accept is the priority.
// Up+Down together does not move. Opening consumes the entire input sample.
// Five action rows follow CheatAction order, then row5 Close. Accepting an
// action keeps the menu open; accepting Close closes it. Unlock persists for
// this object's lifetime, including new_game(), which preserves all UI state.
class CheatMenu {
public:
    explicit CheatMenu(const CheatMenuConfig& config=CheatMenuConfig());
    CheatMenuEvents update(std::uint32_t held);
    CheatMenuEvents close();
    void new_game();
    // Baseline a focus/reconnect sample without synthesizing any input edges.
    void synchronize_input(std::uint32_t held);
    // True only while locked with at least one completed, proper token prefix.
    bool matching() const;
    // Forget an attempt without changing the held-button edge baseline.
    void reset_attempt();
    bool configured() const;
    bool isOpen() const;
    bool isUnlocked() const;
    std::size_t selection() const;
    CheatMenuSnapshot snapshot() const;
private:
    CheatMenuConfig config_;
    std::vector<unsigned char> prefixes_,next_;
    std::uint32_t previous_;
    std::size_t selection_;
    bool unlocked_,open_;
    bool rises(std::uint32_t binding,std::uint32_t held) const;
    bool unlock_press(std::uint32_t held);
};
} }
#endif
