#include <cstring>
#include <cstdlib>        // rand()
#include <cmath>
#include <cstdint>
#include <climits>
#include "pico/time.h"    // absolute_time, time_diff, etc.

#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"

// -----------------------------------------------------------------------------
// Helpers: Steam-style radial mapping for sticks (garantiza 100% en los extremos)
// -----------------------------------------------------------------------------
static inline int16_t clamp_int16_from_int32(int32_t v)
{
    if (v < static_cast<int32_t>(INT16_MIN)) return INT16_MIN;
    if (v > static_cast<int32_t>(INT16_MAX)) return INT16_MAX;
    return static_cast<int16_t>(v);
}

// Aplica deadzone radial + curva tipo "Steam" (potencia gamma) sobre la magnitud,
// preservando dirección. Entrada y salida en rango int16 (-32768..32767).
static inline void apply_stick_steam_radial_int16(int16_t in_x, int16_t in_y,
                                                  float deadzone_fraction, float gamma, float sensitivity,
                                                  int16_t &out_x, int16_t &out_y)
{
    constexpr float INT16_MAX_F = 32767.0f;

    // Normalizar a [-1..1]
    float vx = static_cast<float>(in_x) / INT16_MAX_F;
    float vy = static_cast<float>(in_y) / INT16_MAX_F;

    float mag = std::sqrt(vx*vx + vy*vy);

    // Dentro de deadzone → centro (0)
    if (mag <= deadzone_fraction || mag == 0.0f)
    {
        out_x = 0;
        out_y = 0;
        return;
    }

    if (mag > 1.0f) mag = 1.0f; // por si -32768 produce >1

    // Remap magnitud fuera de deadzone a [0..1]
    float adj = (mag - deadzone_fraction) / (1.0f - deadzone_fraction);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // Curva tipo "Steam"
    float out_frac = std::pow(adj, gamma);

    // Aplicar sensibilidad y clamp (sensitivity=1 → llega a 1 en adj==1)
    out_frac *= sensitivity;
    out_frac = std::fmax(0.0f, std::fmin(1.0f, out_frac));

    // Reconstruir componentes manteniendo dirección
    float scale = out_frac / mag; // mag>0
    float sx = vx * scale;
    float sy = vy * scale;

    // Clamp a [-1..1] por si acaso
    sx = std::fmax(-1.0f, std::fmin(1.0f, sx));
    sy = std::fmax(-1.0f, std::fmin(1.0f, sy));

    // Convertir a int16 utilizando INT16_MAX como escala positiva
    int32_t ix = static_cast<int32_t>(std::lround(sx * INT16_MAX_F));
    int32_t iy = static_cast<int32_t>(std::lround(sy * INT16_MAX_F));

    out_x = clamp_int16_from_int32(ix);
    out_y = clamp_int16_from_int32(iy);
}

// -----------------------------------------------------------------------------
// XInputDevice
// -----------------------------------------------------------------------------
void XInputDevice::initialize()
{
    class_driver_ = *tud_xinput::class_driver();
}

void XInputDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    (void)idx;

    // ---------- ESTADO ESTÁTICO PARA MACROS / TIEMPOS ----------
    static absolute_time_t shot_start_time;
    static bool            is_shooting = false;
    static bool            jump_macro_active = false;
    static absolute_time_t jump_end_time;
    static uint64_t        last_tap_time = 0;
    static bool            tap_state    = false;
    static uint16_t        touchpadMask = 0;
    static uint64_t        aim_last_time_ms = 0;
    static int16_t         aim_jitter_x = 0;
    static int16_t         aim_jitter_y = 0;

    if (gamepad.new_pad_in())
    {
        // Reinicia todo el reporte (el ctor pone report_size)
        in_report_ = XInput::InReport{};

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn   = gp_in.buttons;

        // =========================================================
        // 0. AUTO-DETECCIÓN DEL TOUCHPAD (primer bit desconocido)
        // =========================================================
        if (touchpadMask == 0)
        {
            uint16_t knownMask =
                Gamepad::BUTTON_A    |
                Gamepad::BUTTON_B    |
                Gamepad::BUTTON_X    |
                Gamepad::BUTTON_Y    |
                Gamepad::BUTTON_LB   |
                Gamepad::BUTTON_RB   |
                Gamepad::BUTTON_L3   |
                Gamepad::BUTTON_R3   |
                Gamepad::BUTTON_BACK |
                Gamepad::BUTTON_START|
                Gamepad::BUTTON_SYS  |
                Gamepad::BUTTON_MISC;

            uint16_t unknown = btn & ~knownMask;
            if (unknown)
            {
                for (uint8_t i = 0; i < 16; ++i)
                {
                    uint16_t bit = static_cast<uint16_t>(1u << i);
                    if (unknown & bit)
                    {
                        touchpadMask = bit; // este será nuestro TOUCHPAD
                        break;
                    }
                }
            }
        }

        // =========================================================
        // 1. HAIR TRIGGERS
        // =========================================================
        const bool trig_l_pressed = (gp_in.trigger_l != 0);
        const bool trig_r_pressed = (gp_in.trigger_r != 0);

        uint8_t final_trig_l = trig_l_pressed ? 255 : 0;
        uint8_t final_trig_r = trig_r_pressed ? 255 : 0;

        // =========================================================
        // 2. STICKS BASE EN ESPACIO "FINAL" (el que ve el juego)
        // =========================================================
        int16_t out_lx = gp_in.joystick_lx;
        int16_t out_ly = Range::invert(gp_in.joystick_ly);
        int16_t out_rx = gp_in.joystick_rx;
        int16_t out_ry = Range::invert(gp_in.joystick_ry);

        // clamp seguro: recibe int32_t, evita overflow antes de recortar a int16_t
        auto clamp_to_int16 = [](int32_t v) -> int16_t {
            if (v < static_cast<int32_t>(INT16_MIN)) return static_cast<int16_t>(INT16_MIN);
            if (v > static_cast<int32_t>(INT16_MAX)) return static_cast<int16_t>(INT16_MAX);
            return static_cast<int16_t>(v);
        };

        // =========================================================
        // 3. STICKY AIM (JITTER EN STICK IZQUIERDO SOLO CON R2)
        // =========================================================
        if (final_trig_r)   // solo cuando disparas con R2
        {
            uint64_t now_ms = to_ms_since_boot(get_absolute_time());

            if (now_ms - aim_last_time_ms > 35)
            {
                aim_last_time_ms = now_ms;

                const int32_t JITTER = 6000; // fuerza del aim assist (valor base)
                int32_t ax = (rand() % (2 * JITTER + 1)) - JITTER;
                int32_t ay = (rand() % (2 * JITTER + 1)) - JITTER;

                constexpr int SCALE_PCT = 95;
                ax = (ax * SCALE_PCT) / 100;
                ay = (ay * SCALE_PCT) / 100;

                aim_jitter_x = static_cast<int16_t>(ax);
                aim_jitter_y = static_cast<int16_t>(ay);
            }

            int32_t new_lx = static_cast<int32_t>(out_lx) + static_cast<int32_t>(aim_jitter_x);
            int32_t new_ly = static_cast<int32_t>(out_ly) + static_cast<int32_t>(aim_jitter_y);

            out_lx = clamp_to_int16(new_lx);
            out_ly = clamp_to_int16(new_ly);
        }
        else
        {
            aim_jitter_x = 0;
            aim_jitter_y = 0;
        }

        // =========================================================
        // 4. ANTI-RECOIL DINÁMICO (EJE Y DERECHO, CUANDO R2)
        // =========================================================
        if (final_trig_r)
        {
            if (!is_shooting)
            {
                is_shooting     = true;
                shot_start_time = get_absolute_time();
            }

            int64_t time_shooting_us = absolute_time_diff_us(
                shot_start_time,
                get_absolute_time()
            );

            const int32_t RECOIL_STRONG = 4000; // primer segundo
            const int32_t RECOIL_WEAK   = 3600; // después
            int32_t recoil_force = (time_shooting_us < 1000000)
                                 ? RECOIL_STRONG
                                 : RECOIL_WEAK;

            constexpr int SCALE_PCT = 99;
            recoil_force = (recoil_force * SCALE_PCT) / 100;

            int32_t new_ry = static_cast<int32_t>(out_ry) - recoil_force;
            out_ry = clamp_to_int16(new_ry);
        }
        else
        {
            is_shooting = false;
        }

        // =========================================================
        // 5. DPAD
        // =========================================================
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_UP;
                break;
            case Gamepad::DPAD_DOWN:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_DOWN;
                break;
            case Gamepad::DPAD_LEFT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_LEFT;
                break;
            case Gamepad::DPAD_RIGHT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_RIGHT;
                break;
            case Gamepad::DPAD_UP_LEFT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_UP | XInput::Buttons0::DPAD_LEFT;
                break;
            case Gamepad::DPAD_UP_RIGHT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_UP | XInput::Buttons0::DPAD_RIGHT;
                break;
            case Gamepad::DPAD_DOWN_LEFT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_DOWN | XInput::Buttons0::DPAD_LEFT;
                break;
            case Gamepad::DPAD_DOWN_RIGHT:
                in_report_.buttons[0] |= XInput::Buttons0::DPAD_DOWN | XInput::Buttons0::DPAD_RIGHT;
                break;
            default:
                break;
        }

        // =========================================================
        // 6. BOTONES BÁSICOS + REMAPS
        // =========================================================
        if (btn & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;
        if (btn & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_Y)     in_report_.buttons[1] |= XInput::Buttons1::Y;
        if (btn & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;
        if (btn & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;
        if (btn & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;
        if (btn & Gamepad::BUTTON_SYS)
        {
            in_report_.buttons[1] |= XInput::Buttons1::HOME;
            in_report_.buttons[0] |= (XInput::Buttons0::DPAD_LEFT |
                                      XInput::Buttons0::DPAD_RIGHT);
        }
        if (btn & Gamepad::BUTTON_BACK)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
        }
        if (touchpadMask && (btn & touchpadMask))
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // =========================================================
        // 7. DROP SHOT (R2 sin L2) -> B
        // =========================================================
        if (final_trig_r && !final_trig_l)
        {
            in_report_.buttons[1] |= XInput::Buttons1::B;
        }

        // =========================================================
        // 8. MACRO L1 (LB físico) -> SPAM JUMP (A)
        // =========================================================
        if (btn & Gamepad::BUTTON_LB)
        {
            jump_macro_active = true;
            jump_end_time     = make_timeout_time_ms(1000); // 1 s después de soltar
        }

        if (jump_macro_active)
        {
            uint64_t now = to_ms_since_boot(get_absolute_time());

            if (now - last_tap_time > 50)
            {
                tap_state     = !tap_state;
                last_tap_time = now;
            }

            if (tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }

            if (!(btn & Gamepad::BUTTON_LB) && time_reached(jump_end_time))
            {
                jump_macro_active = false;
                tap_state         = false;
            }
        }

        // =========================================================
        // 9. ANÁLOGOS: aplicar mapeo RADIAL Steam-style antes de remitirse
        //    LEFT: "Ancho"  -> gamma = 1.8 (más resolución cerca del centro)
        //    RIGHT: "Relajado" -> gamma = 1.7 (más despacio, control pro)
        //    Ambos usan sensitivity = 0.8 para más control fino.
        // =========================================================
        constexpr float left_deadzone   = 0.005f;  // 0.5%
        constexpr float right_deadzone  = 0.005f;  // 0.5%
        constexpr float left_gamma      = 1.8f;    // movimiento
        constexpr float right_gamma     = 1.7f;    // apuntado preciso
        constexpr float both_sensitivity = 0.8f;   // menos sensible

        int16_t mapped_lx = 0;
        int16_t mapped_ly = 0;
        int16_t mapped_rx = 0;
        int16_t mapped_ry = 0;

        apply_stick_steam_radial_int16(out_lx, out_ly,
                                       left_deadzone, left_gamma, both_sensitivity,
                                       mapped_lx, mapped_ly);

        apply_stick_steam_radial_int16(out_rx, out_ry,
                                       right_deadzone, right_gamma, both_sensitivity,
                                       mapped_rx, mapped_ry);

        out_lx = mapped_lx;
        out_ly = mapped_ly;
        out_rx = mapped_rx;
        out_ry = mapped_ry;

        // =========================================================
        // 10. ASIGNAR TRIGGERS Y STICKS FINALES
        //     Antes de escribir los ejes finales, limitamos todos
        //     los ejes para que no superen el 97% del rango absoluto.
        // =========================================================
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;

        constexpr int32_t LIM97 = (static_cast<int32_t>(INT16_MAX) * 97) / 100;
        auto clamp97_to_int16 = [LIM97](int32_t v) -> int16_t {
            if (v < -LIM97) return static_cast<int16_t>(-LIM97);
            if (v >  LIM97) return static_cast<int16_t>( LIM97);
            return static_cast<int16_t>(v);
        };

        in_report_.joystick_lx = clamp97_to_int16(static_cast<int32_t>(out_lx));
        in_report_.joystick_ly = clamp97_to_int16(static_cast<int32_t>(out_ly));
        in_report_.joystick_rx = clamp97_to_int16(static_cast<int32_t>(out_rx));
        in_report_.joystick_ry = clamp97_to_int16(static_cast<int32_t>(out_ry));

        // =========================================================
        // 11. ENVIAR REPORTE XINPUT
        // =========================================================
        if (tud_suspended())
        {
            tud_remote_wakeup();
        }

        tud_xinput::send_report(reinterpret_cast<uint8_t*>(&in_report_),
                                sizeof(XInput::InReport));
    }

    // =============================================================
    // 12. RUMBLE (igual que el original)
    // =============================================================
    if (tud_xinput::receive_report(reinterpret_cast<uint8_t*>(&out_report_),
                                   sizeof(XInput::OutReport)) &&
        out_report_.report_id == XInput::OutReportID::RUMBLE)
    {
        Gamepad::PadOut gp_out;
        gp_out.rumble_l = out_report_.rumble_l;
        gp_out.rumble_r = out_report_.rumble_r;
        gamepad.set_pad_out(gp_out);
    }
}

uint16_t XInputDevice::get_report_cb(uint8_t itf,
                                     uint8_t report_id,
                                     hid_report_type_t report_type,
                                     uint8_t *buffer,
                                     uint16_t reqlen)
{
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)reqlen;

    std::memcpy(buffer, &in_report_, sizeof(XInput::InReport));
    return sizeof(XInput::InReport);
}

void XInputDevice::set_report_cb(uint8_t itf,
                                 uint8_t report_id,
                                 hid_report_type_t report_type,
                                 uint8_t const *buffer,
                                 uint16_t bufsize)
{
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

bool XInputDevice::vendor_control_xfer_cb(uint8_t rhport,
                                          uint8_t stage,
                                          tusb_control_request_t const *request)
{
    (void)rhport;
    (void)stage;
    (void)request;
    return false;
}

const uint16_t * XInputDevice::get_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    const char *value = reinterpret_cast<const char*>(XInput::DESC_STRING[index]);
    return get_string_descriptor(value, index);
}

const uint8_t * XInputDevice::get_descriptor_device_cb()
{
    return XInput::DESC_DEVICE;
}

const uint8_t * XInputDevice::get_hid_descriptor_report_cb(uint8_t itf)
{
    (void)itf;
    return nullptr;
}

const uint8_t * XInputDevice::get_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return XInput::DESC_CONFIGURATION;
}

const uint8_t * XInputDevice::get_descriptor_device_qualifier_cb()
{
    return nullptr;
}
