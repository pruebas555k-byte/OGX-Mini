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

    // Macro L1 (spam jump A)
    static bool            jump_macro_active = false;
    static absolute_time_t jump_end_time;
    static uint64_t        last_tap_time = 0;
    static bool            tap_state    = false;

    // Estado para AIM ASSIST (más lento/suave y rotacional)
    static uint64_t        aim_last_time_ms = 0;
    static int16_t         aim_jitter_x = 0;
    static int16_t         aim_jitter_y = 0;
    static uint8_t         aim_step     = 0;  // índice del patrón circular

    // Auto-detección del botón PLAYSTATION (psMask)
    static uint16_t        psMask = 0;

    // Turbo R2 → A
    static uint64_t        r2_last_toggle_ms = 0;
    static bool            r2_turbo_state    = false;

    // Turbo triángulo (Y) por doble toque
    static uint64_t        y_last_press_time_ms = 0;
    static bool            y_last_pressed       = false;
    static bool            y_turbo_active       = false;
    static uint64_t        y_turbo_last_ms      = 0;
    static bool            y_turbo_state        = false;

    if (gamepad.new_pad_in())
    {
        // Reinicia todo el reporte (el ctor pone report_size)
        in_report_ = XInput::InReport{};

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn   = gp_in.buttons;

        // Tiempo actual en ms para todas las macros
        uint64_t now_ms = to_ms_since_boot(get_absolute_time());

        // =========================================================
        // 0. AUTO-DETECCIÓN BOTÓN PS (primer bit desconocido)
        //    TIP: la primera vez, aprieta el botón PS solo.
        // =========================================================
        if (psMask == 0)
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
                Gamepad::BUTTON_MISC;    // mute

            uint16_t unknown = btn & ~knownMask;
            if (unknown)
            {
                for (uint8_t i = 0; i < 16; ++i)
                {
                    uint16_t bit = static_cast<uint16_t>(1u << i);
                    if (unknown & bit)
                    {
                        psMask = bit;
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
        //    + DEADZONE PARA QUITAR DRIFT
        // =========================================================
        int16_t base_lx = gp_in.joystick_lx;
        int16_t base_ly = Range::invert(gp_in.joystick_ly);
        int16_t base_rx = gp_in.joystick_rx;
        int16_t base_ry = Range::invert(gp_in.joystick_ry);

        auto clamp16 = [](int16_t v) -> int16_t {
            if (v < -32768) return -32768;
            if (v >  32767) return  32767;
            return v;
        };

        auto sq = [](int16_t v) -> int32_t {
            return static_cast<int32_t>(v) * v;
        };

        int32_t magL2 = sq(base_lx) + sq(base_ly);
        int32_t magR2 = sq(base_rx) + sq(base_ry);

        // Deadzone para drift
        static const int16_t DZ_L  = 7000;                    // ~20% stick izq
        static const int32_t DZ_L2 = static_cast<int32_t>(DZ_L) * DZ_L;

        static const int16_t DZ_R  = 3000;                    // ~9% stick der
        static const int32_t DZ_R2 = static_cast<int32_t>(DZ_R) * DZ_R;

        if (magL2 < DZ_L2)
        {
            base_lx = 0;
            base_ly = 0;
            magL2   = 0;
        }

        if (magR2 < DZ_R2)
        {
            base_rx = 0;
            base_ry = 0;
            magR2   = 0;
        }

        int16_t out_lx = base_lx;
        int16_t out_ly = base_ly;
        int16_t out_rx = base_rx;
        int16_t out_ry = base_ry;

        // =========================================================
        // 3. STICKY AIM ROTACIONAL (STICK IZQ, SOLO CON R2)
        //
        //   - Solo si el stick está dentro del 80% del recorrido
        //   - Patrón circular, fuerte pero más lento
        // =========================================================
        {
            static const int16_t AIM_CENTER_MAX  = 26000;  // ~0.8 * 32767
            static const int32_t AIM_CENTER_MAX2 =
                static_cast<int32_t>(AIM_CENTER_MAX) * AIM_CENTER_MAX;

            if (final_trig_r && magL2 <= AIM_CENTER_MAX2)
            {
                // Cambiamos el vector cada ~35 ms
                if (now_ms - aim_last_time_ms > 35)
                {
                    aim_last_time_ms = now_ms;

                    const int16_t R = 6500; // radio del círculo (fuerte)
                    static const int16_t patternX[8] =
                        {  R,  R,   0, -R, -R, -R,   0,  R };
                    static const int16_t patternY[8] =
                        {  0,  R,   R,  R,  0, -R, -R, -R };

                    aim_jitter_x = patternX[aim_step];
                    aim_jitter_y = patternY[aim_step];
                    aim_step     = (aim_step + 1) & 0x07; // %8
                }

                out_lx = clamp16(static_cast<int16_t>(out_lx + aim_jitter_x));
                out_ly = clamp16(static_cast<int16_t>(out_ly + aim_jitter_y));
            }
            else
            {
                // Fuera de zona o sin disparar → sin jitter
                aim_jitter_x = 0;
                aim_jitter_y = 0;
                out_lx       = base_lx;
                out_ly       = base_ly;
            }
        }

        // =========================================================
        // 4. ANTI-RECOIL DINÁMICO (EJE Y DERECHO, CUANDO R2)
        //
        //   - Solo si el stick derecho está dentro del 85% del recorrido
        //     y no lo estás moviendo mucho en vertical.
        //   - 0–1.5 s:  fuerza 12500
        //   - >1.5 s:   fuerza 11200 (baja menos pero se mantiene)
        // =========================================================
        {
            static const int16_t RECOIL_MAX   = 28000; // ~0.85 * 32767
            static const int32_t RECOIL_MAX2 =
                static_cast<int32_t>(RECOIL_MAX) * RECOIL_MAX;

            static const int16_t RECOIL_Y_ZONE = 20000; // si mueves mucho el stick, no tocamos

            if (final_trig_r)
            {
                if (!is_shooting)
                {
                    is_shooting     = true;
                    shot_start_time = get_absolute_time();
                }

                int16_t abs_base_ry = (base_ry >= 0) ? base_ry : -base_ry;

                if (magR2 <= RECOIL_MAX2 && abs_base_ry <= RECOIL_Y_ZONE)
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

                    // Restamos para empujar la mira hacia ABAJO
                    out_ry = clamp16(static_cast<int16_t>(base_ry - recoil_force));
                }
                else
                {
                    // Lo estás moviendo mucho → no tocamos
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
        // L1 físico: SOLO macro (más abajo), NO manda LB normal aquí

        // R1 normal
        if (btn & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;

        // A/B/X/Y (sin turbo todavía)
        if (btn & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;

        // Triángulo/Y: lo manejamos más abajo con lógica de turbo
        bool y_pressed_now = (btn & Gamepad::BUTTON_Y) != 0;

        // Sticks pulsados
        if (btn & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;

        // START
        if (btn & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;

        // --- BOTÓN PLAYSTATION ---
        // HOME + DPAD IZQ y DERECHA MANTENIDOS
        if ( (psMask && (btn & psMask)) || (btn & Gamepad::BUTTON_SYS) )
        {
            in_report_.buttons[1] |= XInput::Buttons1::HOME;
            in_report_.buttons[0] |= (XInput::Buttons0::DPAD_LEFT |
                                      XInput::Buttons0::DPAD_RIGHT);
        }

        // --- REMAP: SELECT → LB (L1 virtual) ---
        if (btn & Gamepad::BUTTON_BACK)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
        }

        // --- MUTE (BUTTON_MISC) → SELECT/BACK ---
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // =========================================================
        // 7. DROP SHOT / TURBO R2: R2 SIN L2 → A TURBO
        // =========================================================
        if (final_trig_r && !final_trig_l)
        {
            // Turbo A mientras mantienes solo R2
            if (now_ms - r2_last_toggle_ms > 50) // ~20 pulsos/seg
            {
                r2_last_toggle_ms = now_ms;
                r2_turbo_state    = !r2_turbo_state;
            }
            if (r2_turbo_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }
        }
        else
        {
            r2_turbo_state = false;
        }

        // =========================================================
        // 8. TURBO TRIÁNGULO (Y) POR DOBLE TOQUE
        //    - 1 toque normal → Y normal
        //    - doble toque < 300ms y dejas 2º apretado → turbo
        // =========================================================
        if (y_pressed_now && !y_last_pressed)
        {
            // Rising edge
            if (now_ms - y_last_press_time_ms <= 300)
            {
                // Doble toque → activamos turbo
                y_turbo_active  = true;
                y_turbo_last_ms = now_ms;
                y_turbo_state   = true;
            }
            y_last_press_time_ms = now_ms;
        }

        if (y_turbo_active)
        {
            if (!y_pressed_now)
            {
                // Soltaste el botón → apaga turbo
                y_turbo_active = false;
                y_turbo_state  = false;
            }
            else
            {
                if (now_ms - y_turbo_last_ms > 50) // ~20 pulsos/seg
                {
                    y_turbo_last_ms = now_ms;
                    y_turbo_state   = !y_turbo_state;
                }
                if (y_turbo_state)
                {
                    in_report_.buttons[1] |= XInput::Buttons1::Y;
                }
            }
        }
        else
        {
            // Sin turbo → comportamiento normal
            if (y_pressed_now)
            {
                in_report_.buttons[1] |= XInput::Buttons1::Y;
            }
        }
        y_last_pressed = y_pressed_now;

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
            // usamos now_ms
            if (now_ms - last_tap_time > 50)
            {
                tap_state     = !tap_state;
                last_tap_time = now_ms;
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
