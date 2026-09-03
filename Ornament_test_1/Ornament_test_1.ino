// ============================================================================
// Ornament Controller - Test and Calibration Routine (v0.6.4)
// Target Hardware: Microchip ATtiny414 (tinyAVR 1-Series)
// ============================================================================

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdlib.h>

#define PWM_MAX 32                  

#define MODE0_ON_TIME_MS 300
#define MODE0_OFF_TIME_MS 100

#define LED_ON_TIME_MS 300  
#define LEVEL_DELAY_MS 75    
      
#define MAX_GROUP_LEDS 4           
#define NUM_GROUPS 3               

#define GROUP_CYCLE_MS 1000

#define ENABLE_CSA_READINGS 1   

#define GLOBAL_BRIGHTNESS_LEVEL 70

typedef struct {
    int8_t hi; 
    int8_t lo; 
} LEDMap;

typedef struct {
    uint8_t num;    
    uint8_t level; 
} LEDPair;

static inline uint8_t scale_pwm_val(uint32_t lvl, uint8_t cal) {
    uint32_t l = (lvl * cal) / PWM_MAX;
    l = (l * GLOBAL_BRIGHTNESS_LEVEL) / 50;
    if (l == 0 && lvl > 0) l = 1;
    if (l > PWM_MAX) l = PWM_MAX;
    return (uint8_t)l;
}

const uint8_t calibration_table[30] = {
    14, 14, 30, 7, 10, 7, 14, 14, 30, 10,     
    7, 14, 14, 30, 7, 10, 14, 14, 30, 10,     
    7, 14, 14, 30, 18, 9, 9, 9, 9, 9          
};

const uint8_t reference_leds[] = {3, 9, 14, 19, 24};    
const uint8_t led_pin_masks[] = {0x04, 0x08, 0x10, 0x20, 0x40, 0x80}; 

const LEDMap charlie_map[30] = {
    {5,4}, {4,5}, {5,3}, {3,5}, {4,3}, {3,4}, {5,2}, {2,5}, {4,2}, {2,4}, 
    {3,2}, {2,3}, {5,1}, {1,5}, {4,1}, {1,4}, {3,1}, {1,3}, {2,1}, {1,2}, 
    {5,0}, {0,5}, {4,0}, {0,4}, {3,0}, {0,3}, {2,0}, {0,2}, {1,0}, {0,1}  
};

volatile uint8_t current_mode = 0;          
volatile uint8_t mode_changed = 0;          
volatile unsigned long last_button_press = 0;

ISR(PORTB_PORT_vect) {
    if (PORTB.INTFLAGS & PIN2_bm) {
        unsigned long now = millis();
        if (now - last_button_press > 250) {
            current_mode = (current_mode + 1) % 3; 
            mode_changed = 1;                     
            last_button_press = now;       
        }
        PORTB.INTFLAGS = PIN2_bm; 
    }
}

void uart_send(char c) {
    uint8_t sreg = SREG;
    cli(); 
    PORTB.OUTCLR = PIN3_bm;
    _delay_us(104);
    for (uint8_t i = 0; i < 8; i++) {
        if (c & (1 << i)) PORTB.OUTSET = PIN3_bm;
        else PORTB.OUTCLR = PIN3_bm;
        _delay_us(104);
    }
    PORTB.OUTSET = PIN3_bm;
    _delay_us(104);
    SREG = sreg; 
}

void uart_print_uint(uint16_t n) {
    char buf[6];
    int8_t i = 0;
    if (n == 0) { uart_send('0'); return; }
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    while (i > 0) { uart_send(buf[--i]); }
}

void uart_print_current_1dp(uint16_t ma_x10) {
    uart_print_uint(ma_x10 / 10);
    uart_send('.');
    uart_print_uint(ma_x10 % 10);
}

void uart_print_P(const char* str) {
    char c;
    while ((c = pgm_read_byte(str++))) { 
        uart_send(c);
        _delay_us(150); 
    }
}

