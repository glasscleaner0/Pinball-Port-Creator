#ifndef PINBALL_REWRITE_CHEAT_ACTION_HPP
#define PINBALL_REWRITE_CHEAT_ACTION_HPP
namespace pinball { namespace rules {
// Original XP gameplay code effects. Controller unlock/menu behavior is a
// separate, user-selected console interface; it does not copy the XP parser.
enum class CheatAction { HiddenTest, ExtraBall, UnlimitedBalls, GravityWell, RankUp };
} }
#endif