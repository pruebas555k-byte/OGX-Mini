#ifndef _PS4_DESCRIPTORS_H_
#define _PS4_DESCRIPTORS_H_

#include <stdint.h>
#include <cstring>

namespace PS4
{
    static constexpr uint8_t JOYSTICK_MID = 0x80;
    static constexpr uint8_t COUNTER_MASK = 0x0F;
    static constexpr uint8_t DPAD_MASK    = 0x0F;

    // Botones y D-Pad tal y como los usa el host (USBHost/HostDriver/PS4.cpp)
    namespace Buttons0
    {
        // D-Pad en el nibble bajo
        static constexpr uint8_t DPAD_UP         = 0x00;
        static constexpr uint8_t DPAD_UP_RIGHT   = 0x01;
        static constexpr uint8_t DPAD_RIGHT      = 0x02;
        static constexpr uint8_t DPAD_RIGHT_DOWN = 0x03;
        static constexpr uint8_t DPAD_DOWN       = 0x04;
        static constexpr uint8_t DPAD_DOWN_LEFT  = 0x05;
        static constexpr uint8_t DPAD_LEFT       = 0x06;
        static constexpr uint8_t DPAD_LEFT_UP    = 0x07;

        static constexpr uint8_t SQUARE   = 0x10;
        static constexpr uint8_t CROSS    = 0x20;
        static constexpr uint8_t CIRCLE   = 0x40;
        static constexpr uint8_t TRIANGLE = 0x80;
    };

    namespace Buttons1
    {
        static constexpr uint8_t L1      = 0x01;
        static constexpr uint8_t R1      = 0x02;
        static constexpr uint8_t L2      = 0x04;
        static constexpr uint8_t R2      = 0x08;
        static constexpr uint8_t SHARE   = 0x10;
        static constexpr uint8_t OPTIONS = 0x20;
        static constexpr uint8_t L3      = 0x40;
        static constexpr uint8_t R3      = 0x80;
    };

    namespace Buttons2
    {
        static constexpr uint8_t PS = 0x01;
        static constexpr uint8_t TP = 0x02;
    };

    #pragma pack(push, 1)
    struct InReport
    {
        uint8_t report_id;
        uint8_t reserved0;

        uint8_t buttons[3];
        uint8_t counter;  // se usa con COUNTER_MASK

        uint8_t joystick_lx;
        uint8_t joystick_ly;
        uint8_t joystick_rx;
        uint8_t joystick_ry;

        uint8_t reserved1[2];

        uint8_t trigger_l;
        uint8_t trigger_r;

        uint8_t reserved2[50];

        InReport()
        {
            std::memset(this, 0, sizeof(InReport));
            report_id    = 0x01;
            joystick_lx  = JOYSTICK_MID;
            joystick_ly  = JOYSTICK_MID;
            joystick_rx  = JOYSTICK_MID;
            joystick_ry  = JOYSTICK_MID;
        }
    };
    static_assert(sizeof(InReport) == 64, "PS4::InReport size mismatch");

    struct OutReport
    {
        uint8_t report_id;

        uint8_t motor_left;
        uint8_t motor_right;
        uint8_t set_rumble;

        uint8_t set_led;
        uint8_t lightbar_red;
        uint8_t lightbar_green;
        uint8_t lightbar_blue;

        uint8_t reserved[56];

        OutReport()
        {
            std::memset(this, 0, sizeof(OutReport));
            report_id = 0x05;
        }
    };
    static_assert(sizeof(OutReport) == 64, "PS4::OutReport size mismatch");
    #pragma pack(pop)

} // namespace PS4

#endif // _PS4_DESCRIPTORS_H_
