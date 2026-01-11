#ifndef _PS4_DEVICE_DESCRIPTORS_H_
#define _PS4_DEVICE_DESCRIPTORS_H_

#include <stdint.h>
#include <cstring>

namespace PS4Dev {

    // Valor neutro para sticks
    static constexpr uint8_t JOYSTICK_MID = 0x80;

    // 16 bits de botones
    // Mapping (vista en Gamepad Tester):
    //  B0  -> CROSS  (X)
    //  B1  -> CIRCLE
    //  B2  -> SQUARE
    //  B3  -> TRIANGLE
    //  B4  -> L1
    //  B5  -> R1
    //  B6  -> L2 (digital)
    //  B7  -> R2 (digital)
    //  B8  -> SHARE / CREATE
    //  B9  -> OPTIONS
    //  B10 -> L3
    //  B11 -> R3
    //  B12 -> PS
    //  B13 -> TOUCHPAD
    //  B14 -> libre
    //  B15 -> libre
    namespace Buttons {
        static constexpr uint16_t CROSS     = 1u << 0;   // B0
        static constexpr uint16_t CIRCLE    = 1u << 1;   // B1
        static constexpr uint16_t SQUARE    = 1u << 2;   // B2
        static constexpr uint16_t TRIANGLE  = 1u << 3;   // B3
        static constexpr uint16_t L1        = 1u << 4;   // B4
        static constexpr uint16_t R1        = 1u << 5;   // B5
        static constexpr uint16_t L2        = 1u << 6;   // B6 (digital)
        static constexpr uint16_t R2        = 1u << 7;   // B7 (digital)
        static constexpr uint16_t SHARE     = 1u << 8;   // B8
        static constexpr uint16_t OPTIONS   = 1u << 9;   // B9
        static constexpr uint16_t L3        = 1u << 10;  // B10
        static constexpr uint16_t R3        = 1u << 11;  // B11
        static constexpr uint16_t PS        = 1u << 12;  // B16
        static constexpr uint16_t TOUCHPAD  = 1u << 13;  // B17
        // bits 14 y 15 libres
    }

    // Hat: 0–7 direcciones, 8 = centrado
    namespace Hat {
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

    #pragma pack(push, 1)
    // Report de entrada: 10 bytes
    struct InReport {
        uint8_t  report_id;    // siempre 1
        uint16_t buttons;      // 16 botones (ver arriba)
        uint8_t  hat;          // 0–7 direcciones, 8 = centrado
        uint8_t  joystick_lx;  // 0–255
        uint8_t  joystick_ly;  // 0–255
        uint8_t  joystick_rx;  // 0–255
        uint8_t  joystick_ry;  // 0–255
        uint8_t  trigger_l;    // L2 analógico 0–255
        uint8_t  trigger_r;    // R2 analógico 0–255

        InReport() {
            std::memset(this, 0, sizeof(InReport));
            report_id   = 0x01;
            hat         = Hat::CENTER;
            joystick_lx = JOYSTICK_MID;
            joystick_ly = JOYSTICK_MID;
            joystick_rx = JOYSTICK_MID;
            joystick_ry = JOYSTICK_MID;
            trigger_l   = 0;
            trigger_r   = 0;
        }
    };
    static_assert(sizeof(InReport) == 10, "PS4Dev::InReport size mismatch");
    #pragma pack(pop)

    // Cadenas (simples, como en los otros descriptors del proyecto)
    static const uint8_t STRING_LANGUAGE[]     = { 0x09, 0x04 }; // EN-US
    static const uint8_t STRING_MANUFACTURER[] = "Sony";
    static const uint8_t STRING_PRODUCT[]      = "Wireless Controller";
    static const uint8_t STRING_VERSION[]      = "1.0";

    static const uint8_t* STRING_DESCRIPTORS[] __attribute__((unused)) = {
        STRING_LANGUAGE,
        STRING_MANUFACTURER,
        STRING_PRODUCT,
        STRING_VERSION
    };

    // Device descriptor
    // VID Sony (0x054C), PID "custom" 0x2001 para evitar que Steam lo trate como DS4 real.
    static const uint8_t DEVICE_DESCRIPTORS[] = {
        0x12,        // bLength
        0x01,        // bDescriptorType (Device)
        0x00, 0x02,  // bcdUSB 2.00
        0x00,        // bDeviceClass
        0x00,        // bDeviceSubClass
        0x00,        // bDeviceProtocol
        0x40,        // bMaxPacketSize0 (64)
        0x4C, 0x05,  // idVendor 0x054C (Sony)
        0x01, 0x20,  // idProduct 0x2001 (personalizado)
        0x00, 0x01,  // bcdDevice
        0x01,        // iManufacturer
        0x02,        // iProduct
        0x00,        // iSerialNumber
        0x01,        // bNumConfigurations
    };

