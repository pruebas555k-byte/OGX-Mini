#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "pico/time.h" // make_timeout_time_ms, time_reached

#include "USBDevice/DeviceDriver/PS4/PS4.h"

// Helper: curvas / mapeos de sensibilidad para sticks

static inline uint8_t map_signed_to_uint8(float signed_val)
{
    // signed_val en [-1, 1], centro = 0 -> 128
    float mapped = signed_val * 127.0f + 128.0f;
    int out = static_cast<int>(std::round(mapped));
    if (out < 0) out = 0;
    if (out > 255) out = 255;
    return static_cast<uint8_t>(out);
}

static inline uint8_t apply_stick_linear(int16_t in, float sensitivity, float deadzone_fraction)
{
    constexpr float INT16_MAX_F = 32767.0f;
    float v = static_cast<float>(in) / INT16_MAX_F; // [-1,1]
    float abs_v = std::fabs(v);
    if (abs_v <= deadzone_fraction)
    {
        return 128;
    }

    // Remapear fuera de deadzone a [0..1]
    float adj = (abs_v - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // Aplicar sensibilidad (factor linear)
    float scaled = adj * sensitivity;
    if (scaled > 1.0f) scaled = 1.0f;

    float signed_scaled = (v < 0.0f) ? -scaled : scaled;
    return map_signed_to_uint8(signed_scaled);
}

static inline uint8_t apply_stick_custom_curve(int16_t in,
                                               float mid_in_frac, float mid_out_frac,
                                               float deadzone_fraction)
{
    // mid_in_frac, mid_out_frac en [0..1]
    constexpr float INT16_MAX_F = 32767.0f;
    float v = static_cast<float>(in) / INT16_MAX_F; // [-1,1]
    float abs_v = std::fabs(v);
    if (abs_v <= deadzone_fraction)
    {
        return 128;
    }

    // Remapear fuera de deadzone a [0..1]
    float adj = (abs_v - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    float out_frac;
    // Piecewise linear between (0,0), (mid_in, mid_out), (1,1)
    if (adj <= mid_in_frac)
    {
        if (mid_in_frac <= 0.0f) out_frac = mid_out_frac; // evita división por 0
        else out_frac = (mid_out_frac / mid_in_frac) * adj;
    }
    else
    {
        // tramo (mid_in_frac..1) -> (mid_out_frac..1)
        float denom = (1.0f - mid_in_frac);
        if (denom <= 0.0f)
            out_frac = 1.0f;
        else
            out_frac = mid_out_frac + ((1.0f - mid_out_frac) / denom) * (adj - mid_in_frac);
    }

    // Clamp por si acaso
    out_frac = std::fmax(0.0f, std::fmin(1.0f, out_frac));

    float signed_out = (v < 0.0f) ? -out_frac : out_frac;
    return map_signed_to_uint8(signed_out);
}

// Nueva: función Steam-style usando potencia (gamma) — por eje (no radial)
static inline uint8_t apply_stick_steam_style(int16_t in, float deadzone_fraction,
                                              float gamma, float sensitivity = 1.0f)
{
    constexpr float INT16_MAX_F = 32767.0f;
    float v = static_cast<float>(in) / INT16_MAX_F; // [-1,1]
    float abs_v = std::fabs(v);
    if (abs_v <= deadzone_fraction) return 128;

    // Remapear fuera de deadzone a [0..1]
    float adj = (abs_v - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // Curva Steam-style: potencia gamma
    float out_frac = std::pow(adj, gamma);

    // Aplicar sensibilidad y clamp
    out_frac *= sensitivity;
    out_frac = std::fmax(0.0f, std::fmin(1.0f, out_frac));

    float signed_out = (v < 0.0f) ? -out_frac : out_frac;
    return map_signed_to_uint8(signed_out);
}

// Nueva: versión RADIAL (circular) — remapea magnitud y preserva dirección.
// input: in_x, in_y (int16), deadzone_fraction sobre la MAGNITUD, gamma sobre la MAGNITUD.
// output: out_x, out_y (uint8) - ya mapeados a 0..255 con centro 128.
static inline void apply_stick_steam_radial(int16_t in_x, int16_t in_y,
                                            float deadzone_fraction, float gamma, float sensitivity,
                                            uint8_t &out_x, uint8_t &out_y)
{
    constexpr float INT16_MAX_F = 32767.0f;
    // Umbral para snap más permisivo (ajustable); también aplicamos axis-snap.
    constexpr float SNAP_TO_EDGE_THRESHOLD = 0.98f;
    constexpr float AXIS_SNAP_MIN_OTHER_AXIS = 0.05f; // si el otro eje está por debajo de esto
    constexpr float AXIS_SNAP_MAIN_AXIS = 0.95f;      // y el eje principal >= esto -> snap

    float vx = static_cast<float>(in_x) / INT16_MAX_F; // [-1..1]
    float vy = static_cast<float>(in_y) / INT16_MAX_F; // [-1..1]

    float mag = std::sqrt(vx*vx + vy*vy);

    if (mag <= deadzone_fraction || mag == 0.0f)
    {
        out_x = 128;
        out_y = 128;
        return;
    }

    if (mag > 1.0f) mag = 1.0f;

    // AXIS SNAP: si un eje está casi 0 y el otro está cerca del tope, forzamos ese eje a ±1 exacto.
    if (std::fabs(vx) <= AXIS_SNAP_MIN_OTHER_AXIS && std::fabs(vy) >= AXIS_SNAP_MAIN_AXIS)
    {
        float sy = (vy < 0.0f) ? -1.0f : 1.0f;
        out_x = map_signed_to_uint8(0.0f);
        out_y = map_signed_to_uint8(sy);
        return;
    }
    if (std::fabs(vy) <= AXIS_SNAP_MIN_OTHER_AXIS && std::fabs(vx) >= AXIS_SNAP_MAIN_AXIS)
    {
        float sx = (vx < 0.0f) ? -1.0f : 1.0f;
        out_x = map_signed_to_uint8(sx);
        out_y = map_signed_to_uint8(0.0f);
        return;
    }

    // Magnitude snap: si estamos muy cerca del borde, forzamos vector unitario
    if (mag >= SNAP_TO_EDGE_THRESHOLD)
    {
        float ux = vx / mag;
        float uy = vy / mag;
        out_x = map_signed_to_uint8(ux);
        out_y = map_signed_to_uint8(uy);
        return;
    }

    // Remapear magnitud fuera de deadzone a [0..1]
    float adj = (mag - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // Curve: potencia gamma sobre la magnitud
    float out_frac = std::pow(adj, gamma);

    // Aplicar sensibilidad y clamp
    out_frac *= sensitivity;
    out_frac = std::fmax(0.0f, std::fmin(1.0f, out_frac));

    // Reconstruir componentes manteniendo dirección:
    float scale = out_frac / mag;
    float sx = vx * scale;
    float sy = vy * scale;

    // Clamp just in case
    sx = std::fmax(-1.0f, std::fmin(1.0f, sx));
    sy = std::fmax(-1.0f, std::fmin(1.0f, sy));

    out_x = map_signed_to_uint8(sx);
    out_y = map_signed_to_uint8(sy);
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
    static absolute_time_t muteEndTime; // time-based end
    static bool     muteActive        = false;
    static constexpr uint32_t MUTE_MACRO_DURATION_MS = 483;

    // ---- Nueva macro PS -> R1 + L2 + Triangle (time-based) ----
    static bool     psPrev            = false;
    static absolute_time_t psEndTime; // time-based end for PS macro
    static bool     psActive         = false;
    static constexpr uint32_t PS_MACRO_DURATION_MS = 350; // 350 ms

    Gamepad::PadIn gp_in = gamepad.get_pad_in();
    const uint16_t btn   = gp_in.buttons;

    const bool sharePressed = (btn & Gamepad::BUTTON_BACK)  != 0;  // SHARE
    const bool mutePressed  = (btn & Gamepad::BUTTON_MISC)  != 0;  // usamos MISC como MUTE
    const bool psPressed    = (btn & Gamepad::BUTTON_SYS)   != 0;  // PS button

    // Flanco de subida de MUTE → arranca macro con tiempo absoluto (483 ms)
    if (mutePressed && !mutePrev)
    {
        muteActive = true;
        muteEndTime = make_timeout_time_ms(MUTE_MACRO_DURATION_MS);
    }
    mutePrev = mutePressed;

    // Flanco de subida de PS → arranca macro PS (350 ms) -> time-based
    if (psPressed && !psPrev)
    {
        psActive = true;
        psEndTime = make_timeout_time_ms(PS_MACRO_DURATION_MS);
    }
    psPrev = psPressed;

    // Actualizar estados por tiempo
    if (muteActive && time_reached(muteEndTime))
    {
        muteActive = false;
    }
    if (psActive && time_reached(psEndTime))
    {
        psActive = false;
    }

    const bool macroActive = muteActive; // MUTE macro
    const bool psMacroActive = psActive; // PS macro (time-based)

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

    // ------------------ Sticks analógicos (0-255) con ajustes solicitados ---------------
    // LEFT: "Ancho" -> gamma = 1.8
    // RIGHT: "Relajado" -> gamma = 1.3
    constexpr float left_deadzone   = 0.03f;  // 3% (radial)
    constexpr float right_deadzone  = 0.02f;  // 2% (radial)
    constexpr float left_gamma      = 1.8f;   // "Ancho"
    constexpr float right_gamma     = 1.3f;   // "Relajado"
    constexpr float both_sensitivity = 1.0f;  // 1.0 para que lleguen al 100% si mag==1

    apply_stick_steam_radial(gp_in.joystick_lx, gp_in.joystick_ly,
                             left_deadzone, left_gamma, both_sensitivity,
                             report_in_.leftStickX, report_in_.leftStickY);

    apply_stick_steam_radial(gp_in.joystick_rx, gp_in.joystick_ry,
                             right_deadzone, right_gamma, both_sensitivity,
                             report_in_.rightStickX, report_in_.rightStickY);

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
    const bool baseSquare = (btn & Gamepad::BUTTON_X) != 0;  // Square
    const bool baseCircle = (btn & Gamepad::BUTTON_B) != 0;  // Circle

    const bool squareFinal = baseSquare || macroActive;
    const bool circleFinal = baseCircle || macroActive;

    report_in_.buttonWest  = squareFinal ? 1 : 0;               // Square
    report_in_.buttonEast  = circleFinal ? 1 : 0;               // Circle
    report_in_.buttonSouth = (btn & Gamepad::BUTTON_A) ? 1 : 0; // Cross (X)
    report_in_.buttonNorth = (btn & Gamepad::BUTTON_Y) ? 1 : 0; // Triangle

    // ------------------ Hombros / Triggers (REMAPPING) ------------------
    const bool physL1 = (btn & Gamepad::BUTTON_LB) != 0; // L1 físico
    const bool physR1 = (btn & Gamepad::BUTTON_RB) != 0; // R1 físico
    const bool physL2 = gp_in.trigger_l;                 // L2 físico (digital)
    const bool physR2 = gp_in.trigger_r;                 // R2 físico (digital)

    bool   virtL1 = physL1;
    bool   virtR1 = false;
    bool   virtL2 = false;
    bool   virtR2 = false;
    uint8_t trigL = 0;
    uint8_t trigR = 0;

    if (physR1)
    {
        virtR2 = true;
        trigR  = 0xFF;
    }

    if (physR2)
    {
        virtL2 = true;
        trigL  = 0xFF;
    }

    if (physL2)
    {
        virtR1 = true;
    }

    report_in_.buttonL1 = virtL1 ? 1 : 0;
    report_in_.buttonR1 = virtR1 ? 1 : 0;
    report_in_.buttonL2 = virtL2 ? 1 : 0;
    report_in_.buttonR2 = virtR2 ? 1 : 0;

    report_in_.leftTrigger  = trigL;
    report_in_.rightTrigger = trigR;

    // ------------------ Sobrescribir por la macro PS (si está activa) --------------
    if (psMacroActive)
    {
        report_in_.buttonR1 = 1; // R1
        report_in_.buttonL2 = 1; // L2
        report_in_.buttonNorth = 1; // Triangle
        report_in_.leftTrigger  = 0xFF; // fuerza eje L2 al máximo
    }

    // ------------------ Sticks pulsados ------------------
    report_in_.buttonL3 = (btn & Gamepad::BUTTON_L3) ? 1 : 0;
    report_in_.buttonR3 = (btn & Gamepad::BUTTON_R3) ? 1 : 0;

    // ------------------ Centrales ------------------
    report_in_.buttonSelect = sharePressed ? 1 : 0;
    report_in_.buttonStart  = (btn & Gamepad::BUTTON_START) ? 1 : 0;
    report_in_.buttonHome   = psPressed ? 1 : 0;
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

// ... (rest of callbacks unchanged) ...
