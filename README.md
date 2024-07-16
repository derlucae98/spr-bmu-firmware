# spr-bmu-firmware

Firmware for Battery Management Unit (BMS, IMD evaluation, AIR control) used in SPR21evo and SPR23e

## Known issues BMU SPR21evo

- Hardware is susceptible to EMI: Controller restarts in 1 out of 3 times while turning TS on or at high motor RPM (strange restart reason: JTAG)
- Isolation resistance evaluation is not implemented (IMD PWM protocol)
- Logging is quite slow (1s period)
- File system gets corrupted after some time (possibly a result of EMI resets)
- Logging data is incomplete (SOC is missing)
- Coulomb counter is very inaccurate
- Diag protocol not fully implemented
- Watchdog not fully validated

## Roadmap and current progress

- ✅ Reduce system complexity (Only one ADC for voltage and current measurement instead of three)
- ✅ Increase error verbosity. It is nice to know that an error occurred but it would also be nice to know where and/or why it occurred
- ✅ Two CAN-Busses
    - ✅ One for vehicle integration with only the minimal amount of data required 
    - ✅ Reduce the packaging density of the CAN messages for less headaches
    - ✅ Second CAN bus only for diagnosing purposes with all measurement values, remote calibration ~~and logfile download~~
    - ~~logfile download using ISO-TP (https://github.com/lishen2/isotp-c)~~
- ~~Increase logging speed (100ms instaed of 1s)~~
- ~~Trace the bug that corrupts the file system~~
- Implementation of the Bender IR155 PWM protocol to provide the isolation resistance measurement to the HMI **(POSTPONED)**
- ~~Maybe: Implement CAN bootloader (https://www.feaser.com/en/blog/2020/04/nxp-s32k-support-in-the-openblt-bootloader/)~~
- Update Coulomb counter and SOC(OCV) LUT for new Cells
- ✅ Fully implement diag protocol ~~and logging data~~
- Validate watchdog

The available IsoTP implementations have turned out to be impracticable. Additionally, the integrated logger takes up a lot of RAM and CPU if the data is logged directly as CSV. The logger is very unreliable as well (SD card initialization issues, irregular crashes, etc.). Since the SD card is buried inside the sealed accumulator, IsoTP has been dropped and the reliability of the logger leaves much to be desired, the whole logger will be removed in a future update. Instead, the Diag CAN bus will be logged with the second channel of the canedge2 CAN logger. This also allows the battery data to directly be put in context with the vehicle data.

Implementation of the CAN bootloader would be nice but is postponed indefinitely. It is much easier to just leave the J-Link edu mini connected permanently and provide a USB-Socket in the accumulator wall (So-called TTC (through-the-case) update).  
The system has been extensively tested in the SPR21evo accumulator with no issues at all. All EMI problems seem to be gone!
Implementation of the Bender PWM protocol is postponed as well. Altough a nice feature, it is not necessary for a working car.


