# Bergoz BCM-RF-E EPICS IOC

EPICS IOC for the **Bergoz Turbo-ICT & BCM-RF-E** Beam Charge Monitor.

Based on *BCM-RF Manual Rev. 2.4* (Nov. 2017), firmware ≥ 2.5, hardware rev. 204.4.

## Features

- **Bunch charge measurement** (Sample & Hold mode, up to 2 MHz rep rate)
- **Average beam current measurement** (Track-Continuous mode, 10 MHz bandwidth)
- Full device configuration: mode, trigger, hold delay, ADC averaging
- Calibration constant management (Ucal, Qcal, Ical)
- Temperature-corrected voltage calculation
- Reverse transfer function: automatic charge/current conversion from output voltage
- CAL-FO calibrated pulse generator support
- EEPROM configuration save
- CS-Studio BOB operator screen

## Connection

The BCM-RF-E communicates via USB CDC serial. The IOC supports two connection methods:

### 1. Ethernet-to-serial converter (TCP/IP) — recommended

Use `st.cmd` and edit the IP address and port:

```bash
drvAsynIPPortConfigure("BCM1", "192.168.1.100:4001", 0, 0, 0)
```

Configure the converter for: **9600 baud, 8 data bits, 1 stop bit, no parity**.

### 2. Direct USB serial

Use `st-serial.cmd` and edit the device path:

```bash
drvAsynSerialPortConfigure("BCM1", "/dev/ttyACM0", 0, 0, 0)
```

## Building

Prerequisites: EPICS base, asyn, StreamDevice, calc modules.

Edit `configure/RELEASE` to set the correct paths to your EPICS installation:

```makefile
SUPPORT=/epics/support
ASYN=$(SUPPORT)/asyn
STREAMDEVICE=$(SUPPORT)/StreamDevice
CALC=$(SUPPORT)/calc
AUTOSAVE=$(SUPPORT)/autosave
EPICS_BASE=/epics/epics-base/
```

Build:

```bash
make clean
make
```

## Running

```bash
cd iocBoot/bcm-rf-ioc
# TCP/IP connection:
../../bin/linux-x86_64/bcm-rf-ioc st.cmd
# Serial connection:
../../bin/linux-x86_64/bcm-rf-ioc st-serial.cmd
```

## PV Reference

All PVs use the prefix set by the `P` macro (default: `BCM:RF:01`).

### Measurement

| PV | Type | Description |
|----|------|-------------|
| `$(P):MEAS_RAW_RB` | longin | Raw ADC value (µV or fC/nA) |
| `$(P):VOLTAGE_RB` | calc | Output voltage [V] |
| `$(P):CHARGE_RB` | calc | Bunch charge [pC] (S&H mode) |
| `$(P):CURRENT_RB` | calc | Average current [µA] (T-C mode) |
| `$(P):VOLTAGE_CORR_RB` | calc | Temperature-corrected voltage [V] |
| `$(P):TRIGGER_RB` | longin | Trigger notification (S&H) |

### Mode Configuration

| PV | Type | Description |
|----|------|-------------|
| `$(P):MEAS_MODE_SP` / `_RB` | bo/bi | Measurement mode (T-C / S&H) |
| `$(P):TRIG_MODE_SP` / `_RB` | bo/bi | Trigger mode (External / On-board) |
| `$(P):INT_CLK_SP` / `_RB` | bo/bi | Internal clock (Off / On) |
| `$(P):DELAY_CTRL_SP` / `_RB` | bo/bi | Hold delay source (Digital / Trimmer) |
| `$(P):MODE_SH_CMD` | bo | Quick switch to Sample & Hold |
| `$(P):MODE_TC_CMD` | bo | Quick switch to Track-Continuous |
| `$(P):MODE_DISP` | mbbi | Combined mode display string |

### Hold Delay & Sampling

| PV | Type | Description |
|----|------|-------------|
| `$(P):HOLD_DELAY_SP` / `_RB` | longout/longin | Hold delay [ns] (0–255) |
| `$(P):ADC_SAMPLES_SP` / `_RB` | longout/longin | ADC samples per trigger (1–65535) |

### Special Modes

| PV | Type | Description |
|----|------|-------------|
| `$(P):CALFO_SP` / `_RB` | bo/bi | CAL-FO mode (Off / On) |
| `$(P):REV_FUNC_SP` / `_RB` | bo/bi | Reverse function (Off / On) |
| `$(P):SAVE_CFG_CMD` | bo | Save config to EEPROM |

### Calibration Constants

