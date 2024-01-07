#include <Arduino.h>
#include <vector>
using std::vector;

#define GPS_BUF_LEN 1024

extern RingBuffer<uint8_t> gpsBuffer;
extern int32_t headingAdjustment;
extern elapsedMillis lastPvtMessage;
extern uint32_t timestamp;

void initGPS();
void gpsLoop();
enum fixTypes : uint8_t {
	FIX_NONE			   = 0,
	FIX_DEAD_RECKONING	   = 1,
	FIX_2D				   = 2,
	FIX_3D				   = 3,
	FIX_GPS_DEAD_RECKONING = 4,
	FIX_TIME_ONLY		   = 5,
};

#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62
typedef struct gpsTime {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
} GpsTime;
typedef struct gpsAccuracy {
	// unit: ns
	uint32_t tAcc;
	// unit: mm
	uint32_t hAcc;
	// unit: mm
	uint32_t vAcc;
	// unit: mm/s
	uint32_t sAcc;
	// unit: 10^-5 deg
	uint32_t headAcc;
	// unit: 10^-2
	uint32_t pDop;
} GpsAccuracy;
typedef struct gpsStatus {
	uint8_t gpsInited;
	uint8_t initStep;
	uint8_t fixType;
	uint8_t timeValidityFlags;
	uint8_t flags;
	uint8_t flags2;
	uint16_t flags3;
	uint8_t satCount;
} GpsStatus;
typedef struct gpsMotion {
	// unit: 10^-7 deg
	int32_t lat;
	// unit: 10^-7 deg
	int32_t lon;
	// unit: mm, above mean sea level
	int32_t alt;
	// unit: mm/s
	int32_t velN;
	// unit: mm/s
	int32_t velE;
	// unit: mm/s
	int32_t velD;
	// unit: mm/s
	int32_t gSpeed;
	// unit: 10^-5 deg
	int32_t headMot;
} GpsMotion;
extern GpsAccuracy gpsAcc;
extern GpsTime gpsTime;
extern GpsStatus gpsStatus;
extern GpsMotion gpsMotion;

enum ubx_class : uint8_t {
	UBX_CLASS_NAV = 0x01,
	UBX_CLASS_RXM = 0x02,
	UBX_CLASS_INF = 0x04,
	UBX_CLASS_ACK = 0x05,
	UBX_CLASS_CFG = 0x06,
	UBX_CLASS_UPD = 0x09,
	UBX_CLASS_MON = 0x0A,
	UBX_CLASS_AID = 0x0B,
	UBX_CLASS_TIM = 0x0D,
	UBX_CLASS_ESF = 0x10,
	UBX_CLASS_MGA = 0x13,
	UBX_CLASS_LOG = 0x21,
	UBX_CLASS_SEC = 0x27,
	UBX_CLASS_HNR = 0x28,
	UBX_CLASS_TP5 = 0x31,
};

enum ubx_msg_id : uint8_t {
	UBX_ID_ACK_ACK		 = 0x01,
	UBX_ID_ACK_NAK		 = 0x00,
	UBX_ID_AID_ALM		 = 0x30,
	UBX_ID_AID_AOP		 = 0x33,
	UBX_ID_AID_EPH		 = 0x31,
	UBX_ID_AID_HUI		 = 0x02,
	UBX_ID_AID_INI		 = 0x01,
	UBX_ID_CFG_ANT		 = 0x13,
	UBX_ID_CFG_BATCH	 = 0x93,
	UBX_ID_CFG_CFG		 = 0x09,
	UBX_ID_CFG_DAT		 = 0x06,
	UBX_ID_CFG_DGNSS	 = 0x70,
	UBX_ID_CFG_DOSC		 = 0x61,
	UBX_ID_CFG_ESFALG	 = 0x56,
	UBX_ID_CFG_ESFA		 = 0x4C,
	UBX_ID_CFG_ESFG		 = 0x4D,
	UBX_ID_CFG_ESFWT	 = 0x82,
	UBX_ID_CFG_ESRC		 = 0x60,
	UBX_ID_CFG_GEOFENCE	 = 0x69,
	UBX_ID_CFG_GNSS		 = 0x3E,
	UBX_ID_CFG_HNR		 = 0x5C,
	UBX_ID_CFG_INF		 = 0x02,
	UBX_ID_CFG_ITFM		 = 0x39,
	UBX_ID_CFG_LOGFILTER = 0x47,
	UBX_ID_CFG_MSG		 = 0x01,
	UBX_ID_CFG_NAV5		 = 0x24,
	UBX_ID_CFG_NAVX5	 = 0x23,
	UBX_ID_CFG_NMEA		 = 0x17,
	UBX_ID_CFG_ODO		 = 0x1E,
	UBX_ID_CFG_PM2		 = 0x3B,
	UBX_ID_CFG_PMS		 = 0x86,
	UBX_ID_CFG_PRT		 = 0x00,
	UBX_ID_CFG_PWR		 = 0x57,
	UBX_ID_CFG_RATE		 = 0x08,
	UBX_ID_CFG_RINV		 = 0x34,
	UBX_ID_CFG_RST		 = 0x04,
	UBX_ID_CFG_RXM		 = 0x11,
	UBX_ID_CFG_SBAS		 = 0x16,
	UBX_ID_CFG_SENIF	 = 0x88,
	UBX_ID_CFG_SLAS		 = 0x8D,
	UBX_ID_CFG_SMGR		 = 0x62,
	UBX_ID_CFG_SPT		 = 0x64,
	UBX_ID_CFG_TMODE2	 = 0x3D,
	UBX_ID_CFG_TMODE3	 = 0x71,
	UBX_ID_CFG_TP5		 = 0x31,
	UBX_ID_CFG_TXSLOT	 = 0x53,
	UBX_ID_CFG_USB		 = 0x1B,
	UBX_ID_ESF_ALG		 = 0x14,
	UBX_ID_ESF_INS		 = 0x15,
	UBX_ID_ESF_MEAS		 = 0x02,
	UBX_ID_ESF_RAW		 = 0x03,
	UBX_ID_ESF_STATUS	 = 0x10,
	UBX_ID_HNR_ATT		 = 0x01,
	UBX_ID_HNR_INS		 = 0x02,
	UBX_ID_HNR_PVT		 = 0x00,
	UBX_ID_INF_DEBUG	 = 0x04,
	UBX_ID_INF_ERROR	 = 0x00,
	UBX_ID_INF_NOTICE	 = 0x02,
	UBX_ID_INF_TEST		 = 0x03,
	UBX_ID_INF_WARNING	 = 0x01,
	UBX_ID_SEC_UNIQID	 = 0x03,
	UBX_ID_NAV_DOP		 = 0x04,
	UBX_ID_NAV_PVT		 = 0x07,
	UBX_ID_NAV_SAT		 = 0x35,
};