uint8_t get_cal_val(uint8_t idx) { return calibration_table[idx]; }

void get_led_map(uint8_t idx, LEDMap* map) {
    map->hi = charlie_map[idx].hi;
    map->lo = charlie_map[idx].lo;
}

void set_hardware_led(uint8_t num) {
    PORTA.DIRCLR = 0xFC; 
    PORTA.OUTCLR = 0xFC; 
    if (num < 1 || num > 30) return; 
    LEDMap pair;
    get_led_map(num - 1, &pair);
    PORTA.DIRSET = led_pin_masks[pair.hi] | led_pin_masks[pair.lo]; 
    PORTA.OUTSET = led_pin_masks[pair.hi]; 
}

uint8_t leds_share_pins(uint8_t led1, uint8_t led2) {
    if (led1 < 1 || led1 > 30 || led2 < 1 || led2 > 30) return 1;
    LEDMap map1, map2;
    get_led_map(led1 - 1, &map1);
    get_led_map(led2 - 1, &map2);
    return (map1.hi == map2.hi || map1.hi == map2.lo || map1.lo == map2.hi || map1.lo == map2.lo);
}

uint8_t find_reference_led(uint8_t test_led) {
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t ref = reference_leds[i];
        if (ref < 1 || ref > 30) continue;
        if (ref == test_led || leds_share_pins(test_led, ref)) continue; 
        return ref; 
    }
    return 0; 
}

__attribute__((noinline)) uint8_t select_single_channel_leds(LEDPair channel_leds[MAX_GROUP_LEDS], const LEDPair all_groups[NUM_GROUPS][MAX_GROUP_LEDS]) {
    uint8_t count = 0;
    uint8_t attempts = 0;
    for (uint8_t i = 0; i < MAX_GROUP_LEDS; i++) channel_leds[i].num = 0;

    while (count < MAX_GROUP_LEDS && attempts < 25) {
        uint8_t led = ((uint8_t)rand() % 30) + 1;
        if (led == 25) { attempts++; continue; }

        uint8_t conflict = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (leds_share_pins(led, channel_leds[i].num)) { conflict = 1; break; }
        }
        if (!conflict && all_groups != NULL) {
            for (uint8_t og = 0; og < NUM_GROUPS; og++) {
                for (uint8_t i = 0; i < MAX_GROUP_LEDS; i++) {
                    if (all_groups[og][i].num > 0 && leds_share_pins(led, all_groups[og][i].num)) {
                        conflict = 1;
                        break;
                    }
                }
                if (conflict) break;
            }
        }

        if (!conflict) {
            channel_leds[count].num = led;
            channel_leds[count].level = scale_pwm_val(PWM_MAX, get_cal_val(led - 1));
            count++;
            attempts = 0;
        } else {
            attempts++;
        }
    }
    return count;
}

// ============================================================================
// UNIFIED ADC READING ROUTINE
// Handles configuration, reference selection, pin mapping, stabilization, 
// and raw conversion all in one place.
// ============================================================================
uint16_t read_adc_channel_raw(uint8_t mux_channel, uint16_t stabilization_delay_us) {
    // 1. Disable ADC to safely reconfigure
    ADC0.CTRLA = 0;
    
    // 2. Configure 4.34V internal reference and clock prescaler
    VREF.CTRLA = VREF_ADC0REFSEL_4V34_gc;    
    ADC0.CTRLC = ADC_PRESC_DIV16_gc | ADC_REFSEL_INTREF_gc;  
    
    // 3. Enable ADC
    ADC0.CTRLA = ADC_ENABLE_bm;            
    
    // 4. Configure requested MUX channel and ensure input buffer control
    if (mux_channel == ADC_MUXPOS_AIN10_gc) {
        PORTB.PIN1CTRL &= ~PORT_ISC_gm;
        PORTB.PIN1CTRL |= PORT_ISC_INPUT_DISABLE_gc;
        PORTB.DIRCLR = PIN1_bm; 
    } else if (mux_channel == ADC_MUXPOS_AIN1_gc) {
        PORTA.PIN3CTRL &= ~PORT_ISC_gm;
        PORTA.PIN3CTRL |= PORT_ISC_INPUT_DISABLE_gc;
        PORTA.DIRCLR = PIN3_bm; 
    }
    
    ADC0.MUXPOS = mux_channel;      
    
    // 5. Wait for user-configurable stabilization time
    if (stabilization_delay_us > 0) {
        delayMicroseconds(stabilization_delay_us);
    }
    
    // 6. Perform conversion
    ADC0.INTFLAGS = ADC_RESRDY_bm;
    ADC0.COMMAND = ADC_STCONV_bm;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm));
    return ADC0.RES;
}

