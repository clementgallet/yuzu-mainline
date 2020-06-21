// Copyright 2020 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <atomic>
#include <list>
#include <mutex>
#include <utility>
#include "common/threadsafe_queue.h"
#include "input_common/gcadapter/gc_adapter.h"
#include "input_common/gcadapter/gc_poller.h"

namespace InputCommon {

class GCButton final : public Input::ButtonDevice {
public:
    explicit GCButton(int port_, int button_, int axis_, GCAdapter::Adapter* adapter)
        : port(port_), button(button_), gcadapter(adapter) {}

    ~GCButton() override;

    bool GetStatus() const override {
        return gcadapter->GetPadState()[port].buttons.at(button);
    }

private:
    const int port;
    const int button;
    GCAdapter::Adapter* gcadapter;
};

class GCAxisButton final : public Input::ButtonDevice {
public:
    explicit GCAxisButton(int port_, int axis_, float threshold_, bool trigger_if_greater_,
                          GCAdapter::Adapter* adapter)
        : port(port_), axis(axis_), threshold(threshold_), trigger_if_greater(trigger_if_greater_),
          gcadapter(adapter) {}

    bool GetStatus() const override {
        const float axis_value = (gcadapter->GetPadState()[port].axes.at(axis) - 128.0f) / 128.0f;
        if (trigger_if_greater) {
            return axis_value > 0.10f; // TODO(ameerj) : Fix threshold.
        }
        return axis_value < -0.10f;
    }

private:
    const int port;
    const int axis;
    float threshold;
    bool trigger_if_greater;
    GCAdapter::Adapter* gcadapter;
};

GCButtonFactory::GCButtonFactory() {
    adapter = GCAdapter::Adapter::GetInstance();
}

GCButton::~GCButton() {
    // GCAdapter::Shutdown();
}

std::unique_ptr<Input::ButtonDevice> GCButtonFactory::Create(const Common::ParamPackage& params) {
    int button_id = params.Get("button", 0);
    int port = params.Get("port", 0);
    // For Axis buttons, used by the binary sticks.
    if (params.Has("axis")) {
        const int axis = params.Get("axis", 0);
        const float threshold = params.Get("threshold", 0.5f);
        const std::string direction_name = params.Get("direction", "");
        bool trigger_if_greater;
        if (direction_name == "+") {
            trigger_if_greater = true;
        } else if (direction_name == "-") {
            trigger_if_greater = false;
        } else {
            trigger_if_greater = true;
            LOG_ERROR(Input, "Unknown direction {}", direction_name);
        }
        return std::make_unique<GCAxisButton>(port, axis, threshold, trigger_if_greater, adapter);
    }

    std::unique_ptr<GCButton> button =
        std::make_unique<GCButton>(port, button_id, params.Get("axis", 0), adapter);
    return std::move(button);
}

Common::ParamPackage GCButtonFactory::GetNextInput() {
    Common::ParamPackage params;
    GCAdapter::GCPadStatus pad;
    for (int i = 0; i < 4; i++) {
        while (adapter->GetPadQueue()[i].Pop(pad)) {
            // This while loop will break on the earliest detected button
            params.Set("engine", "gcpad");
            params.Set("port", i);
            // I was debating whether to keep these verbose for ease of reading
            // or to use a while loop shifting the bits to test and set the value.
            if (pad.button & GCAdapter::PAD_BUTTON_A) {
                params.Set("button", GCAdapter::PAD_BUTTON_A);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_B) {
                params.Set("button", GCAdapter::PAD_BUTTON_B);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_X) {
                params.Set("button", GCAdapter::PAD_BUTTON_X);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_Y) {
                params.Set("button", GCAdapter::PAD_BUTTON_Y);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_DOWN) {
                params.Set("button", GCAdapter::PAD_BUTTON_DOWN);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_LEFT) {
                params.Set("button", GCAdapter::PAD_BUTTON_LEFT);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_RIGHT) {
                params.Set("button", GCAdapter::PAD_BUTTON_RIGHT);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_UP) {
                params.Set("button", GCAdapter::PAD_BUTTON_UP);
                break;
            }
            if (pad.button & GCAdapter::PAD_TRIGGER_L) {
                params.Set("button", GCAdapter::PAD_TRIGGER_L);
                break;
            }
            if (pad.button & GCAdapter::PAD_TRIGGER_R) {
                params.Set("button", GCAdapter::PAD_TRIGGER_R);
                break;
            }
            if (pad.button & GCAdapter::PAD_TRIGGER_Z) {
                params.Set("button", GCAdapter::PAD_TRIGGER_Z);
                break;
            }
            if (pad.button & GCAdapter::PAD_BUTTON_START) {
                params.Set("button", GCAdapter::PAD_BUTTON_START);
                break;
            }
            // For Axis button implementation
            if (pad.axis != GCAdapter::PadAxes::Undefined) {
                params.Set("axis", static_cast<u8>(pad.axis));
                params.Set("button", GCAdapter::PAD_STICK);
                if (pad.axis_value > 128) {
                    params.Set("direction", "+");
                    params.Set("threshold", "0.5");
                } else {
                    params.Set("direction", "-");
                    params.Set("threshold", "-0.5");
                }
                break;
            }
        }
    }
    return params;
}

void GCButtonFactory::BeginConfiguration() {
    polling = true;
    for (int i = 0; i < 4; i++) {
        adapter->GetPadQueue()[i].Clear();
    }
    adapter->BeginConfiguration();
}

void GCButtonFactory::EndConfiguration() {
    polling = false;
    for (int i = 0; i < 4; i++) {
        adapter->GetPadQueue()[i].Clear();
    }
    adapter->EndConfiguration();
}

class GCAnalog final : public Input::AnalogDevice {
public:
    GCAnalog(int port_, int axis_x_, int axis_y_, float deadzone_, GCAdapter::Adapter* adapter)
        : port(port_), axis_x(axis_x_), axis_y(axis_y_), deadzone(deadzone_), gcadapter(adapter) {}

