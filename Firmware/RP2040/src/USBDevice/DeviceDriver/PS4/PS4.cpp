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

        // Resetea todo el reporte a estado neutro (constructor de InReport)
        report_in_ = PS4Dev::InReport{};

        // --------------------------------------------------------------------
        // DPAD -> Hat  (usa InReport::setHat(), ya no report_in_.hat)
        // --------------------------------------------------------------------
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:
                report_in_.setHat(PS4Dev::Hat::UP);
                break;

            case Gamepad::DPAD_UP_RIGHT:
                report_in_.setHat(PS4Dev::Hat::UP_RIGHT);
                break;

            case Gamepad::DPAD_RIGHT:
                report_in_.setHat(PS4Dev::Hat::RIGHT);
                break;

            case Gamepad::DPAD_DOWN_RIGHT:
                report_in_.setHat(PS4Dev::Hat::DOWN_RIGHT);
                break;

            case Gamepad::DPAD_DOWN:
                report_in_.setHat(PS4Dev::Hat::DOWN);
                break;

            case Gamepad::DPAD_DOWN_LEFT:
                report_in_.setHat(PS4Dev::Hat::DOWN_LEFT);
                break;

            case Gamepad::DPAD_LEFT:
                report_in_.setHat(PS4Dev::Hat::LEFT);
                break;

            case Gamepad::DPAD_UP_LEFT:
                report_in_.setHat(PS4Dev::Hat::UP_LEFT);
                break;

            default:
                report_in_.setHat(PS4Dev::Hat::CENTER);
                break;
        }

        // --------------------------------------------------------------------
        // BOTONES
        // --------------------------------------------------------------------
        uint16_t buttons = 0;

        // Face buttons
        if (gp_in.buttons & Gamepad::BUTTON_A)
            buttons |= PS4Dev::Buttons::CROSS;      // A -> X (Cruz)
        if (gp_in.buttons & Gamepad::BUTTON_B)
            buttons |= PS4Dev::Buttons::CIRCLE;     // B -> Círculo
        if (gp_in.buttons & Gamepad::BUTTON_X)
            buttons |= PS4Dev::Buttons::SQUARE;     // X -> Cuadrado
        if (gp_in.buttons & Gamepad::BUTTON_Y)
            buttons |= PS4Dev::Buttons::TRIANGLE;   // Y -> Triángulo

        // Hombros / sticks
        if (gp_in.buttons & Gamepad::BUTTON_LB)
            buttons |= PS4Dev::Buttons::L1;
        if (gp_in.buttons & Gamepad::BUTTON_RB)
            buttons |= PS4Dev::Buttons::R1;
        if (gp_in.buttons & Gamepad::BUTTON_L3)
            buttons |= PS4Dev::Buttons::L3;
        if (gp_in.buttons & Gamepad::BUTTON_R3)
            buttons |= PS4Dev::Buttons::R3;

        // Centrales
        if (gp_in.buttons & Gamepad::BUTTON_BACK)
            buttons |= PS4Dev::Buttons::SHARE;
        if (gp_in.buttons & Gamepad::BUTTON_START)
            buttons |= PS4Dev::Buttons::OPTIONS;
        if (gp_in.buttons & Gamepad::BUTTON_SYS)
            buttons |= PS4Dev::Buttons::PS;
        // El "misc" lo usamos como TOUCHPAD
        if (gp_in.buttons & Gamepad::BUTTON_MISC)
            buttons |= PS4Dev::Buttons::TOUCHPAD;

        // --------------------------------------------------------------------
        // TRIGGERS: digital + analógico
        //  - L2_DIG / R2_DIG = botón
        //  - trigger_l / trigger_r = eje 0–255
        // --------------------------------------------------------------------
        if (gp_in.trigger_l)
        {
            buttons |= PS4Dev::Buttons::L2_DIG;
            report_in_.trigger_l = 0xFF;
        }
        else
        {
            report_in_.trigger_l = 0x00;
        }

        if (gp_in.trigger_r)
        {
            buttons |= PS4Dev::Buttons::R2_DIG;
            report_in_.trigger_r = 0xFF;
        }
        else
        {
            report_in_.trigger_r = 0x00;
        }

        // --------------------------------------------------------------------
        // STICKS (0–255)
        // --------------------------------------------------------------------
        report_in_.joystick_lx = Scale::int16_to_uint8(gp_in.joystick_lx);
        report_in_.joystick_ly = Scale::int16_to_uint8(gp_in.joystick_ly);
        report_in_.joystick_rx = Scale::int16_to_uint8(gp_in.joystick_rx);
        report_in_.joystick_ry = Scale::int16_to_uint8(gp_in.joystick_ry);

        // Aplica el bitfield de botones con el helper del InReport
        report_in_.setButtons(buttons);
    }

    // ------------------------------------------------------------------------
    // Enviar el reporte HID
    // ------------------------------------------------------------------------
    if (tud_suspended())
    {
        tud_remote_wakeup();
    }

    if (tud_hid_ready())
    {
        // IMPORTANTE:
        // - Nuestro InReport YA incluye el report_id como primer byte.
        // - Por eso aquí usamos report_id = 0, para que TinyUSB no inserte otro.
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
