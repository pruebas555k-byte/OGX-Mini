#include <cstring>
#include <cmath>
#include <algorithm>
#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"

// Variable estática para la velocidad del Turbo
static uint32_t turbo_tick = 0;

// Variable para el "Temblor" (Shake) del Aim Assist
static bool shake_toggle = false; 

// --------------------------------------------------------------------------------
// HELPERS
// --------------------------------------------------------------------------------

// Helper seguro para limitar valores (Clamp)
static inline int16_t clamp_int16(int32_t val) {
    if (val > 32767) return 32767;
    if (val < -32768) return -32768;
    return (int16_t)val;
}

static void apply_stick_curve_mixed(int16_t in_x, int16_t in_y, 
                                   float deadzone, float gamma, float sensitivity,
                                   int16_t &out_x, int16_t &out_y)
{
    constexpr float MAX_VAL = 32767.0f;

    // 1. Normalizar
    float vx = static_cast<float>(in_x) / MAX_VAL;
    float vy = static_cast<float>(in_y) / MAX_VAL;

    // 2. Magnitud
    float mag = std::sqrt(vx*vx + vy*vy);

    // 3. Deadzone
    if (mag <= deadzone || mag < 0.001f) {
        out_x = 0; out_y = 0; return;
    }

    if (mag > 1.0f) mag = 1.0f;

    // 4. Mapeo y Curva Mixta
    float adj = (mag - deadzone) / (1.0f - deadzone);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    float response_curve = std::pow(adj, gamma); 
    float response_linear = adj;                  
    
    // Mezcla 80/20
    float out_frac = (response_curve * 0.8f) + (response_linear * 0.2f);

    // Sensibilidad
    out_frac *= sensitivity; 

    // 5. Reconstrucción Segura
    float scale = out_frac / mag;
    float final_x = vx * scale * MAX_VAL;
    float final_y = vy * scale * MAX_VAL;

    // Convertimos a int32 primero para poder clipear sin error
    out_x = clamp_int16((int32_t)final_x);
    out_y = clamp_int16((int32_t)final_y);
}

// --------------------------------------------------------------------------------
// CLASE XINPUT
// --------------------------------------------------------------------------------

void XInputDevice::initialize() 
{
    class_driver_ = *tud_xinput::class_driver();
}

