**Task Allocation: Core 0 vs. Core 1**
For the software implementation of the "OLED Terminal," I recommend assigning this task to `Core 0`.

Here is the architectural reasoning for this decision:
-   `Core 1 is for Determinism:`
     You have your panicTask (priority 3) and high-speed ISR logic running on Core 1. We want to keep Core 1 as "quiet" as possible to ensure that when a Panic event occurs, the context switch is near-instant and jitter-free.
-   `Core 0 is for "Housekeeping":`
     Core 0 already handles the WiFi stack and your Web Server. Since the OLED is essentially a local mirror of that diagnostic data, it belongs here.
 -  `SPI Blocking:`
     Even at 20 MHz, pushing 128×128×16-bit pixels (32 KB) is a blocking operation for the CPU if not using DMA. By putting this on Core 0, any latency introduced by the display update will only impact the Web Server response time, never your real-time Panic monitoring.

**Software Implementation: The "OLED Terminal"**
To implement this professionally, we should use a Thread-Safe Queue. Instead of calling `tft.print()` from various tasks (which would cause a race condition on the SPI bus), other tasks will "post" messages to a queue, and a dedicated `displayTask` will render them.

1. **The Queue & Task Setup**
        // Global queue handle
        QueueHandle_t displayQueue;

        struct DisplayMsg {
            char text[32];
            uint16_t color;
        };

        // Prototype
        void displayTask(void* pvParameters);

        // In setup()
        displayQueue = xQueueCreate(10, sizeof(DisplayMsg));
        xTaskCreatePinnedToCore(displayTask, "OLED_Task", 4096, NULL, 1, NULL, 0); // Priority 1, Core 0

2. **The Display Logic (Core 0)**
Using a queue allows the display to act as a non-blocking terminal.

    void displayTask(void* pvParameters) {
        // Initialize SPI and TFT here to keep ownership on Core 0
        SPI.begin(Config::OLED_SCLK, -1, Config::OLED_MOSI, Config::OLED_CS);
        tft.begin(20000000); // Force 20MHz
        tft.fillScreen(0x0000);
    
        DisplayMsg msg;
        int line = 0;

            for(;;) {
             if (xQueueReceive(displayQueue, &msg, portMAX_DELAY) == pdPASS) {
             if(line > 10) { tft.fillScreen(0x0000); line = 0; }
            
                tft.setCursor(0, line * 12);
                tft.setTextColor(msg.color);
                tft.println(msg.text);
                line++;
            }
        }
    }

3. **Professional Macro for Logging**
You can then create a simple helper to send logs to both Serial and OLED:

    void logStatus(const char* info, uint16_t color = 0xFFFF) {
        Serial.println(info);
        DisplayMsg msg;
        strncpy(msg.text, info, sizeof(msg.text));
        msg.color = color;
        xQueueSend(displayQueue, &msg, 0); 
    }

**Note on the Waveshare Schematic**
Looking at the DevKit views you provided, ensure your OLED RST pin is actually connected to a GPIO and not just tied to the ESP32's global EN/Reset line. While tying them together works for simple boot-ups, having software control over the OLED Reset pin (GPIO 14 in our plan) is vital for recovering the display controller if a brown-out or ESD event hangs the SSD1351.

>Since you're planning for E-paper in the final product, would you like to implement a "Page" system for the OLED now, so you can toggle between a Log view and a Signal Strength view?

