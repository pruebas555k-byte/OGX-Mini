#include <cstring>
#include <cstdlib>
#include "pico/time.h"
#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"

void XInputDevice::initialize()
{
    class_driver_ = *tud_xinput::class_driver();
}

void XInputDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    (void)idx;

    // Estado estático para macros / tiempos
    static absolute_time_t shot_start_time;
    static bool is_shooting = false;

    // Macro L1 (spam jump)
    static bool     jump_macro_active = false;
    static absolute_time_t jump_end_time;
    static uint64_t last_tap_time = 0;
    static bool     tap_state    = false;

    if (gamepad.new_pad_in())
    {
        // Limpia el reporte
        std::memset(&in_report_, 0, sizeof(in_report_));
        in_report_.report_id   = 0;                        // XInput usa 0
        in_report_.report_size = sizeof(XInput::InReport); // <-- AQUÍ ESTABA EL ERROR

        Gamepad::PadIn gp_in = gamepad.get_pad_in();
        const uint16_t btn    = gp_in.buttons;

        // --- 1. HAIR TRIGGERS ---
        uint8_t final_trig_l = (gp_in.trigger_l > 5) ? 255 : 0;
        uint8_t final_trig_r = (gp_in.trigger_r > 5) ? 255 : 0;

        // --- 2. STICKY AIM ASSIST (jitter leve cuando disparas/apuntas) ---
        int8_t stick_l_x = gp_in.joystick_lx;
        int8_t stick_l_y = gp_in.joystick_ly;

        if (final_trig_l > 50 || final_trig_r > 50)
        {
            int8_t jitter_x = (rand() % 9) - 4;  // -4..+4
            int8_t jitter_y = (rand() % 9) - 4;
            stick_l_x = Range::clamp(stick_l_x + jitter_x, -128, 127);
            stick_l_y = Range::clamp(stick_l_y + jitter_y, -128, 127);
        }

        // --- 3. ANTI-RECOIL DINÁMICO ---
        int8_t stick_r_y = gp_in.joystick_ry;
        if (final_trig_r > 50)
        {
            if (!is_shooting)
            {
                is_shooting     = true;
                shot_start_time = get_absolute_time();
            }

            int64_t time_shooting_us = absolute_time_diff_us(shot_start_time, get_absolute_time());
            int8_t  recoil_force     = (time_shooting_us < 1000000) ? 25 : 12; // 1s fuerte, luego suave

            stick_r_y = Range::clamp(stick_r_y - recoil_force, -128, 127);
        }
        else
        {
            is_shooting = false;
        }

        // --- 4. DROP SHOT: solo si disparas de cadera (R sin L) ---
        if (final_trig_r > 50 && final_trig_l < 50)
        {
            in_report_.buttons[1] |= XInput::Buttons1::B;   // B = agacharse/tumbarse en muchas configs
        }

        // --- 5. MACRO L1: spam de salto (A A A A...) mientras se mantenga + 1s extra ---
        if (btn & Gamepad::BUTTON_LB)
        {
            jump_macro_active = true;
            jump_end_time     = make_timeout_time_ms(1000); // desde que sueltas, dura 1s más
        }

        if (jump_macro_active)
        {
            uint64_t now = to_ms_since_boot(get_absolute_time());

            // Velocidad del spam
            if (now - last_tap_time > 50)  // 50 ms = 20Hz aprox
            {
                tap_state     = !tap_state;
                last_tap_time = now;
            }

            if (tap_state)
            {
                in_report_.buttons[1] |= XInput::Buttons1::A;
            }

            // Si ya NO estás pulsando L1 y se acaba el segundo extra, apagamos macro
            if (!(btn & Gamepad::BUTTON_LB) && time_reached(jump_end_time))
            {
                jump_macro_active = false;
                tap_state         = false;
            }
        }

        // --- 6. REMAP: SHARE físico -> LB virtual (granadas) ---
        if (btn & Gamepad::BUTTON_BACK)
        {
            in_report_.buttons[1] |= XInput::Buttons1::LB;
        }

        // --- 7. TOUCHPAD PS5 -> SELECT/BACK en XInput ---
        // En Bluepad/GP2040 normalmente el click del touchpad viene en BUTTON_MISC.
        // Esto hará que al pulsar el touchpad se envíe BACK (Select).
        if (btn & Gamepad::BUTTON_MISC)
        {
            in_report_.buttons[0] |= XInput::Buttons0::BACK;
        }

        // --- DPAD ---
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:    in_report_.buttons[0] |= XInput::Buttons0::DPAD_UP;    break;
            case Gamepad::DPAD_DOWN:  in_report_.buttons[0] |= XInput::Buttons0::DPAD_DOWN;  break;
            case Gamepad::DPAD_LEFT:  in_report_.buttons[0] |= XInput::Buttons0::DPAD_LEFT;  break;
            case Gamepad::DPAD_RIGHT: in_report_.buttons[0] |= XInput::Buttons0::DPAD_RIGHT; break;
            default: break;
        }

        // --- BOTONES NORMALES ---
        if (btn & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;
        if (btn & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (btn & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;

        if (btn & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (btn & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (btn & Gamepad::BUTTON_Y)     in_report_.buttons[1] |= XInput::Buttons1::Y;
        if (btn & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;
        if (btn & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;
        if (btn & Gamepad::BUTTON_SYS)   in_report_.buttons[1] |= XInput::Buttons1::HOME;

        // --- EJES FINALES ---
        in_report_.trigger_l   = final_trig_l;
        in_report_.trigger_r   = final_trig_r;
        in_report_.joystick_lx = stick_l_x;
        in_report_.joystick_ly = Range::invert(stick_l_y);
        in_report_.joystick_rx = gp_in.joystick_rx;
        in_report_.joystick_ry = Range::invert(stick_r_y);

        // Enviar a host
        tud_xinput::send_report(
            reinterpret_cast<uint8_t*>(&in_report_),
            sizeof(XInput::InReport)
        );
    }
}
