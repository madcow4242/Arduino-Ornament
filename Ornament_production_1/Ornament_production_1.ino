// ============================================================================
// Ornament Controller - Production Software (v0.1.5)
// Target Hardware: Microchip ATtiny414/814/1614 (tinyAVR 1-Series)
// This software is designed to control a 30-LED Charlieplexed display for an ornament, with various lighting effects and user interaction via a button.
// Kevin Cazabon, 2026 kevin@cazabon.com / http://www.github.com/madcow4242/Arduino-Ornament 
// Licensed under the MIT License (https://opensource.org/licenses/MIT)
// ============================================================================
#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/delay.h>

#define PWM_MAX 32                  
#define GROUP_CYCLE_MS 1000
#define MAX_GROUP_LEDS 4            
#define NUM_GROUPS 3   

#define TWINKLE_DUR_MS 30000
#define ADVENT_RACE_STEP_MS 200     
#define STAR_CHASE_STEP_MS 150      
#define ADVENT_PULSE_SEC 6          
#define RUNTIME_LIMIT_MS 18000000UL // 5 hours  // 86040000UL // 23.9 hours for testing  
#define PREVIEW_LIMIT_MS 3600000UL  // 1 hour

typedef struct { int8_t hi; int8_t lo; } LEDMap;
typedef struct { uint8_t num; uint8_t level; } LEDPair;

static LEDPair groups[NUM_GROUPS][MAX_GROUP_LEDS];

EEMEM uint8_t ee_advent_day = 1;
EEMEM uint8_t ee_global_brightness = 50;

volatile uint8_t enter_date_set = 0;
static uint8_t global_brightness_level = 50;

static uint8_t rng_state = 42;

static uint8_t fast_rand(void) {
    rng_state ^= (uint8_t)millis(); // Inject live entropy from the system timer
    uint8_t x = rng_state;
    x ^= x << 3;
    x ^= x >> 5;
    x ^= x << 4;
    rng_state = x;
    return x;
}

static uint8_t scale_pwm_val(uint8_t lvl, uint8_t cal) {
    uint16_t l = ((uint16_t)lvl * cal) / PWM_MAX;
    l = (l * global_brightness_level) / 50;
    if (l == 0 && lvl) l = 1;
    return l > PWM_MAX ? PWM_MAX : l;
}

const uint8_t calibration_table[30] PROGMEM = {
    14, 14, 30, 7, 10, 7, 14, 14, 30, 10,     
    7, 14, 14, 30, 7, 10, 14, 14, 30, 10,     
    7, 14, 14, 30, 18, 9, 9, 9, 9, 9          
};

const uint8_t led_pin_masks[] = {0x04, 0x08, 0x10, 0x20, 0x40, 0x80}; 

const LEDMap charlie_map[30] PROGMEM = {
    {5,4}, {4,5}, {5,3}, {3,5}, {4,3}, {3,4}, {5,2}, {2,5}, {4,2}, {2,4}, 
    {3,2}, {2,3}, {5,1}, {1,5}, {4,1}, {1,4}, {3,1}, {1,3}, {2,1}, {1,2}, 
    {5,0}, {0,5}, {4,0}, {0,4}, {3,0}, {0,3}, {2,0}, {0,2}, {1,0}, {0,1}  
};

ISR(PORTB_PORT_vect) {
    PORTB.INTFLAGS = PIN2_bm;
}

void check_button() {
    static uint32_t press_start;
    static uint8_t tracking;

    if (!(PORTB.IN & PIN2_bm)) { 
        if (!tracking) {
            press_start = millis();
            tracking = 1;
        } else if (tracking == 1 && (millis() - press_start >= 3000)) {
            enter_date_set = 1;
            tracking = 2; 
        }
    } else {
        tracking = 0; 
    }
}

