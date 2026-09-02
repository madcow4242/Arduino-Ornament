# Ornament Test/Validation Software

This directory contains the test and validation software for the Ornament Controller project, used to verify hardware functionality and perform calibration procedures.

## Purpose and Features

The test software serves to:
- Verify PCB hardware functionality and LED matrix operation
- Measure current usage (baseline, full LED power, PWM LED power)
- Calibrate LED brightness and timing characteristics
- Test basic functionality including button response, current sensing, and light sensor operation
- Validate microcontroller operation

## Usage

### Setup
1. Connect the ornament PCB to your development environment
2. Load the `Ornament_test_1.ino` sketch into the Arduino IDE
3. Select the appropriate board (ATtiny414)
4. Upload the code to the microcontroller

### Test Modes
The test software automatically runs through various test sequences, advancing to the next test each time the button is pressed.  The tests included are, in order:
1. **LED Matrix Test**: Cycles through each LED to verify proper operation, while outputting power / current usage via UART.  It also displays the current light level from the light sensor.
2. **Brightness Calibration**: Tests different PWM levels to help choose the right calibration value.  Each LED cycles through all brightness levels starting at 31, down to 1, while displaying a calibrated reference LED at the same time.  The goal is to visually match the brightness of the test LED to the reference LED, so you can update the calibration table with the correct value.  Simply watch the blinking LED and count down from 31 until the brightness matches, and use that value in the calibration table.
3. **Pattern Verification**: Runs basic LED patterns to verify timing - illuminating up to 12 LEDs concurrently via PWM, and displaying fade-in-fade-out functionality.
4. **System Check**: Verifies button response and sleep/wake functionality

## Hardware Requirements

- Ornament PCB with ATtiny414 microcontroller
- USB-to-Serial adapter for programming
- Power source (Via programmer connection, UART connection, or battery)
- Serial monitor for test output (optional) - 9600 baud

## Troubleshooting

If tests fail:
1. Check all component connections
2. Verify power supply voltage (should be 3.3V to 5V)
3. Confirm correct board selection in Arduino IDE
4. Check for loose or damaged connections
5. Ensure proper grounding

## License

This project is licensed under the MIT License - see the LICENSE file for details.