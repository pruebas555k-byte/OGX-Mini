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

    // ---------- ESTADO ESTÁTICO PARA MACROS / TIEMPOS ----------
    static absolute_time_t shot_start_time;
    static bool            is_shooting = false;

    // Macro L1 (spam jump)
    static bool            jump_macro_active = false;
    static absolute_time_t jump_end_time;
    static uint64_t        jump_last_tap_time = 0;
    static bool            jump_tap_state    = false;

    // Estado para AIM ASSIST (más lento/suave)
    static uint64_t        aim_last_time_ms = 0;
    static int16_t         aim_jitter_x = 0;
    static int16_t         aim_jitter_y = 0;

    // R2 solo → X turbo (sin delay, pero cancelable con L2)
    static bool            r2_was_pressed          = false;
    static bool            r2_turbo_active         = false;
    static bool            r2_had_l2_during_hold   = false;
    static uint64_t        r2_last_tap_time_ms     = 0;
    static bool            r2_tap_state            = false;

    // Triángulo turbo (doble tap)
    static uint64_t        tri_last_press_ms       = 0;
    static bool            tri_turbo_active        = false;
    static uint64_t        tri_last_tap_time_ms    = 0;
    static bool            tri_tap_state           = false;
    static bool            tri_was_pressed         = false;

    if (gamepad.new_pad_in())
    {
        // Reinicia todo el reporte (el ctor pone report_size)
        in_report_ = XInput::InReport{};

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn   = gp_in.buttons;

        uint64_t now_ms = to_ms_since_boot(get_absolute_time());

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
        // Guardamos base_* para poder calcular magnitudes sin el jitter
        int16_t base_lx = gp_in.joystick_lx;
        int16_t base_ly = Range::invert(gp_in.joystick_ly);
        int16_t base_rx = gp_in.joystick_rx;
        int16_t base_ry = Range::invert(gp_in.joystick_ry);

        int16_t out_lx = base_lx;
        int16_t out_ly = base_ly;
        int16_t out_rx = base_rx;
        int16_t out_ry = base_ry;

        auto clamp16 = [](int16_t v) -> int16_t {
            if (v < -32768) return -32768;
            if (v >  32767) return  32767;
            return v;
        };

        // Magnitud^2 de cada stick (para zona de cancelación)
        int32_t magL2 = static_cast<int32_t>(base_lx) * base_lx +
                        static_cast<int32_t>(base_ly) * base_ly;
        int32_t magR2 = static_cast<int32_t>(base_rx) * base_rx +
                        static_cast<int32_t>(base_ry) * base_ry;

        // =========================================================
        // 3. STICKY AIM (JITTER EN STICK IZQUIERDO SOLO CON R2)
        //
        //   - Solo si el stick está dentro del 80% del recorrido
        //   - Si lo empujas muy fuerte, NO hay jitter (no se vuelve loco)
        //   - Jitter fuerte pero más lento (35 ms)
        // =========================================================
        {
            // 80% del recorrido del stick
            static const int16_t AIM_CENTER_MAX  = 26000;  // ~0.8 * 32767
            static const int32_t AIM_CENTER_MAX2 =
                static_cast<int32_t>(AIM_CENTER_MAX) * AIM_CENTER_MAX;

            if (final_trig_r && magL2 <= AIM_CENTER_MAX2)
            {
                // Cambiamos el vector de jitter solo cada ~35 ms
                if (now_ms - aim_last_time_ms > 35)
                {
                    aim_last_time_ms = now_ms;

                    const int16_t JITTER = 6000; // fuerza del aim assist
                    aim_jitter_x = static_cast<int16_t>(
                        (rand() % (2 * JITTER + 1)) - JITTER
                    );
                    aim_jitter_y = static_cast<int16_t>(
                        (rand() % (2 * JITTER + 1)) - JITTER
                    );
                }

                out_lx = clamp16(static_cast<int16_t>(out_lx + aim_jitter_x));
                out_ly = clamp16(static_cast<int16_t>(out_ly + aim_jitter_y));
            }
            else
            {
                // Fuera del centro o sin disparar → sin jitter
                aim_jitter_x = 0;
                aim_jitter_y = 0;
                out_lx = base_lx;
                out_ly = base_ly;
            }
        }

        // =========================================================
        // 4. ANTI-RECOIL DINÁMICO (EJE Y DERECHO, CUANDO R2)
        //
        //   - Solo si el stick derecho está dentro del 85% del recorrido
        //   - Primer 1.5 s:  fuerza 12500
        //   - Después:        fuerza 11200 y se queda así (no baja tanto)
        // =========================================================
        {
            // 85% del recorrido del stick derecho
            static const int16_t RECOIL_MAX   = 28000; // ~0.85 * 32767
            static const int32_t RECOIL_MAX2 =
                static_cast<int32_t>(RECOIL_MAX) * RECOIL_MAX;

            if (final_trig_r)
            {
                if (!is_shooting)
                {
                    is_shooting     = true;
                    shot_start_time = get_absolute_time();
                }

                if (magR2 <= RECOIL_MAX2)
                {
                    int64_t time_shooting_us = absolute_time_diff_us(
                        shot_start_time,
                        get_absolute_time()
                    );

                    static const int64_t STRONG_DURATION_US = 1500000; // 1.5 s
                    static const int16_t RECOIL_STRONG      = 12500;
                    static const int16_t RECOIL_WEAK        = 11200;

                    int16_t recoil_force =
                        (time_shooting_us < STRONG_DURATION_US)
                            ? RECOIL_STRONG
                            : RECOIL_WEAK;

                    // Restamos para empujar hacia ABAJO
                    out_ry = clamp16(static_cast<int16_t>(out_ry - recoil_force));
                }
                // Si magR2 > RECOIL_MAX2 no aplicamos recoil (pero mantenemos el tiempo)
            }
            else
            {
                is_shooting = false;
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
        // 6. BOTONES BÁSICOS (SIN MACROS)
        // =========================================================
        const bool tri_pressed = (btn & Gamepad::BUTTON_Y) != 0;

        // R1 normal
        if (btn & Gamepad::BUTTON_RB)
            in_report_.buttons[1] |= XInput::Buttons1::RB;

        // X y B normales
        if (btn & Gamepad::BUTTON_X)
            in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)
            in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_B)
            in_report_.buttons[1] |= XInput::Buttons1::B;

        // OJO: TRIÁNGULO (Y) lo tratamos aparte para el turbo,
        // aquí NO lo ponemos todavía.

        // Sticks pulsados
        if (btn & Gamepad::BUTTON_L3)
            in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)
            in_report_.buttons[0] |= XInput::Buttons0::R3;

        // START
        if (btn & Gamepad::BUTTON_START)
            in_report_.buttons[0] |= XInput::Buttons0::START;

        // --- BOTÓN PLAYSTATION (SYS) ---
        // HOME + DPAD IZQ y DERECHA mantenidos mientras lo pulses
        if (btn & Gamepad::BUTTON_SYS)
        {
            in_report_.buttons[1] |= XInput::Buttons1::HOME;
            in_report_.buttons[0] |= (XInput::Buttons0::DPAD_LEFT |
                                      XInput::Buttons0::DPAD_RIGHT);
        }

        // --- REMAP: SELECT → LB (L1 virtual) ---
        if (btn & Gamepad::BUTTON_BACK)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
            // No ponemos Buttons0::BACK aquí
        }

        // --- MUTE (BUTTON_MISC) → SELECT/BACK ---
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // =========================================================
        // 7. R2 SOLO → X TURBO (SIN DELAY, PERO CANCELABLE CON L2)
        //
        //   - Si durante ese mismo R2 alguna vez se apretó L2,
        //     NO se activa turbo aunque luego sueltes L2.
        // =========================================================
        {
            if (trig_r_pressed)
            {
                if (!r2_was_pressed)
                {
                    // Primer frame de R2 presionado
                    r2_was_pressed        = true;
                    r2_had_l2_during_hold = trig_l_pressed;

                    if (!trig_l_pressed)
                    {
                        // Aparece R2 solo → turbo inmediato
                        r2_turbo_active     = true;
                        r2_last_tap_time_ms = now_ms;
                        r2_tap_state        = false;
                    }
                    else
                    {
                        // R2 comenzó mientras L2 estaba apretado → jamás turbo
                        r2_turbo_active     = false;
                        r2_tap_state        = false;
                    }
                }
                else
                {
                    // R2 sigue apretado
                    if (trig_l_pressed && !r2_had_l2_during_hold)
                    {
                        // En algún momento de este R2 se apretó L2 → cancelar turbo
                        r2_had_l2_during_hold = true;
                        r2_turbo_active       = false;
                        r2_tap_state          = false;
                    }
                }

                if (r2_turbo_active && !r2_had_l2_during_hold)
                {
                    // Generador de pulsos de X (A)
                    if (now_ms - r2_last_tap_time_ms > 50)  // ~20 Hz
                    {
                        r2_last_tap_time_ms = now_ms;
                        r2_tap_state        = !r2_tap_state;
                    }

                    if (r2_tap_state)
                    {
                        in_report_.buttons[1] |= XInput::Buttons1::A;
                    }
                }
            }
            else
            {
                // R2 suelto → reseteamos todo el estado de turbo R2
                r2_was_pressed        = false;
                r2_turbo_active       = false;
                r2_had_l2_during_hold = false;
                r2_tap_state          = false;
            }
        }

        // =========================================================
        // 8. TRIÁNGULO (Y) DOBLE TAP → TURBO MIENTRAS SE MANTIENE
        //
        //   - Si presionas Triángulo 2 veces en ≤ 0.30 s y el segundo
        //     lo dejas mantenido, entra en modo turbo.
        //   - Si solo lo presionas normal o el doble tap es muy lento,
        //     se comporta como Triángulo normal.
        // =========================================================
        {
            static const uint64_t TRI_DOUBLE_WINDOW_MS = 300; // 0.30 s

            if (tri_pressed)
            {
                if (!tri_was_pressed)
                {
                    // Flanco de subida
                    if (now_ms - tri_last_press_ms <= TRI_DOUBLE_WINDOW_MS)
                    {
                        // Doble tap detectado → activar turbo
                        tri_turbo_active     = true;
                        tri_last_tap_time_ms = now_ms;
                        tri_tap_state        = false;
                    }
                    tri_last_press_ms = now_ms;
                    tri_was_pressed   = true;
                }
            }
            else
            {
                tri_was_pressed = false;
                if (!tri_turbo_active)
                {
                    tri_last_press_ms = 0;
                }
            }

            if (tri_turbo_active && tri_pressed)
            {
                // Turbo de Triángulo
                if (now_ms - tri_last_tap_time_ms > 50) // ~20 Hz
                {
                    tri_last_tap_time_ms = now_ms;
                    tri_tap_state        = !tri_tap_state;
                }

                if (tri_tap_state)
                {
                    in_report_.buttons[1] |= XInput::Buttons1::Y;
                }
            }
            else if (tri_pressed)
            {
                // Triángulo normal (sin turbo)
                in_report_.buttons[1] |= XInput::Buttons1::Y;
                // Si soltamos, se cancela turbo
                tri_turbo_active = false;
                tri_tap_state    = false;
            }
            else
            {
                // Triángulo suelto → apagar turbo
                tri_turbo_active = false;
                tri_tap_state    = false;
            }
        }

        // =========================================================
        // 9. MACRO L1 (LB físico) -> SPAM JUMP (A)
        // =========================================================
        if (btn & Gamepad::BUTTON_LB)
        {
            jump_macro_active = true;
            jump_end_time     = make_timeout_time_ms(1000); // 1 s después de soltar
        }

        if (jump_macro_active)
        {
            // Velocidad del spam (50 ms ≈ 20 pulsos/seg)
            if (now_ms - jump_last_tap_time > 50)
            {
                jump_tap_state     = !jump_tap_state;
                jump_last_tap_time = now_ms;
            }

            if (jump_tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }

            if (!(btn & Gamepad::BUTTON_LB) && time_reached(jump_end_time))
            {
                jump_macro_active = false;
                jump_tap_state    = false;
            }
        }

        // =========================================================
        // 10. ASIGNAR TRIGGERS Y STICKS FINALES
        // =========================================================
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;

        in_report_.joystick_lx = out_lx;
        in_report_.joystick_ly = out_ly;
        in_report_.joystick_rx = out_rx;
        in_report_.joystick_ry = out_ry;

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
