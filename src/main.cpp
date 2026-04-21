#include <Arduino.h>
#include <Preferences.h> // Include the NVS wrapper
#include <string>         // C++ Standard String library
#include <string_view>    // C++17 header for high-performance string handling
                          // Just to data members: a pointer and a length. 
                          // Does not created a copy in memory. Read-only.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include "unused_gpio.h"

// WS2812 Configuration
#define WS2812_PIN      48
#define NUM_LEDS        1
#define LED_BRIGHTNESS  8  // 0-255 (adjust to your preference)
#define LED_TYPE        (NEO_GRB + NEO_KHZ800)

//C++: Namespaces & Constexpr --- 
namespace Config {
    // 'constexpr' tells the compiler this value is known at compile-time.
    // It is more efficient than 'const' and safer than '#define'.

    constexpr int BtnPanic = 47;
    //constexpr int LedPin    = 2;
    constexpr int StrobPin  = 21;

    // HMI Navigation Buttons
    constexpr int BtnUp    = 4;
    constexpr int BtnDown  = 5;
    constexpr int BtnEnter = 6;

    // OLED SPI Pins (FSPI Hardware Primary)
    constexpr int OLED_CS   = 10;
    constexpr int OLED_MOSI = 11;
    constexpr int OLED_SCLK = 12;
    constexpr int OLED_DC   = 13;
    constexpr int OLED_RST  = 14;

    constexpr int ScreenWidth  = 128;
    constexpr int ScreenHeight = 128;
    
    constexpr int LedPin = WS2812_PIN;
}

// Global Objects
Preferences prefs;
SemaphoreHandle_t panicSemaphore;
QueueHandle_t displayQueue;
Adafruit_SSD1351 tft = Adafruit_SSD1351(Config::ScreenWidth, Config::ScreenHeight, &SPI, Config::OLED_CS, Config::OLED_DC, Config::OLED_RST);
Adafruit_NeoPixel ws2812(NUM_LEDS, WS2812_PIN, LED_TYPE);

int LineNumber = 0;
//char MsgBuf[30];        // ! only 21 characters per line can be displayed on OLED

struct DisplayMsg {
    char text[32];
    uint16_t color;
};

// Function Prototypes
void IRAM_ATTR handleButtonInterrupt();
void panicTask(void *pvParameters);
void heartbeatTask(void *pvParameters);
void initOLED();
void espInfo();
void gpioConfig();
void rdPanicCounter();
void displayTask(void* pvParameters);
void logStatus(const char*, uint16_t);

void setup() {
    // Delay to warm up 
    delay(1000);
    Serial.begin(115200);
    delay(1000);

    //Terminate unused GPIOs EARLY (before peripheral init)
    ConfigureUnusedGpios();

    // Configure Hardware using our Namespace
    gpioConfig();

    //Initialioze OLED on start up
    initOLED();

    // Pulling ESP3-S3 HW Information
    espInfo();

    // Reading "Panic Count" on start up
    rdPanicCounter();
  
    // Create a Queue for Display only 1 Msg
    displayQueue = xQueueCreate(1, sizeof(DisplayMsg));

    // Create the Binary Semaphore for ISR
    panicSemaphore = xSemaphoreCreateBinary();

    // Attach Interrupt to (BtnPanic)
    attachInterrupt(digitalPinToInterrupt(Config::BtnPanic), handleButtonInterrupt, FALLING);

    // Panic Handler Task (Highest Priority: 3) on Core 1
    xTaskCreatePinnedToCore(panicTask, "Panic", 4096, NULL, 3, NULL, 1);

    // Standard Heartbeat (Priority: 0) on Core 0
    xTaskCreatePinnedToCore(heartbeatTask, "Heartbeat", 4096, NULL, 0, NULL, 0);

    // Create Display Task (Priority: 1) on Core 0
    xTaskCreatePinnedToCore(displayTask, "OLED_Task", 4096, NULL, 1, NULL, 0); 

    logStatus("Setup() Completed...", 0x07FF);
}

