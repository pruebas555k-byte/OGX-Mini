#ifndef _PS4_DEVICE_DESCRIPTORS_H_
#define _PS4_DEVICE_DESCRIPTORS_H_

#include <stdint.h>

namespace PS4Dev
{
    static constexpr uint8_t JOYSTICK_MID = 0x80;
    static constexpr uint8_t JOYSTICK_MIN = 0x00;
    static constexpr uint8_t JOYSTICK_MAX = 0xFF;

    // Valores de HAT (dpad) como en GP2040-CE
    enum Hat : uint8_t
    {
        HAT_UP        = 0x00,
        HAT_UP_RIGHT  = 0x01,
        HAT_RIGHT     = 0x02,
        HAT_DOWN_RIGHT= 0x03,
        HAT_DOWN      = 0x04,
        HAT_DOWN_LEFT = 0x05,
        HAT_LEFT      = 0x06,
        HAT_UP_LEFT   = 0x07,
        HAT_CENTER    = 0x0F, // Null state (sin pulsar)
    };

    struct __attribute__((packed)) TouchpadXY
    {
        uint8_t counter : 7;
        uint8_t unpressed : 1;

        // 12 bit X, seguido de 12 bit Y
        uint8_t data[3];

        void set_x(uint16_t x)
        {
            data[0] = x & 0xff;
            data[1] = (data[1] & 0xf0) | ((x >> 8) & 0xf);
        }

        void set_y(uint16_t y)
        {
            data[1] = (data[1] & 0x0f) | ((y & 0xf) << 4);
            data[2] = y >> 4;
        }
    };

    struct __attribute__((packed)) TouchpadData
    {
        TouchpadXY p1;
        TouchpadXY p2;
    };

    struct __attribute__((packed)) PSSensor
    {
        int16_t x;
        int16_t y;
        int16_t z;
    };

    struct __attribute__((packed)) PSSensorData
    {
        uint16_t battery;
        PSSensor gyroscope;
        PSSensor accelerometer;
        uint8_t misc[4];
        uint8_t powerLevel : 4;
        uint8_t charging : 1;
        uint8_t headphones : 1;
        uint8_t microphone : 1;
        uint8_t extension : 1;
        uint8_t extData0 : 1;
        uint8_t extData1 : 1;
        uint8_t notConnected : 1;
        uint8_t extData3 : 5;
        uint8_t misc2;
    };

    // ====== ESTE ES EL REPORTE PRINCIPAL (equivalente a PS4Report en GP2040-CE) ======
    struct __attribute__((packed)) InReport
    {
        uint8_t reportID;
        uint8_t leftStickX;
        uint8_t leftStickY;
        uint8_t rightStickX;
        uint8_t rightStickY;

        // 4 bits para el d-pad (hat)
        uint8_t dpad : 4;

        // 14 bits para botones (layout GP2040-CE)
        uint16_t buttonWest : 1;      // Square
        uint16_t buttonSouth : 1;     // Cross
        uint16_t buttonEast : 1;      // Circle
        uint16_t buttonNorth : 1;     // Triangle
        uint16_t buttonL1 : 1;
        uint16_t buttonR1 : 1;
        uint16_t buttonL2 : 1;
        uint16_t buttonR2 : 1;
        uint16_t buttonSelect : 1;    // Share
        uint16_t buttonStart : 1;     // Options
        uint16_t buttonL3 : 1;
        uint16_t buttonR3 : 1;
        uint16_t buttonHome : 1;      // PS
        uint16_t buttonTouchpad : 1;

        // 6 bits: contador de reportes
        uint8_t reportCounter : 6;

        // Los 2 bits que sobran se usan internamente como padding
        uint8_t leftTrigger : 8;
        uint8_t rightTrigger : 8;

        // Vendor specific block (54 bytes)
        union
        {
            uint8_t miscData[54];

            struct __attribute__((packed))
            {
                // 16 bit timing counter
                uint16_t axisTiming;

                PSSensorData sensorData;

                uint8_t touchpadActive : 2;
                uint8_t padding : 6;
                uint8_t tpadIncrement;
                TouchpadData touchpadData;

                uint8_t mystery2[21];
            } gamepad;

            struct __attribute__((packed))
            {
                uint8_t mystery0[22];

                uint8_t powerLevel : 4;
                uint8_t : 4;

                uint8_t mystery1[10];

                uint8_t pickup;
                uint8_t whammy;
                uint8_t tilt;

