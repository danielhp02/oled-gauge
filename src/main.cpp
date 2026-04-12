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

// Setup SPI pins
#define SPI_CLK   13
#define SPI_DATA  11
#define SPI_CS    10
#define SPI_DC    9

// Setup input pins
#define SW_PIN 2

// Note on rotation: 
//  - U8G2_R0: pins up
//  - U8G2_R2

// Create display object
U8G2_SH1106_128X64_NONAME_1_4W_HW_SPI u8g2(U8G2_R2, SPI_CS, SPI_DC);

// Variables to track button presses
int button_state = 0;
int last_button_state = 0;
int counter = 0;
char counter_text[20];

const char *heading = "button";

void setup(void) {
  // Initialise display
  u8g2.begin();

  // Initialise button
  pinMode(SW_PIN, INPUT);
}

void loop(void) {
  u8g2.firstPage();
  do {
    // Set heading
    u8g2.setFont(u8g2_font_inr16_mr);
    u8g2.setFontPosTop();
    u8g2.drawStr(64-u8g2.getStrWidth(heading)/2, 0, heading);

    // check for button press
    button_state = digitalRead(SW_PIN);
    if (button_state != last_button_state) {
      if (button_state == HIGH) { // rising edge
        counter++;
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
    u8g2.drawStr(64-u8g2.getStrWidth(counter_text)/2,64,counter_text);
  } while ( u8g2.nextPage() );
}