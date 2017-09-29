#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266FtpServer.h>
#include <Ticker.h>
#include <FS.h>
#include <time.h>

#include "user_config.h"
#include "config_flash.h"
#include "web.h"

extern "C" {
#include "user_interface.h"
#include "simple_pair.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/lwip_napt.h"
#include "lwip/app/dhcpserver.h"
}

typedef enum td_status {
  TDST_OFF = 0,
  TDST_WIFI_START,
  TDST_WEBSERVER_START, 
} td_status_t;


td_status_t TD_STATUS = TDST_OFF;

Ticker reconnect_task, ap_timer, ntpsync_task, ntpsync_checker;

bool      wifi_connected;
bool      wifi_try_connect;
uint32_t  wifi_disconnect_timer;
uint16_t  DISCONNECT_LIMIT = 15000;

static    ip_addr_t my_ip;
static    ip_addr_t dns_ip;
bool      do_ip_config;

static netif_input_fn       orig_input_ap;
static netif_linkoutput_fn  orig_output_ap;

/* Some stats */
uint64_t Bytes_in, Bytes_out, Bytes_in_last, Bytes_out_last;
uint32_t Packets_in, Packets_out, Packets_in_last, Packets_out_last;

/* Declare Function */
static void patch_netif_ap(netif_input_fn ifn, netif_linkoutput_fn ofn, bool nat);
err_t my_input_ap (struct pbuf *p, struct netif *inp);
err_t my_output_ap (struct netif *outp, struct pbuf *p);
void wifi_handle_event_cb(System_Event_t *evt);


uint8_t mac_buffer[6];
uint8_t aid_buffer;

bool sp_scan=false;

time_t expire_time;

ESP8266WebServer webserver(80);
FtpServer ftpserver;


//const char* ssid        = "MyHome";
//const char* password    = "12345678";
//const char* ap_ssid     = "Trident_AP";
//const char* ap_password = "12345678";

bool ntp_already_sync = false;

void ntp_sync_internettime(){
  Serial.printf("[NTP] Internet Time Sync...every %d mins\n", config.ntp_interval);

  ntpsync_checker.attach(2, [](){
    time_t now = time(nullptr);
    if( now > 1000) {
      ntp_already_sync = true;
      ntpsync_checker.detach();
      Serial.printf("[NTP] %s\n", time_now().c_str());

      if(config.uTridentFirstRun == 0){ //บันทึกเวลาครั้งแรกที่มีการ ใช้งาน uTrident และ sync ต่อ internet สำเร็จ
        config.uTridentFirstRun = now;
        config_save(&config);
        expire_time = config.uTridentFirstRun + config.expire_limit;
      }

      // ถ้า หมดอายุ ระบบ router จะใช้ไม่ได้
      if( isExpire()){
        Serial.println("\nμ-Trident8266 is EXPIRE!\n");
        ip_napt_enable(WiFi.softAPIP(), false); 
      }else{
        //Serial.println("μ-Trident8266 is ON");
        ip_napt_enable(WiFi.softAPIP(), true); 
      }
    }
  });
  
  configTime(config.timezone*3600, config.daylightsaving*3600, 
      (char*)config.ntp_server1, (char*)config.ntp_server2, (char*)config.ntp_server3);

  ntpsync_task.once( (uint16_t)( config.ntp_interval *60), ntp_sync_internettime);
}