                union
                {
                    uint8_t fretValue;

                    struct __attribute__((packed))
                    {
                        uint8_t green : 1;
                        uint8_t red : 1;
                        uint8_t yellow : 1;
                        uint8_t blue : 1;
                        uint8_t orange : 1;
                        uint8_t : 3;
                    } frets;
                };

                union
                {
                    uint8_t soloFretValue;

                    struct __attribute__((packed))
                    {
                        uint8_t green : 1;
                        uint8_t red : 1;
                        uint8_t yellow : 1;
                        uint8_t blue : 1;
                        uint8_t orange : 1;
                        uint8_t : 3;
                    } soloFrets;
                };

                uint8_t mystery2[14];
            } guitar;

            struct __attribute__((packed))
            {
                uint8_t mystery0[22];

                uint8_t powerLevel : 4;
                uint8_t : 4;

                uint8_t mystery1[10];

                uint8_t velocityDrumRed;
                uint8_t velocityDrumBlue;
                uint8_t velocityDrumYellow;
                uint8_t velocityDrumGreen;

                uint8_t velocityCymbalYellow;
                uint8_t velocityCymbalBlue;
                uint8_t velocityCymbalGreen;

                uint8_t mystery2[12];
            } drums;

            struct __attribute__((packed))
            {
                uint8_t mystery0[22];

                uint8_t powerLevel : 4;
                uint8_t : 4;

                uint8_t mystery1[10];

                uint16_t joystickX;
                uint16_t joystickY;
                uint8_t twistRudder;
                uint8_t throttle;
                uint8_t rockerSwitch;

                uint8_t pedalRudder;
                uint8_t pedalLeft;
                uint8_t pedalRight;
            } hotas;

            struct __attribute__((packed))
            {
                uint8_t mystery0[22];

                uint8_t powerLevel : 4;
                uint8_t : 4;

                uint8_t mystery1[10];

                uint16_t steeringWheel;
                uint16_t gasPedal;
                uint16_t brakePedal;
                uint16_t clutchPedal; // ?

                union
                {
                    uint8_t shifterValue;

                    struct __attribute__((packed))
                    {
                        uint8_t shifterGear1 : 1;
                        uint8_t shifterGear2 : 1;
                        uint8_t shifterGear3 : 1;
                        uint8_t shifterGear4 : 1;
                        uint8_t shifterGear5 : 1;
                        uint8_t shifterGear6 : 1;
                        uint8_t shifterGearR : 1;
                        uint8_t : 1;
                    } shifter;
                };

                uint16_t unknownVal;

                uint8_t buttonDialEnter : 1;
                uint8_t buttonDialDown : 1;
                uint8_t buttonDialUp : 1;
                uint8_t buttonMinus : 1;
                uint8_t buttonPlus : 1;

                uint8_t : 3;

                uint8_t mystery2[7];
            } wheel;
        };
    };

    static_assert(sizeof(InReport) == 64, "PS4Dev::InReport debe medir 64 bytes");

    // -----------------------------
    // Strings USB (copiados de GP2040-CE)
    // -----------------------------
    static const uint8_t STRING_LANGUAGE[]     = { 0x09, 0x04 };
    static const uint8_t STRING_MANUFACTURER[] = "Open Stick Community";
    static const uint8_t STRING_PRODUCT[]      = "Wireless Controller (PS4)";
    static const uint8_t STRING_VERSION[]      = "1.0";

    static const uint8_t* const STRING_DESCRIPTORS[] =
    {
        STRING_LANGUAGE,
        STRING_MANUFACTURER,
        STRING_PRODUCT,
        STRING_VERSION
    };

    // -----------------------------
    // Device descriptor
    //   VID/PID = 0x1532:0x0401 (Razer Panthera) – igual que GP2040-CE PS4
    // -----------------------------
    static const uint8_t DEVICE_DESCRIPTORS[] =
    {
        0x12,       // bLength
        0x01,       // bDescriptorType (Device)
        0x00, 0x02, // bcdUSB 2.00
        0x00,       // bDeviceClass
        0x00,       // bDeviceSubClass
        0x00,       // bDeviceProtocol
        0x40,       // bMaxPacketSize0
        0x32, 0x15, // idVendor  0x1532
        0x01, 0x04, // idProduct 0x0401
        0x00, 0x01, // bcdDevice 1.00
        0x01,       // iManufacturer
        0x02,       // iProduct
        0x00,       // iSerialNumber
        0x01        // bNumConfigurations
    };

