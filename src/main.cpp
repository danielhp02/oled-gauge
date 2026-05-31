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

// Define display dimensions
#define DISP_WIDTH 128
#define DISP_HEIGHT 64

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

// Predeclare Functions
uint8_t centreTextPosX(char* text);
void renderText(char* text, char alignment, uint8_t y);
void renderHeading(char* headingText, const uint8_t* font, char alignment);
void renderValue(int value, const uint8_t* font, char alignment);
char checkButtonPress(int pin, int* button_state, int* last_button_state);

// Note on rotation:
//  - U8G2_R0: pins up
//  - U8G2_R2: pins down

// Create display object
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, spi_cs, spi_dc, spi_rst);

// Variables to track button presses
int button_state = 0;
int last_button_state = 0;
int counter = 0;

char heading[20] = "button";

void setup(void)
{
    // Initialise display
    u8g2.begin();

    // Initialise serial
    Serial.begin(115200);

    // Initialise button as an input
    pinMode(sw_pin, INPUT);
}

void loop(void)
{
    u8g2.firstPage();
    do
    {
        // Set heading 
        renderHeading(heading, u8g2_font_inr16_mr, 'c');

        // Check for button press
        if (checkButtonPress(sw_pin, &button_state, &last_button_state))
        {
            counter++;

            // log button press over serial
            Serial.println("Button pressed!");
        }

        // Display value
        renderValue(counter, u8g2_font_inr30_mr, 'c');

    } while (u8g2.nextPage());
}

// Return the x position of the left of a string to centre text
uint8_t centreTextPosX(char* text)
{
    return (DISP_WIDTH - u8g2.getStrWidth(text)) / 2;
}

// Render text assuming font and font pos have been set already
//  - Alignment can be 'l', 'c' or 'r'
void renderText(char* text, char alignment, uint8_t y)
{
    // Work out alignment
    uint8_t x = 0;

    switch (alignment)
    {
        case 'l':
            break;
        
        case 'c':
            x = centreTextPosX(text);
            break;
        
        case 'r':
            x = DISP_WIDTH - u8g2.getStrWidth(text);
            break;

        default: // Defaults to left aligned
            break;
    }

    // Render text
    u8g2.drawStr(x, y, text);
}

// Display the heading
//  - Alignment can be 'l', 'c' or 'r'
void renderHeading(char* headingText, const uint8_t* font, char alignment)
{
    // Set the font, and set reference point to top left of text
    u8g2.setFont(font);
    u8g2.setFontPosTop();

    // Set vertical position of text
    uint8_t y = 0;

    // Render text
    renderText(headingText, alignment, y);
}

// Render a value (int)
//  - Alignment can be 'l', 'c' or 'r'
void renderValue(int value, const uint8_t* font, char alignment)
{
    // Set the font, and set reference point to bottom left of text
    u8g2.setFont(font);
    u8g2.setFontPosBottom();

    // Convert value to char *
    char value_text[20];

    sprintf(value_text, "%d", value);

    // Set vertical position of text
    uint8_t y = DISP_HEIGHT;

    // Render text
    renderText(value_text, alignment, y);
}

// Check for a rising edge of a pushbutton on a given pin. Includes 15 ms debounce
char checkButtonPress(int pin, int* button_state, int* last_button_state)
{
    // Initialise return value to false
    char button_pressed = 0;

    // Read current state
    *button_state = digitalRead(pin);

    // Check if state has changed
    if (*button_state != *last_button_state)
    {
        // Check if the new state is HIGH (rising edge)
        if (*button_state == HIGH)
        {
            // Set return value to true
            button_pressed = 1;
        }

        // debounce
        delay(15); // ms
    }

    // Update previous state
    *last_button_state = *button_state;

    // Return whether a rising edge was detected or not
    return button_pressed;
}