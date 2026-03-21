#!../../bin/linux-x86_64/bcm-rf-ioc

# =====================================================================
# Bergoz BCM-RF-E IOC Startup Script
# Connection via local serial port (USB CDC or RS-232)
#
# Usage: Edit the serial device path, baud rate, and PV prefix below.
# =====================================================================

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/bcm-rf-ioc.dbd"
bcm_rf_ioc_registerRecordDeviceDriver pdbbase

# =====================================================================
# CONNECTION CONFIGURATION
# =====================================================================

# --- Option 2: Local serial port (USB CDC virtual COM port) ---
# drvAsynSerialPortConfigure("PORT_NAME", "DEVICE_PATH", priority, noAutoConnect, noProcessEos)
drvAsynSerialPortConfigure("BCM1", "/dev/ttyACM0", 0, 0, 0)

# Serial port settings
# The BCM-RF-E uses USB CDC serial. Default: 9600, 8N1
asynSetOption("BCM1", 0, "baud",     "9600")
asynSetOption("BCM1", 0, "bits",     "8")
asynSetOption("BCM1", 0, "parity",   "none")
asynSetOption("BCM1", 0, "stop",     "1")
asynSetOption("BCM1", 0, "clocal",   "Y")
asynSetOption("BCM1", 0, "crtscts",  "N")

# --- Debugging (uncomment to enable) ---
#asynSetTraceMask("BCM1", 0, 0x9)    # ERROR + DRIVER
#asynSetTraceIOMask("BCM1", 0, 0x2)  # HEX I/O trace

# =====================================================================
# StreamDevice Configuration
# =====================================================================

epicsEnvSet("STREAM_PROTOCOL_PATH", "$(TOP)/db")

# =====================================================================
# Load Database Records
# =====================================================================

# BCM-RF-E main database
# P    = PV prefix
# PORT = asyn port name
# SCAN = measurement scan period (optional, default "1 second")
dbLoadRecords("db/bcm-rf.db", "P=BCM:RF:01,PORT=BCM1,SCAN=1 second")

# =====================================================================
# IOC Initialization
# =====================================================================

cd "${TOP}/iocBoot/${IOC}"
iocInit

# Post-init status
epicsThreadSleep 1
dbl