void XInputDevice::process(const uint8_t idx, Gamepad& gamepad)
{
    if (gamepad.new_pad_in())
    {
        in_report_.buttons[0] = 0;
        in_report_.buttons[1] = 0;

        Gamepad::PadIn gp_in = gamepad.get_pad_in();

        // --- 1. DPAD ---
        switch (gp_in.dpad)
        {
            case Gamepad::DPAD_UP:         in_report_.buttons[0] = XInput::Buttons0::DPAD_UP; break;
            case Gamepad::DPAD_DOWN:       in_report_.buttons[0] = XInput::Buttons0::DPAD_DOWN; break;
            case Gamepad::DPAD_LEFT:       in_report_.buttons[0] = XInput::Buttons0::DPAD_LEFT; break;
            case Gamepad::DPAD_RIGHT:      in_report_.buttons[0] = XInput::Buttons0::DPAD_RIGHT; break;
            case Gamepad::DPAD_UP_LEFT:    in_report_.buttons[0] = XInput::Buttons0::DPAD_UP | XInput::Buttons0::DPAD_LEFT; break;
            case Gamepad::DPAD_UP_RIGHT:   in_report_.buttons[0] = XInput::Buttons0::DPAD_UP | XInput::Buttons0::DPAD_RIGHT; break;
            case Gamepad::DPAD_DOWN_LEFT:  in_report_.buttons[0] = XInput::Buttons0::DPAD_DOWN | XInput::Buttons0::DPAD_LEFT; break;
            case Gamepad::DPAD_DOWN_RIGHT: in_report_.buttons[0] = XInput::Buttons0::DPAD_DOWN | XInput::Buttons0::DPAD_RIGHT; break;
            default: break;
        }

        // --- 2. BOTONES Y TURBO ---
        if (gp_in.buttons & Gamepad::BUTTON_BACK)  in_report_.buttons[1] |= XInput::Buttons1::LB;
        if (gp_in.buttons & Gamepad::BUTTON_MISC)  in_report_.buttons[0] |= XInput::Buttons0::BACK;

        if (gp_in.buttons & Gamepad::BUTTON_LB) {
            turbo_tick++;
            if ((turbo_tick / 5) % 2 == 0) in_report_.buttons[1] |= XInput::Buttons1::A; 
        } else {
            turbo_tick = 0; 
        }

        if (gp_in.buttons & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;
        if (gp_in.buttons & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (gp_in.buttons & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;
        if (gp_in.buttons & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (gp_in.buttons & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (gp_in.buttons & Gamepad::BUTTON_Y)     in_report_.buttons[1] |= XInput::Buttons1::Y;
        if (gp_in.buttons & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;
        if (gp_in.buttons & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;
        if (gp_in.buttons & Gamepad::BUTTON_SYS)   in_report_.buttons[1] |= XInput::Buttons1::HOME;

        in_report_.trigger_l = (gp_in.trigger_l > 13) ? 255 : 0;
        in_report_.trigger_r = (gp_in.trigger_r > 13) ? 255 : 0;

        // --- 3. PROCESAMIENTO DE STICKS ---
        
        // Variables raw tras aplicar curva
        int16_t curve_lx, curve_ly, curve_rx, curve_ry;
        
        apply_stick_curve_mixed(gp_in.joystick_lx, Range::invert(gp_in.joystick_ly), 
                               0.05f, 1.6f, 1.10f, curve_lx, curve_ly);
                               
        apply_stick_curve_mixed(gp_in.joystick_rx, Range::invert(gp_in.joystick_ry), 
                               0.05f, 1.5f, 1.10f, curve_rx, curve_ry);

        // Usamos int32_t para acumular modificaciones (Aim Assist / Anti-Recoil) de forma segura
        int32_t final_lx = curve_lx;
        int32_t final_ly = curve_ly;
        int32_t final_rx = curve_rx;
        int32_t final_ry = curve_ry;

        // --- 4. AIM ASSIST (TEMBLOR STICK IZQUIERDO) ---
        // Se activa solo si presionamos Gatillo Izquierdo a fondo (Apuntar)
        if (in_report_.trigger_l == 255) 
        {
            // Fuerza del temblor. 
            // 450 es sutil pero suficiente para que el juego detecte movimiento.
            // Si el personaje se mueve visiblemente, reduce este valor (ej. a 300).
            const int32_t SHAKE_FORCE = 450; 

            shake_toggle = !shake_toggle; // Alternar dirección cada frame

            if (shake_toggle) {
                final_lx += SHAKE_FORCE;
                final_ly += SHAKE_FORCE;
            } else {
                final_lx -= SHAKE_FORCE;
                final_ly -= SHAKE_FORCE;
            }
        }

        // --- 5. ANTI-RECOIL (Stick Derecho) ---
        // Mantenemos esto si quieres que siga bajando la mira al disparar
        if (in_report_.trigger_l == 255 && in_report_.trigger_r == 255) 
        {
            const int32_t force = 4500;
            final_ry -= force; 
        }

        // --- 6. ASIGNACIÓN FINAL CON CLAMP ---
        // Clamp_int16 asegura que los valores (incluso con temblor o recoil) no den overflow
        in_report_.joystick_lx = clamp_int16(final_lx);
        in_report_.joystick_ly = clamp_int16(final_ly);
        in_report_.joystick_rx = clamp_int16(final_rx);
        in_report_.joystick_ry = clamp_int16(final_ry);

        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        tud_xinput::send_report((uint8_t*)&in_report_, sizeof(XInput::InReport));
    }

    if (tud_xinput::receive_report(reinterpret_cast<uint8_t*>(&out_report_), sizeof(XInput::OutReport)) &&
        out_report_.report_id == XInput::OutReportID::RUMBLE)
    {
        Gamepad::PadOut gp_out;
        gp_out.rumble_l = out_report_.rumble_l;
        gp_out.rumble_r = out_report_.rumble_r;
        gamepad.set_pad_out(gp_out);
    }
}

// CALLBACKS (Sin cambios)
uint16_t XInputDevice::get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) 
{
    std::memcpy(buffer, &in_report_, sizeof(XInput::InReport));
    return sizeof(XInput::InReport);
}
void XInputDevice::set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {}
bool XInputDevice::vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) { return false; }
const uint16_t * XInputDevice::get_descriptor_string_cb(uint8_t index, uint16_t langid) 
{
    const char *value = reinterpret_cast<const char*>(XInput::DESC_STRING[index]);
    return get_string_descriptor(value, index);
}
const uint8_t * XInputDevice::get_descriptor_device_cb() { return XInput::DESC_DEVICE; }
const uint8_t * XInputDevice::get_hid_descriptor_report_cb(uint8_t itf) { return nullptr; }
const uint8_t * XInputDevice::get_descriptor_configuration_cb(uint8_t index) { return XInput::DESC_CONFIGURATION; }
const uint8_t * XInputDevice::get_descriptor_device_qualifier_cb() { return nullptr; }
