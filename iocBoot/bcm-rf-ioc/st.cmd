#!../../bin/linux-x86_64/bcm-rf-ioc

# =====================================================================
# Bergoz BCM-RF-E IOC Startup Script
# Connection via transparent ethernet-to-serial converter (TCP/IP)
#
# Usage: Edit the IP address, port, and PV prefix below.
# =====================================================================

< envPaths

cd "${TOP}"

## Register all support components
dbLoadDatabase "dbd/bcm-rf-ioc.dbd"
bcm_rf_ioc_registerRecordDeviceDriver pdbbase

# =====================================================================
# CONNECTION CONFIGURATION
# =====================================================================

# --- Option 1: ser2net on plsparcenea001.lnf.infn.it (TCP/IP) ---
# /dev/ttyACM0 -> ser2net port 4001 (raw TCP, 9600 8N1)
# drvAsynIPPortConfigure("PORT_NAME", "HOST:PORT", priority, noAutoConnect, noProcessEos)
drvAsynIPPortConfigure("BCM1", "192.168.197.24:4001", 0, 0, 0)

# --- Debugging (uncomment to enable) ---
#asynSetTraceMask("BCM1", 0, 0x9)    # ERROR + DRIVER
#asynSetTraceIOMask("BCM1", 0, 0x2)  # HEX I/O trace

# Disconnect on read timeout to recover from stale connections
asynSetOption("BCM1", 0, "disconnectOnReadTimeout", "Y")

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
dbLoadRecords("db/bcm-rf.db", "P=BCM:RF:01,PORT=BCM1")

# =====================================================================
# IOC Initialization
# =====================================================================

cd "${TOP}/iocBoot/${IOC}"
iocInit

# Post-init status
epicsThreadSleep 1
dbl
