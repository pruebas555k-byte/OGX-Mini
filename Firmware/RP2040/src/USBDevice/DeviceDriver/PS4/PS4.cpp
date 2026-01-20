#include <cstring>
#include <algorithm>

#include "USBDevice/DeviceDriver/PS4/PS4.h"

void PS4Device::initialize()
{
    class_driver_ =
    {
        .name             = TUD_DRV_NAME("PS4"),
        .init             = hidd_init,
        .deinit           = hidd_deinit,
        .reset            = hidd_reset,
        .open             = hidd_open,
        .control_xfer_cb  = hidd_control_xfer_cb,
        .xfer_cb          = hidd_xfer_cb,
        .sof              = nullptr
    };
}

void PS4Device::process(const uint8_t idx, Gamepad& gamepad)
{
    (void)idx;

    if (gamepad.new_pad_in())
    {
        Gamepad::PadIn gp_in = gamepad.get_pad_in();

        // Limpia y deja todo en neutro
        std::memset(&report_in_, 0, sizeof(report_in_));

        // Report ID 1 (coincide con 0x85,0x01 del descriptor HID)
        report_in_.reportID = 0x01;

        // Sticks analógicos (0-255)
        report_in_.leftStickX  = Scale::int16_to_uint8(gp_in.joystick_lx);
        report_in_.leftStickY  = Scale::int16_to_uint8(gp_in.joystick_ly);
        report_in_.rightStickX = Scale::int16_to_uint8(gp_in.joystick_rx);
        report_in_.rightStickY = Scale::int16_to_uint8(gp_in.joystick_ry);

        // D-Pad → HAT en 4 bits
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:          report_in_.dpad = PS4Dev::HAT_UP;         break;
            case Gamepad::DPAD_UP_RIGHT:    report_in_.dpad = PS4Dev::HAT_UP_RIGHT;   break;
            case Gamepad::DPAD_RIGHT:       report_in_.dpad = PS4Dev::HAT_RIGHT;      break;
            case Gamepad::DPAD_DOWN_RIGHT:  report_in_.dpad = PS4Dev::HAT_DOWN_RIGHT; break;
            case Gamepad::DPAD_DOWN:        report_in_.dpad = PS4Dev::HAT_DOWN;       break;
            case Gamepad::DPAD_DOWN_LEFT:   report_in_.dpad = PS4Dev::HAT_DOWN_LEFT;  break;
            case Gamepad::DPAD_LEFT:        report_in_.dpad = PS4Dev::HAT_LEFT;       break;
            case Gamepad::DPAD_UP_LEFT:     report_in_.dpad = PS4Dev::HAT_UP_LEFT;    break;
            default:                        report_in_.dpad = PS4Dev::HAT_CENTER;     break;
        }

        const uint16_t btn = gp_in.buttons;

        // --- FLAGS AUXILIARES ---
        const bool sharePressed = (btn & Gamepad::BUTTON_BACK)  != 0;  // SHARE
        const bool mutePressed  = (btn & Gamepad::BUTTON_MISC)  != 0;  // usamos MISC como MUTE

        // Face buttons (A/B/X/Y -> CROSS/CIRCLE/SQUARE/TRIANGLE)
        // QUAD / SQUARE: normal + MUTE también lo aprieta
        const bool baseSquare = (btn & Gamepad::BUTTON_X) != 0;  // lo que ya hacía antes
        report_in_.buttonWest  = (baseSquare || mutePressed) ? 1 : 0; // ☐ + MUTE

        report_in_.buttonSouth = (btn & Gamepad::BUTTON_A) ? 1 : 0; // CROSS
        report_in_.buttonEast  = (btn & Gamepad::BUTTON_B) ? 1 : 0; // CIRCLE
        report_in_.buttonNorth = (btn & Gamepad::BUTTON_Y) ? 1 : 0; // TRIANGLE

        // Hombros
        report_in_.buttonL1    = (btn & Gamepad::BUTTON_LB) ? 1 : 0;
        report_in_.buttonR1    = (btn & Gamepad::BUTTON_RB) ? 1 : 0;

        // Sticks pulsados
        report_in_.buttonL3    = (btn & Gamepad::BUTTON_L3) ? 1 : 0;
        report_in_.buttonR3    = (btn & Gamepad::BUTTON_R3) ? 1 : 0;

        // Centrales:
        //  - SHARE aprieta su botón normal
        //  - y además también aprieta el TOUCHPAD
        report_in_.buttonSelect   = sharePressed ? 1 : 0;  // SHARE normal

        // PS
        report_in_.buttonHome     = (btn & Gamepad::BUTTON_SYS)   ? 1 : 0;

        // OPTIONS
        report_in_.buttonStart    = (btn & Gamepad::BUTTON_START) ? 1 : 0;

        // TOUCHPAD: se pulsa si:
        //   - el botón MISC (MUTE) está pulsado, o
        //   - SHARE está pulsado (lo que tú querías)
        const bool touchpadByMisc = mutePressed;
        const bool touchpadByShare = sharePressed;
        report_in_.buttonTouchpad = (touchpadByMisc || touchpadByShare) ? 1 : 0;

        // Triggers: botón + eje analógico (ON/OFF 0 ó 255)
        if (gp_in.trigger_l)
        {
            report_in_.buttonL2    = 1;
            report_in_.leftTrigger = 0xFF;
        }
        else
        {
            report_in_.buttonL2    = 0;
            report_in_.leftTrigger = 0x00;
        }

        if (gp_in.trigger_r)
        {
            report_in_.buttonR2     = 1;
            report_in_.rightTrigger = 0xFF;
        }
        else
        {
            report_in_.buttonR2     = 0;
            report_in_.rightTrigger = 0x00;
        }

        // El resto (sensores, touchpad detallado, etc.) se queda a 0 como en GP2040-CE si no se usa.
    }

    if (tud_suspended())
    {
        tud_remote_wakeup();
    }

    if (tud_hid_ready())
    {
        tud_hid_report(
            0,
            reinterpret_cast<uint8_t*>(&report_in_),
            sizeof(PS4Dev::InReport)
        );
    }
}

uint16_t PS4Device::get_report_cb(uint8_t itf, uint8_t report_id,
                                  hid_report_type_t report_type,
                                  uint8_t *buffer, uint16_t reqlen)
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

void PS4Device::set_report_cb(uint8_t itf, uint8_t report_id,
                              hid_report_type_t report_type,
                              uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
    // De momento ignoramos salida (sin rumble / leds)
}

bool PS4Device::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                       tusb_control_request_t const *request)
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
