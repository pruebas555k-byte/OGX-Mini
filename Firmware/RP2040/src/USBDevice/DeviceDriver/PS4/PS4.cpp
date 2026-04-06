#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "pico/time.h"
#include "USBDevice/DeviceDriver/PS4/PS4.h"

// --------------------------------------------------------------------------------
// HELPERS
// --------------------------------------------------------------------------------

static inline uint8_t map_signed_to_uint8(float signed_val)
{
    if (signed_val >= 0.99f) return 255;
    if (signed_val <= -0.99f) return 0;

    float mapped = signed_val * 127.5f + 127.5f;
    int out = (int)(mapped + 0.5f);

    if (out < 0) out = 0;
    if (out > 255) out = 255;

    return (uint8_t)out;
}

static inline void apply_stick_steam_radial(
    int16_t in_x,
    int16_t in_y,
    float deadzone,
    float sensitivity,
    float curve,
    uint8_t &out_x,
    uint8_t &out_y)
{
    const float inv = 1.0f / 32767.0f;

    float vx = in_x * inv;
    float vy = in_y * inv;

    float mag = sqrtf(vx*vx + vy*vy);

    if (mag <= deadzone || mag < 0.001f)
    {
        out_x = 128;
        out_y = 128;
        return;
    }

    if (mag > 1.0f) mag = 1.0f;

    float adj = (mag - deadzone) / (1.0f - deadzone);

    if (adj < 0.0f) adj = 0.0f;
    if (adj > 1.0f) adj = 1.0f;

    float out_frac = adj * sensitivity;

    if (curve != 1.0f)
        out_frac = powf(out_frac, curve);

    if (out_frac > 1.0f)
        out_frac = 1.0f;

    float scale = out_frac / mag;

    float sx = vx * scale;
    float sy = vy * scale;

    if (sx > 1.0f) sx = 1.0f;
    if (sx < -1.0f) sx = -1.0f;

    if (sy > 1.0f) sy = 1.0f;
    if (sy < -1.0f) sy = -1.0f;

    out_x = map_signed_to_uint8(sx);
    out_y = map_signed_to_uint8(sy);
}

// --------------------------------------------------------------------------------
// PS4 DEVICE
// --------------------------------------------------------------------------------

void PS4Device::initialize()
{
    class_driver_ =
    {
        .name = TUD_DRV_NAME("PS4"),
        .init = hidd_init,
        .deinit = hidd_deinit,
        .reset = hidd_reset,
        .open = hidd_open,
        .control_xfer_cb = hidd_control_xfer_cb,
        .xfer_cb = hidd_xfer_cb,
        .sof = nullptr
    };
}

