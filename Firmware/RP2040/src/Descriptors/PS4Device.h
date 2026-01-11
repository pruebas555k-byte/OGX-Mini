#ifndef _PS4_DEVICE_DESCRIPTORS_H_
#define _PS4_DEVICE_DESCRIPTORS_H_

#include <stdint.h>
#include <cstring>

namespace PS4Dev
{
    // Valor central para los sticks (0–255)
    static constexpr uint8_t JOYSTICK_MID = 0x80;

    // ---------------------------------
    // Botones (16 bits)
    // ---------------------------------
    namespace Buttons
    {
        static constexpr uint16_t SQUARE    = 1u << 0;   // B0
        static constexpr uint16_t CROSS     = 1u << 1;   // B1
        static constexpr uint16_t CIRCLE    = 1u << 2;   // B2
        static constexpr uint16_t TRIANGLE  = 1u << 3;   // B3

        static constexpr uint16_t L1        = 1u << 4;   // B4
        static constexpr uint16_t R1        = 1u << 5;   // B5
        static constexpr uint16_t L2_DIG    = 1u << 6;   // B6
        static constexpr uint16_t R2_DIG    = 1u << 7;   // B7

        static constexpr uint16_t SHARE     = 1u << 8;   // B8
        static constexpr uint16_t OPTIONS   = 1u << 9;   // B9
        static constexpr uint16_t L3        = 1u << 10;  // B10
        static constexpr uint16_t R3        = 1u << 11;  // B11
        static constexpr uint16_t PS        = 1u << 12;  // B12
        static constexpr uint16_t TOUCHPAD  = 1u << 13;  // B13
    }

    // ---------------------------------
    // Hat (D-pad)
    // ---------------------------------
    namespace Hat
    {
        static constexpr uint8_t UP         = 0x00;
        static constexpr uint8_t UP_RIGHT   = 0x01;
        static constexpr uint8_t RIGHT      = 0x02;
        static constexpr uint8_t DOWN_RIGHT = 0x03;
        static constexpr uint8_t DOWN       = 0x04;
        static constexpr uint8_t DOWN_LEFT  = 0x05;
        static constexpr uint8_t LEFT       = 0x06;
        static constexpr uint8_t UP_LEFT    = 0x07;
        static constexpr uint8_t CENTER     = 0x08;
    }

    // ---------------------------------
    // Reporte de ENTRADA EXTENDIDO (más parecido al DS4 real)
    // 64 bytes para máxima compatibilidad
    // ---------------------------------
    #pragma pack(push, 1)
    struct InReport
    {
        uint8_t  report_id;        // 0x01
        uint8_t  joystick_lx;      // stick izquierdo X
        uint8_t  joystick_ly;      // stick izquierdo Y
        uint8_t  joystick_rx;      // stick derecho X
        uint8_t  joystick_ry;      // stick derecho Y
        
        uint8_t  buttons1;         // botones byte 1
        uint8_t  buttons2;         // botones byte 2
        uint8_t  buttons3;         // botones byte 3
        
        uint8_t  trigger_l;        // L2 analógico
        uint8_t  trigger_r;        // R2 analógico
        
        // Datos adicionales que espera un DS4 real
        uint8_t  timestamp_lo;     // timestamp byte bajo
        uint8_t  timestamp_hi;     // timestamp byte alto
        uint8_t  battery;          // nivel de batería
        
        uint8_t  gyro_x_lo;        // giroscopio X (low byte)
        uint8_t  gyro_x_hi;        // giroscopio X (high byte)
        uint8_t  gyro_y_lo;        // giroscopio Y (low byte)
        uint8_t  gyro_y_hi;        // giroscopio Y (high byte)
        uint8_t  gyro_z_lo;        // giroscopio Z (low byte)
        uint8_t  gyro_z_hi;        // giroscopio Z (high byte)
        
        uint8_t  accel_x_lo;       // acelerómetro X (low byte)
        uint8_t  accel_x_hi;       // acelerómetro X (high byte)
        uint8_t  accel_y_lo;       // acelerómetro Y (low byte)
        uint8_t  accel_y_hi;       // acelerómetro Y (high byte)
        uint8_t  accel_z_lo;       // acelerómetro Z (low byte)
        uint8_t  accel_z_hi;       // acelerómetro Z (high byte)
        
        uint8_t  reserved[5];      // bytes reservados
        uint8_t  ext_status;       // estado extendido
        uint8_t  reserved2[2];
        uint8_t  num_touches;      // número de toques touchpad
        uint8_t  touch_data[9];    // datos del touchpad
        uint8_t  padding[21];      // padding hasta 64 bytes (ajustado)