| PV | Type | Description |
|----|------|-------------|
| `$(P):UCAL_SP` | ao | Ucal calibration constant [V] |
| `$(P):QCAL_SP` | ao | Qcal calibration constant [pC] |
| `$(P):ICAL_SP` | ao | Ical calibration constant [µA] |
| `$(P):TEMP_COEFF_BCM_SP` | ao | Temperature coefficient BCM-RF-E [V/K] |
| `$(P):TEMP_COEFF_TURBO_SP` | ao | Temperature coefficient Turbo-ICT [V/K] |
| `$(P):TEMP_REF_SP` | ao | Reference calibration temperature [°C] |
| `$(P):TEMP_CABINET` | ai | Cabinet ambient temperature [°C] |
| `$(P):TEMP_TURBOICT` | ai | Turbo-ICT ambient temperature [°C] |

### Device Calibration (raw IEEE754 hex)

| PV | Type | Description |
|----|------|-------------|
| `$(P):CAL_QI_HI_SP` / `_RB` | longout/longin | Qcal/Ical upper 16-bit |
| `$(P):CAL_QI_LO_SP` / `_RB` | longout/longin | Qcal/Ical lower 16-bit |
| `$(P):CAL_U_HI_SP` / `_RB` | longout/longin | Ucal upper 16-bit |
| `$(P):CAL_U_LO_SP` / `_RB` | longout/longin | Ucal lower 16-bit |

## Protocol Details

The BCM-RF-E communicates using ASCII character strings terminated by `\n\0` (LF + NULL).

**Command frame** (host → device): `FrameType` `FrameNum` `:` `HexValue(4)` `\n\0`

**Query frame** (host → device): `FrameType` `FrameNum` `?` `\n\0`

**Response frame** (device → host): `FrameType` `FrameNum` `:` `Counter(4)` `=` `HexValue(8)` `\n\0`

The device **auto-sends ADC measurement data** as `A0:nnnn=vvvvvvvv` frames. This may occasionally interfere with query-response commands. Readback records use `PINI=YES` for initialization and will recover on subsequent scans.

### Sensitivity Formulas

- **S&H mode**: $Q_{in} = Q_{cal} \times 10^{V_{out} / U_{cal}}$ [pC]
- **T-C mode**: $I_{in} = I_{cal} \times 10^{V_{out} / U_{cal}}$ [µA]

### Temperature Correction

$$V_{corr} = V_{meas} + c_{BCM} \cdot (T_{cabinet} - T_{ref}) + c_{Turbo} \cdot (T_{TurboICT} - T_{ref})$$

## File Structure

```
turbo-bergoz-bcm/
├── Makefile                          # Top-level build
├── README.md                         # This file
├── configure/                        # EPICS build configuration
│   ├── CONFIG, CONFIG_SITE
│   ├── RELEASE                       # Module dependencies
│   ├── RULES, RULES.ioc, RULES_DIRS, RULES_TOP
│   └── Makefile
├── bcmApp/                           # Application
│   ├── Makefile
│   ├── src/
│   │   ├── Makefile                  # IOC binary build
│   │   └── bcmMain.cpp              # IOC main entry point
│   └── Db/
│       ├── Makefile
│       ├── bcm-rf.proto             # StreamDevice protocol
│       └── bcm-rf.db               # EPICS database
├── iocBoot/
│   ├── Makefile
│   └── bcm-rf-ioc/
│       ├── Makefile
│       ├── st.cmd                   # Startup: TCP/IP connection
│       └── st-serial.cmd           # Startup: serial connection
├── opi/
│   └── bcm-rf.bob                   # CS-Studio operator screen
└── docs/
    └── BCM-RF Manual 2.4.txt       # Device manual
```

## Troubleshooting

### Null byte terminator

The BCM-RF-E protocol uses `\n\0` (LF + NULL byte) as the frame terminator.
If your ethernet-to-serial converter strips null bytes, edit `bcm-rf.proto` and change:

```
InTerminator  = "\n\x00";
OutTerminator = "\n\x00";
```

to:

```
InTerminator  = "\n";
OutTerminator = "\n";
```

### Auto-sent data interference

The device continuously sends ADC measurement data. Query-response
commands (readbacks) may occasionally fail due to interleaved measurement
frames. This is normal — records will show a brief COMM alarm and recover
on the next scan cycle.

To reduce interference, increase the ADC averaging count:
```
caput BCM:RF:01:ADC_SAMPLES_SP 100
```

### Debugging serial communication

Enable asyn debug traces by uncommenting in `st.cmd`:
```
asynSetTraceMask("BCM1", 0, 0x9)
asynSetTraceIOMask("BCM1", 0, 0x2)
```

## References

- Bergoz Instrumentation: https://www.bergoz.com
- BCM-RF Manual Rev. 2.4

## License

Internal use.
