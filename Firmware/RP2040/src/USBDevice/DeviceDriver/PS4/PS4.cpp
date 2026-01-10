#include <cstring>
#include <algorithm>

#include "USBDevice/DeviceDriver/PS4/PS4.h"

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
        .sof = NULL
    };
}

void PS4Device::process(const uint8_t idx, Gamepad& gamepad)
{
    (void) idx;

    if (gamepad.new_pad_in())
    {
        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        report_in_ = PS4_DEV::InReport(); // limpia struct y setea valores por defecto

        // Sticks
        report_in_.joystick_lx = Scale::int16_to_uint8(gp_in.joystick_lx);
        report_in_.joystick_ly = Scale::int16_to_uint8(gp_in.joystick_ly);
        report_in_.joystick_rx = Scale::int16_to_uint8(gp_in.joystick_rx);
        report_in_.joystick_ry = Scale::int16_to_uint8(gp_in.joystick_ry);

        // D-Pad -> hat
        uint8_t hat = PS4_DEV::Hat::DPAD_CENTER;
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:          hat = PS4_DEV::Hat::DPAD_UP; break;
            case Gamepad::DPAD_UP_RIGHT:    hat = PS4_DEV::Hat::DPAD_UP_RIGHT; break;
            case Gamepad::DPAD_RIGHT:       hat = PS4_DEV::Hat::DPAD_RIGHT; break;
            case Gamepad::DPAD_DOWN_RIGHT:  hat = PS4_DEV::Hat::DPAD_DOWN_RIGHT; break;
            case Gamepad::DPAD_DOWN:        hat = PS4_DEV::Hat::DPAD_DOWN; break;
            case Gamepad::DPAD_DOWN_LEFT:   hat = PS4_DEV::Hat::DPAD_DOWN_LEFT; break;
            case Gamepad::DPAD_LEFT:        hat = PS4_DEV::Hat::DPAD_LEFT; break;
            case Gamepad::DPAD_UP_LEFT:     hat = PS4_DEV::Hat::DPAD_UP_LEFT; break;
            default:
                break;
        }
        report_in_.hat = hat;

        // Botones frontales (A/B/X/Y → ✕ / ○ / □ / △)
        if (gp_in.buttons & Gamepad::BUTTON_A)
            report_in_.buttons0 |= PS4_DEV::Buttons0::CROSS;
        if (gp_in.buttons & Gamepad::BUTTON_B)
            report_in_.buttons0 |= PS4_DEV::Buttons0::CIRCLE;
        if (gp_in.buttons & Gamepad::BUTTON_X)
            report_in_.buttons0 |= PS4_DEV::Buttons0::SQUARE;
        if (gp_in.buttons & Gamepad::BUTTON_Y)
            report_in_.buttons0 |= PS4_DEV::Buttons0::TRIANGLE;

        // L1 / R1
        if (gp_in.buttons & Gamepad::BUTTON_LB)
            report_in_.buttons0 |= PS4_DEV::Buttons0::L1;
        if (gp_in.buttons & Gamepad::BUTTON_RB)
            report_in_.buttons0 |= PS4_DEV::Buttons0::R1;

        // Click sticks
        if (gp_in.buttons & Gamepad::BUTTON_L3)
            report_in_.buttons1 |= PS4_DEV::Buttons1::L3;
        if (gp_in.buttons & Gamepad::BUTTON_R3)
            report_in_.buttons1 |= PS4_DEV::Buttons1::R3;

        // Share / Options
        if (gp_in.buttons & Gamepad::BUTTON_BACK)
            report_in_.buttons1 |= PS4_DEV::Buttons1::SHARE;
        if (gp_in.buttons & Gamepad::BUTTON_START)
            report_in_.buttons1 |= PS4_DEV::Buttons1::OPTIONS;

        // Botón PS / Touchpad click
        if (gp_in.buttons & Gamepad::BUTTON_SYS)
            report_in_.buttons1 |= PS4_DEV::Buttons1::PS;
        if (gp_in.buttons & Gamepad::BUTTON_MISC)
            report_in_.buttons1 |= PS4_DEV::Buttons1::TOUCHPAD;

        // Triggers analógicos + “botón”
        report_in_.l2_axis = gp_in.trigger_l;
        report_in_.r2_axis = gp_in.trigger_r;

        if (gp_in.trigger_l)
            report_in_.buttons0 |= PS4_DEV::Buttons0::L2;
        if (gp_in.trigger_r)
            report_in_.buttons0 |= PS4_DEV::Buttons0::R2;
    }

    if (tud_suspended())
    {
        tud_remote_wakeup();
    }

    if (tud_hid_ready())
    {
        tud_hid_report(
            PS4_DEV::ReportID::INPUT,
            reinterpret_cast<uint8_t*>(&report_in_),
            sizeof(PS4_DEV::InReport));
    }

    // De momento ignoramos feedback (rumble/LEDs) en modo device PS4
}

uint16_t PS4Device::get_report_cb(uint8_t itf,
                                  uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t *buffer,
                                  uint16_t reqlen)
{
    (void) itf;

    if (report_type == HID_REPORT_TYPE_INPUT &&
        report_id == PS4_DEV::ReportID::INPUT)
    {
        uint16_t len = std::min<uint16_t>(reqlen, sizeof(PS4_DEV::InReport));
        std::memcpy(buffer, &report_in_, len);
        return len;
    }

    return 0;
}

void PS4Device::set_report_cb(uint8_t itf,
                              uint8_t report_id,
                              hid_report_type_t report_type,
                              uint8_t const *buffer,
                              uint16_t bufsize)
{
    (void) itf;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;

    // Aquí podrías parsear rumble / LEDs si quieres.
}

bool PS4Device::vendor_control_xfer_cb(uint8_t rhport,
                                       uint8_t stage,
                                       tusb_control_request_t const *request)
{
    (void) rhport;
    (void) stage;
    (void) request;
    return false;
}

const uint16_t* PS4Device::get_descriptor_string_cb(uint8_t index,
                                                    uint16_t langid)
{
    (void) langid;
    const char *value =
        reinterpret_cast<const char*>(PS4_DEV::STRING_DESCRIPTORS[index]);
    return get_string_descriptor(value, index);
}

const uint8_t* PS4Device::get_descriptor_device_cb()
{
    return PS4_DEV::DEVICE_DESCRIPTORS;
}

const uint8_t* PS4Device::get_hid_descriptor_report_cb(uint8_t itf)
{
    (void) itf;
    return PS4_DEV::REPORT_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return PS4_DEV::CONFIGURATION_DESCRIPTORS;
}

const uint8_t* PS4Device::get_descriptor_device_qualifier_cb()
{
    return nullptr;
}
