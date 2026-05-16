#include <asynPortDriver.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <epicsString.h>
#include <epicsTypes.h>
#include <iocsh.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

class BcmAsynMonDriver : public asynPortDriver {
public:
    BcmAsynMonDriver(const char *portName, const char *host, int tcpPort, int reconnectMs)
        : asynPortDriver(portName,
                         1,
                         asynInt32Mask | asynOctetMask | asynDrvUserMask,
                         asynInt32Mask | asynOctetMask,
                         ASYN_CANBLOCK,
                         1,
                         0,
                         0),
          host_(host ? host : "127.0.0.1"),
          tcpPort_(tcpPort),
          reconnectMs_(reconnectMs > 0 ? reconnectMs : 1000),
          monSock_(-1) {
        createParam("MEAS_RAW_HEX", asynParamInt32, &pMeasRawHex_);
        createParam("TRIGGER_RB", asynParamInt32, &pTriggerRb_);
        createParam("CONNECTED", asynParamInt32, &pConnected_);
        createParam("SWITCH_CFG_SP", asynParamInt32, &pSwitchCfgSp_);
        createParam("SWITCH_CFG_RB", asynParamInt32, &pSwitchCfgRb_);
        createParam("HOLD_DELAY_SP", asynParamInt32, &pHoldDelaySp_);
        createParam("HOLD_DELAY_RB", asynParamInt32, &pHoldDelayRb_);
        createParam("CALFO_SP", asynParamInt32, &pCalfoSp_);
        createParam("CALFO_RB", asynParamInt32, &pCalfoRb_);
        createParam("REV_FUNC_SP", asynParamInt32, &pReverseSp_);
        createParam("REV_FUNC_RB", asynParamInt32, &pReverseRb_);
        createParam("ADC_SAMPLES_SP", asynParamInt32, &pAdcSamplesSp_);
        createParam("ADC_SAMPLES_RB", asynParamInt32, &pAdcSamplesRb_);
        createParam("SERIAL_NUM_RB", asynParamInt32, &pSerialNumRb_);
        createParam("SAVE_CFG_CMD", asynParamInt32, &pSaveCfgCmd_);
        createParam("CAL_QI_HI_SP", asynParamInt32, &pCalQiHiSp_);
        createParam("CAL_QI_LO_SP", asynParamInt32, &pCalQiLoSp_);
        createParam("CAL_QI_HI_RB", asynParamInt32, &pCalQiHiRb_);
        createParam("CAL_QI_LO_RB", asynParamInt32, &pCalQiLoRb_);
        createParam("CAL_U_HI_SP", asynParamInt32, &pCalUHiSp_);
        createParam("CAL_U_LO_SP", asynParamInt32, &pCalULoSp_);
        createParam("CAL_U_HI_RB", asynParamInt32, &pCalUHiRb_);
        createParam("CAL_U_LO_RB", asynParamInt32, &pCalULoRb_);

        setIntegerParam(pMeasRawHex_, 0);
        setIntegerParam(pTriggerRb_, 0);
        setIntegerParam(pConnected_, 0);
        callParamCallbacks();

        epicsThreadCreate("BCM_MON", epicsThreadPriorityMedium, epicsThreadGetStackSize(epicsThreadStackMedium),
                          &BcmAsynMonDriver::runThreadC, this);
    }

    ~BcmAsynMonDriver() override {
        if (monSock_ >= 0) {
            ::close(monSock_);
        }
    }

    asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value) override {
        int function = pasynUser->reason;
        if (isQueryParam(function)) {
            epicsInt32 fresh = 0;
            if (!queryParam(function, fresh)) {
                return asynError;
            }
            setIntegerParam(function, fresh);
        }
        getIntegerParam(function, value);
        return asynSuccess;
    }

    asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value) override {
        int function = pasynUser->reason;
        if (!writeParam(function, value)) {
            return asynError;
        }
        setIntegerParam(function, value);
        callParamCallbacks();
        return asynSuccess;
    }

    asynStatus writeOctet(asynUser *pasynUser,
                          const char *value,
                          size_t maxChars,
                          size_t *nActual) override {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (!ensureMonitorConnected()) {
            return asynError;
        }

        ssize_t sent = ::send(monSock_, value, maxChars, 0);
        if (sent < 0) {
            disconnectMonitor();
            return asynError;
        }

        *nActual = static_cast<size_t>(sent);
        pasynUser->errorMessageSize = 0;
        return asynSuccess;
    }

    asynStatus readOctet(asynUser *pasynUser,
                         char *value,
                         size_t maxChars,
                         size_t *nActual,
                         int *eomReason) override {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (!ensureMonitorConnected()) {
            return asynError;
        }

        if (maxChars == 0) {
            *nActual = 0;
            if (eomReason) {
                *eomReason = ASYN_EOM_CNT;
            }
            return asynSuccess;
        }

        double timeout = pasynUser && pasynUser->timeout > 0.0 ? pasynUser->timeout : 1.0;
        struct timeval tv;
        tv.tv_sec = static_cast<int>(timeout);
        tv.tv_usec = static_cast<int>((timeout - tv.tv_sec) * 1e6);

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(monSock_, &rfds);
        int ready = ::select(monSock_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ready == 0) {
            *nActual = 0;
            if (eomReason) {
                *eomReason = 0;
            }
            return asynTimeout;
        }
        if (ready < 0) {
            disconnectMonitor();
            return asynError;
        }

        ssize_t n = ::recv(monSock_, value, maxChars, 0);
        if (n <= 0) {
            disconnectMonitor();
            return asynError;
        }

        *nActual = static_cast<size_t>(n);
        if (eomReason) {
            *eomReason = (static_cast<size_t>(n) == maxChars) ? ASYN_EOM_CNT : 0;
        }
        return asynSuccess;
    }

    asynStatus flushOctet(asynUser *pasynUser) override {
        (void)pasynUser;
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (monSock_ < 0) {
            return asynSuccess;
        }

        char dump[256];
        while (true) {
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(monSock_, &rfds);
            struct timeval tv{0, 0};
            int ready = ::select(monSock_ + 1, &rfds, nullptr, nullptr, &tv);
            if (ready <= 0) {
                break;
            }

            ssize_t n = ::recv(monSock_, dump, sizeof(dump), 0);
            if (n <= 0) {
                disconnectMonitor();
                break;
            }
        }
        return asynSuccess;
    }