void set_hardware_led(uint8_t num) {
    PORTA.DIRCLR = 0xFC; 
    PORTA.OUTCLR = 0xFC; 
    if (num < 1 || num > 30) return; 
    uint8_t idx = num - 1;
    uint8_t hi = pgm_read_byte(&charlie_map[idx].hi);
    uint8_t lo = pgm_read_byte(&charlie_map[idx].lo);
    PORTA.DIRSET = led_pin_masks[hi] | led_pin_masks[lo]; 
    PORTA.OUTSET = led_pin_masks[hi]; 
}

uint8_t leds_share_pins(uint8_t led1, uint8_t led2) {
    if (led1 < 1 || led1 > 30 || led2 < 1 || led2 > 30) return 1;
    int8_t h1 = pgm_read_byte(&charlie_map[led1 - 1].hi);
    int8_t l1 = pgm_read_byte(&charlie_map[led1 - 1].lo);
    int8_t h2 = pgm_read_byte(&charlie_map[led2 - 1].hi);
    int8_t l2 = pgm_read_byte(&charlie_map[led2 - 1].lo);
    return (h1 == h2 || h1 == l2 || l1 == h2 || l1 == l2);
}

uint8_t select_single_channel_leds(LEDPair channel_leds[MAX_GROUP_LEDS], uint8_t advent_day) {
    uint8_t count = 0, attempts = 0;
    for (uint8_t i = 0; i < MAX_GROUP_LEDS; i++) channel_leds[i].num = 0;

    while (count < MAX_GROUP_LEDS && attempts < 25) {
        uint8_t led = (fast_rand() % 30) + 1;
        if (led == 25 && advent_day != 25) { attempts++; continue; }

        uint8_t conflict = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (leds_share_pins(led, channel_leds[i].num)) { conflict = 1; break; }
        }

        if (!conflict) {
            uint8_t cal = pgm_read_byte(&calibration_table[led - 1]);
            channel_leds[count].num = led;
            channel_leds[count].level = scale_pwm_val(PWM_MAX, cal);
            count++;
            attempts = 0;
        } else {
            attempts++;
        }
    }
    return count;
}

void output_leds_common(const uint8_t active_counts[NUM_GROUPS]) {
    uint32_t frame_start = millis();
    while ((millis() - frame_start) < 5) {
        if (enter_date_set) return;
        for (uint8_t g = 0; g < NUM_GROUPS; g++) {
            uint8_t count = active_counts[g];
            uint8_t total_active_units = 0;
            for (uint8_t i = 0; i < count; i++) {
                if (groups[g][i].level > 0) {
                    total_active_units += groups[g][i].level;
                }
            }
            uint8_t off_units = (total_active_units < PWM_MAX) ? (PWM_MAX - total_active_units) : 0;

            for (uint8_t i = 0; i < count; i++) {
                uint8_t lvl = groups[g][i].level;
                if (lvl > 0) {
                    set_hardware_led(groups[g][i].num);
                    for (uint8_t l = 0; l < lvl; l++) _delay_us(20);
                }
            }
            set_hardware_led(0);
            for (uint8_t l = 0; l < off_units; l++) _delay_us(20);
        }
    }
}