        InReport()
        {
            std::memset(this, 0, sizeof(InReport));
            report_id   = 0x01;
            joystick_lx = JOYSTICK_MID;
            joystick_ly = JOYSTICK_MID;
            joystick_rx = JOYSTICK_MID;
            joystick_ry = JOYSTICK_MID;
            
            // D-pad centrado (bits 0-3 del buttons1)
            buttons1 = 0x08; // hat centrado
            buttons2 = 0x00;
            buttons3 = 0x00;
            
            trigger_l = 0;
            trigger_r = 0;
            
            timestamp_lo = 0;
            timestamp_hi = 0;
            battery = 0xFF; // batería llena
            
            // Sensores en reposo
            gyro_x_lo = gyro_x_hi = 0;
            gyro_y_lo = gyro_y_hi = 0;
            gyro_z_lo = gyro_z_hi = 0;
            
            accel_x_lo = accel_x_hi = 0;
            accel_y_lo = accel_y_hi = 0;
            accel_z_lo = 0x00;  // gravedad Z
            accel_z_hi = 0x20;  // = 8192
            
            num_touches = 0;
        }
        
        // Función helper para setear botones desde el formato anterior
        // Compatibilidad con código anterior que usa .buttons
        void setButtons(uint16_t btn)
        {
            // Los primeros 4 bits de buttons1 son el HAT (D-pad)
            // Mantenemos esos bits y agregamos los botones
            uint8_t hat = buttons1 & 0x0F;
            
            buttons1 = hat | 
                      ((btn & Buttons::SQUARE)   ? 0x10 : 0) |
                      ((btn & Buttons::CROSS)    ? 0x20 : 0) |
                      ((btn & Buttons::CIRCLE)   ? 0x40 : 0) |
                      ((btn & Buttons::TRIANGLE) ? 0x80 : 0);
            
            buttons2 = ((btn & Buttons::L1)      ? 0x01 : 0) |
                      ((btn & Buttons::R1)      ? 0x02 : 0) |
                      ((btn & Buttons::L2_DIG)  ? 0x04 : 0) |
                      ((btn & Buttons::R2_DIG)  ? 0x08 : 0) |
                      ((btn & Buttons::SHARE)   ? 0x10 : 0) |
                      ((btn & Buttons::OPTIONS) ? 0x20 : 0) |
                      ((btn & Buttons::L3)      ? 0x40 : 0) |
                      ((btn & Buttons::R3)      ? 0x80 : 0);
            
            buttons3 = ((btn & Buttons::PS)       ? 0x01 : 0) |
                      ((btn & Buttons::TOUCHPAD) ? 0x02 : 0);
        }
        
        // Propiedad helper para compatibilidad con código anterior
        uint16_t& buttons = *reinterpret_cast<uint16_t*>(&buttons1);
        
        // Propiedad helper para .hat
        uint8_t getHat() const { return buttons1 & 0x0F; }
        void setHat(uint8_t hat_value)
        {
            // Preservar botones, actualizar solo HAT (bits 0-3)
            buttons1 = (buttons1 & 0xF0) | (hat_value & 0x0F);
        }
        
        // Propiedad compatible con acceso directo a .hat
        struct HatProxy {
            InReport& parent;
            HatProxy(InReport& p) : parent(p) {}
            operator uint8_t() const { return parent.getHat(); }
            HatProxy& operator=(uint8_t val) { parent.setHat(val); return *this; }
        };
        HatProxy hat{*this};
    };
    static_assert(sizeof(InReport) == 64, "PS4Dev::InReport debe ser 64 bytes");
    #pragma pack(pop)

    // ---------------------------------
    // Strings USB
    // ---------------------------------
    static const uint8_t STRING_LANGUAGE[]     = { 0x09, 0x04 };
    static const char STRING_MANUFACTURER[]    = "Sony Interactive Entertainment";
    static const char STRING_PRODUCT[]         = "Wireless Controller";
    static const char STRING_VERSION[]         = "1.0";
    
    // Array de punteros para compatibilidad
    static const char* const STRING_DESCRIPTORS[] = {
        reinterpret_cast<const char*>(STRING_LANGUAGE),
        STRING_MANUFACTURER,
        STRING_PRODUCT,
        STRING_VERSION
    };

    // ---------------------------------
    // Descriptor de dispositivo
    // VID/PID = 054C:09CC (DualShock 4 v2)
    // ---------------------------------
    static const uint8_t DEVICE_DESCRIPTORS[] =
    {
        0x12,        // bLength
        0x01,        // bDescriptorType (Device)
        0x00, 0x02,  // bcdUSB 2.00
        0x00,        // bDeviceClass
        0x00,        // bDeviceSubClass
        0x00,        // bDeviceProtocol
        0x40,        // bMaxPacketSize0 64
        0x4C, 0x05,  // idVendor  0x054C (Sony)
        0xCC, 0x09,  // idProduct 0x09CC (DS4 v2)
        0x00, 0x01,  // bcdDevice 1.00
        0x01,        // iManufacturer
        0x02,        // iProduct
        0x03,        // iSerialNumber
        0x01,        // bNumConfigurations
    };

