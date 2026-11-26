#pragma once
// The service button on the back of the pedal (0.6.2): a Maplin push to break switch between
// Pi pin 37 (GPIO 26) and pin 39 (ground), read with the Pi's pull-up on. Push to break, so the
// pin reads low normally and high while it's pressed. Nothing fitted reads high too.
//
// Jobs, decided when you let go, so a broken wire (which reads as a press that never ends)
// can't shut the rig down or touch the Wi-Fi:
//   hold 3 s, let go    shut the Pi down cleanly
//   hold 10 s, let go   Keystone's Wi-Fi off, so the Pi goes back onto your phone's hotspot
//   held over 20 s      treated as a fault (stuck or a broken wire) and ignored until it lets go
// A press already down when Keystone starts is ignored until it lets go, so a button held
// (or a wire broken) through boot does nothing either.
//
// Pure logic, fed the pin's level and the time: no hardware here, so the tests can drive it.

#include <string>

namespace ks
{
class ServiceButton
{
public:
    static constexpr double kShutdownHold = 3.0;
    static constexpr double kWifiHold = 10.0;
    static constexpr double kStuckHold = 20.0;
    static constexpr double kSettle = 0.05;   // a level has to hold this long to count (contact bounce)

    enum class Action { None, Shutdown, WifiReset };
    // what a hold has reached so far, for a countdown on the phone (and Hev's eyes later)
    enum class Stage { Idle, Counting, Shutdown, WifiReset, Stuck };
    enum class Fitted { Unknown, NotSeen, Fitted };

    // pressed: the pin's raw level (high = pressed). Returns what to do on this call, if anything.
    Action update (bool pressed, double now)
    {
        if (! started) { started = true; level = raw = pressed; rawSince = now; heldAtStart = pressed; if (pressed) downSince = now; else seenReleased = true; return Action::None; }
        if (pressed != raw) { raw = pressed; rawSince = now; }
        if (raw == level || now - rawSince < kSettle) { stage = stageFor (now); return Action::None; }
        level = raw;
        if (level)
        {
            downSince = now;
            stage = Stage::Counting;
            return Action::None;
        }
        // let go
        seenReleased = true;
        const double held = now - downSince;
        const bool counted = ! heldAtStart;
        heldAtStart = false;
        stage = Stage::Idle;
        if (! counted) return Action::None;
        if (held >= kStuckHold) { ++faults; return Action::None; }
        if (held >= kWifiHold) return Action::WifiReset;
        if (held >= kShutdownHold) return Action::Shutdown;
        return Action::None;
    }

    Stage currentStage() const { return stage; }
    bool down() const { return level; }
    double heldFor (double now) const { return level ? now - downSince : 0.0; }
    int faultCount() const { return faults; }

    // pull-up and nothing wired reads pressed for ever: until it's been seen let go, it's not fitted
    Fitted fitted() const { return ! started ? Fitted::Unknown : seenReleased ? Fitted::Fitted : Fitted::NotSeen; }

    // a line for the phone's Health page
    std::string healthText (double now) const
    {
        if (! started) return "Not read yet";
        if (! seenReleased) return "Not fitted yet (or held since Keystone started)";
        if (stage == Stage::Stuck) return "Reads pressed for " + std::to_string ((int) heldFor (now)) + " s: check its wires";
        if (faults > 0) return "Read pressed for over 20 s earlier: check its wires";
        return "Hold 3 s to shut down, 10 s to reset the Wi-Fi";
    }
    bool healthOk (double now) const { return started && seenReleased && stageFor (now) != Stage::Stuck && faults == 0; }

private:
    Stage stageFor (double now) const
    {
        if (! level) return Stage::Idle;
        if (heldAtStart) return Stage::Stuck;
        const double h = now - downSince;
        return h >= kStuckHold ? Stage::Stuck : h >= kWifiHold ? Stage::WifiReset : h >= kShutdownHold ? Stage::Shutdown : Stage::Counting;
    }

    bool started = false, level = false, raw = false, heldAtStart = false, seenReleased = false;
    double rawSince = 0, downSince = 0;
    Stage stage = Stage::Idle;
    int faults = 0;
};
}