// ============================================================================
// SPECIALIZED MEASUREMENT WRAPPERS
// ============================================================================
uint16_t measure_current_x10(void) {
    if (!ENABLE_CSA_READINGS) return 0;
    
    // Read AIN10 (PB1) with a 100us stabilization delay
    uint16_t raw = read_adc_channel_raw(ADC_MUXPOS_AIN10_gc, 100);
    
    // Convert raw ADC counts to current in 0.1mA units (x10) using 4.34V scale factor
    return (uint16_t)(((uint32_t)raw * 434UL) / 1024UL);
}

uint8_t measure_light_sensor(void) {
    // Enable light sensor power pin
    PORTB.OUTSET = PIN0_bm; 
    
    // Read AIN1 (PA1/PB1 depending on board, using AIN1 here) with a 100us stabilization delay
    uint16_t raw = read_adc_channel_raw(ADC_MUXPOS_AIN1_gc, 100);
    
    // Disable light sensor power pin
    PORTB.OUTCLR = PIN0_bm; 
    
    // Convert raw value to percentage
    return (uint8_t)((raw * 100UL) / 1023);
}

void drive_multi_group_pwm(const LEDPair groups[NUM_GROUPS][MAX_GROUP_LEDS], const uint8_t group_counts[NUM_GROUPS], uint16_t duration_ms) {
    if (duration_ms == 0) return;
    uint32_t start_time = millis();

    while ((millis() - start_time) < duration_ms) {
        if (mode_changed) return;
        for (uint8_t g = 0; g < NUM_GROUPS; g++) {
            uint8_t count = group_counts[g];
            uint8_t total_active_units = 0;
            for (uint8_t i = 0; i < count; i++) {
                if (groups[g][i].level > 0 && groups[g][i].num >= 1 && groups[g][i].num <= 30) {
                    total_active_units += groups[g][i].level;
                }
            }
            uint8_t off_units = (total_active_units < PWM_MAX) ? (PWM_MAX - total_active_units) : 0;

            for (uint8_t i = 0; i < count; i++) {
                uint8_t lvl = groups[g][i].level;
                if (lvl > 0 && groups[g][i].num >= 1 && groups[g][i].num <= 30) {
                    set_hardware_led(groups[g][i].num);
                    for (uint8_t l = 0; l < lvl; l++) _delay_us(20);
                }
            }
            set_hardware_led(0);
            for (uint8_t l = 0; l < off_units; l++) _delay_us(20);
        }
    }
}

