/*
 * Copyright (C) 2021 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "AlsCorrection.h"

#include <cutils/properties.h>
#include <fstream>
#include <cmath>
#include <log/log.h>
#include <time.h>

namespace android {
namespace hardware {
namespace sensors {
namespace V2_1 {
namespace implementation {

static int red_max_lux, green_max_lux, blue_max_lux, white_max_lux, max_brightness, cali_coe;
static int als_bias, max_lux, test, not_good_cnt;
static float coeff_a, coeff_b, algo_bias, prev_light_corrected;

template <typename T>
static T get(const std::string& path, const T& def) {
    std::ifstream file(path);
    T result;

    file >> result;
    return file.fail() ? def : result;
}

void AlsCorrection::init() {
    red_max_lux = get("/mnt/vendor/persist/engineermode/red_max_lux", 0);
    green_max_lux = get("/mnt/vendor/persist/engineermode/green_max_lux", 0);
    blue_max_lux = get("/mnt/vendor/persist/engineermode/blue_max_lux", 0);
    white_max_lux = get("/mnt/vendor/persist/engineermode/white_max_lux", 0);
    als_bias = get("/mnt/vendor/persist/engineermode/als_bias", 0);
    cali_coe = get("/mnt/vendor/persist/engineermode/cali_coe", 1000);
    max_brightness = get("/sys/class/backlight/panel0-backlight/max_brightness", 255);
    max_lux = red_max_lux + green_max_lux + blue_max_lux + white_max_lux;
    ALOGD("max r = %d, max g = %d, max b = %d, max_white: %d, cali_coe: %d, als_bias: %d, max_brightness: %d, max_lux: %d", red_max_lux, green_max_lux, blue_max_lux, white_max_lux, cali_coe, als_bias, max_brightness, max_lux);
    test = 0;
    coeff_a = 1.43f;
    coeff_b = 2.0f;
    not_good_cnt = 0;
    algo_bias = 0.0f;
    prev_light_corrected = 0.0f;
}

void AlsCorrection::correct(float& light) {
    pid_t pid = property_get_int32("vendor.sensors.als_correction.pid", 0);
    if (pid != 0) {
        kill(pid, SIGUSR1);
    }
    uint8_t r = property_get_int32("vendor.sensors.als_correction.r", 0);
    uint8_t g = property_get_int32("vendor.sensors.als_correction.g", 0);
    uint8_t b = property_get_int32("vendor.sensors.als_correction.b", 0);
    ALOGV("Screen Color Above Sensor: %d, %d, %d", r, g, b);
    ALOGV("Original reading: %f", light);
    int screen_brightness = get("/sys/class/backlight/panel0-backlight/brightness", 0);
    float correction = 0.0f;
    float brightness_factor = 0.0f;
    float frac = 0.0f;
    float exp = 0.0f;
    float rgb_avg = 0.0f;
    uint32_t rgb_min = 0;
    float light_frac = 0.0f;
    float coe_frac = ((float) cali_coe) / 1000.0f;
    if (max_lux > 0) {
        frac = ((float) screen_brightness) / ((float) max_brightness);
        light_frac = (light > max_lux) ? 1.0f : light/((float) max_lux);
        rgb_min = std::min({r, g, b});
        rgb_avg = (r + g + b)/3/255.0f;
        correction += ((float) rgb_min) / 255.0f * ((float) white_max_lux);
        correction += ((float) r) / 255.0f * ((float) red_max_lux);
        correction += ((float) g) / 255.0f * ((float) green_max_lux);
        correction += ((float) b) / 255.0f * ((float) blue_max_lux);
        correction += als_bias;
        correction = correction/max_lux;
        light_frac = light_frac * coe_frac + light_frac * std::pow(correction, 0.2f); // frac of lux reading that is due to screen brightness
        exp = (correction < 0.1f) ? 1.2f - std::pow(frac, 0.05f) : 1.2f - std::pow(correction*frac, 0.2f);
        brightness_factor = std::pow(frac, exp);
        light_frac = light_frac * brightness_factor;
        light_frac = (light > max_lux) ? light_frac - (light - max_lux)/((float) max_lux) : light_frac;
        light_frac = (light_frac > 0.65f) ? 0.65f : light_frac < 0.0f ? 0.1f : light_frac;
    }
    // Do not apply correction if < 0, prevent unstable adaptive brightness
    ALOGD("Original: %f  Corrected: %f Correction: %f brightness: %d brightness_factor: %f exp: %f light_frac: %f", light, light * (1.0f - light_frac), correction, screen_brightness, brightness_factor, exp, light_frac);
    light = light * (1.0f - light_frac);
    not_good_cnt = 0;
    prev_light_corrected = light;
}

}  // namespace implementation
}  // namespace V2_1
}  // namespace sensors
}  // namespace hardware
}  // namespace android