void twinkle(uint32_t duration_ms, uint8_t advent_day) {
    static LEDPair base_groups[NUM_GROUPS][MAX_GROUP_LEDS];
    uint8_t group_counts[NUM_GROUPS];
    uint32_t group_start_times[NUM_GROUPS];
    uint32_t start_time = millis();
    uint32_t now = start_time;

    for (uint8_t g = 0; g < NUM_GROUPS; g++) {
        group_counts[g] = select_single_channel_leds(base_groups[g], advent_day);
        if (group_counts[g] == 0) group_counts[g] = 1;
        group_start_times[g] = now - ((uint32_t)g * (GROUP_CYCLE_MS / NUM_GROUPS));
    }
    
    while (!enter_date_set && (millis() - start_time < duration_ms)) {
        check_button(); 
        if (enter_date_set) return;

        now = millis();
        uint8_t active_counts[NUM_GROUPS];

        for (uint8_t g = 0; g < NUM_GROUPS; g++) {
            uint32_t elapsed = now - group_start_times[g];
            if (elapsed >= GROUP_CYCLE_MS) {
                group_start_times[g] += GROUP_CYCLE_MS;
                elapsed = now - group_start_times[g]; 
                group_counts[g] = select_single_channel_leds(base_groups[g], advent_day);
                if (group_counts[g] == 0) group_counts[g] = 1;
            }

            uint16_t progress_permille = (elapsed * 1000) / GROUP_CYCLE_MS;
            if (progress_permille > 1000) progress_permille = 1000; 

            uint8_t current_pwm_level = (progress_permille <= 500)
                ? (progress_permille * PWM_MAX) / 500
                : PWM_MAX - (((progress_permille - 500) * PWM_MAX) / 500);

            active_counts[g] = group_counts[g];
            for (uint8_t i = 0; i < group_counts[g]; i++) {
                uint8_t num = base_groups[g][i].num;
                groups[g][i].num = num;
                uint8_t cal = pgm_read_byte(&calibration_table[num - 1]);
                groups[g][i].level = scale_pwm_val(current_pwm_level, cal);
            }
        }
        output_leds_common(active_counts);
    }
}

void advent_single_round(uint8_t advent_day) {
    for (uint8_t current_led = 1; current_led < advent_day && !enter_date_set; current_led++) {
        uint32_t step_start = millis();
        while ((millis() - step_start) < ADVENT_RACE_STEP_MS && !enter_date_set) {
            check_button(); 
            if (enter_date_set) return;

            uint16_t elapsed = millis() - step_start;
            uint16_t progress = (elapsed * 1000) / ADVENT_RACE_STEP_MS;
            
            uint8_t lead_pwm = (progress <= 500) ? (progress * PWM_MAX) / 500 : PWM_MAX - (((progress - 500) * PWM_MAX) / 500);
            groups[0][0].num = current_led;
            groups[0][0].level = scale_pwm_val(lead_pwm, pgm_read_byte(&calibration_table[current_led - 1]));

            uint8_t count = 1;
            if (current_led > 1) {
                groups[0][1].num = current_led - 1;
                groups[0][1].level = scale_pwm_val((PWM_MAX / 2) - ((progress * (PWM_MAX / 2)) / 1000), pgm_read_byte(&calibration_table[current_led - 2]));
                count++;
            }
            if (current_led > 2) {
                groups[0][2].num = current_led - 2;
                groups[0][2].level = scale_pwm_val((PWM_MAX / 5) - ((progress * (PWM_MAX / 5)) / 1000), pgm_read_byte(&calibration_table[current_led - 3]));
                count++;
            }
            uint8_t counts[NUM_GROUPS] = {count, 0, 0};
            output_leds_common(counts);
        }
    }

    uint32_t phase2_start = millis();
    while ((millis() - phase2_start) < ((uint32_t)ADVENT_PULSE_SEC * 1000) && !enter_date_set) {
        check_button(); 
        if (enter_date_set) return;

        uint32_t elapsed = millis() - phase2_start;
        uint16_t rem = elapsed % 1000;
        uint8_t pulse_pwm = (rem <= 500) ? (rem * PWM_MAX) / 500 : PWM_MAX - (((rem - 500) * PWM_MAX) / 500);

        groups[0][0].num = advent_day;
        groups[0][0].level = scale_pwm_val(pulse_pwm, pgm_read_byte(&calibration_table[advent_day - 1]));

        uint16_t step_index = elapsed / STAR_CHASE_STEP_MS;
        uint16_t step_progress = ((elapsed % STAR_CHASE_STEP_MS) * 1000) / STAR_CHASE_STEP_MS;

        groups[1][0].num = 26 + (step_index % 5);
        groups[1][0].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[groups[1][0].num - 1]));

        groups[1][1].num = 26 + ((step_index + 4) % 5);
        groups[1][1].level = scale_pwm_val((PWM_MAX / 2) - ((step_progress * (PWM_MAX / 2)) / 1000), pgm_read_byte(&calibration_table[groups[1][1].num - 1]));

        groups[1][2].num = 26 + ((step_index + 3) % 5);
        groups[1][2].level = scale_pwm_val((PWM_MAX / 5) - ((step_progress * (PWM_MAX / 5)) / 1000), pgm_read_byte(&calibration_table[groups[1][2].num - 1]));

        uint8_t counts[NUM_GROUPS] = {1, 3, 0};
        output_leds_common(counts);
    }
}

