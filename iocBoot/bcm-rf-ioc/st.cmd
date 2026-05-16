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

# Native asyn driver for unsolicited measurement/trigger frames and
# command/reply transactions used by StreamDevice records.
# bcmAsynMonConfigure("PORT_NAME", "HOST", TCP_PORT, reconnect_ms)
bcmAsynMonConfigure("BCM_MON", "192.168.197.24", 4001, 1000)

# --- Debugging (uncomment to enable) ---
#asynSetTraceMask("BCM_MON", 0, 0x9)    # ERROR + DRIVER
#asynSetTraceIOMask("BCM_MON", 0, 0x2)  # HEX I/O trace

# Disconnect on read timeout to recover from stale connections
# asynSetOption("BCM_MON", 0, "disconnectOnReadTimeout", "Y")

# =====================================================================
# Load Database Records
# =====================================================================

# BCM-RF-E main database
# P    = PV prefix
# PORT = asyn port name
# SCAN = measurement scan period
dbLoadRecords("db/bcm-rf.db", "P=SPARC:DIAG:TURBOBCM,PORT=BCM_MON,SCAN=.1 second")

# =====================================================================
# IOC Initialization
# =====================================================================

cd "${TOP}/iocBoot/${IOC}"
iocInit

# Post-init status
epicsThreadSleep 1
dbl
