#ifndef _PS4_DEVICE_DESCRIPTORS_H_
#define _PS4_DEVICE_DESCRIPTORS_H_

#include <stdint.h>
#include <cstring>

namespace PS4_DEV {

static constexpr uint8_t JOYSTICK_MID = 0x80;

namespace ReportID {
    static constexpr uint8_t INPUT = 0x01;
};

namespace Hat {
    static constexpr uint8_t DPAD_UP         = 0x00;
    static constexpr uint8_t DPAD_UP_RIGHT   = 0x01;
    static constexpr uint8_t DPAD_RIGHT      = 0x02;
    static constexpr uint8_t DPAD_DOWN_RIGHT = 0x03;
    static constexpr uint8_t DPAD_DOWN       = 0x04;
    static constexpr uint8_t DPAD_DOWN_LEFT  = 0x05;
    static constexpr uint8_t DPAD_LEFT       = 0x06;
    static constexpr uint8_t DPAD_UP_LEFT    = 0x07;
    static constexpr uint8_t DPAD_CENTER     = 0x08;
};

namespace Buttons0 {
    static constexpr uint8_t CROSS    = 0x01;
    static constexpr uint8_t CIRCLE   = 0x02;
    static constexpr uint8_t SQUARE   = 0x04;
    static constexpr uint8_t TRIANGLE = 0x08;
    static constexpr uint8_t L1       = 0x10;
    static constexpr uint8_t R1       = 0x20;
    static constexpr uint8_t L2       = 0x40;
    static constexpr uint8_t R2       = 0x80;
};

namespace Buttons1 {
    static constexpr uint8_t SHARE    = 0x01;
    static constexpr uint8_t OPTIONS  = 0x02;
    static constexpr uint8_t L3       = 0x04;
    static constexpr uint8_t R3       = 0x08;
    static constexpr uint8_t PS       = 0x10;
    static constexpr uint8_t TOUCHPAD = 0x20;
};

#pragma pack(push,1)
struct InReport {
    uint8_t report_id;
    uint8_t joystick_lx;
    uint8_t joystick_ly;
    uint8_t joystick_rx;
    uint8_t joystick_ry;
    uint8_t hat;
    uint8_t buttons0;
    uint8_t buttons1;
    uint8_t l2_axis;
    uint8_t r2_axis;
    uint8_t reserved[54];

    InReport() {
        std::memset(this, 0, sizeof(InReport));
        report_id   = ReportID::INPUT;
        joystick_lx = JOYSTICK_MID;
        joystick_ly = JOYSTICK_MID;
        joystick_rx = JOYSTICK_MID;
        joystick_ry = JOYSTICK_MID;
        hat         = Hat::DPAD_CENTER;
    }
};
static_assert(sizeof(InReport) == 64, "PS4_DEV::InReport size mismatch");

struct OutReport {
    uint8_t report_id;
    uint8_t reserved[63];

    OutReport() {
        std::memset(this, 0, sizeof(OutReport));
        report_id = 0x05;
    }
};
static_assert(sizeof(OutReport) == 64, "PS4_DEV::OutReport size mismatch");
#pragma pack(pop)

// ----- String descriptors -----
static const uint8_t STRING_LANGUAGE[]     = { 0x09, 0x04 };
static const uint8_t STRING_MANUFACTURER[] = "Sony Interactive Entertainment";
static const uint8_t STRING_PRODUCT[]      = "Wireless Controller";
static const uint8_t STRING_VERSION[]      = "1.0";

static const uint8_t *STRING_DESCRIPTORS[] __attribute__((unused)) = {
    STRING_LANGUAGE,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_VERSION
};

// ----- Device descriptor (054C:05C4 DualShock 4 v1) -----
static const uint8_t DEVICE_DESCRIPTORS[] = {
    0x12,        // bLength
    0x01,        // bDescriptorType (Device)
    0x00, 0x02,  // bcdUSB 2.00
    0x00,        // bDeviceClass
    0x00,        // bDeviceSubClass
    0x00,        // bDeviceProtocol
    0x40,        // bMaxPacketSize0
    0x4C, 0x05,  // idVendor 0x054C (Sony)
    0xC4, 0x05,  // idProduct 0x05C4 (DualShock 4 v1)
    0x00, 0x01,  // bcdDevice 1.00
    0x01,        // iManufacturer
    0x02,        // iProduct
    0x00,        // iSerialNumber
    0x01,        // bNumConfigurations
};

// ----- HID report descriptor -----
static const uint8_t REPORT_DESCRIPTORS[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Gamepad)
    0xA1, 0x01,        // Collection (Application)