void loop() {
    // Arduino task is no longer needed
    vTaskDelete(NULL);
}

// --- Interrupt Service Routine (ISR) ---
// IRAM_ATTR ensures this runs from RAM, critical for S3 stability
void IRAM_ATTR handleButtonInterrupt() {

    static BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    // Notify the task that the button was pressed
    xSemaphoreGiveFromISR(panicSemaphore, &xHigherPriorityTaskWoken);
    
    // If the panic task has higher priority, this forces a context switch 
    // immediately upon exiting the ISR for "instant" feel.

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void initOLED() {
    // Initialize SPI with our custom pins
    SPI.begin(Config::OLED_SCLK, -1, Config::OLED_MOSI, Config::OLED_CS);
    LineNumber = 0;

    //tft.setTextColor(0xffff); // Sets text colour to White
    //tft.setTextColor(0x07FF); // Sets text clour to Cyan
    //tft.setTextColor(0x07e1); // Sets text colour to Green
    //tft.setTextColor(0xffc0); // Sets text colour to Yellow
    
    // Initialize OLED display
    tft.begin(20000000); // Force SPI clik=20MHz;  20MHz is max)
    tft.fillScreen(0x0000); // Clear to black
    tft.setTextSize(1); // 1-Small size; 2-M; 3-Large; 4-XL
    //  Small size 5x7 [6x8] (21 charaters per line)
    //  Medium size 10x14 [12x16] (10 charters per line)
    tft.setCursor(15, LineNumber);
    tft.setTextColor(0xff40); // Yellow color   
    tft.print("S3 MONITOR ACTIVE");    
    LineNumber++;
}

void espInfo() {
    Serial.println("\n--- Connected via CH343 UART (COM?) ---");
    Serial.println(  "--- ESP32-S3 Dual Core Booting --------");
    Serial.println("\n--- ESP Hardware Info------------------");
    
    // Display ESP Information
    Serial.printf("Chip ID: %u\n", ESP.getChipModel()); // Get the 24-bit chip ID
    Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz()); // Get CPU frequency
    Serial.printf("SDK Version: %s\n", ESP.getSdkVersion()); // Get SDK version

    // Get and print flash memory information
    Serial.printf("Flash Chip Size: %u bytes\n", ESP.getFlashChipSize()); // Get total flash size
   
    // Get and print SRAM memory information
    Serial.printf("Internal free Heap at setup: %d bytes\n", ESP.getFreeHeap());
    if(psramFound()){
        Serial.printf("Total PSRAM Size: %d bytes", ESP.getPsramSize());
    } else {
         Serial.print("No PSRAM found");
    }
    Serial.println("\n---------------------------------------");
    Serial.println("\n");
}

void gpioConfig() {
   // Configure Hardware using our Namespace
    pinMode(Config::LedPin, OUTPUT);
    pinMode(Config::BtnPanic, INPUT_PULLUP);
    pinMode(Config::StrobPin, OUTPUT);
    digitalWrite(Config::StrobPin, LOW);
}

void rdPanicCounter() {
  // Reading "Panic Count" and send value to terminal
    prefs.begin("system", true); // Open in Read-Only mode
    Serial.printf("Total Lifetime Panic Events: %u\n", prefs.getUInt("panic_count", 0));
    // convert message to string
    // sprintf(MsgBuf, "FS: %u B", ESP.getFlashChipSize());
    // logStatus(MsgBuf, 0x07FF);
    // logStatus("FS: B", 0x07FF);
    // prefs.end();
    prefs.end();
}

 void logStatus(const char* info, uint16_t color = 0xFFFF) {
    //Serial.println(info);
    
    DisplayMsg msg;
    // Safe string copy with guaranteed null-termination
    strncpy(msg.text, info, sizeof(msg.text) - 1);
    msg.text[sizeof(msg.text) - 1] = '\0';  // Adding null-termination
    msg.color = color;
    
    // Block briefly if queue is full (10ms max), drop otherwise
    if (xQueueSend(displayQueue, &msg, pdMS_TO_TICKS(10)) != pdPASS) {
        Serial.println(F("[WARN] Display queue full, message dropped"));
    }
}