    // ---------------------------------
    // Descriptor HID - Compatible con DS4 real
    // ---------------------------------
    static const uint8_t REPORT_DESCRIPTORS[] =
    {
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x05,       // Usage (Game Pad)
        0xA1, 0x01,       // Collection (Application)
        
        0x85, 0x01,       //   Report ID (1)
        
        // Sticks (X, Y, Z, Rz)
        0x09, 0x30,       //   Usage (X)
        0x09, 0x31,       //   Usage (Y)
        0x09, 0x32,       //   Usage (Z)
        0x09, 0x35,       //   Usage (Rz)
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x04,       //   Report Count (4)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        
        // D-pad + botones byte 1
        0x05, 0x09,       //   Usage Page (Button)
        0x19, 0x01,       //   Usage Minimum (1)
        0x29, 0x0E,       //   Usage Maximum (14)
        0x15, 0x00,       //   Logical Minimum (0)
        0x25, 0x01,       //   Logical Maximum (1)
        0x75, 0x01,       //   Report Size (1)
        0x95, 0x0E,       //   Report Count (14)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        
        // Relleno
        0x75, 0x01,       //   Report Size (1)
        0x95, 0x02,       //   Report Count (2)
        0x81, 0x03,       //   Input (Const,Var,Abs)
        
        // Hat Switch
        0x05, 0x01,       //   Usage Page (Generic Desktop)
        0x09, 0x39,       //   Usage (Hat switch)
        0x15, 0x00,       //   Logical Minimum (0)
        0x25, 0x07,       //   Logical Maximum (7)
        0x46, 0x3B, 0x01, //   Physical Maximum (315)
        0x65, 0x14,       //   Unit (Degrees)
        0x75, 0x04,       //   Report Size (4)
        0x95, 0x01,       //   Report Count (1)
        0x81, 0x42,       //   Input (Data,Var,Abs,Null)
        
        // Triggers
        0x65, 0x00,       //   Unit (None)
        0x05, 0x09,       //   Usage Page (Button)
        0x19, 0x01,       //   Usage Minimum (1)
        0x29, 0x0F,       //   Usage Maximum (15)
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x02,       //   Report Count (2)
        0x81, 0x02,       //   Input (Data,Var,Abs)
        
        // Timestamp, batería, sensores, etc (resto del reporte)
        0x06, 0x00, 0xFF, //   Usage Page (Vendor)
        0x09, 0x20,       //   Usage (Vendor)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x36,       //   Report Count (54) - resto hasta 64 bytes
        0x81, 0x02,       //   Input (Data,Var,Abs)
        
        0xC0              // End Collection
    };

    // ---------------------------------
    // Descriptor de configuración
    // ---------------------------------
    static const uint8_t CONFIGURATION_DESCRIPTORS[] =
    {
        // Config
        0x09,        // bLength
        0x02,        // bDescriptorType (Configuration)
        0x29, 0x00,  // wTotalLength 41
        0x01,        // bNumInterfaces 1
        0x01,        // bConfigurationValue
        0x00,        // iConfiguration
        0xC0,        // bmAttributes (Self powered, Remote wakeup)
        0xFA,        // bMaxPower 500mA

        // Interface 0
        0x09,        // bLength
        0x04,        // bDescriptorType (Interface)
        0x00,        // bInterfaceNumber 0
        0x00,        // bAlternateSetting
        0x02,        // bNumEndpoints 2
        0x03,        // bInterfaceClass (HID)
        0x00,        // bInterfaceSubClass
        0x00,        // bInterfaceProtocol
        0x00,        // iInterface

        // HID descriptor
        0x09,        // bLength
        0x21,        // bDescriptorType (HID)
        0x11, 0x01,  // bcdHID 1.11
        0x00,        // bCountryCode
        0x01,        // bNumDescriptors
        0x22,        // bDescriptorType (Report)
        sizeof(REPORT_DESCRIPTORS) & 0xFF,
        (sizeof(REPORT_DESCRIPTORS) >> 8) & 0xFF,

        // Endpoint IN (device -> host)
        0x07,        // bLength
        0x05,        // bDescriptorType (Endpoint)
        0x81,        // bEndpointAddress (EP1 IN)
        0x03,        // bmAttributes (Interrupt)
        0x40, 0x00,  // wMaxPacketSize 64
        0x05,        // bInterval 5ms

        // Endpoint OUT (host -> device)
        0x07,        // bLength
        0x05,        // bDescriptorType (Endpoint)
        0x02,        // bEndpointAddress (EP2 OUT)
        0x03,        // bmAttributes (Interrupt)
        0x40, 0x00,  // wMaxPacketSize 64
        0x05,        // bInterval 5ms
    };

} // namespace PS4Dev

#endif // _PS4_DEVICE_DESCRIPTORS_H_