void handle_date_setting(uint8_t* current_day) {
    enter_date_set = 0;
    uint32_t inactivity_timer = millis();
    uint32_t blink_timer = millis();
    uint8_t led_state = 1;

    while (!(PORTB.IN & PIN2_bm));
    _delay_ms(50); 
    inactivity_timer = millis(); 

    while (millis() - inactivity_timer < 10000) {
        if (!(PORTB.IN & PIN2_bm)) {
            uint32_t press_press_time = millis();
            while (!(PORTB.IN & PIN2_bm)) {
                if (millis() - press_press_time >= 1000) {
                    eeprom_update_byte(&ee_advent_day, *current_day);
                    set_hardware_led(0);
                    _delay_ms(300);
                    goto brightness_phase;
                }
            }
            _delay_ms(30); 
            if (++(*current_day) > 25) *current_day = 1;
            inactivity_timer = millis(); 
            blink_timer = millis();
            led_state = 1;
            _delay_ms(50);
        }

        if (millis() - blink_timer >= 300) {
            led_state = !led_state;
            blink_timer = millis();
        }

        uint8_t counts[NUM_GROUPS] = {0, 0, 0};
        if (led_state) {
            groups[0][0].num = *current_day;
            groups[0][0].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[*current_day - 1]));
            counts[0] = 1;
        }
        output_leds_common(counts);
    }
    
    eeprom_update_byte(&ee_advent_day, *current_day);
    set_hardware_led(0);
    _delay_ms(300);

brightness_phase:
    inactivity_timer = millis();
    blink_timer = millis();
    led_state = 1;

    while (millis() - inactivity_timer < 10000) {
        if (!(PORTB.IN & PIN2_bm)) {
            uint32_t press_press_time = millis();
            while (!(PORTB.IN & PIN2_bm)) {
                if (millis() - press_press_time >= 1000) {
                    // Button held for 1+ second - save brightness and return to normal operation
                    eeprom_update_byte(&ee_global_brightness, global_brightness_level);
                    set_hardware_led(0);
                    _delay_ms(300);
                    enter_date_set = 0;
                    return;
                }
            }
            _delay_ms(30);
            
            // Button pressed briefly - cycle brightness level
            if (global_brightness_level == 10) global_brightness_level = 25;
            else if (global_brightness_level == 25) global_brightness_level = 50;
            else if (global_brightness_level == 50) global_brightness_level = 75;
            else if (global_brightness_level == 75) global_brightness_level = 100;
            else global_brightness_level = 10;

            inactivity_timer = millis();
            blink_timer = millis();
            led_state = 1;
            _delay_ms(50);
        }

        if (millis() - blink_timer >= 300) {
            led_state = !led_state;
            blink_timer = millis();
        }

        uint8_t counts[NUM_GROUPS] = {0, 0, 0};
        if (led_state) {
            groups[0][0].num = 3;   groups[0][0].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[2]));
            groups[0][1].num = 25;  groups[0][1].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[24]));
            groups[0][2].num = 4;   groups[0][2].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[3]));
            groups[0][3].num = 18;  groups[0][3].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[17]));
            counts[0] = 4;

            groups[1][0].num = 10;  groups[1][0].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[9]));
            groups[1][1].num = 7;   groups[1][1].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[6]));
            groups[1][2].num = 26;  groups[1][2].level = scale_pwm_val(PWM_MAX, pgm_read_byte(&calibration_table[25]));
            counts[1] = 3;
            
            counts[2] = 0;
        }
        output_leds_common(counts);
    }

    // Timeout - save brightness and return to normal operation
    eeprom_update_byte(&ee_global_brightness, global_brightness_level);
    set_hardware_led(0);
    enter_date_set = 0;
    _delay_ms(250);
}