private:
    static void runThreadC(void *arg) {
        static_cast<BcmAsynMonDriver *>(arg)->runThread();
    }

    void runThread() {
        std::string rx;
        rx.reserve(1024);

        while (true) {
            if (!ensureMonitorConnected()) {
                epicsThreadSleep(reconnectMs_ / 1000.0);
                continue;
            }

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(monSock_, &rfds);
            struct timeval tv{0, 200000};
            int ready = ::select(monSock_ + 1, &rfds, nullptr, nullptr, &tv);
            if (ready < 0) {
                disconnectMonitor();
                epicsThreadSleep(reconnectMs_ / 1000.0);
                continue;
            }
            if (ready == 0) {
                continue;
            }

            char buf[512];
            ssize_t n;
            {
                std::lock_guard<std::mutex> lock(ioMutex_);
                if (monSock_ < 0) {
                    n = -1;
                } else {
                    n = ::recv(monSock_, buf, sizeof(buf), 0);
                }
            }

            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                disconnectMonitor();
                epicsThreadSleep(reconnectMs_ / 1000.0);
                continue;
            }
            if (n == 0) {
                disconnectMonitor();
                epicsThreadSleep(reconnectMs_ / 1000.0);
                continue;
            }

            rx.append(buf, static_cast<size_t>(n));
            size_t pos = 0;
            while (true) {
                size_t end = rx.find('\0', pos);
                if (end == std::string::npos) {
                    if (pos > 0) {
                        rx.erase(0, pos);
                    }
                    break;
                }

                std::string frame = rx.substr(pos, end - pos);
                parseFrame(frame);
                pos = end + 1;
            }
        }
    }

    int connectSocket() {
        struct addrinfo hints;
        std::memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        char portStr[16];
        std::snprintf(portStr, sizeof(portStr), "%d", tcpPort_);

        struct addrinfo *res = nullptr;
        int gai = ::getaddrinfo(host_.c_str(), portStr, &hints, &res);
        if (gai != 0 || !res) {
            return -1;
        }

        int s = -1;
        for (struct addrinfo *p = res; p; p = p->ai_next) {
            s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s < 0) {
                continue;
            }

            if (::connect(s, p->ai_addr, p->ai_addrlen) == 0) {
                break;
            }

            ::close(s);
            s = -1;
        }

        ::freeaddrinfo(res);
        if (s >= 0) {
            int flags = ::fcntl(s, F_GETFL, 0);
            if (flags >= 0) {
                ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
            }
        }
        return s;
    }

    bool ensureMonitorConnected() {
        if (monSock_ >= 0) {
            return true;
        }

        int s = connectSocket();
        if (s < 0) {
            return false;
        }

        monSock_ = s;
        setIntegerParam(pConnected_, 1);
        callParamCallbacks();
        return true;
    }

    void disconnectMonitor() {
        if (monSock_ >= 0) {
            ::close(monSock_);
            monSock_ = -1;
        }
        setIntegerParam(pConnected_, 0);
        callParamCallbacks();
    }

    void parseFrame(std::string &frame) {
        while (!frame.empty() && (frame.back() == '\n' || frame.back() == '\r')) {
            frame.pop_back();
        }
        if (frame.empty()) {
            return;
        }

        unsigned int fnum = 0;
        unsigned int counter = 0;
        unsigned int value = 0;

        if (std::sscanf(frame.c_str(), "A%x:%x=%x", &fnum, &counter, &value) == 3) {
            setIntegerParam(pMeasRawHex_, static_cast<epicsInt32>(value));
            callParamCallbacks();
            return;
        }

        if (std::sscanf(frame.c_str(), "!%x:%x=%x", &fnum, &counter, &value) == 3) {
            setIntegerParam(pTriggerRb_, static_cast<epicsInt32>(value));
            callParamCallbacks();
        }
    }

    static bool parseReplyFrame(const std::string &frame, char &cmd, unsigned int &idx, unsigned int &val) {
        unsigned int counter = 0;
        char c = 0;
        unsigned int i = 0;
        unsigned int v = 0;
        if (std::sscanf(frame.c_str(), "%c%x:%x=%x", &c, &i, &counter, &v) == 4) {
            cmd = c;
            idx = i;
            val = v;
            return true;
        }
        return false;
    }

    bool transactQuery(char cmd, unsigned int idx, unsigned int &val, unsigned int *respIdx = nullptr) {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (!ensureMonitorConnected()) {
            return false;
        }

        char out[16];
        std::snprintf(out, sizeof(out), "%c%1X?\n\0", cmd, idx & 0xF);
        if (::send(monSock_, out, std::strlen(out) + 1, 0) < 0) {
            disconnectMonitor();
            return false;
        }

        for (int tries = 0; tries < 20; ++tries) {
            std::string frame;
            if (!readFrame(frame, 1.0)) {
                return false;
            }

            parseFrame(frame);

            char gotCmd = 0;
            unsigned int gotIdx = 0;
            unsigned int gotVal = 0;
            if (!parseReplyFrame(frame, gotCmd, gotIdx, gotVal)) {
                continue;
            }

            if (gotCmd != cmd) {
                continue;
            }

            val = gotVal;
            if (respIdx) {
                *respIdx = gotIdx;
            }
            return true;
        }

        return false;
    }

    bool transactSet(char cmd, unsigned int idx, unsigned int value) {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (!ensureMonitorConnected()) {
            return false;
        }

        char out[24];
        std::snprintf(out, sizeof(out), "%c%1X:%04X\n\0", cmd, idx & 0xF, value & 0xFFFF);
        if (::send(monSock_, out, std::strlen(out) + 1, 0) < 0) {
            disconnectMonitor();
            return false;
        }
        return true;
    }

    bool readFrame(std::string &frame, double timeoutSec) {
        while (true) {
            size_t end = cmdRxBuf_.find('\0');
            if (end != std::string::npos) {
                frame = cmdRxBuf_.substr(0, end);
                cmdRxBuf_.erase(0, end + 1);
                while (!frame.empty() && (frame.back() == '\n' || frame.back() == '\r')) {
                    frame.pop_back();
                }
                return true;
            }

            struct timeval tv;
            tv.tv_sec = static_cast<int>(timeoutSec);
            tv.tv_usec = static_cast<int>((timeoutSec - tv.tv_sec) * 1e6);

            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(monSock_, &rfds);
            int ready = ::select(monSock_ + 1, &rfds, nullptr, nullptr, &tv);
            if (ready <= 0) {
                return false;
            }

            char buf[256];
            ssize_t n = ::recv(monSock_, buf, sizeof(buf), 0);
            if (n <= 0) {
                disconnectMonitor();
                return false;
            }
            cmdRxBuf_.append(buf, static_cast<size_t>(n));
        }
    }

    bool writeParam(int function, epicsInt32 value) {
        if (function == pSwitchCfgSp_) return transactSet('I', 0, static_cast<unsigned int>(value));
        if (function == pHoldDelaySp_) return transactSet('D', 0, static_cast<unsigned int>(value));
        if (function == pCalfoSp_) return transactSet('K', 0, static_cast<unsigned int>(value));
        if (function == pReverseSp_) return transactSet('M', 0, static_cast<unsigned int>(value));
        if (function == pAdcSamplesSp_) return transactSet('T', 0, static_cast<unsigned int>(value));
        if (function == pCalQiHiSp_) return transactSet('V', 1, static_cast<unsigned int>(value));
        if (function == pCalQiLoSp_) return transactSet('V', 0, static_cast<unsigned int>(value));
        if (function == pCalUHiSp_) return transactSet('W', 1, static_cast<unsigned int>(value));
        if (function == pCalULoSp_) return transactSet('W', 0, static_cast<unsigned int>(value));
        if (function == pSaveCfgCmd_) {
            if (value == 0) return true;
            return transactSet('E', 0, 1);
        }
        return true;
    }

    bool queryParam(int function, epicsInt32 &out) {
        unsigned int v = 0;
        if (function == pSwitchCfgRb_ && transactQuery('I', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pHoldDelayRb_ && transactQuery('D', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pCalfoRb_ && transactQuery('K', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pReverseRb_ && transactQuery('M', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pAdcSamplesRb_ && transactQuery('T', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pSerialNumRb_ && transactQuery('S', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pCalQiHiRb_ && transactQuery('V', 1, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pCalQiLoRb_ && transactQuery('V', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pCalUHiRb_ && transactQuery('W', 1, v)) { out = static_cast<epicsInt32>(v); return true; }
        if (function == pCalULoRb_ && transactQuery('W', 0, v)) { out = static_cast<epicsInt32>(v); return true; }
        return false;
    }

    bool isQueryParam(int function) const {
        return function == pSwitchCfgRb_ || function == pHoldDelayRb_ ||
               function == pCalfoRb_ || function == pReverseRb_ ||
               function == pAdcSamplesRb_ || function == pSerialNumRb_ ||
               function == pCalQiHiRb_ || function == pCalQiLoRb_ ||
               function == pCalUHiRb_ || function == pCalULoRb_;
    }

private:
    std::string host_;
    int tcpPort_;
    int reconnectMs_;
    int monSock_;

    std::mutex ioMutex_;
    std::string cmdRxBuf_;

    int pMeasRawHex_;
    int pTriggerRb_;
    int pConnected_;
    int pSwitchCfgSp_;
    int pSwitchCfgRb_;
    int pHoldDelaySp_;
    int pHoldDelayRb_;
    int pCalfoSp_;
    int pCalfoRb_;
    int pReverseSp_;
    int pReverseRb_;
    int pAdcSamplesSp_;
    int pAdcSamplesRb_;
    int pSerialNumRb_;
    int pSaveCfgCmd_;
    int pCalQiHiSp_;
    int pCalQiLoSp_;
    int pCalQiHiRb_;
    int pCalQiLoRb_;
    int pCalUHiSp_;
    int pCalULoSp_;
    int pCalUHiRb_;
    int pCalULoRb_;
};

extern "C" int bcmAsynMonConfigure(const char *portName, const char *host, int tcpPort, int reconnectMs) {
    if (!portName || !host || tcpPort <= 0) {
        return asynError;
    }
    new BcmAsynMonDriver(portName, host, tcpPort, reconnectMs);
    return asynSuccess;
}

static const iocshArg cfgArg0 = {"portName", iocshArgString};
static const iocshArg cfgArg1 = {"host", iocshArgString};
static const iocshArg cfgArg2 = {"tcpPort", iocshArgInt};
static const iocshArg cfgArg3 = {"reconnectMs", iocshArgInt};
static const iocshArg *cfgArgs[] = {&cfgArg0, &cfgArg1, &cfgArg2, &cfgArg3};
static const iocshFuncDef cfgDef = {"bcmAsynMonConfigure", 4, cfgArgs};

static void cfgCall(const iocshArgBuf *args) {
    bcmAsynMonConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

extern "C" void bcmAsynMonRegister(void) {
    iocshRegister(&cfgDef, cfgCall);
}

epicsExportRegistrar(bcmAsynMonRegister);
