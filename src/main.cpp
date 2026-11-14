// gauge
// 128x64 monochrome OLED display for output of data from an ELM327 OBDII dongle
// using BLEDevice as a client

// Display docs: https://github.com/olikraus/u8g2/wiki
// Display wired up as: https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf

// Font Reference:
// - u8g2_font_scrum_tr: nice general font, 15px tall
// - u8g2_font_inr30_mr: inconsolata, 30px tall (also 16, 19, 21, 24, 27)
// - u8g2_font_scrum_tf: scrum, 8px tall

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>

#include <cstring>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

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
uint8_t centreTextPosX(const char* text);
void renderText(const char* text, char alignment, uint8_t y);
void renderHeading(const char* headingText, const uint8_t* font, char alignment);
void renderValue(int value, const uint8_t* font, char alignment);
void renderStatus(const char* statusText);

char checkButtonPress(int pin, int* button_state, int* last_button_state);
void updateValueDisplay(char* heading, int value);
void updateStatusDisplay(const char* status);

bool connectToReader();

void requestData();
void displayData(uint8_t *rawData, bool oldData);

// Bluetooth and connection constants
int scanTime = 5; // in seconds
BLEScan* pBLEScan;

static boolean doScan = true;
static boolean doConnect = false;
static boolean connected = false;
static BLEAdvertisedDevice *obdDevice = nullptr;
static BLEClient *pClient = nullptr;

static BLERemoteService *pService = nullptr;
static BLERemoteCharacteristic *pTxCharacteristic = nullptr;
static BLERemoteCharacteristic *pRxCharacteristic = nullptr;

static bool waitingForResponse = false;
static bool displayLock = false;

// ------------------ Callback functions ---------------------------------------
// Callback class for Bluetooth
class MyCallbacks: public BLEAdvertisedDeviceCallbacks
{
  // Callback for when a device is found
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    std::string deviceName = advertisedDevice.getName();
    Serial.printf("device name: %s \n", deviceName.c_str());

    if (advertisedDevice.getName() == "OBDII")
    {
      Serial.printf("\nOBD device found\n");

      // stop scanning and connect
      BLEDevice::getScan()->stop();
      obdDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    }
  }
};

// Client callback class to handle connection events
class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
    Serial.println("Connected to OBDII reader");
  }

  void onDisconnect(BLEClient *pclient)
  { 
    connected = false;
    doScan = true;
    Serial.println("Disconnected from OBDII reader");
  }
};

// Callback for received data
void notifyCallback(BLERemoteCharacteristic* characteristic, uint8_t* data, size_t length, bool isNotify)
{
    Serial.print("Rx: ");
    Serial.write(data, length);
    // Serial.printf(", length: %d", length);
    Serial.println();

    // Update display if correct response (i.e. to mode 1 request)
    if (data[0] == '4' && data[1] == '1')
    {
        displayData(data, false);
        displayLock = false;
    }
    // Otherwise check if OBD reader is unable to communicate with the vehicle
    else if (strstr((const char*)data, "NO DATA"))
    {
        updateStatusDisplay("no data");
        displayLock = true;
    }

    // Check if OBDII reader has responded with '>', indicating it is ready for another command
    // Check first two characters in case this message does not contain other data
    for (int i = 0; i < length; i++)
    {
        if (data[i] == '>')
        {
            // no longer waiting for a response, so ok to request more data
            waitingForResponse = false;
        }
    }
    
}

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

// Set fonts
const uint8_t *headingFont          = u8g2_font_inr16_mr;
const uint8_t *valueFont            = u8g2_font_inr30_mr;
const uint8_t *statusFontLarge      = valueFont;
const uint8_t *statusFontSmall      = headingFont;
const uint8_t *statusFontVerySmall  = u8g2_font_scrum_tf;

// Tracking data to display
const int numFields = 2; // number of supported fields
int prevData[numFields]; 

// Initialise reponse timeout timer, threshold is 2000ms
unsigned long responseTimer = 0;
unsigned long responseTimeoutThresh = 2000; // ms

