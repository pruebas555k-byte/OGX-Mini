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

    // ---- Estado de la macro MUTE (cuadrado + círculo 3.5 s) ----
    static bool     mutePrev          = false;
    static uint32_t muteMacroTicks    = 0;
    // Suponiendo que process() se llama aprox. cada 1 ms.
    static constexpr uint32_t MUTE_MACRO_DURATION_TICKS = 3500;

    Gamepad::PadIn gp_in = gamepad.get_pad_in();
    const uint16_t btn   = gp_in.buttons;

    const bool sharePressed = (btn & Gamepad::BUTTON_BACK)  != 0;  // SHARE
    const bool mutePressed  = (btn & Gamepad::BUTTON_MISC)  != 0;  // usamos MISC como MUTE

    // Flanco de subida de MUTE → arranca macro 3.5 s
    if (mutePressed && !mutePrev)
    {
        muteMacroTicks = MUTE_MACRO_DURATION_TICKS;
    }
    mutePrev = mutePressed;

    const bool macroActive = (muteMacroTicks > 0);
    if (muteMacroTicks > 0)
        --muteMacroTicks;

    // ----------------------------------------------------------------
    // Construimos SIEMPRE el reporte desde cero
    // ----------------------------------------------------------------
    std::memset(&report_in_, 0, sizeof(report_in_));

    // Report ID 1 (coincide con 0x85,0x01 del descriptor HID)
    report_in_.reportID = 0x01;

    // Touchpad: sin dedos para que no salga el punto azul fijo
    report_in_.gamepad.touchpadActive = 0;
    report_in_.gamepad.touchpadData.p1.unpressed = 1;
    report_in_.gamepad.touchpadData.p2.unpressed = 1;

    // Sticks analógicos (0-255)
    report_in_.leftStickX  = Scale::int16_to_uint8(gp_in.joystick_lx);
    report_in_.leftStickY  = Scale::int16_to_uint8(gp_in.joystick_ly);
    report_in_.rightStickX = Scale::int16_to_uint8(gp_in.joystick_rx);
    report_in_.rightStickY = Scale::int16_to_uint8(gp_in.joystick_ry);

    // ------------------ D-Pad → HAT ------------------
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

    // ------------------ Face buttons + MACRO ------------------
    // Base (sin macro)
    const bool baseSquare = (btn & Gamepad::BUTTON_X) != 0;  // Square
    const bool baseCircle = (btn & Gamepad::BUTTON_B) != 0;  // Circle

    // Macro: MUTE mantiene Square + Circle durante ~3.5 s
    const bool squareFinal = baseSquare || macroActive;
    const bool circleFinal = baseCircle || macroActive;

    report_in_.buttonWest  = squareFinal ? 1 : 0;               // Square
    report_in_.buttonEast  = circleFinal ? 1 : 0;               // Circle
    report_in_.buttonSouth = (btn & Gamepad::BUTTON_A) ? 1 : 0; // Cross
    report_in_.buttonNorth = (btn & Gamepad::BUTTON_Y) ? 1 : 0; // Triangle

    // ------------------ Hombros / Triggers (REMAPPING) ------------------
    const bool physL1 = (btn & Gamepad::BUTTON_LB) != 0; // L1 físico
    const bool physR1 = (btn & Gamepad::BUTTON_RB) != 0; // R1 físico
    const bool physL2 = gp_in.trigger_l;                 // L2 físico (digital)
    const bool physR2 = gp_in.trigger_r;                 // R2 físico (digital)

    // Queremos:
    //  - R1 físico  → R2 (botón + eje)
    //  - R2 físico  → L2 (botón + eje)
    //  - L2 físico  → R1 (solo botón, sin eje trigger)
    bool   virtL1 = physL1;
    bool   virtR1 = false;
    bool   virtL2 = false;
    bool   virtR2 = false;
    uint8_t trigL = 0;
    uint8_t trigR = 0;

    // R1 físico => R2 virtual (botón + trigger derecho)
    if (physR1)
    {
        virtR2 = true;
        trigR  = 0xFF;
    }

    // R2 físico => L2 virtual (botón + trigger izquierdo)
    if (physR2)
    {
        virtL2 = true;
        trigL  = 0xFF;
    }

    // L2 físico => R1 virtual (solo botón, SIN eje)
    if (physL2)
    {
        virtR1 = true;
        // NO tocamos trigL / trigR aquí → así no aprieta R2
    }

    report_in_.buttonL1 = virtL1 ? 1 : 0;
    report_in_.buttonR1 = virtR1 ? 1 : 0;
    report_in_.buttonL2 = virtL2 ? 1 : 0;
    report_in_.buttonR2 = virtR2 ? 1 : 0;

    report_in_.leftTrigger  = trigL;
    report_in_.rightTrigger = trigR;

    // ------------------ Sticks pulsados ------------------
    report_in_.buttonL3 = (btn & Gamepad::BUTTON_L3) ? 1 : 0;
    report_in_.buttonR3 = (btn & Gamepad::BUTTON_R3) ? 1 : 0;

    // ------------------ Centrales ------------------
    // SHARE normal
    report_in_.buttonSelect = sharePressed ? 1 : 0;
    // OPTIONS
    report_in_.buttonStart  = (btn & Gamepad::BUTTON_START) ? 1 : 0;
    // PS
    report_in_.buttonHome   = (btn & Gamepad::BUTTON_SYS) ? 1 : 0;
    // TOUCHPAD click: lo hace SHARE (y solo SHARE)
    report_in_.buttonTouchpad = sharePressed ? 1 : 0;

    // ----------------------------------------------------------------
    // Enviar el reporte HID
    // ----------------------------------------------------------------
    if (tud_suspended())
    {
        tud_remote_wakeup();
    }

    if (tud_hid_ready())
    {
        tud_hid_report(
            0, // TinyUSB no añade ID; el buffer ya empieza en reportID = 1
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