    float GetAxis(int axis) const {
        std::lock_guard lock{mutex};
        // division is not by a perfect 128 to account for some variance in center location
        // e.g. my device idled at 131 in X, 120 in Y, and full range of motion was in range
        // [20-230]
        return (gcadapter->GetPadState()[port].axes.at(axis) - 128.0f) / 95.0f;
    }

    std::tuple<float, float> GetAnalog(int axis_x, int axis_y) const {
        float x = GetAxis(axis_x);
        float y = GetAxis(axis_y);

        // Make sure the coordinates are in the unit circle,
        // otherwise normalize it.
        float r = x * x + y * y;
        if (r > 1.0f) {
            r = std::sqrt(r);
            x /= r;
            y /= r;
        }

        return std::make_tuple(x, y);
    }

    std::tuple<float, float> GetStatus() const override {
        const auto [x, y] = GetAnalog(axis_x, axis_y);
        const float r = std::sqrt((x * x) + (y * y));
        if (r > deadzone) {
            return std::make_tuple(x / r * (r - deadzone) / (1 - deadzone),
                                   y / r * (r - deadzone) / (1 - deadzone));
        }
        return std::make_tuple<float, float>(0.0f, 0.0f);
    }

    bool GetAnalogDirectionStatus(Input::AnalogDirection direction) const override {
        const auto [x, y] = GetStatus();
        const float directional_deadzone = 0.4f;
        switch (direction) {
        case Input::AnalogDirection::RIGHT:
            return x > directional_deadzone;
        case Input::AnalogDirection::LEFT:
            return x < -directional_deadzone;
        case Input::AnalogDirection::UP:
            return y > directional_deadzone;
        case Input::AnalogDirection::DOWN:
            return y < -directional_deadzone;
        }
        return false;
    }

private:
    const int port;
    const int axis_x;
    const int axis_y;
    const float deadzone;
    mutable std::mutex mutex;
    GCAdapter::Adapter* gcadapter;
};

/// An analog device factory that creates analog devices from GC Adapter
GCAnalogFactory::GCAnalogFactory() {
    adapter = GCAdapter::Adapter::GetInstance();
};

/**
 * Creates analog device from joystick axes
 * @param params contains parameters for creating the device:
 *     - "port": the nth gcpad on the adapter
 *     - "axis_x": the index of the axis to be bind as x-axis
 *     - "axis_y": the index of the axis to be bind as y-axis
 */
std::unique_ptr<Input::AnalogDevice> GCAnalogFactory::Create(const Common::ParamPackage& params) {
    const std::string guid = params.Get("guid", "0");
    const int port = params.Get("port", 0);
    const int axis_x = params.Get("axis_x", 0);
    const int axis_y = params.Get("axis_y", 1);
    const float deadzone = std::clamp(params.Get("deadzone", 0.0f), 0.0f, .99f);

    return std::make_unique<GCAnalog>(port, axis_x, axis_y, deadzone, adapter);
}

void GCAnalogFactory::BeginConfiguration() {
    polling = true;
    for (int i = 0; i < 4; i++) {
        adapter->GetPadQueue()[i].Clear();
    }
    adapter->BeginConfiguration();
}

void GCAnalogFactory::EndConfiguration() {
    polling = false;
    for (int i = 0; i < 4; i++) {
        adapter->GetPadQueue()[i].Clear();
    }
    adapter->EndConfiguration();
}

Common::ParamPackage GCAnalogFactory::GetNextInput() {
    GCAdapter::GCPadStatus pad;
    for (int i = 0; i < 4; i++) {
        while (adapter->GetPadQueue()[i].Pop(pad)) {
            if (pad.axis == GCAdapter::PadAxes::Undefined ||
                std::abs((pad.axis_value - 128.0f) / 128.0f) < 0.1) {
                continue;
            }
            // An analog device needs two axes, so we need to store the axis for later and wait for
            // a second SDL event. The axes also must be from the same joystick.
            const u8 axis = static_cast<u8>(pad.axis);
            if (analog_x_axis == -1) {
                analog_x_axis = axis;
                controller_number = i;
            } else if (analog_y_axis == -1 && analog_x_axis != axis && controller_number == i) {
                analog_y_axis = axis;
            }
        }
    }
    Common::ParamPackage params;
    if (analog_x_axis != -1 && analog_y_axis != -1) {
        params.Set("engine", "gcpad");
        params.Set("port", controller_number);
        params.Set("axis_x", analog_x_axis);
        params.Set("axis_y", analog_y_axis);
        analog_x_axis = -1;
        analog_y_axis = -1;
        controller_number = -1;
        return params;
    }
    return params;
}

} // namespace InputCommon
