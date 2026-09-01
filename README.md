# Ornament Controller

A custom LED ornament controller with advent calendar functionality, built using Arduino and KiCad.

## Project Structure

This project is organized into three main directories:

### 1. KiCad Files (`KiCad_PCB_Ornament/`)
Contains all KiCad design files for the PCB:
- Schematic files (.kicad_sch)
- PCB layout files (.kicad_pcb)
- Component libraries (.lib, .dcm)
- Footprint files (.kicad_mod)
- Exported fabrication files (.gbr, .drl)

NOTE: the board design includes optional current monitoring circuit and light sensing circuit.  Neither of these are implemented in the production software, however they are fully functional and usable in the test software.  Those features can be ported to production as needed.

### 2. Production Software (`Ornament_production_1/`)
The main firmware for the ornament controller:
- Main Arduino sketch (`Ornament_production_1.ino`)
- Configuration and constants
- LED control algorithms
- Date setting and brightness adjustment routines
- Software fits in 3987 bytes of flash, and uses 68 bytes of dynamic storage

### 3. Test/Validation Software (`Ornament_test_1/`)
Code for testing and validating the PCB functionality:
- Calibration routines
- Basic functionality tests
- Hardware verification code
- Software uses less than the ATTiny414's 4k of flash and 256 bytes of dynamic storage
- Includes ADC control for current monitoring and light level measurement, if needed (not used in production code)

## Features

- Advent calendar display (December 1-25)
- Custom LED patterns with twinkling effects, including PWM fade-in, fade-out
- Date setting via button interface
- Adjustable brightness levels (10% to 100%)
- Sleep/wake cycle automation
- Preview mode for manual activation during sleep, without affecting 24-hour auto-on timer

## Hardware Requirements

- Microchip ATtiny414 microcontroller (or others in that series)
- Custom PCB with LED matrix
- Power switch and button for user interface
- Battery or power supply for operation

## Getting Started

1. Clone this repository
2. Open the KiCad project to view or modify the PCB design
3. Use the Arduino IDE to compile and upload the production firmware
4. Test with the validation software to verify hardware functionality

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Notes

- Custom LCG (Linear Congruential Generator) for random number generation to save flash space
- Charlieplexing technique for LED control - it uses 6 GPIOs to allow individual addressability for 30 LEDs.
- PWM LED control for up to 12 LEDs simultaneously, using 3x 32-step groups for each PWM cycle (4 LEDs per cycle)
- (more LEDs can be controlled simultaneously via configuration, but will limit maximum LED brightness)