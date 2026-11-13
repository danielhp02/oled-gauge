// gauge
// 128x64 monochrome OLED display for output of data from an ELM327 OBDII dongle
// using BLEDevice as a client

// Display docs: https://github.com/olikraus/u8g2/wiki
// Display wired up as: https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf

// Font Reference:
// - u8g2_font_scrum_tr: nice general font, 15px tall
// - u8g2_font_inr30_mr: inconsolata, 30px tall (also 16, 19, 21, 24, 27)

#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <Wire.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
// #include <BLEServer.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// Define display dimensions
#define DISP_WIDTH 128
#define DISP_HEIGHT 64

// Define Bluetooth UUIDs
// #define SERVICE_UUID        "6b4b1061-f195-4060-963a-b7bfe1761866"
// #define CHARACTERISTIC_UUID "84a0ca94-4cb6-4440-b507-1819c97e7a83"

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
void updateValueDisplay(char* heading, int value);
bool connectToReader();
int parseCoolantTemp(char *rawTemp);

int scanTime = 5; // in seconds
BLEScan* pBLEScan;

static boolean doScan = false;
static boolean doConnect = false;
static boolean connected = false;
static BLEAdvertisedDevice *obdDevice = nullptr;
static BLEClient *pClient = nullptr;

// static BLEUUID serviceUUID;
static BLERemoteService *pService = nullptr;
static BLERemoteCharacteristic *pTxCharacteristic = nullptr;
static BLERemoteCharacteristic *pRxCharacteristic = nullptr;

static bool waitingForResponse = false;

// Callback class for Bluetooth
class MyCallbacks: public BLEAdvertisedDeviceCallbacks {
    // Callback for when a characteristic is written to using Nordic nRF on an Android device
    // void onWrite(BLECharacteristic *pCharacteristic) {
    //     // Get new value of characteristic
    //     std::string value = pCharacteristic->getValue();

    //     // Check if value is valid (non-zero length) and print each character
    //     // as a string
    //     if (value.length() > 0) {
    //         for (int i = 0; i < value.length(); i++)
    //             Serial.print(value[i]);

    //     // Write a newline to console
    //     Serial.println();

    //     // Update display
    //     char ble_heading[] = "bluetooth";
    //     updateValueDisplay(ble_heading, value[0]);
    //   }
    // }

  // Callback for when a device is found
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    // Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
    std::string deviceName = advertisedDevice.getName();
    Serial.printf("device name: %s \n", deviceName.c_str());
    if (advertisedDevice.getName() == "OBDII") {
      Serial.printf("\nOBD device found\n");

      // stop scanning and connect
      BLEDevice::getScan()->stop();
      obdDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = false;
    }
  }
};

// Client callbacks to handle connection events
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient *pclient) {
    Serial.println("Connected to OBDII reader");
  }

  void onDisconnect(BLEClient *pclient) {
    connected = false;
    Serial.println("Disconnected from OBDII reader");
  }
};

// Callback for received data
void notifyCallback(
    BLERemoteCharacteristic* characteristic,
    uint8_t* data,
    size_t length,
    bool isNotify
) {
    Serial.print("Received: ");
    Serial.write(data, length);
    Serial.println();

    // Convert value to int
    char temp[2] = {data[6], data[7]};
    int coolantTemp = parseCoolantTemp(temp);

    // update display
    char coolantHeading[] = "coolant";
    updateValueDisplay(coolantHeading, coolantTemp);

    // no longer waiting for a response, so ok to request more data
    waitingForResponse = false;
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

void setup(void)
{
    // Initialise serial
    Serial.begin(115200);
    delay(2000); // wait for serial to initialise
    Serial.println("Initialising OLED display");

    // Initialise display
    u8g2.begin();
    // updateValueDisplay(heading, counter);

    // Initialise button as an input
    pinMode(sw_pin, INPUT);

    // ------ Initialise BLE ------ 
    Serial.println("Initialising BLE\n");
    // Create BLE device
    BLEDevice::init("ESP32 OLED Gauge");

    // initialise scan
    pBLEScan = BLEDevice::getScan();

    // Attach callback function
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
    // connect to obd reader if found
    if (doConnect)
    {
        Serial.println("Connecting to device.");

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
        Serial.println("Scanning for OBD readers.");
        BLEDevice::getScan()->start(5, false);
        delay(1000);
    }

    // Successfully connected, update display
    if (connected)
    {
        if (!waitingForResponse)
        {
            // Request coolant temp
            pTxCharacteristic->writeValue("01 05\r");

            // set flag to prevent spamming adapter
            waitingForResponse = true;

            // The response will be picked up by the notification callback function
        }

        delay(5000);
    }

    // Check for button press
    // TODO: replace with a screen toggle or something
    if (checkButtonPress(sw_pin, &button_state, &last_button_state))
    {
        // Increment counter
        counter++;

        // Update display
        // updateValueDisplay(heading, counter);

        // log button press over serial
        Serial.println("Button pressed!");
    }
}

bool connectToReader()
{
    Serial.println("Connecting to OBDII reader");

    // create client
    pClient = BLEDevice::createClient();
    Serial.println(" - Created client");
    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the reader
    pClient->connect(obdDevice);
    Serial.println(" - Connected to server");

    connected = true;

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

int parseCoolantTemp(char *rawTemp)
{
    return (int)strtol(rawTemp, NULL, 16) - 40; // offset to include negative temps
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

// Update display value with a given heading
// Both text fields will be centre aligned
void updateValueDisplay(char* heading, int value)
{
    u8g2.firstPage();
    do
    {
        // Set heading 
        renderHeading(heading, u8g2_font_inr16_mr, 'c');

        // Display value
        renderValue(value, u8g2_font_inr30_mr, 'c');

    } while (u8g2.nextPage());
}