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
    static uint64_t        last_tap_time = 0;
    static bool            tap_state    = false;

    // Auto-detección del bit del TOUCHPAD
    static uint16_t        touchpadMask = 0;

    // Estado para AIM ASSIST (para que sea más lento/suave)
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

        auto clamp16 = [](int16_t v) -> int16_t {
            if (v < -32768) return -32768;
            if (v >  32767) return  32767;
            return v;
        };

        // =========================================================
        // 3. STICKY AIM (JITTER EN STICK IZQUIERDO SOLO CON R2)
        //    Más FUERTE pero MÁS LENTO:
        //      - jitter ~20% del rango
        //      - se actualiza cada ~35 ms
        //    Ahora limitamos el efecto al 95% del rango para que no
        //    mueva la cámara hasta el máximo absoluto y evite giros
        //    extremos.
        // =========================================================
        if (final_trig_r)   // solo cuando disparas con R2
        {
            uint64_t now_ms = to_ms_since_boot(get_absolute_time());

            // Cambiamos el vector de jitter solo cada 35 ms aprox.
            if (now_ms - aim_last_time_ms > 35)
            {
                aim_last_time_ms = now_ms;

                const int16_t JITTER = 6000; // fuerza del aim assist (valor base)
                // Generamos jitter en rango [-JITTER, +JITTER]
                aim_jitter_x = static_cast<int16_t>((rand() % (2 * JITTER + 1)) - JITTER);
                aim_jitter_y = static_cast<int16_t>((rand() % (2 * JITTER + 1)) - JITTER);

                // Escalamos el efecto máximo al 95% para seguridad
                constexpr int SCALE_PCT = 95;
                aim_jitter_x = static_cast<int16_t>((aim_jitter_x * SCALE_PCT) / 100);
                aim_jitter_y = static_cast<int16_t>((aim_jitter_y * SCALE_PCT) / 100);
            }

            out_lx = clamp16(out_lx + aim_jitter_x);
            out_ly = clamp16(out_ly + aim_jitter_y);
        }
        else
        {
            // Sin disparar, no queremos jitter
            aim_jitter_x = 0;
            aim_jitter_y = 0;
        }

        // =========================================================
        // 4. ANTI-RECOIL DINÁMICO (EJE Y DERECHO, CUANDO R2)
        //
        //   - Primer segundo: fuerte
        //   - Luego: más suave
        //   Antes lo sumábamos con el signo equivocado (subía),
        //   ahora lo RESTAMOS para que baje la mira.
        //   Limitamos el efecto al 95% para evitar empujar el eje
        //   hasta los extremos absolutos.
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

            // Fuerzas fuertes para que se note bien
            const int16_t RECOIL_STRONG = 12000; // primer segundo
            const int16_t RECOIL_WEAK   =  6000; // después

            int16_t recoil_force = (time_shooting_us < 1000000)
                                 ? RECOIL_STRONG
                                 : RECOIL_WEAK;

            // Escalamos el recoil al 95% máximo
            constexpr int SCALE_PCT = 95;
            recoil_force = static_cast<int16_t>((recoil_force * SCALE_PCT) / 100);

            // RESTAMOS para que empuje hacia ABAJO
            out_ry = clamp16(out_ry - recoil_force);
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
        // L1 físico: SOLO macro (abajo), NO manda LB normal aquí

        // R1 normal
        if (btn & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;

        // A/B/X/Y
        if (btn & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_Y)     in_report_.buttons[1] |= XInput::Buttons1::Y;
        if (btn & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;

        // Sticks pulsados
        if (btn & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;

        // START
        if (btn & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;

        // --- BOTÓN PLAYSTATION (SYS) ---
        // HOME + D-Pad Izq + Der
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

        // --- TOUCHPAD DETECTADO → BACK/SELECT ---
        if (touchpadMask && (btn & touchpadMask))
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // --- MUTE (BUTTON_MISC) actuará ahora como TOUCHPAD (BACK) ---
        // De esta forma el botón MUTE se comporta igual que el touchpad.
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

            // Velocidad del spam (50 ms ≈ 20 pulsos/seg)
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
        // 9. ASIGNAR TRIGGERS Y STICKS FINALES
        //    Antes de escribir los ejes finales, limitamos todos
        //    los ejes para que no superen el 95% del rango absoluto.
        // =========================================================
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;

        // Limitar ejes al 95% del máximo para evitar giros extremos
        constexpr int16_t AXIS_95 = static_cast<int16_t>((32767 * 95) / 100); // ≈ 31128
        auto clamp95 = [](int16_t v) -> int16_t {
            constexpr int16_t LIM = static_cast<int16_t>((32767 * 95) / 100);
            if (v < -LIM) return -LIM;
            if (v >  LIM) return  LIM;
            return v;
        };

        in_report_.joystick_lx = clamp95(out_lx);
        in_report_.joystick_ly = clamp95(out_ly);
        in_report_.joystick_rx = clamp95(out_rx);
        in_report_.joystick_ry = clamp95(out_ry);

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
