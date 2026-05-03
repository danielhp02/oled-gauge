// gauge
// 128x64 monochrome OLED display for output of data from an ELM327 OBDII dongle

// Display docs: https://github.com/olikraus/u8g2/wiki
// Display wired up as: https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf

// Font Reference:
// - u8g2_font_scrum_tr: nice general font, 15px tall
// - u8g2_font_inr30_mr: inconsolata, 30px tall (also 16, 19, 21, 24, 27)

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>

// Setup I/O pins
#if defined(CONFIG_IDF_TARGET_ESP32C3)
    // ESP32 C3 pins
    // SPI
    constexpr int spi_clk   = SCK;       // 4
    constexpr int spi_data  = MOSI;      // 6
    constexpr int spi_rst   = 9;
    constexpr int spi_cs    = SS;        // 7
    constexpr int spi_dc    = 8;

    // Input
    constexpr int sw_pin = 3;

#elif defined(ARDUINO_AVR_UNO)
    // Arduino Uno pins (for testing)
    // SPI
    constexpr int spi_clk = 13;
    constexpr int spi_data = 11;
    constexpr int spi_rst = 1;
    constexpr int spi_cs = 10;
    constexpr int spi_dc = 9;

    // Input
    constexpr int sw_pin = 2;

#else
    // undefined board, throw an error
    #error "Untested board. Please define pins."
#endif

// Note on rotation:
//  - U8G2_R0: pins up
//  - U8G2_R2: pins down

// Create display object
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, spi_cs, spi_dc, spi_rst);

// Variables to track button presses
int button_state = 0;
int last_button_state = 0;
int counter = 0;
char counter_text[20];

const char *heading = "button";

void setup(void)
{
    // Initialise display
    u8g2.begin();

    // Initialise serial
    Serial.begin(115200);

    // Initialise button
    pinMode(sw_pin, INPUT);
}

void loop(void)
{
    u8g2.firstPage();
    do
    {
        // Set heading
        u8g2.setFont(u8g2_font_inr16_mr);
        u8g2.setFontPosTop();
        u8g2.drawStr(64 - u8g2.getStrWidth(heading) / 2, 0, heading);

        // check for button press
        button_state = digitalRead(sw_pin);
        if (button_state != last_button_state)
        {
            if (button_state == HIGH)
            { // rising edge
                counter++;
                Serial.println("Button pressed!");
            }
            delay(15); // debounce
        }

        last_button_state = button_state;

        // counter to text
        sprintf(counter_text, "%d", counter);

        u8g2.setFont(u8g2_font_inr30_mr);
        u8g2.setFontPosBottom();
        // u8g2.drawStr(0,30,"Hello World!");

        // Render number of keypresses centre aligned horizontally
        u8g2.drawStr(64 - u8g2.getStrWidth(counter_text) / 2, 64, counter_text);
    } while (u8g2.nextPage());
}