    // HID Report Descriptor (95 bytes, 0x5F)
    static const uint8_t REPORT_DESCRIPTORS[] = {
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x05,       // Usage (Game Pad)
        0xA1, 0x01,       // Collection (Application)

        0x85, 0x01,       //   Report ID (1)

        // 16 botones
        0x05, 0x09,       //   Usage Page (Button)
        0x19, 0x01,       //   Usage Minimum (1)
        0x29, 0x10,       //   Usage Maximum (16)
        0x15, 0x00,       //   Logical Minimum (0)
        0x25, 0x01,       //   Logical Maximum (1)
        0x75, 0x01,       //   Report Size (1)
        0x95, 0x10,       //   Report Count (16)
        0x81, 0x02,       //   Input (Data,Var,Abs)

        // Hat switch (D-Pad)
        0x05, 0x01,       //   Usage Page (Generic Desktop)
        0x09, 0x39,       //   Usage (Hat switch)
        0x15, 0x00,       //   Logical Minimum (0)
        0x25, 0x07,       //   Logical Maximum (7)
        0x35, 0x00,       //   Physical Minimum (0)
        0x46, 0x3B, 0x01, //   Physical Maximum (315)
        0x65, 0x14,       //   Unit (Degrees)
        0x75, 0x04,       //   Report Size (4)
        0x95, 0x01,       //   Report Count (1)
        0x81, 0x42,       //   Input (Data,Var,Abs,Null State)

        // Relleno 4 bits
        0x75, 0x04,       //   Report Size (4)
        0x95, 0x01,       //   Report Count (1)
        0x81, 0x03,       //   Input (Const,Var,Abs)

        // 4 ejes de sticks: LX, LY, RX, RY (0–255)
        0x05, 0x01,       //   Usage Page (Generic Desktop)
        0x09, 0x30,       //   Usage (X)
        0x09, 0x31,       //   Usage (Y)
        0x09, 0x32,       //   Usage (Z)
        0x09, 0x35,       //   Usage (Rz)
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x35, 0x00,       //   Physical Minimum (0)
        0x46, 0xFF, 0x00, //   Physical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x04,       //   Report Count (4)
        0x81, 0x02,       //   Input (Data,Var,Abs)

        // Triggers L2/R2 analógicos (0–255)
        0x05, 0x02,       //   Usage Page (Simulation Controls)
        0x09, 0xC4,       //   Usage (Accelerator) -> L2
        0x09, 0xC5,       //   Usage (Brake)       -> R2
        0x15, 0x00,       //   Logical Minimum (0)
        0x26, 0xFF, 0x00, //   Logical Maximum (255)
        0x75, 0x08,       //   Report Size (8)
        0x95, 0x02,       //   Report Count (2)
        0x81, 0x02,       //   Input (Data,Var,Abs)

        0xC0              // End Collection
    };

    // Config descriptor (1 interfaz HID, 2 endpoints)
    static const uint8_t CONFIGURATION_DESCRIPTORS[] = {
        // Configuration
        0x09,        // bLength
        0x02,        // bDescriptorType (Configuration)
        0x29, 0x00,  // wTotalLength 41
        0x01,        // bNumInterfaces 1
        0x01,        // bConfigurationValue
        0x00,        // iConfiguration
        0x80,        // bmAttributes (Bus powered)
        0xFA,        // bMaxPower 500mA

        // Interface 0, alt 0
        0x09,        // bLength
        0x04,        // bDescriptorType (Interface)
        0x00,        // bInterfaceNumber 0
        0x00,        // bAlternateSetting
        0x02,        // bNumEndpoints 2 (IN + OUT)
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
        0x22,        // bDescriptorType[0] (Report)
        0x5F, 0x00,  // wDescriptorLength[0] 95 bytes

        // Endpoint OUT (host -> device)
        0x07,        // bLength
        0x05,        // bDescriptorType (Endpoint)
        0x02,        // bEndpointAddress (EP2 OUT)
        0x03,        // bmAttributes (Interrupt)
        0x40, 0x00,  // wMaxPacketSize 64
        0x01,        // bInterval 1

        // Endpoint IN (device -> host)
        0x07,        // bLength
        0x05,        // bDescriptorType (Endpoint)
        0x81,        // bEndpointAddress (EP1 IN)
        0x03,        // bmAttributes (Interrupt)
        0x40, 0x00,  // wMaxPacketSize 64
        0x01,        // bInterval 1
    };

} // namespace PS4Dev

#endif // _PS4_DEVICE_DESCRIPTORS_H_
