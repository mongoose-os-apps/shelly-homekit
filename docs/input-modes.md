## Switches and light bulbs input modes

### Input modes comparison

| Current<br/>state of<br/>output | Input change<br/>scenario | Momentary | Toggle | Edge  | Detached |    Activation     | Activation Once |
|:-------------------------------:|:-------------------------:|:---------:|:------:|:-----:|:--------:|:-----------------:|:---------------:|
|              `OFF`              |           `→ON`           |   `ON`    |  `ON`  | `ON`  |   `-`    |       `ON`        |      `ON`       |
|              `ON`               |          `→OFF`           |    `-`    | `OFF`  | `OFF` |   `-`    |   `Timer reset`   |       `-`       |
|              `OFF`              |          `→OFF`           |    `-`    |  `-`   | `ON`  |   `-`    |        `-`        |       `-`       |
|              `ON`               |           `→ON`           |   `OFF`   |  `-`   | `OFF` |   `-`    | `Timer reset`[^1] |       `-`       |

[^1]: Except for the LED lights (RGBW2, Bulb, Duo, Vintage)

#### Input change scenarios:

- `→ON`: Input changes from `OFF` to `ON`. Examples:
    - External switch is turned on
    - Doorbell button is pressed (but not released yet)
    - 2-way switch is flipped to the other position
- `→OFF`: Input changes from `ON` to `OFF`. Examples:
    - External switch is turned off
    - Doorbell button is released (if was pressed before)
    - 2-way switch is flipped to the other position

#### Output outcomes:

- `ON`: Output turns `ON`, Auto-off timer started[^2].
- `OFF`: Output turns `OFF`, Auto-off timer is stopped[^2].
- `-`: Output stays the same (`ON` if was `ON`, `OFF` if was `OFF`), Auto-off timer also stays the same (running if was
  started, stopped if was stopped)[^2].
- `Timer reset`: Output stays `ON`, Auto-off timer is reset (restarted if was running, started if was stopped)[^2].

[^2]: Considering the Auto-off timer is enabled in the device configuration.

### Input modes use cases and examples

#### Momentary

Can be used in conjunction with a stateless switch (doorbell button). **Single press of the button will flip the output
**
state (turn on if was off, turn off if was on). The output will stay in that state until the next button press.

#### Toggle

In conjunction with a stateful switch 1-way. Usage with 2-way switch is possible, but Edge mode is a better fit for that
case. The output will **copy the state of the switch** (at the time of the press). Meaning, if the switch flips on, the
output will turn on, if the switch flips off, the output will turn off.

On the other hand, if the output was already on, and the switch flips from "off" to "on", the output will not change,
and vice versa. So in the scenario where the lights were turned on from the app, while the switch was in the "off"
position, flipping the switch to "on" will not change the output state (the lights will stay on). To turn the lights
off, the switch needs to be flipped to "on" and then back to "off" (double flip).

#### Edge

In conjunction with a stateful 1-way or 2-way switch. The output will turn change state to the opposite (turn on if was
off, turn off if was on) when the switch is flipped from any state to any other. Meaning, if the output was off, and the
switch was flipped from "on" to "off", the output will turn on and vice versa.

On the other hand, this may (and often will) lead to the situation where the state of the physical switch and the output
are different (lights are on, while the switch is in the "off" position). For example, if the lights were turned on
from the app, while the switch was in the "off" position, flipping the switch to "on" will turn the lights off.

#### Detached

The switch has no effect on the output. The output can be controlled only from the app.

#### Activation

In conjunction with a stateless switch (doorbell button) or a motion sensor and the Auto-off timer. The output will turn
on when the switch is pressed (or motion is detected), and will turn off only after the Auto-off timer expires. If
during the timer countdown the switch is pressed again (or motion is detected again), the timer will be reset and the
output will stay on.

Note the different behavior for the LED lights (RGBW2, Bulb, Duo, Vintage) and the rest of the devices:
In LED lights the timer is not reset when the signal from the button or motion sensor stops.
In contrast, on the other devices, the timer will reset if the signal disappears (for example when motion stops).

#### Activation Once

In conjunction with an external timer (for example, christmas lights or pool timer) to turn the device on and internal
Auto-off timer to turn it off. The output will turn on when ether the external timer turns on or it is turned on from
the app, and will turn off only after the Auto-off timer expires, no matter how the input changes. If during the
auto-off timer countdown the external timer turns on or off, this will have no effect on the output.

This is useful for example for a pool pump, where you have an external timer to turn it on at 15:00 every day (a setting
of the external timer) and run for 2 hours (a setting of the internal auto-off timer), but you want to be able to
override the external timer from the app on demand (manually start early at 14:00) and when the external timer kicks in,
you don't want it to restart the auto-off timer like in "Activation" mode.