void rtc_init(void) {
    while (RTC.STATUS > 0) { ; }
    RTC.CLKSEL = RTC_CLKSEL_INT32K_gc; // Use internal 32.768kHz oscillator
    RTC.PITCTRLA = RTC_PERIOD_CYC32768_gc | RTC_PITEN_bm; // 1-second interval + enable PIT
}

ISR(RTC_PIT_vect) {
    RTC.PITINTFLAGS = RTC_PI_bm; // Clear the PIT interrupt flag
}

void execute_show(uint8_t day) {
    check_button();
    if (!enter_date_set) twinkle(TWINKLE_DUR_MS, day);
    check_button();
    if (!enter_date_set) advent_single_round(day);
}

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0x00);
    init();
    rtc_init();

    PORTB.DIRSET = PIN3_bm; 
    PORTB.OUTSET = PIN3_bm; 
    PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc; 

    PORTB.DIRCLR = PIN2_bm;         
    PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc; 

    sei(); 

    rng_state ^= (uint8_t)millis();
    if (rng_state == 0) rng_state = 42;

    uint8_t current_advent_day = eeprom_read_byte(&ee_advent_day);
    if (current_advent_day < 1 || current_advent_day > 25) {
        current_advent_day = 1;
        eeprom_update_byte(&ee_advent_day, current_advent_day);
    }

    uint8_t stored_brightness = eeprom_read_byte(&ee_global_brightness);
    if (stored_brightness >= 10 && stored_brightness <= 100) {
        global_brightness_level = stored_brightness;
    } else {
        global_brightness_level = 50;
        eeprom_update_byte(&ee_global_brightness, global_brightness_level);
    }

    while (1) {
        check_button();
        if (enter_date_set) {
            handle_date_setting(&current_advent_day);
            continue;
        }

        uint32_t run_start_ms = millis();

        // Main operational loop for the active period (5 hours)
        while ((millis() - run_start_ms < RUNTIME_LIMIT_MS) && !enter_date_set) {
            execute_show(current_advent_day);
        }

        if (!enter_date_set) {
            if (++current_advent_day > 25) current_advent_day = 1;
            eeprom_update_byte(&ee_advent_day, current_advent_day);
        } else {
            continue;
        }

        // Enter sleep mode until the 24-hour cycle completes or button is pressed
        set_hardware_led(0);
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        
        uint32_t remaining_sleep_ms = 86400000UL - (millis() - run_start_ms);
        RTC.PITINTCTRL = RTC_PI_bm; 
        
        while (remaining_sleep_ms > 0) {
            sei(); 
            sleep_mode(); 
            
            if (!(PORTB.IN & PIN2_bm)) {
                RTC.PITINTCTRL = 0; 
                uint32_t preview_start = millis();
                while ((millis() - preview_start < PREVIEW_LIMIT_MS) && !enter_date_set) {
                    execute_show(current_advent_day);
                }
                set_hardware_led(0);
                while (!(PORTB.IN & PIN2_bm));
                _delay_ms(50);
                
                if (enter_date_set) break; 

                remaining_sleep_ms = 86400000UL - (millis() - run_start_ms);
                RTC.PITINTCTRL = RTC_PI_bm; 
            } else {
                if (remaining_sleep_ms > 1000) {
                    remaining_sleep_ms -= 1000;
                } else {
                    remaining_sleep_ms = 0;
                }
            }
        }
        RTC.PITINTCTRL = 0;
    }
}