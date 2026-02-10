#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include "Arduino.h"
#include "LovyanGFX.hpp"
#include "esp_heap_caps.h"
SerialMock Serial;

uint32_t sim_millis = 0;
inline uint32_t get_sim_millis() { return sim_millis; }
#define millis get_sim_millis

#define setup arduino_setup
#define loop arduino_loop
#include "../ESP32_Fractal_Poem_Final.ino"
#undef setup
#undef loop

int main() {
    arduino_setup();
    // Simulate 200 frames to see camera movement
    for(int i=0; i<400; i++) {
        sim_millis += 16; // 60 fps
        arduino_loop();
        if (i % 40 == 0) {
            char buf[64]; sprintf(buf, "frame_%d.ppm", i);
            tft.savePPM(buf);
        }
    }
    printf("Simulation finished.\n");
    return 0;
}
