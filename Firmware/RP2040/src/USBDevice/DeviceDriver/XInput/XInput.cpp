#include <cstring>
#include <cstdlib>        // rand()
#include "pico/time.h"    // absolute_time, time_diff, etc.

#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"

void XInputDevice::initialize()
{
    class_driver_ = *tud_xinput::class_driver();
}

void XInputDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    (void)idx;

    // ---------- ESTADOS ESTÁTICOS ----------
    // Anti-recoil
    static absolute_time_t shot_start_time;
    static bool            is_shooting = false;

    // Macro L1 (turbo X/A + 1s de cola)
    static bool            jump_macro_active = false;
    static uint64_t        jump_stop_time_ms = 0;
    static uint64_t        jump_last_tap_ms  = 0;
    static bool            jump_tap_state    = false;

    // Macro R2 (turbo X/A instante, bloqueable por L2)
    static bool     r2_macro_active  = false;
    static bool     r2_macro_blocked = false;
    static bool     r2_prev_pressed  = false;
    static uint64_t r2_last_tap_ms   = 0;
    static bool     r2_tap_state     = false;

    // Turbo triángulo (doble tap)
    static bool     tri_prev_pressed    = false;
    static bool     tri_turbo_active    = false;
    static uint64_t tri_last_press_ms   = 0;
    static uint64_t tri_last_tap_ms     = 0;
    static bool     tri_tap_state       = false;

    // Aim assist (stick izquierdo)
    static uint64_t aim_last_time_ms = 0;
    static int16_t  aim_jitter_x     = 0;
    static int16_t  aim_jitter_y     = 0;

    // Suavizado del stick derecho (para drift)
    static int16_t  smooth_rx = 0;
    static int16_t  smooth_ry = 0;

    if (gamepad.new_pad_in())
    {
        // Reinicia todo el reporte
        in_report_ = XInput::InReport{};

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn   = gp_in.buttons;

        uint64_t now_ms = to_ms_since_boot(get_absolute_time());

        // =========================================================
        // 1. TRIGGERS Y ESTADO R2 MACRO
        // =========================================================
        const bool trig_l_pressed = (gp_in.trigger_l != 0);
        const bool trig_r_pressed = (gp_in.trigger_r != 0);

        uint8_t final_trig_l = trig_l_pressed ? 255 : 0;
        uint8_t final_trig_r = trig_r_pressed ? 255 : 0;

        // R2 macro: solo se arma si R2 empieza sin L2.
        if (trig_r_pressed && !r2_prev_pressed)
        {
            if (trig_l_pressed)
            {
                // R2 empezó mientras L2 estaba apretado → bloqueamos macro.
                r2_macro_active  = false;
                r2_macro_blocked = true;
            }
            else
            {
                // R2 solo → macro ON
                r2_macro_blocked = false;
                r2_macro_active  = true;
                r2_tap_state     = true;
                r2_last_tap_ms   = now_ms;
            }
        }
        else if (!trig_r_pressed && r2_prev_pressed)
        {
            // R2 se suelta → reseteamos todo
            r2_macro_active  = false;
            r2_macro_blocked = false;
            r2_tap_state     = false;
        }
        r2_prev_pressed = trig_r_pressed;

        // =========================================================
        // 2. STICKS BASE (coordenadas finales que ve el juego)
        // =========================================================
        // Stick izquierdo sin cambios
        int16_t base_lx = gp_in.joystick_lx;
        int16_t base_ly = Range::invert(gp_in.joystick_ly);

        // Stick derecho: aplicamos deadzone + suavizado para reducir drift
        int16_t raw_rx = gp_in.joystick_rx;
        int16_t raw_ry = Range::invert(gp_in.joystick_ry);

        // Deadzone radial ~10 % para el stick derecho
        static const int16_t R_DEADZONE  = 3200; // ~10 % de 32767
        static const int32_t R_DEADZONE2 =
            (int32_t)R_DEADZONE * (int32_t)R_DEADZONE;

        int32_t magR2_raw =
            (int32_t)raw_rx * raw_rx +
            (int32_t)raw_ry * raw_ry;

        if (magR2_raw < R_DEADZONE2)
        {
            // Movimiento muy pequeño → lo tratamos como 0 (limpia drift suave)
            raw_rx = 0;
            raw_ry = 0;
        }

        // Suavizado: 75% valor anterior + 25% valor nuevo
        smooth_rx = (int16_t)(( (int32_t)smooth_rx * 3 + raw_rx ) / 4);
        smooth_ry = (int16_t)(( (int32_t)smooth_ry * 3 + raw_ry ) / 4);

        int16_t base_rx = smooth_rx;
        int16_t base_ry = smooth_ry;

        int16_t out_lx = base_lx;
        int16_t out_ly = base_ly;
        int16_t out_rx = base_rx;
        int16_t out_ry = base_ry;

        auto clamp16 = [](int16_t v) -> int16_t {
            if (v < -32768) return -32768;
            if (v >  32767) return  32767;
            return v;
        };

        // Magnitud del stick izquierdo (para límites de aim assist)
        int32_t magL2 =
            (int32_t)base_lx * base_lx +
            (int32_t)base_ly * base_ly;

        // =========================================================
        // 3. AIM ASSIST (STICK IZQUIERDO, SOLO CON R2)
        //    Rotacional fuerte pero controlado.
        //    Solo si el stick está dentro del 80% del recorrido.
        // =========================================================
        {
            static const int16_t AIM_CENTER_MAX  = 26000;  // ~80 %
            static const int32_t AIM_CENTER_MAX2 =
                (int32_t)AIM_CENTER_MAX * AIM_CENTER_MAX;

            if (final_trig_r && magL2 <= AIM_CENTER_MAX2)
            {
                // Cambiamos vector de jitter cada ~25 ms
                if (now_ms - aim_last_time_ms > 25)
                {
                    aim_last_time_ms = now_ms;

                    const int16_t JITTER = 10000; // fuerza del aim assist
                    aim_jitter_x = (int16_t)((rand() % (2 * JITTER + 1)) - JITTER);
                    aim_jitter_y = (int16_t)((rand() % (2 * JITTER + 1)) - JITTER);
                }

                out_lx = clamp16((int16_t)(out_lx + aim_jitter_x));
                out_ly = clamp16((int16_t)(out_ly + aim_jitter_y));
            }
            else
            {
                // Sin R2 o fuera del centro → sin jitter
                aim_jitter_x = 0;
                aim_jitter_y = 0;
                out_lx       = base_lx;
                out_ly       = base_ly;
            }
        }

        // =========================================================
        // 4. ANTI-RECOIL (STICK DERECHO Y, SOLO R2 SIN MACRO)
        //
        //   - Solo si |Y| < 90–95 % del recorrido → si lo llevas al fondo,
        //     se respeta tu movimiento.
        //   - Primer 1.5 s: fuerza fuerte
        //   - Después: fuerza un poco menor, estable
        //   - Siempre empuja hacia ABAJO.
        // =========================================================
        {
            static const int16_t RECOIL_MAX    = 31128;  // ~95 %
            static const int64_t STRONG_US    = 1500000; // 1.5 s

            // ⚠️ MÁS FUERTE QUE ANTES
            static const int16_t RECOIL_STRONG = 10500;
            static const int16_t RECOIL_WEAK   = 9500;

            int16_t abs_ry = (base_ry >= 0) ? base_ry : (int16_t)-base_ry;

            if (final_trig_r && !r2_macro_active)
            {
                if (!is_shooting)
                {
                    is_shooting     = true;
                    shot_start_time = get_absolute_time();
                }

                if (abs_ry < RECOIL_MAX)
                {
                    int64_t time_us = absolute_time_diff_us(
                        shot_start_time,
                        get_absolute_time()
                    );

                    int16_t recoil_force =
                        (time_us < STRONG_US) ? RECOIL_STRONG : RECOIL_WEAK;

                    // Restamos para empujar hacia ABAJO
                    int32_t val = (int32_t)base_ry - recoil_force;

                    // Limitamos a ±95 % para que no salte de extremo a extremo
                    if (val < -RECOIL_MAX) val = -RECOIL_MAX;
                    if (val >  RECOIL_MAX) val =  RECOIL_MAX;

                    out_ry = (int16_t)val;
                }
                else
                {
                    // Ya estás >95 % → no tocamos el stick
                    out_ry = base_ry;
                }
            }
            else
            {
                is_shooting = false;
                out_ry      = base_ry;
            }
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
        const bool lb_pressed   = (btn & Gamepad::BUTTON_LB)   != 0;
        const bool rb_pressed   = (btn & Gamepad::BUTTON_RB)   != 0;
        const bool tri_pressed  = (btn & Gamepad::BUTTON_Y)    != 0;
        const bool sys_pressed  = (btn & Gamepad::BUTTON_SYS)  != 0;
        const bool back_pressed = (btn & Gamepad::BUTTON_BACK) != 0;

        // R1 normal
        if (rb_pressed)                      in_report_.buttons[1] |= XInput::Buttons1::RB;

        // X / Cuadrado / Círculo normales
        if (btn & Gamepad::BUTTON_X)         in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)         in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_B)         in_report_.buttons[1] |= XInput::Buttons1::B;

        // Triángulo: normal + modo turbo (doble tap)
        {
            if (tri_pressed && !tri_prev_pressed)
            {
                uint64_t diff = now_ms - tri_last_press_ms;
                if (diff <= 300)
                {
                    tri_turbo_active = true;
                    tri_tap_state    = true;
                    tri_last_tap_ms  = now_ms;
                }
                tri_last_press_ms = now_ms;
            }

            if (!tri_pressed)
            {
                tri_turbo_active = false;
            }

            tri_prev_pressed = tri_pressed;

            if (tri_pressed)
            {
                if (tri_turbo_active)
                {
                    if (now_ms - tri_last_tap_ms > 50)
                    {
                        tri_last_tap_ms = now_ms;
                        tri_tap_state   = !tri_tap_state;
                    }
                    if (tri_tap_state)
                    {
                        in_report_.buttons[1] |= XInput::Buttons1::Y;
                    }
                }
                else
                {
                    in_report_.buttons[1] |= XInput::Buttons1::Y;
                }
            }
        }

        // Sticks pulsados
        if (btn & Gamepad::BUTTON_L3)        in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)        in_report_.buttons[0] |= XInput::Buttons0::R3;

        // START
        if (btn & Gamepad::BUTTON_START)     in_report_.buttons[0] |= XInput::Buttons0::START;

        // Botón PlayStation: HOME + D-Pad izquierda y derecha mantenidos
        if (sys_pressed)
        {
            in_report_.buttons[1] |= XInput::Buttons1::HOME;
            in_report_.buttons[0] |= (XInput::Buttons0::DPAD_LEFT |
                                      XInput::Buttons0::DPAD_RIGHT);
        }

        // SELECT → LB virtual (L1)
        if (back_pressed)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
        }

        // MUTE (BUTTON_MISC) → BACK
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // =========================================================
        // 7. MACRO R2 → TURBO X/A (solo cuando NO está bloqueado)
        // =========================================================
        if (r2_macro_active && !r2_macro_blocked)
        {
            if (now_ms - r2_last_tap_ms > 50)
            {
                r2_last_tap_ms = now_ms;
                r2_tap_state   = !r2_tap_state;
            }
            if (r2_tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }
        }

        // =========================================================
        // 8. MACRO L1 → TURBO X/A (con 1 s de cola)
        // =========================================================
        if (lb_pressed)
        {
            jump_macro_active = true;
            jump_stop_time_ms = now_ms + 1000; // dura 1s después de soltar
        }

        if (jump_macro_active)
        {
            if (now_ms - jump_last_tap_ms > 50)
            {
                jump_last_tap_ms = now_ms;
                jump_tap_state   = !jump_tap_state;
            }

            if (jump_tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }

            if (!lb_pressed && now_ms >= jump_stop_time_ms)
            {
                jump_macro_active = false;
                jump_tap_state    = false;
            }
        }

        // =========================================================
        // 9. ASIGNAR TRIGGERS Y STICKS FINALES
        // =========================================================
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;

        in_report_.joystick_lx = out_lx;
        in_report_.joystick_ly = out_ly;
        in_report_.joystick_rx = out_rx;
        in_report_.joystick_ry = out_ry;

        // =========================================================
        // 10. ENVIAR REPORTE XINPUT
        // =========================================================
        if (tud_suspended())
        {
            tud_remote_wakeup();
        }

        tud_xinput::send_report(reinterpret_cast<uint8_t*>(&in_report_),
                                sizeof(XInput::InReport));
    }

    // =============================================================
    // 11. RUMBLE (igual que el original)
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

const uint16_t * XInputDevice::get_descriptor_string_cb(uint8_t index,
                                                        uint16_t langid)
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
