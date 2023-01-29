# spr-bmu-firmware

Firmware for Battery Management Unit (BMS, IMD evaluation, AIR control)  used in SPR21evo and SPR23e

## Known issues BMU SPR22e

- Hardware is susceptible to EMI: Controller restarts in 1 out of 3 times while turning TS on or at high motor RPM (strange restart reason: JTAG)
- Isolation resistance evaluation is not implemented (IMD PWM protocol)
- Logging is quite slow (1s period)
- File system gets corrupted after some time (possibly a result of EMI resets)
- Logging data is incomplete (SOC is missing)
- Coulomb counter is very inaccurate
- Diag protocol not fully implemented
- Watchdog not fully validated

## Roadmap SPR23e

- Reduce system complexity (Only one ADC for voltage and current measurement instead of three)
- Less temperature sensors due to new cells
    - Increased update frequency for voltage and temperature values
- Two CAN-Busses
    - One for vehicle integration with only the minimal amount of data required
    - Reduce the packaging density of the CAN messages for less headaches
    - Second CAN bus only for diagnosing purposes with all measurement values, remote calibration and logfile download
    - logfile download using ISO-TP (https://github.com/lishen2/isotp-c)
- Increase logging speed (100ms instaed of 1s)
- Trace the bug that corrupts the file system
- Implementation of the Bender IR155 PWM protocol to provide the isolation resistance measurement to the HMI
- Maybe: Implement CAN bootloader (https://www.feaser.com/en/blog/2020/04/nxp-s32k-support-in-the-openblt-bootloader/)
- Update Coulomb counter and SOC(OCV) LUT for new Cells
- Fully implement diag protocol and logging data
- Validate watchdog

