#ifndef _USER_CONFIG_
#define _USER_CONFIG_

#define VERSION             "1.0beta"

#define EXPIRE_LIMIT        7  //days

//#define WIFI_SSID           "ssid"
//#define WIFI_PASSWORD       "none"
#define WIFI_SSID           "MyHome"
#define WIFI_PASSWORD       "12345678"

#define WIFI_AP_SSID        "μ-Trident8266"
#define WIFI_AP_PASSWORD    "11223344"

#define SIMPLEPAIR_KEY      "MY_SIMPLEPAIRKEY"   // 16 length
#define SIMPLEPAIR_DATA     "SIMPLEPAIR..DATA"   // 16 length

#define NTP_SERVER1         "pool.ntp.org"
#define NTP_SERVER2         "time.nist.gov"
#define NTP_SERVER3         "ntp.ku.ac.th"
#define TIMEZONE            7
#define DAYLIGHTSAVING      0

#define MAX_CLIENTS         8

#define CLOCK_SPEED         80   // 80 or 160MHz

//
// Define this to support the setting of the WiFi PHY mode
//
#define PHY_MODE            1
#define SMART_CONFIG        0

#define STATUS_LED_ENABLE   0

#endif // _USER_CONFIG_