    0x85, 0x01,        //   Report ID (1)

    // Sticks: LX, LY, RX, RY
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x00,        //   Logical Min (0)
    0x26, 0xFF, 0x00,  //   Logical Max (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Hat (D-Pad)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Min (0)
    0x25, 0x07,        //   Logical Max (7)
    0x35, 0x00,        //   Physical Min (0)
    0x46, 0x3B, 0x01,  //   Physical Max (315)
    0x65, 0x14,        //   Unit (Degrees)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    // Padding 4 bits
    0x75, 0x04,
    0x95, 0x01,
    0x81, 0x03,        //   Input (Const,Var,Abs)

    // 14 botones
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Min (Button 1)
    0x29, 0x0E,        //   Usage Max (Button 14)
    0x15, 0x00,        //   Logical Min (0)
    0x25, 0x01,        //   Logical Max (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Padding 2 bits
    0x75, 0x02,
    0x95, 0x01,
    0x81, 0x03,

    // Triggers analógicos como Z y Rz
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Min (0)
    0x26, 0xFF, 0x00,  //   Logical Max (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Relleno/vendor (54 bytes) para llegar a 64
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor 0xFF00)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x36,        //   Report Count (54)
    0x15, 0x00,        //   Logical Min (0)
    0x26, 0xFF, 0x00,  //   Logical Max (255)
    0x81, 0x03,        //   Input (Const,Var,Abs)

    0xC0,              // End Collection
};

// ----- Configuration descriptor -----
static const uint8_t CONFIGURATION_DESCRIPTORS[] = {
    0x09,        // bLength
    0x02,        // bDescriptorType (Configuration)
    0x29, 0x00,  // wTotalLength 41
    0x01,        // bNumInterfaces 1
    0x01,        // bConfigurationValue
    0x00,        // iConfiguration
    0x80,        // bmAttributes
    0xFA,        // bMaxPower 500mA

    // Interface
    0x09,        // bLength
    0x04,        // bDescriptorType (Interface)
    0x00,        // bInterfaceNumber
    0x00,        // bAlternateSetting
    0x02,        // bNumEndpoints 2
    0x03,        // bInterfaceClass (HID)
    0x00,        // bInterfaceSubClass
    0x00,        // bInterfaceProtocol
    0x00,        // iInterface

    // HID
    0x09,        // bLength
    0x21,        // bDescriptorType (HID)
    0x11, 0x01,  // bcdHID 1.11
    0x00,        // bCountryCode
    0x01,        // bNumDescriptors
    0x22,        // bDescriptorType (Report)
    static_cast<uint8_t>(sizeof(REPORT_DESCRIPTORS) & 0xFF),
    static_cast<uint8_t>((sizeof(REPORT_DESCRIPTORS) >> 8) & 0xFF),

    // Endpoint OUT
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x02,        // bEndpointAddress (OUT, EP2)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize 64
    0x01,        // bInterval 1ms

    // Endpoint IN
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x81,        // bEndpointAddress (IN, EP1)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize 64
    0x01,        // bInterval 1ms
};

} // namespace PS4_DEV

#endif // _PS4_DEVICE_DESCRIPTORS_H_