void setup(void)
{
    // Initialise display
    u8g2.begin();

    // Display boot message
    updateStatusDisplay("hello");

    // Initialise serial and display
    Serial.begin(115200);

    // Wait for serial to initialise
    delay(2000);

    // Initialise button as an input
    pinMode(sw_pin, INPUT);

    // ------ Initialise BLE ------ 
    Serial.println("Initialising BLE\n");
    // Create BLE device
    BLEDevice::init("ESP32 OLED Gauge");

    // initialise scan
    pBLEScan = BLEDevice::getScan();

    // Attach callback object
    pBLEScan->setAdvertisedDeviceCallbacks(new MyCallbacks());

    pBLEScan->setActiveScan(true); // active scan uses more power, but get results

    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);  // less or equal setInterval value

    // Start a scan
    pBLEScan->start(scanTime, false);

    Serial.println("Initialised successfully");
}

void loop(void)
{
    // Connect to obd reader if found
    if (doConnect)
    {
        Serial.println("Connecting to OBDII reader.");

        if (connectToReader())
        {
            Serial.println("Connected successfully.");
        }
        else
        {
            Serial.println("Failed to connect.");
        }

        doConnect = false;
    }

    // Restart scan if disconnected
    if (!connected && doScan)
    {
        // Update display to alert user
        updateStatusDisplay("disconnected");

        Serial.println("Scanning for OBD readers.");
        BLEDevice::getScan()->start(5, false);
        delay(1000);
    }

    // Successfully connected, update display
    if (connected)
    {
        if (!waitingForResponse)
        {
            // Request data from the OBDII reader
            requestData();

            // Set flag to prevent spam
            waitingForResponse = true;

            // Start a timer incase the response times out or an unexpected response is received
            responseTimer = millis();

            // The response will be picked up by the notification callback function
        }
        // If waiting too long, give up and ask again
        else if (millis() - responseTimer > responseTimeoutThresh)
        {
            waitingForResponse = false;

            Serial.println("Response timed out.");
        }
    }

    // Check for button press and cycle field
    if (checkButtonPress(sw_pin, &button_state, &last_button_state))
    {
        // Increment counter
        counter++;

        // "Overflow" counter if exceeds number of supported fields
        counter %= numFields;

        // Update display to prevent weird numbers appearing
        if (connected && !displayLock)
        {
            displayData(nullptr, true);
            Serial.println("Old value displayed.");
        }

        // log button press over serial
        Serial.printf("Button pressed! Current field: %d, waiting status: %d\n", counter, waitingForResponse);
    }
}

// Function to handle device selection and connection
// Returns bool representing success
bool connectToReader()
{
    // Create client
    pClient = BLEDevice::createClient();
    Serial.println(" - Created client");
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the reader
    pClient->connect(obdDevice);
    Serial.println(" - Connected to server");

    connected = true;
    doScan = false;

    // Get all discovered services
    auto* services = pClient->getServices();

    // Check for the service with the desired characteristics (1 write, 1 notify)
    int writeChr = 0;
    int notifyChr = 0;

    for (auto const& servicePair : *services)
    {
        BLERemoteService* service = servicePair.second;

        Serial.printf("\nService: %s\n",
            service->getUUID().toString().c_str());

        // Get all characteristics in this service
        auto* characteristics = service->getCharacteristics();

        for (auto const& charPair : *characteristics)
        {
            BLERemoteCharacteristic* chr = charPair.second;

            Serial.printf("  Characteristic: %s\n",
                chr->getUUID().toString().c_str());

            Serial.printf("    Read: %d\n", chr->canRead());
            Serial.printf("    Write: %d\n", chr->canWrite());
            Serial.printf("    Notify: %d\n", chr->canNotify());
            Serial.printf("    Indicate: %d\n", chr->canIndicate());

            // If this is a writable characteristic, save and increment counter
            if (chr->canWrite())
            {
                pTxCharacteristic = chr;
                writeChr++;
            }

            // If this is a notifying characteristic, save and increment counter
            if (chr->canNotify())
            {
                pRxCharacteristic = chr;
                notifyChr++;
            }
        }

        // Check if the desired service has been found, if so save
        if (writeChr == 1 && notifyChr == 1)
        {
            pService = service;

            // Register notification callback function
            pRxCharacteristic->registerForNotify(notifyCallback);

            return true;
        }

        // If the service wasn't found, reset counters and check the next service
        writeChr = 0;
        notifyChr = 0;
    }

    // Could not find a valid service
    Serial.printf("Could not find a valid service, is this a compatible OBDII reader?\n");
    return false;
}