void PS4Device::process(const uint8_t idx, Gamepad& gamepad)
{
    (void)idx;

    static bool mutePrev = false;
    static absolute_time_t muteEndTime;
    static bool muteActive = false;

    constexpr uint32_t MUTE_MS = 487;

    static absolute_time_t next_report_time = make_timeout_time_ms(0);

    Gamepad::PadIn gp_in = gamepad.get_pad_in();

    const uint16_t btn = gp_in.buttons;

    const bool mutePressed = (btn & Gamepad::BUTTON_MISC) != 0;
    const bool psPressed = (btn & Gamepad::BUTTON_SYS) != 0;
    const bool sharePressed = (btn & Gamepad::BUTTON_BACK) != 0;

    if (mutePressed && !mutePrev)
    {
        muteActive = true;
        muteEndTime = make_timeout_time_ms(MUTE_MS);
    }

    mutePrev = mutePressed;

    if (muteActive && time_reached(muteEndTime))
        muteActive = false;

    std::memset(&report_in_, 0, sizeof(report_in_));

    report_in_.reportID = 0x01;

    report_in_.gamepad.touchpadActive = 0;
    report_in_.gamepad.touchpadData.p1.unpressed = 1;
    report_in_.gamepad.touchpadData.p2.unpressed = 1;

    constexpr float left_deadzone = 0.016f;
    constexpr float right_deadzone = 0.07f;

    apply_stick_steam_radial(
        gp_in.joystick_lx,
        gp_in.joystick_ly,
        left_deadzone,
        1.0f,
        1.0f,
        report_in_.leftStickX,
        report_in_.leftStickY
    );

    apply_stick_steam_radial(
        gp_in.joystick_rx,
        gp_in.joystick_ry,
        right_deadzone,
        1.0f,
        1.0f,
        report_in_.rightStickX,
        report_in_.rightStickY
    );

    switch (gp_in.dpad)
    {
        case Gamepad::DPAD_UP: report_in_.dpad = PS4Dev::HAT_UP; break;
        case Gamepad::DPAD_UP_RIGHT: report_in_.dpad = PS4Dev::HAT_UP_RIGHT; break;
        case Gamepad::DPAD_RIGHT: report_in_.dpad = PS4Dev::HAT_RIGHT; break;
        case Gamepad::DPAD_DOWN_RIGHT: report_in_.dpad = PS4Dev::HAT_DOWN_RIGHT; break;
        case Gamepad::DPAD_DOWN: report_in_.dpad = PS4Dev::HAT_DOWN; break;
        case Gamepad::DPAD_DOWN_LEFT: report_in_.dpad = PS4Dev::HAT_DOWN_LEFT; break;
        case Gamepad::DPAD_LEFT: report_in_.dpad = PS4Dev::HAT_LEFT; break;
        case Gamepad::DPAD_UP_LEFT: report_in_.dpad = PS4Dev::HAT_UP_LEFT; break;
        default: report_in_.dpad = PS4Dev::HAT_CENTER; break;
    }

    report_in_.buttonWest  = ((btn & Gamepad::BUTTON_X) || muteActive);
    report_in_.buttonEast  = ((btn & Gamepad::BUTTON_B) || muteActive);
    report_in_.buttonSouth = (btn & Gamepad::BUTTON_A);
    report_in_.buttonNorth = (btn & Gamepad::BUTTON_Y);

    const bool physL1 = (btn & Gamepad::BUTTON_LB);
    const bool physR1 = (btn & Gamepad::BUTTON_RB);

    uint8_t physL2_val = gp_in.trigger_l;
    uint8_t physR2_val = gp_in.trigger_r;

    bool virtL1 = physL1;
    bool virtR1 = false;
    bool virtL2 = false;
    bool virtR2 = false;

    uint8_t trigL_val = 0;
    uint8_t trigR_val = 0;

    if (physR1)
    {
        virtR2 = true;
        trigR_val = 255;
    }

    if (physR2_val > 20)
    {
        virtL2 = true;
        trigL_val = physR2_val;
    }

    if (physL2_val > 191)
        virtR1 = true;

    report_in_.buttonL1 = virtL1;
    report_in_.buttonR1 = virtR1;
    report_in_.buttonL2 = virtL2;
    report_in_.buttonR2 = virtR2;

    report_in_.leftTrigger  = trigL_val;
    report_in_.rightTrigger = trigR_val;

    report_in_.buttonL3 = (btn & Gamepad::BUTTON_L3);
    report_in_.buttonR3 = (btn & Gamepad::BUTTON_R3);

    report_in_.buttonSelect = sharePressed;
    report_in_.buttonStart  = (btn & Gamepad::BUTTON_START);
    report_in_.buttonHome   = psPressed;
    report_in_.buttonTouchpad = sharePressed;

    if (tud_suspended())
        tud_remote_wakeup();

    if (time_reached(next_report_time) && tud_hid_ready())
    {
        tud_hid_report(
            0,
            (uint8_t*)&report_in_,
            sizeof(PS4Dev::InReport)
        );

        next_report_time = make_timeout_time_ms(1);
    }
}

// --------------------------------------------------------------------------------
// CALLBACKS
// --------------------------------------------------------------------------------

uint16_t PS4Device::get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)itf;
    (void)report_id;

    if (report_type == HID_REPORT_TYPE_INPUT)
    {
        uint16_t len = std::min<uint16_t>(reqlen, sizeof(PS4Dev::InReport));
        std::memcpy(buffer, &report_in_, len);
        return len;
    }

    return 0;
}

void PS4Device::set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool PS4Device::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

const uint16_t* PS4Device::get_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    const char* value =
        reinterpret_cast<const char*>(PS4Dev::STRING_DESCRIPTORS[index]);

    return get_string_descriptor(value, index);
}

const uint8_t* PS4Device::get_descriptor_device_cb()
{
    return PS4Dev::DEVICE_DESCRIPTORS;
}

const uint8_t* PS4Device::get_hid_descriptor_report_cb(uint8_t itf)
{
    (void)itf;
    return PS4Dev::REPORT_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return PS4Dev::CONFIGURATION_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_device_qualifier_cb()
{
    return nullptr;
}
