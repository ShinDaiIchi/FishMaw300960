#include <Arduino.h>

#include "config_flash.h"

extern "C" {
  #include "user_interface.h"
  #include "lwip/ip.h"
}

void config_load_default(sysconfig_p config)
{   
    memset(config, 0, sizeof(sysconfig_t));
    Serial.print("Loading default configuration\r\n");
    //----------------------------------------------------
    config->magic_number                    = MAGIC_NUMBER;
    config->length                          = sizeof(sysconfig_t);

    config->reset_wifi                      = 1; //เริ่มต้นมาให้ ล้าง wifi ที่จำบน flash rom
    config->uTridentFirstRun                = 0; 
    config->expire_limit                    = EXPIRE_LIMIT*24*60*60;  //EXPIRE_LIMIT (days) , expire_time (secs)
    sprintf((char*)config->ssid,"%s",       WIFI_SSID);
    sprintf((char*)config->password,"%s",   WIFI_PASSWORD);
    memset(config->bssid, 0, 6);
    config->auto_connect                    = 1;

    sprintf((char*)config->ap_ssid,"%s",    WIFI_AP_SSID);
    sprintf((char*)config->ap_password,"%s",WIFI_AP_PASSWORD);
    config->ap_channel                      = 6;
    config->ap_open                         = (strcmp((char*)config->ap_password, "")==0)? 1:0;
    config->ap_enable                       = 1;
    config->ap_hidden                       = 0;

    config->ap_simplepair_enable            = 1;
    sprintf((char*)config->simplepair_key,"%s", SIMPLEPAIR_KEY);
    sprintf((char*)config->simplepair_data,"%s", SIMPLEPAIR_DATA);
    
    config->locked                          = 0;
    config->lock_password[0]                = '\0';
    
    IP4_ADDR(&config->ap_addr, 10, 0, 0, 1);    
    IP4_ADDR(&config->dns_addr, 8, 8, 8, 8);

    config->sta_addr.addr                    = 0;  // use DHCP   
    config->sta_netmask.addr                 = 0;  // use DHCP   
    config->sta_gw.addr                      = 0;  // use DHCP

#ifdef PHY_MODE
    config->phy_mode                        = 3;  // mode n
#endif//PHY_MODE

#ifdef SMART_CONFIG
    config->smart_config                    = 1;
#endif //SMART_CONFIG

    config->clock_speed                     = CLOCK_SPEED;
    config->wifi_led_enable               = STATUS_LED_ENABLE;

    sprintf((char*)config->ntp_server1,"%s",NTP_SERVER1);
    sprintf((char*)config->ntp_server2,"%s",NTP_SERVER2);
    sprintf((char*)config->ntp_server3,"%s",NTP_SERVER3);
    config->ntp_interval                 = 5;  //minutes
    config->timezone                        = TIMEZONE;
    config->daylightsaving                  = DAYLIGHTSAVING;

    wifi_get_macaddr(SOFTAP_IF, config->AP_MAC_address);  
    wifi_get_macaddr(STATION_IF, config->STA_MAC_address);  
    sprintf((char*)config->hostname, "μ-Trident8266_%02X%02X%02X", config->AP_MAC_address[3], config->AP_MAC_address[4],config->AP_MAC_address[5]);
}

//----------------------------------------------------------
void storeStruct(void *data_source, size_t size)
{
  EEPROM.begin(size * 2);
  for(size_t i = 0; i < size; i++)
  {
    char data = ((char *)data_source)[i];
    EEPROM.write(i, data);
  }
  EEPROM.commit();
}

void loadStruct(void *data_dest, size_t size)
{
    EEPROM.begin(size * 2);
    for(size_t i = 0; i < size; i++)
    {
        char data = EEPROM.read(i);
        ((char *)data_dest)[i] = data;
    }
}
//----------------------------------------------------------
int config_load(sysconfig_p config){
    if (config == NULL) return -1;
    loadStruct( config, sizeof(sysconfig_t));
    if((config->magic_number != MAGIC_NUMBER)){
      Serial.println("\r\nNo config found, saving default in flash\r\n");
      config_load_default(config);
      config_save(config);
    }

    Serial.print("\r\nConfig found and loaded\r\n");
    if (config->length != sizeof(sysconfig_t))
    {
      Serial.print("Length Mismatch, probably old version of config, loading defaults\r\n");
      config_load_default(config);
      config_save(config);
      return false;
    }
    return 1;
}
    
void config_save(sysconfig_p config)
{
    Serial.print("Saving configuration\r\n");
    storeStruct( config, sizeof(sysconfig_t));
}

//String mac2String(uint8_t* mac){
//  char mac_buf[20];
//  sprintf(mac_buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
//  return String(mac_buf);
//}

sysconfig_t config;