String getContentType(String filename){
  if(webserver.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool web_file_handler(String path){
  Serial.println("web_file_handler(): " + path);
  if(path.endsWith("/")) path += "index.htm";

  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = webserver.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void web_wifi_handler(){
  Serial.println("Hello WiFi");
  bool change = false, sta_change=false, ap_change =false;
  for (uint8_t i=0; i< webserver.args(); i++){
    if( webserver.argName(i) == "ssid"){
      change = true; sta_change = true;
      Serial.printf("%s : %s\n", webserver.argName(i).c_str() , webserver.arg(i).c_str());

      page_html_wifi_part1.replace( String((char*)config.ssid), webserver.arg(i));
      
      strcpy( (char*)config.ssid, (char*) webserver.arg(i).c_str());
    }
  }
  if( change ) {
    Serial.println(String((char*)config.ssid));
    config_save(&config);
    Serial.println(String((char*)config.ssid));

    if( sta_change){
      sta_change = false;

      wifi_station_disconnect();

      wifi_connected        = false;
      wifi_try_connect      = true;
      wifi_disconnect_timer = millis();    
      WiFi.reconnect();
    }
    
    if( ap_change){
      WiFi.mode(WIFI_STA);
      WiFi.mode(WIFI_AP_STA);
      WiFi.softAP((char*)config.ap_ssid, (char*)config.ap_password);
    }
    
    change = false;
  }


  String ssid_form        = String((char*)config.ssid); Serial.println("Checker --> " + ssid_form);
  String password_form    = String((char*)config.password);
  String bssid_form       = (config.bssid[0]==0)? "": mac2String(config.bssid);
  String dns_form         = (config.dns_addr.addr==0)? "": IPAddress(config.dns_addr.addr).toString();
  String my_ip_form       = (my_ip.addr==0)? "": IPAddress(my_ip.addr).toString();
  String sta_addr_form    = (config.sta_addr.addr==0)? "": IPAddress(config.sta_addr.addr).toString();
  String sta_gw_form      = (config.sta_gw.addr==0)? "": IPAddress(config.sta_gw.addr).toString();
  String sta_netmark_form = (config.sta_netmask.addr==0)? "": IPAddress(config.sta_netmask.addr).toString();
  String sta_mac_form     = mac2String(config.STA_MAC_address);

  page_html_wifi_part1.replace("%s_ssid", ssid_form);
  page_html_wifi_part1.replace("%s_password", password_form);
  page_html_wifi_part1.replace("%s_bssid", bssid_form);
  page_html_wifi_part1.replace("%s_dns", dns_form);
  page_html_wifi_part1.replace("%s_my_ip", my_ip_form);
  page_html_wifi_part1.replace("%s_sta_addr", sta_addr_form);
  page_html_wifi_part1.replace("%s_sta_gw", sta_gw_form);
  page_html_wifi_part1.replace("%s_sta_netmask", sta_netmark_form);
  page_html_wifi_part1.replace("%s_sta_mac", sta_mac_form);

  //Serial.println(page_html_wifi_part1);
  //String page_html_wifi_part2 = _page_html_wifi_part2;

  String ap_ssid_form         = String((char*)config.ap_ssid);
  String ap_password_form     = String((char*)config.ap_password);
  String ap_open_form         = (config.ap_open)? "selected" : "";
  String ap_wpa2_form         = (config.ap_open)? "" : "selected";
  String ap_addr_form         = (config.ap_addr.addr==0)? "": IPAddress(config.ap_addr.addr).toString();
  String ap_mac_form          = mac2String(config.AP_MAC_address);
  String ap_enable_form           = "1";
  String ap_hidden_form       = "1";

  page_html_wifi_part2.replace("%s_ap_ssid", ap_ssid_form);
  page_html_wifi_part2.replace("%s_ap_password", ap_password_form);
  page_html_wifi_part2.replace("%s_ap_open", ap_open_form);
  page_html_wifi_part2.replace("%s_ap_wpa2", ap_wpa2_form);
  page_html_wifi_part2.replace("%s_ap_addr", ap_addr_form);
  page_html_wifi_part2.replace("%s_ap_mac", ap_mac_form);
  
  int len_begin = strlen_P(page_html_begin);
  int len_end   = page_html_end.length();
  
  int len_wifi1 = page_html_wifi_part1.length();
  int len_wifi2 = page_html_wifi_part2.length();
  
  int len       = len_begin + len_end + len_wifi1 + len_wifi2; // + len_wifi2;  + len_end 

  Serial.printf( "begin:%d  +  end:%d  + body1:%d  + body2:%d = total:%d\n"
        , len_begin, len_end, len_wifi1, len_wifi2, len);
  
  webserver.setContentLength(len);
  webserver.send(200, "text/html", "");
  webserver.sendContent_P(page_html_begin, len_begin);
  webserver.sendContent_P(page_html_wifi_part1.c_str(), len_wifi1);
  webserver.sendContent_P(page_html_wifi_part2.c_str(), len_wifi2);
  webserver.sendContent_P(page_html_end.c_str(), len_end);

}

void simple_pair_status_handler(uint8_t* smac, uint8_t sp_status){
  uint8_t sp_key[16];
  memcpy(sp_key, config.simplepair_key, 16);
  uint8_t sp_data[16];
  if( !sp_scan ) { //SP_AP
    memcpy(sp_data, config.simplepair_data, 16);
  }
  
  switch(sp_status){    
    case SP_ST_AP_FINISH:  // = 0  อันเดียวกับ case SP_ST_STA_FINISH: // = 0
      simple_pair_get_peer_ref(NULL, NULL, sp_data);
      if( sp_scan ) { //SP_STA
        sprintf((char*)config.simplepair_data,"%s", sp_data);
        Serial.printf("[Simple-Pair] recv. data : %s\n", (char*)config.simplepair_data);
        sp_scan = false;
      }
      //simple_pair_deinit();
      break;
    case SP_ST_AP_RECV_NEG:  // SP_AP จับมือ กับ SP_STA
      simple_pair_set_peer_ref(smac, sp_key, sp_data);
      simple_pair_ap_start_negotiate();
      //simple_pair_ap_refuse_negotiate();
      break;
    case SP_ST_STA_AP_REFUSE_NEG: // SP_STA ถูก SP_AP ปฏิเสธ จับมือ
      Serial.println("[Simple-Pair] Recv AP Refuse Negotiate");
      break;
    //---------------------------------------------------------
    // จัดการ Error แบบต่างๆ
    case SP_ST_WAIT_TIMEOUT:
      Serial.println("[Simple-Pair] Negotiate Time out");
      simple_pair_state_reset();
      if(sp_scan){ //SP_STA
        simple_pair_sta_enter_scan_mode();
        //wifi_station_scan(NULL, scan_done);
      } else { // SP_AP
        simple_pair_ap_enter_announce_mode();
      }
      break;
    case SP_ST_SEND_ERROR:
      Serial.println("[Simple-Pair] Send Error");
      break;
    case SP_ST_KEY_INSTALL_ERR:
      Serial.println("[Simple-Pair] Key Install Error");
      break;
    case SP_ST_KEY_OVERLAP_ERR:
      Serial.println("[Simple-Pair] Key Overlap Error");
      break;
    case SP_ST_OP_ERROR:
      Serial.println("[Simple-Pair] Operation Order Error");
      break;
  }
}

void wifi_event_handler(System_Event_t *evt){
  uint8_t mac_str[20];
  switch(evt->event){
    case EVENT_STAMODE_CONNECTED:
      Serial.printf("[STA] WiFi connect to ssid %s, channel %d\n" , 
        evt->event_info.connected.ssid, evt->event_info.connected.channel);
      wifi_connected    = true;
      wifi_try_connect  = false;
      break;
    case EVENT_STAMODE_DISCONNECTED:
      Serial.printf("[STA] WiFi disconnect from ssid %s, reason %d\n", 
        evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
      if(wifi_connected){
        wifi_connected        = false;
        wifi_try_connect      = true;
        wifi_disconnect_timer = millis();

        ntpsync_task.detach();
      }

      if( wifi_try_connect && millis() - wifi_disconnect_timer > DISCONNECT_LIMIT ) {
        Serial.printf("[STA] Stop try WiFi connection %d sec.\n", DISCONNECT_LIMIT/1000);
        wifi_station_disconnect();
        wifi_try_connect = false;
        reconnect_task.once_ms(DISCONNECT_LIMIT,[](){ 
          Serial.println("[STA] Start try WiFi connection ...");
          wifi_try_connect      = true;
          wifi_disconnect_timer = millis();
          wifi_station_connect();
        });
      }

      break;
    case EVENT_STAMODE_GOT_IP:
      Serial.printf("[STA] ip:" IPSTR " , mask:" IPSTR " , gw:" IPSTR "\n",
          IP2STR(&evt->event_info.got_ip.ip),
          IP2STR(&evt->event_info.got_ip.mask),
          IP2STR(&evt->event_info.got_ip.gw));


      ntp_sync_internettime();
      

      // IP Forward ให้มา map IP ภายนอก ด้วย WiFi.localIP() ที่มีการเปลี่ยนแปลงไป
      for(int i = 0; i < IP_PORTMAP_MAX; i++){
        if(ip_portmap_table[i].valid) {
           ip_portmap_table[i].maddr = uint32_t(WiFi.localIP());
        }
      }


//      uint8_t retval;
//      retval = ip_portmap_add(IP_PROTO_TCP, uint32_t(WiFi.localIP()), 8888, uint32_t(IPAddress(10,0,0,100)), 80);
//      Serial.println(retval);
//      Serial.println("[IP Forward] add TCP "  + WiFi.localIP().toString() + ":8888 -> " 
//                                              + IPAddress(10,0,0,100).toString() + ":80");
      

      break;

    case EVENT_STAMODE_AUTHMODE_CHANGE://วิธีเข้ารัหส password , WPA2 เป็นต้น
      Serial.printf("[WiFi Event] mode: %d -> %d\n", 
        evt->event_info.auth_change.old_mode, evt->event_info.auth_change.new_mode);
      break;
      
    case EVENT_SOFTAPMODE_STACONNECTED:
      sprintf((char*)mac_str, MACSTR, MAC2STR(evt->event_info.sta_connected.mac));
//      Serial.printf("station add   [AID %d] :  %s join     [total %d stations]\n", 
//              evt->event_info.sta_connected.aid,  mac_str, wifi_softap_get_station_num());

      memcpy(mac_buffer, evt->event_info.sta_connected.mac, 6);
      aid_buffer = evt->event_info.sta_connected.aid;
      ap_timer.once_ms(1000, []() {
        struct station_info *sta_info = wifi_softap_get_station_info();
        while ( sta_info != NULL) {
          if ( memcmp(mac_buffer , sta_info->bssid, 6) == 0) {
            Serial.printf("[AP] station add   [AID %d] :  %s -> ip " IPSTR " {total %d stations}\n", 
                  aid_buffer, mac2String(mac_buffer).c_str(), IP2STR(&sta_info->ip), wifi_softap_get_station_num());
            break;
          } sta_info = sta_info->next;
        }
      });
      
      break;
    case EVENT_SOFTAPMODE_STADISCONNECTED:
      sprintf((char*)mac_str, MACSTR, MAC2STR(evt->event_info.sta_disconnected.mac));
      Serial.printf("[AP] station leave [AID %d] :  %s leave            {total %d stations}\n", 
              evt->event_info.sta_disconnected.aid,  mac_str, wifi_softap_get_station_num());
      break;
  }
}

void setup() {
  Serial.begin(115200); Serial.println();

  Serial.printf("\r\n            _________  ___  ___  ____ ____\r\n"
                    "   __ __ __/_  __/ _ \\( _ )|_  |/ __// __/\r\n" 
                    "  / // //__// / / // / _  / __// _ \\/ _ \\\r\n"
                    " / _,_/    /_/ /____/\\___/____/\\___/\\___/\r\n"
                    "/_/             micro-Trident8266 v %s\r\n\r\n", VERSION );
  
  wifi_set_event_handler_cb( wifi_event_handler );
  register_simple_pair_status_cb( simple_pair_status_handler );

  WiFi.mode(WIFI_AP_STA);

  do_ip_config = false;
  my_ip.addr = 0;
  Bytes_in = Bytes_out = Bytes_in_last = Bytes_out_last = 0,
  Packets_in = Packets_out = Packets_in_last = Packets_out_last = 0;

  config_load(&config);

  WiFi.hostname((char*)config.hostname);
  Serial.printf("μ-Trident8266 : %s\n",WiFi.hostname().c_str());

  if( config.uTridentFirstRun != 0){
    char timestr_buf[10];
    struct tm timeinfo;
    gmtime_r(&config.uTridentFirstRun, &timeinfo);
    Serial.printf("   start on  %02d/%02d/%02d %02d:%02d:%02d\n", 
      timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    expire_time = config.uTridentFirstRun + config.expire_limit;
    gmtime_r(&expire_time, &timeinfo);
    Serial.printf("   expire on %02d/%02d/%02d %02d:%02d:%02d\n", 
      timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  wifi_led_init();
  
  // เปิดใช้ ระบบ filesystem บน flash mem
  SPIFFS.begin();
  { // แสดง file ที่มี ใน SPIFFS พร้อมบอกขนาด
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
  
#ifdef PHY_MODE
  wifi_set_phy_mode((phy_mode_t)config.phy_mode);     // กำหนด มาตรฐาน WiFi IEEE11b =1, g=2, n=3
#endif
  system_update_cpu_freq(config.clock_speed);

  if( config.ap_enable)
  {//---------- AP ----------------------
    ip_addr_t ap_ip;
    ip_addr_t ap_gw;
    
    ap_ip = config.ap_addr;
    ip4_addr4(&ap_ip) = 1;
    ap_gw = ap_ip;
    
//    wifi_softap_dhcps_stop();
//    struct dhcps_lease dhcp_lease;
//    dhcp_lease.start_ip.addr = network_ip.addr;
//    dhcp_lease.end_ip.addr   = network_ip.addr;
//    ip4_addr4(&dhcp_lease.start_ip) = 2;
//    ip4_addr4(&dhcp_lease.end_ip)   = 128;
//    wifi_softap_set_dhcps_lease(&dhcp_lease);
//    wifi_softap_dhcps_start();
    
    WiFi.softAPConfig(IPAddress(ap_ip.addr), IPAddress(ap_gw.addr), IPAddress(255,255,255,0));   
    //WiFi.softAP((char*)config.ap_ssid, (char*)config.ap_password);
    WiFi.softAP((char*)config.ap_ssid, (char*)config.ap_password, config.ap_channel, config.ap_hidden, MAX_CLIENTS );
  }
  
  wifi_connected          = false;
  wifi_try_connect        = true;
  wifi_disconnect_timer   = millis();
  WiFi.begin((char*)config.ssid, (char*)config.password);


  ip_napt_enable(WiFi.softAPIP(), true); 


  simple_pair_init();
  simple_pair_ap_enter_announce_mode();
}


void loop() {
  // put your main code here, to run repeatedly:
  if(Serial.available()){
    String raw_command;
    while(Serial.available()){
      raw_command = Serial.readStringUntil('\n');
    }
    //Serial.println(raw_command);
    char *token[10];
    char * token_bufer = strtok( (char*)raw_command.c_str(), " ");
    uint8_t MAX_TOKEN=0;
    while( token_bufer != NULL) {
      token[MAX_TOKEN++] = token_bufer;
      token_bufer = strtok(NULL, " ");
    }

    String command = String(token[0]); command.toUpperCase();

    if(command == "IPFORWARD"){
      if( MAX_TOKEN != 6 || MAX_TOKEN != 4){
        String tok1 = String(token[1]); tok1.toUpperCase();
        String tok2 = String(token[2]); tok2.toUpperCase();

        if( tok1 == "LIST" ){
          struct portmap_table *p;
          ip_addr_t i_ip;
          for (int i = 0; i<IP_PORTMAP_MAX; i++) {
            p = &ip_portmap_table[i];
            if(p->valid) {
              i_ip.addr = p->daddr;
              Serial.printf( "[IP Forward List] %s:  %s:%d -> "  IPSTR ":%d\n", 
                    p->proto==IP_PROTO_TCP?"TCP":p->proto==IP_PROTO_UDP?"UDP":"???",
                    (char*)WiFi.localIP().toString().c_str(), ntohs(p->mport), IP2STR(&i_ip), ntohs(p->dport));
            }
          }
          
          return;
        }
         
        bool add      = ( tok1 == "ADD")?    true : false;
        bool remove   = ( tok1 == "REMOVE")? true : false;

        uint8_t proto = ( tok2 == "TCP")? IP_PROTO_TCP : IP_PROTO_UDP;

        IPAddress outer_ip    = WiFi.localIP();
        uint16_t  outer_port  = String(token[3]).toInt();
        IPAddress inner_ip;
        uint16_t  inner_port;

        if( add ) {
           inner_ip.fromString(String(token[4]));
           inner_port = String(token[5]).toInt();
        }
        if( add ) {
          if( outer_port==0){
            Serial.printf("[IP Forward] can't add External Port : %d (Reserved)\n", outer_port);
            return;
          }
          else if (inner_port==0 ){
            Serial.printf("[IP Forward] can't add Internal Port : %d (Reserved)\n", inner_port);
            return;             
          }
          
          uint8_t retval = ip_portmap_add(proto, uint32_t(outer_ip), outer_port, uint32_t(inner_ip), inner_port);
          //Serial.println(retval);
          Serial.println("[IP Forward] add " + tok2 + ":  "+ outer_ip.toString() + ":" + String(outer_port) + " -> " + inner_ip.toString() + ":" + String(inner_port));
        }
        if( remove) {
          uint8_t retval = ip_portmap_remove(proto, outer_port);
          //Serial.println(retval);
          Serial.println("[IP Forward] remove " + tok2 + ":  " + outer_ip.toString() + ":" + String(outer_port));
        }
      }
    } // end if IPFORWARD
    else if(command == "FINDMAC"){
      // หาก mac ของ station ที่เชื่อมต่ออยู่ด้วย ip
      IPAddress ip_buffer; ip_buffer.fromString(String(token[1]));
      Serial.print(ip_buffer);
      Serial.print(" -> ");
      if( IPAddress2macString(ip_buffer)!= "" ) {
        Serial.println( IPAddress2macString(ip_buffer) );
      } else {
        Serial.println("Not Found!");
      }
    } // end if FINDMAC
    else if(command == "FINDIP"){
      // หา ip ของ station ที่ เชื่อมต่ออยุ่ ด้วย mac
      String macstr = String(token[1]);
    
      uint8_t mac[6];
      String2mac(macstr, mac);

      Serial.print(mac2String(mac));
      Serial.print(" -> ");
      if(!uint32_t(mac2IPAddress(mac))){
        Serial.println(  mac2IPAddress(mac) );
      } else {
        Serial.println("Not Found!");
      }
    } // end if FINDIP
    else if(command == "STA_LIST" || command == "STALIST"){
      // แสดง Station ทั้งหมดที่กำลัง เชื่อมไวไฟมาที่ตัว ap นี้
      int n = wifi_softap_get_station_num();
      Serial.printf("[STATION LIST] %d stations\n", n);
      if( n ) {
        // แบบที่รับจาก dhcpserver.h
        int dhcps_count=0; 
        struct dhcps_pool *dhcps_p = dhcps_get_mapping(dhcps_count++);
        while(dhcps_p !=NULL){
          time_t lease_clock = time(nullptr); 
          char lease_timestr[20]="";

          if(lease_clock > 1000) {
            lease_clock += 60*dhcps_p->lease_timer;
            struct tm timeinfo;
            gmtime_r(&lease_clock, &timeinfo);
            sprintf(lease_timestr, "%02d/%02d/%d %02d:%02d", 
                      timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
                      timeinfo.tm_hour, timeinfo.tm_min + (timeinfo.tm_sec)? 1 :0);
          }
          
          Serial.printf("       sta%02d : %s    %s    %d mins    %s\n",
                        dhcps_count,
                        mac2String(dhcps_p->mac).c_str(), 
                        IPAddress(dhcps_p->ip.addr).toString().c_str(),
                        dhcps_p->lease_timer, 
                        lease_timestr );
          dhcps_p = dhcps_get_mapping(dhcps_count++);
        };
      }
    } //end if STA_LIST   
    else if(command == "AP_LIST" || command == "APLIST"){
      // scan AP ทั้งหมด
      Serial.print("AP scanning...");
      int n = WiFi.scanNetworks();
      Serial.println("done");
      if (n == 0)
        Serial.println("no networks found");
      else {
        for (int i = 0; i < n; ++i) {
          Serial.printf("[AP LIST] %d.  %s [%s] (%d dBm) %s %s\n", 
              i+1, (char*)(WiFi.SSID(i).c_str()), mac2String(WiFi.BSSID(i)).c_str(), 
              WiFi.RSSI(i), (char*)(WiFi.isSimplePair(i)?"Simple-Pair":""), 
              (char*)((WiFi.encryptionType(i) == ENC_TYPE_NONE)?  " ":"*" ) );
          delay(10);
        }
      }
      Serial.println();
    }//end if AP_LIST
    else if(command == "WIFI"){
      String subcommand = String(token[1]); subcommand.toUpperCase();
      String Value      = String(token[2]); Value.toUpperCase();
      if( subcommand == "SSID" ){
        sprintf((char*)config.ssid,"%s", (char*) String(token[2]).c_str());
        Serial.printf("[WiFi] set SSID = %s\n", config.ssid);
      }
      else if(subcommand == "PASSWORD") {
        if(Value == "NULL"){
          memset(config.password,0,64);
        }else{
          sprintf((char*)config.password,"%s", (char*) String(token[2]).c_str());
        }
        Serial.printf("[WiFi] set PASSWORD = %s\n", config.password);
      }
      else if(subcommand == "BSSID") {
        if(Value == "NULL"){
          memset(config.bssid,0,6);
        }else{
          uint8_t mac_buf[6];
          if( String2mac( String(token[2]), mac_buf)) {
            memcpy( config.bssid, mac_buf, 6);
          }
        }
        Serial.printf("[WiFi] set BSSID = %s\n", mac2String(config.bssid).c_str());
      }
      else if( subcommand == "AP_SSID" ){
        sprintf((char*)config.ap_ssid,"%s", (char*) String(token[2]).c_str());
        Serial.printf("[WiFi] set AP_SSID = %s\n", config.ap_ssid);
      }
      else if(subcommand == "AP_PASSWORD") {
        if(Value == "NULL"){
          memset(config.ap_password,0,64);
        }else{
          sprintf((char*)config.ap_password,"%s", (char*) String(token[2]).c_str());
        }
        Serial.printf("[WiFi] set AP_PASSWORD = %s\n", config.ap_password);
      }
      else if(subcommand == "LED") {
        config.wifi_led_enable = ( String(token[2]) == "1")? 1 : 0;
        Serial.printf("[WiFi] set WiFi_Led = %s\n", config.wifi_led_enable? (char*)"True":(char*)"False");
        wifi_led_init();
      }
      else if(subcommand == "SCAN"){
        // scan AP ทั้งหมด
        Serial.print("AP scanning...");
        int n = WiFi.scanNetworks();
        Serial.println("done");
        if (n == 0)
          Serial.println("no networks found");
        else {
          for (int i = 0; i < n; ++i) {
            Serial.printf("[AP LIST] %d.  %s [%s] (%d dBm) %s %s\n", 
                i+1, (char*)(WiFi.SSID(i).c_str()), mac2String(WiFi.BSSID(i)).c_str(), 
                WiFi.RSSI(i), (char*)(WiFi.isSimplePair(i)?"Simple-Pair":""), 
                (char*)((WiFi.encryptionType(i) == ENC_TYPE_NONE)?  " ":"*" ) );
            delay(10);
          }
        }
        Serial.println();
        return;
      }
      else if(subcommand == "RECONNECT"){
        WiFi.reconnect();
        return;
      }
      else if(subcommand == "AP_HIDDEN"){
        bool h_buf = String(token[2]).toInt(); 
        Serial.printf("[AP] %s is %s\n", (char*)config.ap_ssid, h_buf? (char*)"hidden":"unhidden");
        if( (config.ap_hidden != h_buf) && (h_buf ==0 || h_buf ==1 ) ) {
          config.ap_hidden = h_buf;
          WiFi.softAPdisconnect();
          WiFi.softAP((char*)config.ap_ssid, (char*)config.ap_password, config.ap_channel, config.ap_hidden, MAX_CLIENTS );
          ip_napt_enable(WiFi.softAPIP(), true);
        }
      }
      config_save(&config);
    } // end if WIFI
    else if(command == "NTP"){
      String subcommand = String(token[1]); subcommand.toUpperCase();
      if( subcommand == "TIMEZONE" ){
        config.timezone = String(token[2]).toInt(); // ชั่วโมง
        Serial.printf("[NTP] set TimeZone = %s%d\n", (config.timezone>0)? (char*)"+":(char*)"",config.timezone);
      }
      else if( subcommand == "DAYLIGHT" ){
        config.daylightsaving = String(token[2]).toInt(); //ชั่วโมง
        Serial.printf("[NTP] set Daylight-Saving = %d hours\n", config.daylightsaving);
      }
      else if( subcommand == "INTERVAL" ){
        if(String(token[2]).toInt()>=1) {
          config.ntp_interval = String(token[2]).toInt(); // นาที
          Serial.printf("[NTP] set interval = %d mins\n", config.ntp_interval);
        }else{
          Serial.println("[NTP] can't set 0 interval");
          return;
        }
      }
      else if( subcommand == "SERVER1"){
        if(String(token[2])!= ""){
          IPAddress ntp_addr;
          WiFi.hostByName(String(token[2]).c_str(), ntp_addr);
          if( !uint32_t(ntp_addr)) {
            sprintf((char*)config.ntp_server1,"%s", (char*)(String(token[2]).c_str()));
          }
        }
      }
      else if( subcommand == "SERVER2"){
        if(String(token[2])!= ""){
          IPAddress ntp_addr;
          WiFi.hostByName(String(token[2]).c_str(), ntp_addr);
          if( !uint32_t(ntp_addr)) {
            sprintf((char*)config.ntp_server2,"%s", (char*)(String(token[2]).c_str()));
          }
        }
      }
      else if( subcommand == "SERVER3"){
        if(String(token[2])!= ""){
          IPAddress ntp_addr;
          WiFi.hostByName(String(token[2]).c_str(), ntp_addr);
          if( !uint32_t(ntp_addr)) {
            sprintf((char*)config.ntp_server3,"%s", (char*)(String(token[2]).c_str()));
          }
        }
      }
      config_save(&config);
      ntpsync_task.detach();
      ntp_sync_internettime();
    } //end if NTP
    else if(command == "SYSTEM"){
      String subcommand = String(token[1]); subcommand.toUpperCase();
      if(subcommand == "EXPIRE"){
        if(String(token[2]).toInt() > 0 ) {  // วัน
          config.expire_limit = String(token[2]).toInt()*24*60*60;
        }
      }
      else if(subcommand == "EXPIRE_MIN"){
        if(String(token[2]).toInt() > 0 ) {  // วัน
          config.expire_limit = String(token[2]).toInt()*60;          
        }
      }
      config_save(&config);

      struct tm timeinfo;
      expire_time = config.uTridentFirstRun + config.expire_limit;
      gmtime_r(&expire_time, &timeinfo);
      Serial.printf("μ-Trident8266 expire on %02d/%02d/%02d %02d:%02d:%02d\n", 
        timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);  
      ntpsync_task.detach();
      ntp_sync_internettime();
    } //end if SYSTEM
    else if(command == "SHOW"){

      Serial.println();
      Serial.printf("μ-Trident8266 : %s\n",WiFi.hostname().c_str());
    
      if( config.uTridentFirstRun != 0){
        char timestr_buf[10];
        struct tm timeinfo;
        gmtime_r(&config.uTridentFirstRun, &timeinfo);
        Serial.printf("   start on  %02d/%02d/%02d %02d:%02d:%02d\n", 
          timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    
        expire_time = config.uTridentFirstRun + config.expire_limit;
        gmtime_r(&expire_time, &timeinfo);
        Serial.printf("   expire on %02d/%02d/%02d %02d:%02d:%02d\n", 
          timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      }
  
      Serial.println("WIFI:");
      Serial.printf("  STA:\n    ssid     : %s\n    password : %s\n",
              (char*)(config.ssid), (char*)(config.password));
      Serial.printf("    localIP  : %s\n    MAC      : %s\n",
              WiFi.localIP().toString().c_str(), WiFi.macAddress().c_str());
      Serial.printf("  AP:\n    ssid     : %s\n    password : %s\n",
              (char*)(config.ap_ssid), (char*)(config.ap_password));
      Serial.printf("    softAPIP : %s\n    MAC      : %s\n",
              WiFi.softAPIP().toString().c_str(), WiFi.softAPmacAddress().c_str());
      Serial.printf("    open     : %s\n", config.ap_open? (char*)"true": (char*)"false");
      Serial.printf("    enable   : %s\n", config.ap_enable? (char*)"true": (char*)"false");
      Serial.printf("    hidden   : %s\n", config.ap_hidden? (char*)"true": (char*)"false");
 
      int n= wifi_softap_get_station_num();
      Serial.printf("    stations : %d\n",n);
      if( n ) {
        char macstr[20];
        // แบบที่รับจาก dhcpserver.h
        int dhcps_count=0; 
        struct dhcps_pool *dhcps_p = dhcps_get_mapping(dhcps_count++);
        while(dhcps_p !=NULL){
          time_t lease_clock = time(nullptr); 
          char lease_timestr[20]="";

          if(lease_clock > 1000) {
            lease_clock += 60*dhcps_p->lease_timer;
            struct tm timeinfo;
            gmtime_r(&lease_clock, &timeinfo);
            sprintf(lease_timestr, "%02d/%02d/%d %02d:%02d", 
                      timeinfo.tm_mday, timeinfo.tm_mon, 1900+timeinfo.tm_year,
                      timeinfo.tm_hour, timeinfo.tm_min + (timeinfo.tm_sec)? 1 :0);
          }
          
          Serial.printf("       sta%02d : %s    %s    %d mins    %s\n",
                        dhcps_count,
                        mac2String(dhcps_p->mac).c_str(), 
                        IPAddress(dhcps_p->ip.addr).toString().c_str(),
                        dhcps_p->lease_timer, 
                        lease_timestr );
          dhcps_p = dhcps_get_mapping(dhcps_count++);
        };

      }
      
      struct portmap_table *p;
      ip_addr_t i_ip;
      for (int i = 0; i<IP_PORTMAP_MAX; i++) {
        p = &ip_portmap_table[i];
        if(p->valid) {
          i_ip.addr = p->daddr;
          if(i==0) Serial.println("IP Forward:");
            Serial.printf("    [%s] %s:%d -> "  IPSTR ":%d\n", 
                p->proto==IP_PROTO_TCP?"TCP":p->proto==IP_PROTO_UDP?"UDP":"???",
                (char*)WiFi.localIP().toString().c_str(), ntohs(p->mport), IP2STR(&i_ip), ntohs(p->dport));
        }
      }

      Serial.println("Simple-Pair:");
      Serial.printf("    enable   : %s\n",  config.ap_simplepair_enable? (char*)"true": (char*)"false");
      Serial.printf("    key      : %s\n",  (char*)config.simplepair_key);
      Serial.printf("    data     : %s\n",  (char*)config.simplepair_data);
      Serial.println("NTP:");
      Serial.printf("    TIME     : %s\n", time_now().c_str());
      Serial.printf("    TimeZone : %s%d\n", (config.timezone>0)? (char*)"+":(char*)"", config.timezone);
      Serial.printf("    Daylight : %d hours\n", config.daylightsaving); 
      Serial.printf("    Server1  : %s\n", (char*) config.ntp_server1);
      Serial.printf("    Server2  : %s\n", (char*) config.ntp_server2);
      Serial.printf("    Server3  : %s\n", (char*) config.ntp_server3);
      Serial.printf("    Interval : %d mins\n", config.ntp_interval);
      Serial.printf("Clock Speed  : %d MHz\n", config.clock_speed);

    } // end if SHOW
    else if(command == "RESTART"){
      ESP.restart();
    } // end if RESTART
    else if(command == "TIME"){
      Serial.print("[TIME] ");
      Serial.println(time_now());
    }
    else if(command == "SIMPLEPAIR"){
      String subcommand = String(token[1]); subcommand.toUpperCase();
      if(subcommand == "PAIR"){
        
      }
      else if(subcommand == "SCAN"){
        Serial.println("[Simple-Pair] scan SP_AP");
      }
    }
    else if(command == "HOSTNAME"){
      if( MAX_TOKEN == 1 ){
        Serial.print("[HOSTNAME] ");
        Serial.println(WiFi.hostname());          // get hostname
      }
      else if( MAX_TOKEN == 2){
        if(WiFi.hostname(token[1])){
          Serial.printf("[HOSTNAME] set HOSTNAME to %s\n", token[1] );  // set hostname
        }
      }
    }
  }//end Serial input
}

String mac2String(uint8_t* mac){
  char macstr[20];
  sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  return String(macstr);
}

bool String2mac(String macstr, uint8_t* mac ){
  char * token_bufer = strtok( (char*)macstr.c_str(), ":");
  uint8_t i=0;
  while( token_bufer != NULL) {
    mac[i++] = (uint8_t) strtol(token_bufer, NULL, 16);;
    token_bufer = strtok(NULL, ":");
  }
  return (i==6)? true : false;
}

IPAddress mac2IPAddress(uint8_t* mac){
  IPAddress station_ip = IPAddress(0,0,0,0);

  struct station_info *sta_info = wifi_softap_get_station_info();
  while ( sta_info != NULL) {
    if ( memcmp(mac , sta_info->bssid, 6) == 0) {
      station_ip = IPAddress(sta_info->ip.addr);
      break;
    } sta_info = sta_info->next;
  }
  return station_ip;
}

String IPAddress2macString(IPAddress station_ip){
  char macstr[20];

  struct station_info *sta_info = wifi_softap_get_station_info();
  while ( sta_info != NULL) {
    if ( station_ip == IPAddress(sta_info->ip.addr)) {
      sprintf( macstr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          sta_info->bssid[0],sta_info->bssid[1],sta_info->bssid[2],
          sta_info->bssid[3],sta_info->bssid[4],sta_info->bssid[5]);
      return String(macstr);
    } sta_info = sta_info->next;
  }
  return "";
}

String time_now(){
  char timestr_buf[10];
  time_t now; struct tm timeinfo;
  now = time(nullptr);
  gmtime_r(&now, &timeinfo);
  sprintf(timestr_buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(timestr_buf);
}

void wifi_led_init(){
  if (config.wifi_led_enable) {
    wifi_status_led_install(2,  PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
  }else{
    wifi_status_led_uninstall ();
    digitalWrite(2,1);
  }
}

bool isExpire(){
  time_t diff = expire_time - time(nullptr);
  //Serial.println(diff);
  return (diff < 0)? 1:0;    
}

err_t ICACHE_FLASH_ATTR 
my_input_ap (struct pbuf *p, struct netif *inp) 
{
//  os_printf("Got packet from STA\r\n");
  wifi_led_init();

  Bytes_in += p->tot_len;
  Packets_in++;
  
  orig_input_ap (p, inp);
}

err_t ICACHE_FLASH_ATTR 
my_output_ap (struct netif *outp, struct pbuf *p) {

//  os_printf("Send packet to STA\r\n");
  wifi_led_init();

  Bytes_out += p->tot_len;
  Packets_out++;
  
  orig_output_ap (outp, p);
}

static void ICACHE_FLASH_ATTR 
patch_netif_ap(netif_input_fn ifn, netif_linkoutput_fn ofn, bool nat)
{  
  struct netif *nif;
  ip_addr_t ap_ip;

  ap_ip = config.ap_addr;
  ip4_addr4(&ap_ip) = 1;
  
  for (nif = netif_list; nif != NULL && nif->ip_addr.addr != ap_ip.addr; nif = nif->next);
  if (nif == NULL) return;
  
  nif->napt = nat?1:0;
  if (nif->input != ifn) {
    orig_input_ap = nif->input;
    nif->input = ifn;
  }
  if (nif->linkoutput != ofn) {
    orig_output_ap = nif->linkoutput;
    nif->linkoutput = ofn;
  }
}

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