    // -----------------------------
    // HID report descriptor
    //   Copiado de ps4_report_descriptor de GP2040-CE
    //   Modificado para evitar múltiples Application Collections (ver notas)
    // -----------------------------
    static const uint8_t REPORT_DESCRIPTORS[] =
    {
        0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
        0x09, 0x05,        // Usage (Game Pad)
        0xA1, 0x01,        // Collection (Application)
        0x85, 0x01,        //   Report ID (1)
        0x09, 0x30,        //   Usage (X)
        0x09, 0x31,        //   Usage (Y)
        0x09, 0x32,        //   Usage (Z)
        0x09, 0x35,        //   Usage (Rz)
        0x15, 0x00,        //   Logical Minimum (0)
        0x26, 0xFF, 0x00,  //   Logical Maximum (255)
        0x75, 0x08,        //   Report Size (8)
        0x95, 0x04,        //   Report Count (4)
        0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

        0x09, 0x39,        //   Usage (Hat switch)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x07,        //   Logical Maximum (7)
        0x35, 0x00,        //   Physical Minimum (0)
        0x46, 0x3B, 0x01,  //   Physical Maximum (315)
        0x65, 0x14,        //   Unit (System: English Rotation, Length: Centimeter)
        0x75, 0x04,        //   Report Size (4)
        0x95, 0x01,        //   Report Count (1)
        0x81, 0x42,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)

        0x65, 0x00,        //   Unit (None)
        0x05, 0x09,        //   Usage Page (Button)
        0x19, 0x01,        //   Usage Minimum (0x01)
        0x29, 0x0E,        //   Usage Maximum (0x0E)
        0x15, 0x00,        //   Logical Minimum (0)
        0x25, 0x01,        //   Logical Maximum (1)
        0x75, 0x01,        //   Report Size (1)
        0x95, 0x0E,        //   Report Count (14)
        0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

        0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
        0x09, 0x20,        //   Usage (0x20)
        0x75, 0x06,        //   Report Size (6)
        0x95, 0x01,        //   Report Count (1)
        0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

        0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
        0x09, 0x33,        //   Usage (Rx)
        0x09, 0x34,        //   Usage (Ry)
        0x15, 0x00,        //   Logical Minimum (0)
        0x26, 0xFF, 0x00,  //   Logical Maximum (255)
        0x75, 0x08,        //   Report Size (8)
        0x95, 0x02,        //   Report Count (2)
        0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

