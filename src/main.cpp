#include <Arduino.h>
#include <Preferences.h> // Include the NVS wrapper
#include <string>         // C++ Standard String library
#include <string_view>    // C++17 header for high-performance string handling
                          // Just to data members: a pointer and a length. 
                          // Does not created a copy in memory. Read-only.
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1351.h>
#include <SPI.h>

//C++: Namespaces & Constexpr --- 
namespace Config {
    // 'constexpr' tells the compiler this value is known at compile-time.
    // It is more efficient than 'const' and safer than '#define'.

    constexpr int BtnPanic = 47;
    constexpr int LedPin    = 2;
    constexpr int StrobPin  = 48;

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
}

// Global Objects
Preferences prefs;
SemaphoreHandle_t panicSemaphore;
QueueHandle_t displayQueue;
Adafruit_SSD1351 tft = Adafruit_SSD1351(Config::ScreenWidth, Config::ScreenHeight, &SPI, Config::OLED_CS, Config::OLED_DC, Config::OLED_RST);

int LineNumber = 0;

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
void logStatus(const char* info, uint16_t color = 0xFFFF);

void setup() {
    // Delay to warm up 
    delay(1000);
    Serial.begin(115200);
    delay(1000);

    // Configure Hardware using our Namespace
    gpioConfig();

    //Initialioze OLED on start up
    initOLED();

    // Pulling ESP3-S3 HW Information
    espInfo();

    // Reading "Panic Count" on start up
    rdPanicCounter();


    // Reading "Panic Count" on start up
    prefs.begin("system", true); // Open in Read-Only mode
    Serial.printf("Total Lifetime Panic Events: %u\n", prefs.getUInt("panic_count", 0));
    prefs.end();
  
    // Create a Queue for Display
    displayQueue = xQueueCreate(10, sizeof(DisplayMsg));

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
}

void loop() {
    // Arduino task is no longer needed
    vTaskDelete(NULL);
}

// --- Interrupt Service Routine (ISR) ---
// IRAM_ATTR ensures this runs from RAM, critical for S3 stability
void IRAM_ATTR handleButtonInterrupt() {

    // Set GPIO 0 high to indicate that we are jumped into ISR
    digitalWrite(Config::StrobPin, HIGH);

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

    //tft.setTextColor(0xffff); // Sets text colour to White
    //tft.setTextColor(0x07FF); // Sets text clour to Cyan
    //tft.setTextColor(0x07e1); // Sets text colour to Green
    //tft.setTextColor(0xffc0); // Sets text colour to Yellow
    
    // Initialize OLED display
    tft.begin(20000000); // Force 20MHz
    tft.fillScreen(0x0000); // Clear to black
    tft.setCursor(LineNumber, 5);
    tft.setTextColor(0x07FF); // Cyan color to match your Web UI
    tft.print("S3 MONITOR ACTIVE"); 
      

    //  Small size 5x7 [6x8]
    //tft.setTextSize(1);
    //tft.setCursor(32, 0);
    //tft.setTextColor(0x07FF); // Sets text clour to Cyan
    //tft.setTextColor(0x07e1); // Sets text colour to Green
    //tft.setTextColor(0xffff); // Sets text colour to White
    //tft.print("SMALL SIZE");

    //  Small size (21 charaters per line)
    //tft.setTextSize(1);
    //tft.setCursor(0, 8);
    //tft.setTextColor(0xff40); // Sets text colour to Yellow
    //tft.print("012345678901234567890");

     //  Medium size 10x14 [12x16]
    //tft.setTextSize(2);
    //tft.setCursor(24, 24);
    //tft.setTextColor(0x07e1); // Sets text colour to Green
    //tft.print("MEDIUM");

    //  Medium size (10 charters per line)
    //tft.setTextSize(2);
    //tft.setCursor(0, 40);
    //tft.setTextColor(0xffc0); // Sets text colour to Yellow
    //tft.setTextColor(0x07FF); // Sets text clour to Cyan
    //tft.print("0123456789");

    //  Large size [24 pixels]
    //tft.setTextSize(3);
    //tft.setCursor(0, 72);
    //tft.setTextColor(0xf800); // Sets text colour to Red
    //tft.print("-LARGE-");

    //  Extre Large size
    //tft.setTextSize(4);
    //tft.setCursor(0, 72);
    //tft.setTextColor(0x07FF);
    //tft.print("XTL");
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
  // Reading "Panic Count" on start up
    prefs.begin("system", true); // Open in Read-Only mode
    Serial.printf("Total Lifetime Panic Events: %u\n", prefs.getUInt("panic_count", 0));
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
            digitalWrite(45, HIGH); // Your PANIC monitor signal

            // --- NVS Logic ---
            prefs.begin("system", false); // Open "system" namespace in R/W mode
            uint32_t count = prefs.getUInt("panic_count", 0); // Get existing or default to 0
            count++;
            prefs.putUInt("panic_count", count); // Save new count to Flash
            prefs.end();

            Serial.printf("\n[Panic] Event #%u recorded in NVS!\n", count);

            // Your strobe feedback logic...
            for(int i = 0; i < 20; i++) {
                digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            digitalWrite(45, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
            while(xSemaphoreTake(panicSemaphore, 0) == pdPASS); 
            attachInterrupt(digitalPinToInterrupt(Config::BtnPanic), handleButtonInterrupt, FALLING);
        }
    }
}

    void displayTask(void* pvParameters) {
        DisplayMsg msg;
        if (xQueueReceive(displayQueue, &msg, portMAX_DELAY) == pdPASS) {
            if(LineNumber > 10) { 
                tft.fillScreen(0x0000); 
                LineNumber = 0; 
            }
                tft.setCursor(0, LineNumber * 12);
                tft.setTextColor(msg.color);
                tft.println(msg.text);
                LineNumber++;
        }
    }

void heartbeatTask(void *pvParameters) {
     for(;;) {
        digitalWrite(Config::LedPin, !digitalRead(Config::LedPin));
 //       Serial.printf("[Core 0] Normal Heartbeat... (Uptime: %lu s)\n", millis()/1000);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}