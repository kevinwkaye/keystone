#pragma once
// FootswitchLogic, copied from Prism Patch's PedalControl (the house reference) with the
// pairEnabled switch from Quack. Turns plain press and release messages into taps, holds
// and two-switch combos, so any momentary footswitch works. No JUCE: it lives in the
// portable core so the Pi build shares it.

#include <array>
#include <functional>

namespace ks
{
class FootswitchLogic
{
public:
    enum Event { Down, Tap, HoldStart, HoldEnd, ComboAB, ComboCD, ComboHoldAB, ComboHoldCD };
    std::function<void (int sw, Event)> onEvent;

    static constexpr double kHoldSeconds = 0.45, kComboWindow = 0.12, kComboHoldSeconds = 1.0;
    // how long each switch is held before it counts as a hold: B (next song) is quicker
    std::array<double, 4> holdSeconds { kHoldSeconds, 0.3, kHoldSeconds, kHoldSeconds };

    // Combos a plugin doesn't use are switched off, so a quick A then B is two presses.
    std::array<bool, 2> pairEnabled { true, true };    // Keystone: A+B held swaps rig, C+D swaps mode

    void press (int sw, double now)
    {
        if (sw < 0 || sw > 3 || down[(size_t) sw]) return;
        down[(size_t) sw] = true;
        held[(size_t) sw] = false;
        combo[(size_t) sw] = false;
        downAt[(size_t) sw] = now;

        const int partner = sw ^ 1;
        if (pairEnabled[(size_t) (sw / 2)] && down[(size_t) partner] && ! held[(size_t) partner]
            && now - downAt[(size_t) partner] < kComboWindow)
        {
            combo[(size_t) sw] = combo[(size_t) partner] = true;
            comboAt[(size_t) (sw / 2)] = now;
            comboHeld[(size_t) (sw / 2)] = false;
            if (onEvent) onEvent (sw < 2 ? 0 : 2, sw < 2 ? ComboAB : ComboCD);
            return;
        }
        if (onEvent) onEvent (sw, Down);
    }

    void release (int sw, double now)
    {
        if (sw < 0 || sw > 3 || ! down[(size_t) sw]) return;
        tick (now);
        down[(size_t) sw] = false;
        if (combo[(size_t) sw]) return;
        if (onEvent) onEvent (sw, held[(size_t) sw] ? HoldEnd : Tap);
    }

    void tick (double now)
    {
        for (int pair = 0; pair < 2; ++pair)
        {
            const int a = pair * 2, b = a + 1;
            if (down[(size_t) a] && down[(size_t) b] && combo[(size_t) a] && combo[(size_t) b]
                && ! comboHeld[(size_t) pair] && now - comboAt[(size_t) pair] >= kComboHoldSeconds)
            {
                comboHeld[(size_t) pair] = true;
                if (onEvent) onEvent (a, pair == 0 ? ComboHoldAB : ComboHoldCD);
            }
        }
        for (int i = 0; i < 4; ++i)
            if (down[(size_t) i] && ! held[(size_t) i] && ! combo[(size_t) i] && now - downAt[(size_t) i] >= holdSeconds[(size_t) i])
            {
                held[(size_t) i] = true;
                if (onEvent) onEvent (i, HoldStart);
            }
    }

    void reset() { down = {}; held = {}; combo = {}; comboHeld = {}; }
    bool isDown (int sw) const { return down[(size_t) sw]; }

private:
    std::array<bool, 4> down {}, held {}, combo {};
    std::array<double, 4> downAt {};
    std::array<double, 2> comboAt {};
    std::array<bool, 2> comboHeld {};
};
} // namespace ks