        0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
        0x09, 0x21,        //   Usage (0x21)
        0x95, 0x36,        //   Report Count (54)
        0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)

        0x85, 0x05,        //   Report ID (5)
        0x09, 0x22,        //   Usage (0x22)
        0x95, 0x1F,        //   Report Count (31)
        0x91, 0x02,        //   Output (...)

        0x85, 0x03,        //   Report ID (3)
        0x0A, 0x21, 0x27,  //   Usage (0x2721)
        0x95, 0x2F,        //   Report Count (47)
        0xB1, 0x02,        //   Feature (...)

        0x85, 0x02,        //   Report ID (2)
        0x09, 0x24,        //   Usage (0x24)
        0x95, 0x24,        //   Report Count (36)
        0xB1, 0x02,        //   Feature
        0x85, 0x08,        //   Report ID (8)
        0x09, 0x25,        //   Usage (0x25)
        0x95, 0x03,        //   Report Count (3)
        0xB1, 0x02,        //   Feature
        0x85, 0x10,        //   Report ID (16)
        0x09, 0x26,        //   Usage (0x26)
        0x95, 0x04,        //   Report Count (4)
        0xB1, 0x02,        //   Feature
        0x85, 0x11,        //   Report ID (17)
        0x09, 0x27,        //   Usage (0x27)
        0x95, 0x02,        //   Report Count (2)
        0xB1, 0x02,        //   Feature
        0x85, 0x12,        //   Report ID (18)
        0x06, 0x02, 0xFF,  //   Usage Page (Vendor Defined 0xFF02)
        0x09, 0x21,        //   Usage (0x21)
        0x95, 0x0F,        //   Report Count (15)
        0xB1, 0x02,        //   Feature
        0x85, 0x13,        //   Report ID (19)
        0x09, 0x22,        //   Usage (0x22)
        0x95, 0x16,        //   Report Count (22)
        0xB1, 0x02,        //   Feature
        0x85, 0x14,        //   Report ID (20)
        0x06, 0x05, 0xFF,  //   Usage Page (Vendor Defined 0xFF05)
        0x09, 0x20,        //   Usage (0x20)
        0x95, 0x10,        //   Report Count (16)
        0xB1, 0x02,        //   Feature
        0x85, 0x15,        //   Report ID (21)
        0x09, 0x21,        //   Usage (0x21)
        0x95, 0x2C,        //   Report Count (44)
        0xB1, 0x02,        //   Feature
        0x06, 0x80, 0xFF,  //   Usage Page (Vendor Defined 0xFF80)
        0x85, 0x80,        //   Report ID (128)
        0x09, 0x20,        //   Usage (0x20)
        0x95, 0x06,        //   Report Count (6)
        0xB1, 0x02,        //   Feature
        0x85, 0x81,        //   Report ID (129)
        0x09, 0x21,        //   Usage (0x21)
        0x95, 0x06,        //   Report Count (6)
        0xB1, 0x02,        //   Feature
        0x85, 0x82,        //   Report ID (130)
        0x09, 0x22,        //   Usage (0x22)
        0x95, 0x05,        //   Report Count (5)
        0xB1, 0x02,        //   Feature
        0x85, 0x83,        //   Report ID (131)
        0x09, 0x23,        //   Usage (0x23)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0x84,        //   Report ID (132)
        0x09, 0x24,        //   Usage (0x24)
        0x95, 0x04,        //   Report Count (4)
        0xB1, 0x02,        //   Feature
        0x85, 0x85,        //   Report ID (133)
        0x09, 0x25,        //   Usage (0x25)
        0x95, 0x06,        //   Report Count (6)
        0xB1, 0x02,        //   Feature
        0x85, 0x86,        //   Report ID (134)
        0x09, 0x26,        //   Usage (0x26)
        0x95, 0x06,        //   Report Count (6)
        0xB1, 0x02,        //   Feature
        0x85, 0x87,        //   Report ID (135)
        0x09, 0x27,        //   Usage (0x27)
        0x95, 0x23,        //   Report Count (35)
        0xB1, 0x02,        //   Feature
        0x85, 0x88,        //   Report ID (136)
        0x09, 0x28,        //   Usage (0x28)
        0x95, 0x22,        //   Report Count (34)
        0xB1, 0x02,        //   Feature
        0x85, 0x89,        //   Report ID (137)
        0x09, 0x29,        //   Usage (0x29)
        0x95, 0x02,        //   Report Count (2)
        0xB1, 0x02,        //   Feature
        0x85, 0x90,        //   Report ID (144)
        0x09, 0x30,        //   Usage (0x30)
        0x95, 0x05,        //   Report Count (5)
        0xB1, 0x02,        //   Feature
        0x85, 0x91,        //   Report ID (145)
        0x09, 0x31,        //   Usage (0x31)
        0x95, 0x03,        //   Report Count (3)
        0xB1, 0x02,        //   Feature
        0x85, 0x92,        //   Report ID (146)
        0x09, 0x32,        //   Usage (0x32)
        0x95, 0x03,        //   Report Count (3)
        0xB1, 0x02,        //   Feature
        0x85, 0x93,        //   Report ID (147)
        0x09, 0x33,        //   Usage (0x33)
        0x95, 0x0C,        //   Report Count (12)
        0xB1, 0x02,        //   Feature
        0x85, 0xA0,        //   Report ID (160)
        0x09, 0x40,        //   Usage (0x40)
        0x95, 0x06,        //   Report Count (6)
        0xB1, 0x02,        //   Feature
        0x85, 0xA1,        //   Report ID (161)
        0x09, 0x41,        //   Usage (0x41)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xA2,        //   Report ID (162)
        0x09, 0x42,        //   Usage (0x42)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xA3,        //   Report ID (163)
        0x09, 0x43,        //   Usage (0x43)
        0x95, 0x30,        //   Report Count (48)
        0xB1, 0x02,        //   Feature
        0x85, 0xA4,        //   Report ID (164)
        0x09, 0x44,        //   Usage (0x44)
        0x95, 0x0D,        //   Report Count (13)
        0xB1, 0x02,        //   Feature
        0x85, 0xA5,        //   Report ID (165)
        0x09, 0x45,        //   Usage (0x45)
        0x95, 0x15,        //   Report Count (21)
        0xB1, 0x02,        //   Feature
        0x85, 0xA6,        //   Report ID (166)
        0x09, 0x46,        //   Usage (0x46)
        0x95, 0x15,        //   Report Count (21)
        0xB1, 0x02,        //   Feature
        0x85, 0xA7,        //   Report ID (247)
        0x09, 0x4A,        //   Usage (0x4A)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xA8,        //   Report ID (250)
        0x09, 0x4B,        //   Usage (0x4B)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xA9,        //   Report ID (251)
        0x09, 0x4C,        //   Usage (0x4C)
        0x95, 0x08,        //   Report Count (8)
        0xB1, 0x02,        //   Feature
        0x85, 0xAA,        //   Report ID (252)
        0x09, 0x4E,        //   Usage (0x4E)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xAB,        //   Report ID (253)
        0x09, 0x4F,        //   Usage (0x4F)
        0x95, 0x39,        //   Report Count (57)
        0xB1, 0x02,        //   Feature
        0x85, 0xAC,        //   Report ID (254)
        0x09, 0x50,        //   Usage (0x50)
        0x95, 0x39,        //   Report Count (57)
        0xB1, 0x02,        //   Feature
        0x85, 0xAD,        //   Report ID (255)
        0x09, 0x51,        //   Usage (0x51)
        0x95, 0x0B,        //   Report Count (11)
        0xB1, 0x02,        //   Feature
        0x85, 0xAE,        //   Report ID (256)
        0x09, 0x52,        //   Usage (0x52)
        0x95, 0x01,        //   Report Count (1)
        0xB1, 0x02,        //   Feature
        0x85, 0xAF,        //   Report ID (175)
        0x09, 0x53,        //   Usage (0x53)
        0x95, 0x02,        //   Report Count (2)
        0xB1, 0x02,        //   Feature
        0x85, 0xB0,        //   Report ID (176)
        0x09, 0x54,        //   Usage (0x54)
        0x95, 0x3F,        //   Report Count (63)
        0xB1, 0x02,        //   Feature
        // <- se quitó el 0xC0 que cerraba la primera Collection Application
        0x06, 0xF0, 0xFF,  // Usage Page (Vendor Defined 0xFFF0)
        0x09, 0x40,        // Usage (0x40)
        0xA1, 0x02,        //   Collection (Logical)  <-- antes era Application (ahora Logical para anidar)
        0x85, 0xF0,        //   Report ID (-16) AUTH F0
        0x09, 0x47,        //   Usage (0x47)
        0x95, 0x3F,        //   Report Count (63)
        0xB1, 0x02,        //   Feature
        0x85, 0xF1,        //   Report ID (-15) AUTH F1
        0x09, 0x48,        //   Usage (0x48)
        0x95, 0x3F,        //   Report Count (63)
        0xB1, 0x02,        //   Feature
        0x85, 0xF2,        //   Report ID (-14) AUTH F2
        0x09, 0x49,        //   Usage (0x49)
        0x95, 0x0F,        //   Report Count (15)
        0xB1, 0x02,        //   Feature
        0x85, 0xF3,        //   Report ID (-13) Auth F3 (Reset)
        0x0A, 0x01, 0x47,  //   Usage (0x4701)
        0x95, 0x07,        //   Report Count (7)
        0xB1, 0x02,        //   Feature
        0xC0,              // End Collection (cierra la Logical)
        0xC0,              // End Collection (cierra la Application principal)
    };

    // -----------------------------
    // Descriptor de configuración
    // -----------------------------
    static const uint8_t CONFIGURATION_DESCRIPTORS[] =
    {
        // Config
        0x09,        // bLength
        0x02,        // bDescriptorType (Configuration)
        0x29, 0x00,  // wTotalLength 41
        0x01,        // bNumInterfaces 1
        0x01,        // bConfigurationValue
        0x00,        // iConfiguration
        0x80,        // bmAttributes (Bus powered)
        0x32,        // bMaxPower (100mA)

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
        0x01,        // bInterval 1ms

        // Endpoint OUT (host -> device)
        0x07,        // bLength
        0x05,        // bDescriptorType (Endpoint)
        0x02,        // bEndpointAddress (EP2 OUT)
        0x03,        // bmAttributes (Interrupt)
        0x40, 0x00,  // wMaxPacketSize 64
        0x01,        // bInterval 1ms
    };

} // namespace PS4Dev

#endif // _PS4_DEVICE_DESCRIPTORS_H_
