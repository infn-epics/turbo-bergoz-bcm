TOP = ../..

include $(TOP)/configure/CONFIG

# Build support library
LIBRARY_IOC += bcmAsynMonSupport
bcmAsynMonSupport_SRCS += bcmAsynMon.cpp

# Build an IOC application
PROD_IOC = bcm-rf-ioc

# bcm-rf-ioc.dbd will be created and installed
DBD += bcm-rf-ioc.dbd

# DBD dependencies
bcm-rf-ioc_DBD += base.dbd
bcm-rf-ioc_DBD += asyn.dbd
bcm-rf-ioc_DBD += drvAsynIPPort.dbd
bcm-rf-ioc_DBD += drvAsynSerialPort.dbd
bcm-rf-ioc_DBD += calcSupport.dbd
bcm-rf-ioc_DBD += bcmAsynMon.dbd

# Source files
bcm-rf-ioc_SRCS += bcm-rf-ioc_registerRecordDeviceDriver.cpp
bcm-rf-ioc_SRCS += bcmMain.cpp

# Link libraries
bcm-rf-ioc_LIBS += bcmAsynMonSupport
bcm-rf-ioc_LIBS += asyn
bcm-rf-ioc_LIBS += calc
bcm-rf-ioc_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