// Function to form and send an OBDII request over Bluetooth
void requestData()
{
    // Initialise request string with mode string
    char req[10] = "01 ";

    switch (counter)
    {
        // Coolant temp
        case 0:
            strcat(req, "05");
            break;

        // RPM
        case 1:
            strcat(req, "0C");
            break;
    }

    // Add carriage return
    strcat(req, "\r");

    // Send request
    pTxCharacteristic->writeValue(req);
}

// Function to parse and display received data
// Input: raw received buffer, and flag in case previous values are to be used
// Source for parsing calculations: https://en.wikipedia.org/wiki/OBD-II_PIDs
void displayData(uint8_t *rawData, bool oldData)
{
    // Initialise variables
    char parsedByteA[2];
    char parsedByteB[2];

    int byteA;
    int byteB;

    int displayValue;

    // Display
    switch (counter)
    {
        // Coolant temp
        case 0:
            if (!oldData)
            {
                // Coolant temp is just one byte
                parsedByteA[0] = rawData[6];
                parsedByteA[1] = rawData[7];

                displayValue = (int)strtol(parsedByteA, NULL, 16) - 40; // offset to include negative temps
            }

            // Update display
            strcpy(heading, "coolant");

            break;
            
        // RPM
        case 1:
            if (!oldData)
            {
                // RPM is two bytes
                parsedByteA[0] = rawData[6];
                parsedByteA[1] = rawData[7];
                parsedByteB[0] = rawData[9]; // skip space
                parsedByteB[1] = rawData[10];

                byteA = (int)strtol(parsedByteA, NULL, 16);
                byteB = (int)strtol(parsedByteB, NULL, 16);

                displayValue = (256 * byteA + byteB)/4;
            }

            // update display
            strcpy(heading, "rpm");

            break;
    }

    // Store current value, or recall if needed
    if (!oldData)
    {
        prevData[counter] = displayValue;
    }
    else
    {
        displayValue = prevData[counter];
    }

    updateValueDisplay(heading, displayValue);
}

// Return the x position of the left of a string to centre text
uint8_t centreTextPosX(const char* text)
{
    return (DISP_WIDTH - u8g2.getStrWidth(text)) / 2;
}

// Return the y position of the top of a string to centre text based on the current font
uint8_t centreTextPosY(void)
{
    return (DISP_HEIGHT - u8g2.getMaxCharHeight()) / 2;
}

// Render text assuming font and font pos have been set already
//  - Alignment can be 'l', 'c' or 'r'
void renderText(const char* text, char alignment, uint8_t y)
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
void renderHeading(const char* headingText, const uint8_t* font, char alignment)
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

void renderStatus(const char* statusText)
{
    // Initially set to large font
    u8g2.setFont(statusFontLarge);

    // Check length, if too long, make font smaller
    if (u8g2.getStrWidth(statusText) >= DISP_WIDTH)
    {
        u8g2.setFont(statusFontSmall);

        // Check if still too big
        if (u8g2.getStrWidth(statusText) >= DISP_WIDTH)
        {
            u8g2.setFont(statusFontVerySmall);
        }
    }

    u8g2.setFontPosTop();

    // Set position to centre
    uint8_t x = centreTextPosX(statusText);
    uint8_t y = centreTextPosY();

    // Render status message
    u8g2.drawStr(x, y, statusText);
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

// -------------- DISPLAY STATES --------------------

// Update display value with a given heading
// Both text fields will be centre aligned
void updateValueDisplay(char* heading, int value)
{
    u8g2.firstPage();
    do
    {
        u8g2.setDrawColor(1);
        u8g2.setFontMode(1);  // Transparent font rendering

        // Set heading 
        renderHeading(heading, headingFont, 'c');

        // Display value
        renderValue(value, valueFont, 'c');

    } while (u8g2.nextPage());
}

void updateStatusDisplay(const char* status)
{
    u8g2.firstPage();
    do
    {
        u8g2.setDrawColor(1);
        u8g2.setFontMode(1);  // Transparent font rendering

        // Set status 
        renderStatus(status);

    } while (u8g2.nextPage());
}