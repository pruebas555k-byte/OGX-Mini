#include <cstring>
#include <cmath>
#include <algorithm>
#include "USBDevice/DeviceDriver/XInput/tud_xinput/tud_xinput.h"
#include "USBDevice/DeviceDriver/XInput/XInput.h"

// Variable estática para la velocidad del Turbo
static uint32_t turbo_tick = 0;

// Variables para el Aim Assist Rotacional
static float aim_assist_angle = 0.0f;

// --------------------------------------------------------------------------------
// HELPERS: MATEMÁTICAS Y CURVAS (La magia para Warzone)
// --------------------------------------------------------------------------------

// Función de Curva Mixta (80% Curva + 20% Lineal) -> Sensación "Relajado Mejorado"
// Devuelve el valor ajustado manteniendo el rango -32768 a 32767
static void apply_stick_curve_mixed(int16_t in_x, int16_t in_y, 
                                   float deadzone, float gamma, float sensitivity,
                                   int16_t &out_x, int16_t &out_y)
{
    constexpr float MAX_VAL = 32767.0f;

    // 1. Normalizar a float -1.0 a 1.0
    float vx = static_cast<float>(in_x) / MAX_VAL;
    float vy = static_cast<float>(in_y) / MAX_VAL;

    // 2. Calcular Magnitud
    float mag = std::sqrt(vx*vx + vy*vy);

    // 3. Deadzone Circular
    if (mag <= deadzone || mag < 0.001f) {
        out_x = 0;
        out_y = 0;
        return;
    }

    // Clamp magnitud
    if (mag > 1.0f) mag = 1.0f;

    // 4. Mapear magnitud (quitar deadzone)
    float adj = (mag - deadzone) / (1.0f - deadzone);
    adj = std::fmax(0.0f, std::fmin(1.0f, adj));

    // 5. FÓRMULA MIXTA (La clave de la precisión)
    float response_curve = std::pow(adj, gamma); // Parte curva (lenta al inicio)
    float response_linear = adj;                 // Parte lineal (estable)
    
    // Mezcla: 80% Curva (Precisión) + 20% Lineal (Evita brusquedad final)
    float out_frac = (response_curve * 0.8f) + (response_linear * 0.2f);

    // 6. Aplicar Sensibilidad (Overdrive para asegurar 100%)
    out_frac *= sensitivity;
    if (out_frac > 1.0f) out_frac = 1.0f;

    // 7. Reconstruir vector
    float scale = out_frac / mag;
    float final_x = vx * scale * MAX_VAL;
    float final_y = vy * scale * MAX_VAL;

    // Clamp final a enteros de 16 bits
    out_x = (int16_t)std::fmax(-32767.0f, std::fmin(32767.0f, final_x));
    out_y = (int16_t)std::fmax(-32767.0f, std::fmin(32767.0f, final_y));
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
        // Resetear botones
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

        // --- 2. REMAPEO Y TURBO ---
        
        // SELECT físico (BACK) -> Envía L1 (LB)
        if (gp_in.buttons & Gamepad::BUTTON_BACK)  in_report_.buttons[1] |= XInput::Buttons1::LB;
        
        // MUTE físico (MISC) -> Envía SELECT (BACK)
        if (gp_in.buttons & Gamepad::BUTTON_MISC)  in_report_.buttons[0] |= XInput::Buttons0::BACK;

        // TURBO: Cuando L1 (LB) está activo, dispara EQUIS (A en XInput) en ráfaga
        if (gp_in.buttons & Gamepad::BUTTON_LB) 
        {
            turbo_tick++;
            if ((turbo_tick / 5) % 2 == 0) {
                in_report_.buttons[1] |= XInput::Buttons1::A; 
            }
        } else {
            turbo_tick = 0; 
        }

        // --- 3. BOTONES ESTÁNDAR ---
        if (gp_in.buttons & Gamepad::BUTTON_START) in_report_.buttons[0] |= XInput::Buttons0::START;
        if (gp_in.buttons & Gamepad::BUTTON_L3)    in_report_.buttons[0] |= XInput::Buttons0::L3;
        if (gp_in.buttons & Gamepad::BUTTON_R3)    in_report_.buttons[0] |= XInput::Buttons0::R3;
        if (gp_in.buttons & Gamepad::BUTTON_X)     in_report_.buttons[1] |= XInput::Buttons1::X;
        if (gp_in.buttons & Gamepad::BUTTON_A)     in_report_.buttons[1] |= XInput::Buttons1::A;
        if (gp_in.buttons & Gamepad::BUTTON_Y)     in_report_.buttons[1] |= XInput::Buttons1::Y;
        if (gp_in.buttons & Gamepad::BUTTON_B)     in_report_.buttons[1] |= XInput::Buttons1::B;
        if (gp_in.buttons & Gamepad::BUTTON_RB)    in_report_.buttons[1] |= XInput::Buttons1::RB;
        if (gp_in.buttons & Gamepad::BUTTON_SYS)   in_report_.buttons[1] |= XInput::Buttons1::HOME;

        // --- 4. GATILLOS DIGITALES (HAIR TRIGGER) ---
        // Si el gatillo se presiona más del 5% (13/255), manda 100% (255)
        in_report_.trigger_l = (gp_in.trigger_l > 13) ? 255 : 0;
        in_report_.trigger_r = (gp_in.trigger_r > 13) ? 255 : 0;

        // --- 5. STICKS ANALÓGICOS (AQUÍ ESTÁ LA MEJORA) ---
        
        // Configuración de Curvas (Igual que en PS4)
        // Gamma 1.6 Izq (Movimiento), 1.5 Der (Cámara) + Mezcla Lineal interna
        int16_t out_lx, out_ly, out_rx, out_ry;
        
        apply_stick_curve_mixed(gp_in.joystick_lx, Range::invert(gp_in.joystick_ly), 
                               0.05f, 1.6f, 1.10f, out_lx, out_ly); // Izquierdo
                               
        apply_stick_curve_mixed(gp_in.joystick_rx, Range::invert(gp_in.joystick_ry), 
                               0.05f, 1.5f, 1.10f, out_rx, out_ry); // Derecho

        // --- 6. AIM ASSIST ROTACIONAL (SHAKE) ---
        // Lógica: Si apuntas (L2) Y te estás moviendo (Stick Izq),
        // añade un micro-movimiento circular al Stick Derecho.
        // Esto activa la asistencia rotacional del juego.
        bool is_aiming = (in_report_.trigger_l == 255);
        bool is_moving = (abs(out_lx) > 4000 || abs(out_ly) > 4000); // 12% aprox

        if (is_aiming && is_moving) {
            constexpr float RADIUS = 280.0f; // Magnitud sutil (imperceptible al ojo, detectable por el juego)
            constexpr float SPEED = 0.35f;   // Velocidad de rotación
            
            aim_assist_angle += SPEED;
            if (aim_assist_angle > 6.2831f) aim_assist_angle -= 6.2831f;

            out_rx += (int16_t)(cos(aim_assist_angle) * RADIUS);
            out_ry += (int16_t)(sin(aim_assist_angle) * RADIUS);
            
            // Re-clamp por seguridad
            if (out_rx > 32767) out_rx = 32767; else if (out_rx < -32767) out_rx = -32767;
            if (out_ry > 32767) out_ry = 32767; else if (out_ry < -32767) out_ry = -32767;
        }

        // --- 7. ANTI-RECOIL ---
        // Se activa al apuntar y disparar (L2 + R2 digitales al máximo)
        if (in_report_.trigger_l == 255 && in_report_.trigger_r == 255) 
        {
            const int32_t force = 4500; // Valor de compensación hacia abajo
            int32_t calc_ry = (int32_t)out_ry - force;
            
            if (calc_ry > 32767) calc_ry = -32767; else if (calc_ry < -32767) calc_ry = -32767;
            out_ry = (int16_t)calc_ry;
        }

        // Asignar valores finales al reporte
        in_report_.joystick_lx = out_lx;
        in_report_.joystick_ly = out_ly;
        in_report_.joystick_rx = out_rx;
        in_report_.joystick_ry = out_ry;

        // --- 8. ENVÍO DE REPORTE ---
        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        tud_xinput::send_report((uint8_t*)&in_report_, sizeof(XInput::InReport));
    }

    // RUMBLE / VIBRACIÓN
    if (tud_xinput::receive_report(reinterpret_cast<uint8_t*>(&out_report_), sizeof(XInput::OutReport)) &&
        out_report_.report_id == XInput::OutReportID::RUMBLE)
    {
        Gamepad::PadOut gp_out;
        gp_out.rumble_l = out_report_.rumble_l;
        gp_out.rumble_r = out_report_.rumble_r;
        gamepad.set_pad_out(gp_out);
    }
}

// --- CALLBACKS REQUERIDOS ---
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
