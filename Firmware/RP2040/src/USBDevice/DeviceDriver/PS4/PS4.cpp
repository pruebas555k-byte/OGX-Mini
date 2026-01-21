#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "USBDevice/DeviceDriver/PS4/PS4.h"

// Helper: curvas de sensibilidad para sticks
// - Para regates (left stick) usamos un exponente < 1 para aumentar la respuesta en pequeñas entradas.
// - Para tiro de precisión (right stick) usamos un exponente > 1 para suavizar cerca del centro.
//
// Mapping: int16_t in [-32768,32767] -> float v in [-1,1] -> aplicar deadzone y curva -> uint8_t [0..255]
// Centro exacto se sitúa en 128.
static inline uint8_t apply_stick_curve(int16_t in, float exponent, float deadzone_fraction)
{
    constexpr float INT16_MAX_F = 32767.0f;

    // Normalizamos a [-1, 1]
    float v = static_cast<float>(in) / INT16_MAX_F;

    float abs_v = std::fabs(v);
    if (abs_v <= deadzone_fraction)
    {
        // Centro
        return 128;
    }

    // Remapear el valor fuera de la deadzone a [0..1]
    float adj = (abs_v - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // Aplicar la curva de sensibilidad
    float curved = std::pow(adj, exponent);

    // Restaurar signo y mapear a uint8 0..255 (centro = 128)
    float signed_curved = (v < 0.0f) ? -curved : curved;
    float mapped = signed_curved * 127.0f + 128.0f;

    // Clamp y retorno
    int out = static_cast<int>(std::round(mapped));
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return static_cast<uint8_t>(out);
}

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

    // ---- Estado de la macro MUTE (cuadrado + círculos) ----
    static bool     mutePrev          = false;
    static uint32_t muteMacroTicks    = 0;
    // Suponiendo que process() se llama aprox. cada 1 ms.
    static constexpr uint32_t MUTE_MACRO_DURATION_TICKS = 485;

    // ---- Nueva macro PS -> R1 + L2 + Triangle + X (400 ms) ----
    static bool     psPrev            = false;
    static uint32_t psMacroTicks      = 0;
    static constexpr uint32_t PS_MACRO_DURATION_TICKS = 400; // 400 ms

    Gamepad::PadIn gp_in = gamepad.get_pad_in();
    const uint16_t btn   = gp_in.buttons;

    const bool sharePressed = (btn & Gamepad::BUTTON_BACK)  != 0;  // SHARE
    const bool mutePressed  = (btn & Gamepad::BUTTON_MISC)  != 0;  // usamos MISC como MUTE
    const bool psPressed    = (btn & Gamepad::BUTTON_SYS)   != 0;  // PS button

    // Flanco de subida de MUTE → arranca macro 3.5 s
    if (mutePressed && !mutePrev)
    {
        muteMacroTicks = MUTE_MACRO_DURATION_TICKS;
    }
    mutePrev = mutePressed;

    // Flanco de subida de PS → arranca macro PS (400 ms)
    if (psPressed && !psPrev)
    {
        psMacroTicks = PS_MACRO_DURATION_TICKS;
    }
    psPrev = psPressed;

    const bool macroActive = (muteMacroTicks > 0);
    if (muteMacroTicks > 0)
        --muteMacroTicks;

    const bool psMacroActive = (psMacroTicks > 0);
    if (psMacroTicks > 0)
        --psMacroTicks;

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

    // ------------------ Sticks analógicos (0-255) con curvas mejoradas ---------------
    // Parámetros ajustables:
    // - left: diseñado para regates -> más reactivo en pequeñas entradas (exponente < 1)
    // - right: diseñado para precisión en tiros -> más fino alrededor del centro (exponente > 1)
    //
    // Si quieres cambiar comportamiento:
    //  - ajustar left_exponent (p. ej. 0.7..1.0)
    //  - ajustar right_exponent (p. ej. 1.0..2.5)
    //  - ajustar deadzones (fracciones de la máxima entrada)
    constexpr float left_exponent  = 0.85f;  // <1 => amplifica pequeñas entradas (mejor regates)
    constexpr float right_exponent = 1.8f;   // >1 => suaviza cerca del centro (mejor precisión)
    constexpr float left_deadzone  = 0.03f;  // ~ 0.03 * 32767 ≈ 980
    constexpr float right_deadzone = 0.02f;

    report_in_.leftStickX  = apply_stick_curve(gp_in.joystick_lx, left_exponent,  left_deadzone);
    report_in_.leftStickY  = apply_stick_curve(gp_in.joystick_ly, left_exponent,  left_deadzone);
    report_in_.rightStickX = apply_stick_curve(gp_in.joystick_rx, right_exponent, right_deadzone);
    report_in_.rightStickY = apply_stick_curve(gp_in.joystick_ry, right_exponent, right_deadzone);

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

    // NOTA: no hay remapeo que mueva el stick izquierdo al pulsar Square.
    report_in_.buttonWest  = squareFinal ? 1 : 0;               // Square
    report_in_.buttonEast  = circleFinal ? 1 : 0;               // Circle
    report_in_.buttonSouth = (btn & Gamepad::BUTTON_A) ? 1 : 0; // Cross (X)
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

    // ------------------ Sobrescribir por la macro PS (si está activa) --------------
    // La macro fuerza R1 + L2 + Triangle + X durante PS_MACRO_DURATION_TICKS ms.
    if (psMacroActive)
    {
        // Forzamos R1 (botón digital) y L2 (botón digital)
        report_in_.buttonR1 = 1; // R1
        report_in_.buttonL2 = 1; // L2

        // Forzamos Triangle (North) y X (South)
        report_in_.buttonNorth = 1; // Triangle
        report_in_.buttonSouth = 1; // X (Cross)

        // Forzamos eje de gatillo izquierdo a máximo para L2 (si el juego espera eje)
        report_in_.leftTrigger  = 0xFF;

        // NO forzamos rightTrigger ni buttonR2 (R1 es un hombro digital en este caso)
    }

    // ------------------ Sticks pulsados ------------------
    report_in_.buttonL3 = (btn & Gamepad::BUTTON_L3) ? 1 : 0;
    report_in_.buttonR3 = (btn & Gamepad::BUTTON_R3) ? 1 : 0;

    // ------------------ Centrales ------------------
    // SHARE normal
    report_in_.buttonSelect = sharePressed ? 1 : 0;
    // OPTIONS
    report_in_.buttonStart  = (btn & Gamepad::BUTTON_START) ? 1 : 0;
    // PS
    report_in_.buttonHome   = psPressed ? 1 : 0;
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
