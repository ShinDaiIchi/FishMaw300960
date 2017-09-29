#ifndef __CONFIG_FLASH_H__
#define __CONFIG_FLASH_H__

#include <Arduino.h>
#include <EEPROM.h>
#include "user_config.h"

extern "C" {
#include "lwip/ip.h"
#include "lwip/app/dhcpserver.h"
}

#define MAGIC_NUMBER    0x014005fc


typedef struct {
    // To check if the structure is initialized or not in flash
    uint32_t    magic_number;

    // Length of the structure, since this is a evolving library, the variant may change
    // hence used for verification
    uint16_t    length;

    /* Below variables are specific to my code */
    uint8_t     reset_wifi;     // Shold we clean SSID/PASSWORD on Flash Rom 
    time_t      uTridentFirstRun;     //ตัวแปรบันทึก การเริ่มทำงานครั้งแรกของ uTrident อุปกรณ์นี้
    time_t      expire_limit;    // seconds
    uint8_t     ssid[32];       // SSID of the AP to connect to
    uint8_t     password[64];   // Password of the network
    uint8_t     bssid[6];       // BSSID of AP
    uint8_t     auto_connect;   // Should we auto connect

    uint8_t     ap_ssid[32];      // SSID of the own AP
    uint8_t     ap_password[64];  // Password of the own network
    uint8_t     ap_channel;       // AP channel
    uint8_t     ap_open;          // Should we use on WPA?
    uint8_t     ap_enable;        // AP enabled?
    uint8_t     ap_hidden;        // Hidden ap SSID?

    uint8_t     ap_simplepair_enable; // enable ap-simplepair
    uint8_t     simplepair_key[17];   // simplepair key ปกติยาว 16 ต้องเผื่อ 1
    uint8_t     simplepair_data[17];  // simplepair data

    uint8_t     locked;             // Should we allow for config changes
    uint8_t     lock_password[32];  // Password of config lock

    ip_addr_t   ap_addr;          // Address of the internal network
    ip_addr_t   dns_addr;         // Optional: address of the dns server
    ip_addr_t   sta_addr;          // Optional (if not DHCP): IP address of the uplink side
    ip_addr_t   sta_netmask;       // Optional (if not DHCP): IP netmask of the uplink side
    ip_addr_t   sta_gw;            // Optional (if not DHCP): Gateway of the uplink side

#ifdef PHY_MODE
    uint16_t    phy_mode;         // WiFi PHY mode
#endif //PHY_MODE

#ifdef SMART_CONFIG
    uint8_t     smart_config;     // Should we use Smart Config
#endif //SMART_CONFIG

    uint16_t    clock_speed;      // Freq of the CPU
    uint8_t     wifi_led_enable;    // turn on/off wifi status led

    uint8_t     ntp_server1[32];
    uint8_t     ntp_server2[32];
    uint8_t     ntp_server3[32];
    uint16_t    ntp_interval;     // รีซิงค์ทุกๆ กี่นาที
    uint8_t     timezone;
    uint8_t     daylightsaving;

    uint8_t     AP_MAC_address[6];  // MAC address of the AP
    uint8_t     STA_MAC_address[6]; // MAC address of the STA
    uint8_t     hostname[32];   // DHCP hostname
} sysconfig_t, *sysconfig_p;

int config_load(sysconfig_p config);
void config_save(sysconfig_p config);

//String mac2String(uint8_t* mac);

extern sysconfig_t config;
#endif

