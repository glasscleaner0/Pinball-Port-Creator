#include "cheat_menu.hpp"
#include <algorithm>
#include <stdexcept>
namespace pinball { namespace rules {
CheatMenuConfig::CheatMenuConfig(const std::vector<std::uint32_t>& unlock,std::uint32_t u,
                               std::uint32_t d,std::uint32_t a,std::uint32_t c,std::uint32_t b)
    :sequence(unlock),up(u),down(d),accept(a),close(c),back(b) {}
CheatMenu::CheatMenu(const CheatMenuConfig& config)
    :config_(config),prefixes_(config.sequence.size(),0),next_(config.sequence.size(),0),
     previous_(0),selection_(0),unlocked_(false),open_(false) {
    for(std::size_t i=0;i<config_.sequence.size();++i)
        if(config_.sequence[i]==0)throw std::invalid_argument("unlock token requires a rising button");
}
bool CheatMenu::rises(std::uint32_t binding,std::uint32_t held) const {
    return binding!=0&&(held&binding)==binding&&(previous_&binding)!=binding;
}
bool CheatMenu::unlock_press(std::uint32_t held) {
    std::fill(next_.begin(),next_.end(),static_cast<unsigned char>(0));
    for(std::size_t p=0;p<config_.sequence.size();++p) {
        if(p!=0&&!prefixes_[p])continue;
        const std::uint32_t token=config_.sequence[p];
        if(held==token) {
            if(p+1==config_.sequence.size())return true;
            next_[p+1]=1;
        } else if((held&token)==held) {
            next_[p]=1;
        }
    }
    prefixes_.swap(next_);
    return false;
}
CheatMenuEvents CheatMenu::update(std::uint32_t held) {
    CheatMenuEvents events;
    if(!configured()) {previous_=held;return events;}
    if(!unlocked_) {
        if((held&~previous_)!=0&&unlock_press(held)) {
            unlocked_=true;open_=true;events.opened=true;
            std::fill(prefixes_.begin(),prefixes_.end(),static_cast<unsigned char>(0));
        }
        previous_=held;
        return events;
    }
    if(rises(config_.back,held)) {
        open_=!open_;
        events.opened=open_;events.closed=!open_;
    } else if(open_&&rises(config_.close,held)) {
        open_=false;events.closed=true;
    } else if(open_) {
        const bool up=rises(config_.up,held),down=rises(config_.down,held);
        const bool up_held=config_.up!=0&&(held&config_.up)==config_.up;
        const bool down_held=config_.down!=0&&(held&config_.down)==config_.down;
        if(!(up_held&&down_held)) {
            if(up!=down)selection_=(selection_+(up?5:1))%6;
            // A direction sample navigates only; it cannot also activate a row.
            else if(!up&&rises(config_.accept,held)) {
                if(selection_==5) {open_=false;events.closed=true;}
                else {
                    events.action=true;
                    events.selected_action=static_cast<CheatAction>(selection_);
                }
            }
        }
    }
    previous_=held;
    return events;
}
CheatMenuEvents CheatMenu::close() {
    CheatMenuEvents events;
    events.closed=open_;open_=false;return events;
}
void CheatMenu::new_game() {
    // App-lifetime UI state is independent of round state.
}
void CheatMenu::synchronize_input(std::uint32_t held) {previous_=held;}
bool CheatMenu::matching() const {
    if(unlocked_)return false;
    for(std::size_t i=1;i<prefixes_.size();++i)if(prefixes_[i])return true;
    return false;
}
void CheatMenu::reset_attempt() {
    std::fill(prefixes_.begin(),prefixes_.end(),static_cast<unsigned char>(0));
    std::fill(next_.begin(),next_.end(),static_cast<unsigned char>(0));
}
bool CheatMenu::configured() const {return !config_.sequence.empty();}
bool CheatMenu::isOpen() const {return open_;}
bool CheatMenu::isUnlocked() const {return unlocked_;}
std::size_t CheatMenu::selection() const {return selection_;}
CheatMenuSnapshot CheatMenu::snapshot() const {
    CheatMenuSnapshot result={configured(),unlocked_,open_,selection_};return result;
}
} }