void mode_0_test() {
    uart_print_P(PSTR("Mode 0 - Current Test (Raw Diagnostics Enabled)\r\n"));
    for (uint8_t n = 1; n <= 30; n++) {
        if (mode_changed) break; 
        set_hardware_led(0); 
        _delay_ms(500); // Wait for full stabilization before baseline measurement
        
        // Capture raw ADC count directly for debugging baseline voltage
        uint16_t base_raw = read_adc_channel_raw(ADC_MUXPOS_AIN10_gc, 100);
        uint16_t base_cur = (uint16_t)(((uint32_t)base_raw * 434UL) / 1024UL);
        
        // Measure current and light level while sensor power (PIN0) is active
        uint16_t sensor_cur = 0;
        uint8_t light_level = 0;
        if (ENABLE_CSA_READINGS) {
            PORTB.OUTSET = PIN0_bm; 
            _delay_us(100);
            
            uint16_t raw_cur = read_adc_channel_raw(ADC_MUXPOS_AIN2_gc, 50);
            sensor_cur = (uint16_t)(((uint32_t)raw_cur * 434UL) / 1024UL);

            light_level = measure_light_sensor(); 
            PORTB.OUTCLR = PIN0_bm; 
        } else {
            light_level = measure_light_sensor();
        }

        set_hardware_led(n);                                    
        _delay_ms(1000); // Longer stabilization time for LED measurement
        
        // Capture raw ADC count for full LED load
        uint16_t full_raw = read_adc_channel_raw(ADC_MUXPOS_AIN10_gc, 100);
        uint16_t full_cur = (uint16_t)(((uint32_t)full_raw * 434UL) / 1024UL);
        
        int16_t full_delta = (int16_t)full_cur - (int16_t)base_cur;
        if (full_delta < 0) full_delta = 0;

        uint8_t cal_val = get_cal_val(n - 1);
        uint16_t pwm_cur = ((uint32_t)full_cur * scale_pwm_val(PWM_MAX, cal_val)) / PWM_MAX;
        int16_t pwm_delta = (int16_t)pwm_cur - (int16_t)base_cur;
        if (pwm_delta < 0) pwm_delta = 0;
        
        set_hardware_led(0);
        _delay_ms(500); 

        LEDPair groups[NUM_GROUPS][MAX_GROUP_LEDS] = {{{n, scale_pwm_val(cal_val, cal_val)}}};
        uint8_t counts[NUM_GROUPS] = {1, 0, 0};
        drive_multi_group_pwm(groups, counts, 300);

        // Print formatted output including RAW ADC counts and mV for verification
        uart_print_P(PSTR("D"));
        uart_print_uint(n);
        uart_print_P(PSTR(" | Base Raw: "));
        uart_print_uint(base_raw);
        uart_print_P(PSTR(" ("));
        uart_print_current_1dp(base_cur);
        uart_print_P(PSTR("mA) | Full Raw: "));
        uart_print_uint(full_raw);
        uart_print_P(PSTR(" ("));
        uart_print_current_1dp(full_cur);
        uart_print_P(PSTR("mA) | Light: "));
        uart_print_uint(light_level);
        uart_print_P(PSTR("%\r\n"));

        _delay_ms(MODE0_OFF_TIME_MS);
    }
}