// --- Tasks ---
void panicTask(void *pvParameters) {
    for(;;) {
        if (xSemaphoreTake(panicSemaphore, portMAX_DELAY) == pdPASS) {
            detachInterrupt(digitalPinToInterrupt(Config::BtnPanic));

            // --- NVS Logic ---
            prefs.begin("system", false); // Open "system" namespace in R/W mode
            uint32_t count = prefs.getUInt("panic_count", 0); // Get existing or default to 0
            count++;
            prefs.putUInt("panic_count", count); // Save new count to Flash
            prefs.end();

            Serial.printf("\n[Panic] Event #%u recorded in NVS!\n", count);

            bool ledOn = false;
            // Your strobe feedback logic...
            for(int i = 0; i < 20; i++) {
//                digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));

            if (ledOn) {
//                ws2812.setPixelColor(0, ws2812.Color(0, 150, 255));  // Panic (Cyan/Blue)
                ws2812.setPixelColor(0, ws2812.Color(255, 0, 0));  // Panic (Red)
             } else {
                // Heartbeat OFF
                ws2812.setPixelColor(0, 0, 0, 0);
             }
        
            ws2812.show();          // Push data to the LED
            ledOn = !ledOn;         // Toggle state

            vTaskDelay(pdMS_TO_TICKS(50));
            }

            vTaskDelay(pdMS_TO_TICKS(100));
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            attachInterrupt(digitalPinToInterrupt(Config::BtnPanic), handleButtonInterrupt, FALLING);
        }
    }
}

void displayTask(void* pvParameters) {
    DisplayMsg msg;
    while (true) {
        if (xQueueReceive(displayQueue, &msg, portMAX_DELAY) == pdPASS) {
            // xQueueReceive(...): The FreeRTOS API function that attempts to read and remove an item from a queue.
            // displayQueue: The handle to the queue being read.
            // &msg: A pointer to the buffer where the received data will be copied
            // portMAX_DELAY: The timeout value. Because this is set, the task will block (enter the Blocked state) and wait forever for data to become available
            // Alternatives: If you want to check the queue and continue without waiting, use a timeout of 0 (polling) instead of portMAX_DELAY
            // == pdPASS: The check to confirm that data was received successfully (it returns pdTRUE/pdPASS if data was received, otherwise errQUEUE_EMPTY if a timeout occurred). 
            if(LineNumber > 10) { 
                tft.fillScreen(0x0000); 
                LineNumber = 0; 
            }
            tft.setCursor(0, LineNumber * 12);
            tft.setTextColor(msg.color);
            tft.println(msg.text);
            LineNumber++;

            // Optional: prevent I2C/SPI bus saturation
            vTaskDelay(pdMS_TO_TICKS(5)); 
        }
    }    
}
//based on inboard BLU LED connected to GPIO 2
//void heartbeatTask(void *pvParameters) {
//     for(;;) { 
//        digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
//        Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis()/1000);
//        vTaskDelay(pdMS_TO_TICKS(1000)); 
//    }
//}

void heartbeatTask(void *pvParameters) {
    bool ledOn = false;

    // Initialize LED to OFF state
    ws2812.setBrightness(LED_BRIGHTNESS);
    ws2812.setPixelColor(0, 0, 0, 0);
    ws2812.show();

    for (;;) {
        if (ledOn) {
            //ws2812.setPixelColor(0, ws2812.Color(0, 150, 255));  // Heartbeat ON (e.g., cyan/blue)
            ws2812.setPixelColor(0, ws2812.Color(0, 255, 0));  // Heartbeat ON (e.g., green)   
        } else {
            // Heartbeat OFF
            ws2812.setPixelColor(0, 0, 0, 0);
        }
        
        ws2812.show();          // Push data to the LED
        ledOn = !ledOn;         // Toggle state

    //    Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis() / 1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

