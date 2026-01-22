#include <cstring>
#include <cstdlib>        // rand(), srand()
#include <cmath>          // std::sin, std::cos
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
    static bool            jump_l1_macro_active = false;
    static uint64_t        jump_l1_stop_time_ms = 0;
    static uint64_t        jump_l1_last_tap_ms  = 0;
    static bool            jump_l1_tap_state    = false;

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

    // Aim assist (horizontal, suave y "humano")
    static uint64_t aim_last_time_ms = 0;
    static int16_t  aim_direction    = 1;  // 1 (derecha), -1 (izquierda)
    static bool     aim_active       = false;
    static uint16_t aim_interval_ms  = 80; // variable, recalculada
    static bool     rng_seeded       = false; // para srand una sola vez

    if (gamepad.new_pad_in())
    {
        // Reinicia todo el reporte
        in_report_ = XInput::InReport{};

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn   = gp_in.buttons;

        uint64_t now_ms = to_ms_since_boot(get_absolute_time());

        // Seed RNG una vez con tiempo (para jitter / timing irregular)
        if (!rng_seeded)
        {
            srand((unsigned)now_ms);
            rng_seeded = true;
        }

        // =========================================================
        // 1. TRIGGERS Y ESTADO R2 MACRO
        // =========================================================
        const bool trig_l_pressed = (gp_in.trigger_l != 0);
        const bool trig_r_pressed = (gp_in.trigger_r != 0);

        // Hair trigger "digital": si hay algo de presión → 255
        uint8_t final_trig_l = trig_l_pressed ? 255 : 0;
        uint8_t final_trig_r = trig_r_pressed ? 255 : 0;

        // R2 macro: solo se arma si R2 empieza sin L2.
        if (trig_r_pressed && !r2_prev_pressed)
        {
            if (trig_l_pressed)
            {
                r2_macro_active  = false;
                r2_macro_blocked = true;
            }
            else
            {
                r2_macro_blocked = false;
                r2_macro_active  = true;
                r2_tap_state     = true;
                r2_last_tap_ms   = now_ms;
            }
        }
        else if (!trig_r_pressed && r2_prev_pressed)
        {
            r2_macro_active  = false;
            r2_macro_blocked = false;
            r2_tap_state     = false;
        }
        r2_prev_pressed = trig_r_pressed;

        // =========================================================
        // 2. STICKS BASE (coordenadas finales que ve el juego)
        // =========================================================
        int16_t base_lx = gp_in.joystick_lx;
        int16_t base_ly = Range::invert(gp_in.joystick_ly);

        int16_t base_rx = gp_in.joystick_rx;
        int16_t base_ry = Range::invert(gp_in.joystick_ry);

        // ---- DEADZONE AJUSTADO: 5% ----
        static const int16_t R_DEADZONE  = 1638; // ~5 % de 32767
        static const int32_t R_DEADZONE2 =
            (int32_t)R_DEADZONE * (int32_t)R_DEADZONE;

        int32_t magR2_raw =
            (int32_t)base_rx * base_rx +
            (int32_t)base_ry * base_ry;

        if (magR2_raw < R_DEADZONE2)
        {
            base_rx = 0;
            base_ry = 0;
        }

        int16_t out_lx = base_lx;
        int16_t out_ly = base_ly;
        int16_t out_rx = base_rx;
        int16_t out_ry = base_ry;

        auto clamp16 = [](int16_t v) -> int16_t {
            if (v < -32768) return -32768;
            if (v >  32767) return  32767;
            return v;
        };

        int32_t magL2 =
            (int32_t)base_lx * base_lx +
            (int32_t)base_ly * base_ly;

        // Right stick magnitude squared (post-deadzone)
        int32_t magR2 = (int32_t)base_rx * base_rx + (int32_t)base_ry * base_ry;

        // =========================================================
        // 3. AIM ASSIST HORIZONTAL: proporcional, jitter, timing irregular
        // =========================================================
        {
            // Centros y límites
            static const int16_t AIM_CENTER_MAX  = 35000; // ~80%
            static const int32_t AIM_CENTER_MAX2 =
                (int32_t)AIM_CENTER_MAX * (int32_t)AIM_CENTER_MAX;

            // Parámetros recomendados y rangos
            static const int16_t AIM_PULSE_MAX_RECOMMENDED = 1400; // tope sugerido
            static const int16_t AIM_PULSE_MIN_RECOMMENDED = 700;

            // Threshold para aplicar aim assist: solo cuando estás "apuntando"
            // (stick L cerca del centro) y R-stick casi quieto (no puedes estar moviendo R).
            static const int32_t R_STILL_THRESHOLD = 5000 * 5000; // según tu sugerencia

            if (final_trig_r && magL2 <= AIM_CENTER_MAX2 && magR2 < R_STILL_THRESHOLD)
            {
                // Activación: configuraciones iniciales
                if (!aim_active)
                {
                    aim_active       = true;
                    aim_last_time_ms = now_ms;
                    // dirección inicial aleatoria para evitar patrón exacto
                    aim_direction = (rand() & 1) ? 1 : -1;
                    // intervalo inicial aleatorio 60..100 ms
                    aim_interval_ms = 60 + (rand() % 41); // 60..100
                }

                // Timing irregular: recomputa intervalo tras cada ejecución
                if ((now_ms - aim_last_time_ms) > aim_interval_ms)
                {
                    aim_last_time_ms = now_ms;
                    // recalcular intervalo irregular: 60..100 ms
                    aim_interval_ms = 60 + (rand() % 41);

                    // Con baja probabilidad invierte dirección para hacerlo asimétrico
                    if ((rand() % 100) < 30) // 30% de probabilidad de flip
                    {
                        aim_direction = -aim_direction;
                    }
                }

                // Fuerza proporcional al movimiento del stick L (imita slowdown)
                int16_t abs_lx = (base_lx >= 0) ? base_lx : (int16_t)-base_lx;
                int16_t dynamic_assist;
                // Mapeo sugerido (ajustable)
                if (abs_lx < 6000) dynamic_assist = 1200;
                else if (abs_lx < 12000) dynamic_assist = 700;
                else dynamic_assist = 0;

                // Clamp a recomendaciones máximas
                if (dynamic_assist > AIM_PULSE_MAX_RECOMMENDED) dynamic_assist = AIM_PULSE_MAX_RECOMMENDED;
                if (dynamic_assist < AIM_PULSE_MIN_RECOMMENDED && dynamic_assist != 0) dynamic_assist = AIM_PULSE_MIN_RECOMMENDED;

                // Micro-offset aleatorio (jitter) para romper firmas
                int16_t jitter = (rand() % 201) - 100; // -100 .. +100

                // Solo aplicar si dynamic_assist > 0
                if (dynamic_assist > 0)
                {
                    // Añade la asistencia teniendo en cuenta dirección y jitter
                    int32_t tmp = (int32_t)out_lx + (int32_t)aim_direction * (int32_t)dynamic_assist + (int32_t)jitter;

                    out_lx = clamp16(static_cast<int16_t>(tmp));
                }
                else
                {
                    out_lx = base_lx;
                }
            }
            else
            {
                // no aplicable → restablecer estados
                aim_active       = false;
                aim_last_time_ms = 0;
                aim_direction    = 1;
                out_lx           = base_lx;
                out_ly           = base_ly;
            }
        }

        // =========================================================
        // 4. ANTI-RECOIL (PRO): responder al tiempo, al input, ruido por bala
        // =========================================================
        {
            // Timings en microsegundos (ajustables)
            static const int64_t RAMP_US      = 40000; // tiempo de rampa
            static const int64_t STRONG_US    = 65000; // tiempo para fuerza fuerte sostenida
            static const int64_t DECAY_US     = 35000; // decay (30-50 ms recomendados) -> 35 ms

            // Valores de recoil (ajustados a rango recomendado)
            static const int16_t RECOIL_STRONG = 8000;   // fuerza pico
            static const int16_t RECOIL_WEAK   = 7000;   // fuerza mínima sostenida

            static const int16_t RECOIL_MAX    = 33128;

            int16_t abs_ry = (base_ry >= 0) ? base_ry : (int16_t)-base_ry;

            if (final_trig_r && !r2_macro_active)
            {
                if (!is_shooting)
                {
                    is_shooting     = true;
                    shot_start_time = get_absolute_time();
                }

                // Si el jugador ya está jalando fuertemente hacia abajo, no interfieras
                if (base_ry < -4000)
                {
                    out_ry = base_ry;
                }
                else
                {
                    if (abs_ry < RECOIL_MAX)
                    {
                        int64_t t_us = absolute_time_diff_us(
                            shot_start_time,
                            get_absolute_time()
                        );

                        // Base de recoil calculada por el tiempo (rampa / plateau / decay)
                        float recoil_force_f = 0.0f;

                        if (t_us < RAMP_US)
                        {
                            recoil_force_f = (float)RECOIL_STRONG * ((float)t_us / (float)RAMP_US);
                        }
                        else if (t_us < STRONG_US)
                        {
                            recoil_force_f = (float)RECOIL_STRONG;
                        }
                        else
                        {
                            int64_t t2 = t_us - STRONG_US;
                            if (t2 >= DECAY_US)
                            {
                                recoil_force_f = (float)RECOIL_WEAK;
                            }
                            else
                            {
                                recoil_force_f = (float)RECOIL_STRONG +
                                    ((float)(RECOIL_WEAK - RECOIL_STRONG) * ((float)t2 / (float)DECAY_US));
                            }
                        }

                        // Escala según cuánto jala el jugador (player_pull = |base_ry|)
                        int16_t player_pull = abs_ry;
                        float recoil_scale;
                        if (player_pull < 2000) recoil_scale = 1.0f;
                        else if (player_pull < 6000) recoil_scale = 0.7f;
                        else recoil_scale = 0.4f;

                        // Ruido por "bala" (micro-variación)
                        int16_t recoil_noise = (rand() % 301) - 150; // -150 .. +150

                        // Aplicar ruido y escala
                        recoil_force_f += (float)recoil_noise;
                        recoil_force_f *= recoil_scale;

                        // Convertir a entero y restar del input del jugador
                        int32_t recoil_force = (int32_t)std::lround(recoil_force_f);

                        int32_t val = (int32_t)base_ry - recoil_force;

                        if (val < -RECOIL_MAX) val = -RECOIL_MAX;
                        if (val >  RECOIL_MAX) val =  RECOIL_MAX;

                        out_ry = (int16_t)val;
                    }
                    else
                    {
                        out_ry = base_ry;
                    }
                }
            }
            else
            {
                is_shooting = false;
                out_ry      = base_ry;
            }
        }

        // ----------------- resto igual -----------------

        // 5. DPAD
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

        // 6. BOTONES BÁSICOS + REMAPS
        const bool lb_pressed   = (btn & Gamepad::BUTTON_LB)   != 0;
        const bool rb_pressed   = (btn & Gamepad::BUTTON_RB)   != 0;
        const bool tri_pressed  = (btn & Gamepad::BUTTON_Y)    != 0;
        const bool sys_pressed  = (btn & Gamepad::BUTTON_SYS)  != 0;
        const bool back_pressed = (btn & Gamepad::BUTTON_BACK) != 0;
        const bool l3_pressed   = (btn & Gamepad::BUTTON_L3)   != 0;
        const bool r3_pressed   = (btn & Gamepad::BUTTON_R3)   != 0;

        if (rb_pressed)                      in_report_.buttons[1] |= XInput::Buttons1::RB;
        if (btn & Gamepad::BUTTON_X)         in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)         in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_B)         in_report_.buttons[1] |= XInput::Buttons1::B;

        // Turbo triángulo (doble tap)
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

        if (l3_pressed)                      in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (r3_pressed)                      in_report_.buttons[0] |= XInput::Buttons0::R3;
        if (btn & Gamepad::BUTTON_START)     in_report_.buttons[0] |= XInput::Buttons0::START;
        if (sys_pressed)
        {
            in_report_.buttons[1] |= XInput::Buttons1::HOME;
            in_report_.buttons[0] |= (XInput::Buttons0::DPAD_LEFT |
                                      XInput::Buttons0::DPAD_RIGHT);
        }
        if (back_pressed)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
        }
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // 7. MACRO R2 → TURBO X/A (solo cuando NO está bloqueado)
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

        // 8. MACRO L1 (turbo A con 1 s de cola)
        if (lb_pressed)
        {
            jump_l1_macro_active = true;
            jump_l1_stop_time_ms = now_ms + 1000;
        }
        if (jump_l1_macro_active)
        {
            if (now_ms - jump_l1_last_tap_ms > 50)
            {
                jump_l1_last_tap_ms = now_ms;
                jump_l1_tap_state   = !jump_l1_tap_state;
            }

            if (jump_l1_tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }

            if (!lb_pressed && now_ms >= jump_l1_stop_time_ms)
            {
                jump_l1_macro_active = false;
                jump_l1_tap_state    = false;
            }
        }

        // 9. ASIGNAR TRIGGERS Y STICKS FINALES
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;
        in_report_.joystick_lx = out_lx;
        in_report_.joystick_ly = out_ly;
        in_report_.joystick_rx = out_rx;
        in_report_.joystick_ry = out_ry;

        // 10. ENVIAR REPORTE XINPUT
        if (tud_suspended())
        {
            tud_remote_wakeup();
        }
        tud_xinput::send_report(reinterpret_cast<uint8_t*>(&in_report_),
                                sizeof(XInput::InReport));
    }

    // 11. RUMBLE (igual que el original)
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