void calibrate_stabilization_times() {
    uart_print_P(PSTR("Calibration: Stabilization Times\r\n"));
    
    uart_print_P(PSTR("System check - measuring baseline with no LEDs or external power\r\n"));
    set_hardware_led(0);
    _delay_ms(2000); 
    
    uint32_t baseline_sum = 0;
    uint8_t baseline_count = 10;
    for (uint8_t i = 0; i < baseline_count; i++) {
        baseline_sum += measure_current_x10();
        _delay_ms(100); 
    }
    uint16_t system_baseline = baseline_sum / baseline_count;
    
    uart_print_P(PSTR("System baseline current: "));
    uart_print_current_1dp(system_baseline);
    uart_print_P(PSTR("mA (avg of "));
    uart_print_uint(baseline_count);
    uart_print_P(PSTR(" readings)\r\n"));
    
    if (system_baseline > 200) { 
        uart_print_P(PSTR("WARNING: System baseline is unexpectedly high! Check power supply and connections.\r\n"));
        uart_print_P(PSTR("Proceeding with LED tests anyway, but results may be affected.\r\n"));
    }
    
    const uint8_t test_leds[] = {2,3,6,9,17,24,25};
    uint8_t num_test_leds = sizeof(test_leds) / sizeof(test_leds[0]);
    
    for (uint8_t led_idx = 0; led_idx < num_test_leds; led_idx++) {
        uint8_t test_led = test_leds[led_idx];
        
        uart_print_P(PSTR("Testing LED "));
        uart_print_uint(test_led);
        uart_print_P(PSTR("\r\n"));
        
        uint32_t baseline_sum = 0;
        uint8_t baseline_count = 5;
        for (uint8_t i = 0; i < baseline_count; i++) {
            set_hardware_led(0);
            _delay_ms(1000); 
            baseline_sum += measure_current_x10();
            _delay_ms(50); 
        }
        uint16_t base_current = baseline_sum / baseline_count;
        
        uart_print_P(PSTR("  Baseline current: "));
        uart_print_current_1dp(base_current);
        uart_print_P(PSTR("mA (avg of "));
        uart_print_uint(baseline_count);
        uart_print_P(PSTR(" readings)\r\n"));
        
        set_hardware_led(test_led);
        _delay_ms(500); 
        uint16_t current_500ms = measure_current_x10();
        uart_print_P(PSTR("  After 500ms: "));
        uart_print_current_1dp(current_500ms);
        uart_print_P(PSTR("mA\r\n"));
        
        _delay_ms(500); 
        uint16_t current_1000ms = measure_current_x10();
        uart_print_P(PSTR("  After 1000ms: "));
        uart_print_current_1dp(current_1000ms);
        uart_print_P(PSTR("mA\r\n"));
        
        _delay_ms(500); 
        uint16_t current_1500ms = measure_current_x10();
        uart_print_P(PSTR("  After 1500ms: "));
        uart_print_current_1dp(current_1500ms);
        uart_print_P(PSTR("mA\r\n"));
        
        set_hardware_led(0);
        _delay_ms(500); 
        
        uart_print_P(PSTR("  Test complete\r\n"));
    }
    
    uart_print_P(PSTR("Stabilization test complete\r\n"));
}

void mode_1_calibration() {
    uart_print_P(PSTR("Mode 1 - Brightness Calibration\r\n"));
    for (uint8_t n = 1; n <= 30; n++) {
        if (mode_changed) break;
        uint8_t ref_led = find_reference_led(n);
        
        LEDPair groups[NUM_GROUPS][MAX_GROUP_LEDS] = {{{0}}};
        uint8_t counts[NUM_GROUPS] = { (uint8_t)((ref_led > 0) ? 2 : 1), 0, 0 };
        groups[0][0].num = n;
        groups[0][0].level = scale_pwm_val(PWM_MAX, get_cal_val(n - 1));
        if (ref_led > 0) {
            groups[0][1].num = ref_led;
            groups[0][1].level = scale_pwm_val(PWM_MAX, get_cal_val(ref_led - 1));
        }

        for (int16_t level = PWM_MAX; level >= 0; level--) {
            if (mode_changed) break;
            groups[0][0].level = scale_pwm_val(level, get_cal_val(n - 1));
            drive_multi_group_pwm(groups, counts, LED_ON_TIME_MS);
            set_hardware_led(0);
            _delay_ms(LEVEL_DELAY_MS);
        }
    }
}

int main(void) {
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0x00);
    init();

    PORTB.DIRSET = PIN3_bm | PIN0_bm; 
    PORTB.OUTSET = PIN3_bm; 
    PORTB.OUTCLR = PIN0_bm; 

    PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc; 

    // Seed random generator using an initial light sensor read via our unified routine
    PORTB.OUTSET = PIN0_bm;
    uint16_t seed_raw = read_adc_channel_raw(ADC_MUXPOS_AIN1_gc, 100);
    srand(seed_raw);
    PORTB.OUTCLR = PIN0_bm;

    sei(); 

    while (1) {
        mode_changed = 0; 
        if (current_mode == 0) mode_0_test();
        else if (current_mode == 1) mode_1_calibration();
        else if (current_mode == 2) calibrate_stabilization_times();
    }
}