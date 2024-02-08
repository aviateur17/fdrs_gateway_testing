#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <espnow.h>
#elif defined(ESP32)
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#endif

#define PEER_TIMEOUT 300000
FDRSPeer peer_list[16];
const uint8_t espnow_size = 250 / sizeof(DataReading);

#ifdef ESP32
esp_now_peer_info_t peerInfo;
#endif

const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const uint8_t mac_prefix[] = {MAC_PREFIX};
uint8_t selfAddress[] = {MAC_PREFIX, UNIT_MAC};
uint8_t incMAC[6];

uint8_t ESPNOW1[] = {MAC_PREFIX, ESPNOW_NEIGHBOR_1};
uint8_t ESPNOW2[] = {MAC_PREFIX, ESPNOW_NEIGHBOR_2};
extern time_t now;
// JL - pingFlagEspNow var probably to be removed
bool pingFlagEspNow = false;

void recvTimeEspNow(uint32_t t) {
  // Process time if there is no master set yet or if LoRa is the master or if we are already the time master
  if(timeMaster.tmType == TM_NONE || timeMaster.tmType == TM_LORA || (timeMaster.tmType == TM_ESPNOW && timeMaster.tmAddress == incMAC[4] << 8 | incMAC[5])) {
    DBG("Received time via ESP-NOW from 0x" + String(incMAC[5], HEX));
    if(timeMaster.tmAddress == 0x0000) {
      timeMaster.tmType = TM_ESPNOW;
      timeMaster.tmAddress = incMAC[4] << 8 & incMAC[5];
      DBG("ESP-NOW time master is 0x" + String(incMAC[5], HEX));
    }
    setTime(t);
    timeMaster.tmLastTimeSet = millis();
  }
  else {
    DBG("ESP-NOW 0x" + String(incMAC[5], HEX) + " is not time master, discarding request");
  }
  return;
}

// Set ESP-NOW send and receive callbacks for either ESP8266 or ESP32
#if defined(ESP8266)
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus)
{
}
void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len)
{
#elif defined(ESP32)
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
}
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
#endif
  memcpy(&incMAC, mac, sizeof(incMAC));
  if (len < sizeof(DataReading))
  {
    DBG("Incoming ESP-NOW System Packet from 0x" + String(incMAC[5], HEX));
    memcpy(&theCmd, incomingData, sizeof(theCmd));
    // processing is handled in the handlecommands() function in gateway.h - do not process here
  }
  else {
    memcpy(&theData, incomingData, sizeof(theData));
    DBG("Incoming ESP-NOW Data Reading from 0x" + String(incMAC[5], HEX));
    ln = len / sizeof(DataReading);
    if (memcmp(&incMAC, &ESPNOW1, 6) == 0)
    {
      newData = event_espnow1;
      return;
    }
    if (memcmp(&incMAC, &ESPNOW2, 6) == 0)
    {
      newData = event_espnow2;
      return;
    }
    newData = event_espnowg;
  }
}

void begin_espnow()
{
  DBG("Initializing ESP-NOW!");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  // Init ESP-NOW for either ESP8266 or ESP32 and set MAC address
#if defined(ESP8266)
#ifdef USE_LR
  DBG(" LR mode is only available on ESP32. ESP-NOW will begin in normal mode.");
#endif
  wifi_set_macaddr(STATION_IF, selfAddress);
  if (esp_now_init() != 0)
  {
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

#elif defined(ESP32)
#ifdef USE_LR
  DBG(" ESP-NOW LR mode is active!");
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
#endif
  esp_wifi_set_mac(WIFI_IF_STA, &selfAddress[0]);
  if (esp_now_init() != ESP_OK)
  {
    DBG("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr, broadcast_mac, 6);
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    DBG("Failed to add peer bcast");
    return;
  }
#endif // ESP8266
  DBG(" ESP-NOW Initialized.");
}

// Returns an expired entry in peer_list, -1 if full.
int find_espnow_peer()
{
  for (int i = 0; i < 16; i++)
  {
    if (peer_list[i].last_seen == 0)
    {
      // DBG("Using peer entry " + String(i));
      return i;
    }
  }
  for (int i = 0; i < 16; i++)
  {
    if ((millis() - peer_list[i].last_seen) >= PEER_TIMEOUT)
    {
      // DBG("Recycling peer entry " + String(i));
      esp_now_del_peer(peer_list[i].mac);

      return i;
    }
  }
  DBG("No open peers");
  return -1;
}

// Returns the index of the peer list array element that contains the provided MAC address, -1 if not found
int getFDRSPeer(uint8_t *mac)
{
  // DBG("Getting peer #");

  for (int i = 0; i < 16; i++)
  {
    if (memcmp(mac, &peer_list[i].mac, 6) == 0)
    {
      DBG("Peer is entry #" + String(i));
      return i;
    }
  }

  // DBG("Couldn't find peer");
  return -1;
}

void add_espnow_peer()
{
  DBG("Device requesting peer registration: 0x" + String(incMAC[5], HEX));
  int peer_num = getFDRSPeer(&incMAC[0]);
  if (peer_num == -1) // if the device isn't registered
  {
    // DBG("Device not yet registered, adding to peer list");
    int open_peer = find_espnow_peer();            // find open spot in peer_list
    memcpy(&peer_list[open_peer].mac, &incMAC, 6); // save MAC to open spot
    peer_list[open_peer].last_seen = millis();


#if defined(ESP32)
    esp_now_peer_info_t peerInfo;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, incMAC, 6);
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
      DBG("Failed to add peer");
      return;
    }
#endif
#if defined(ESP8266)
    esp_now_add_peer(incMAC, ESP_NOW_ROLE_COMBO, 0, NULL, 0);

#endif
    SystemPacket sys_packet = {.cmd = cmd_add, .param = PEER_TIMEOUT};
    esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
  }
  else
  {
    DBG("Refreshing existing peer registration for 0x" + String(incMAC[5], HEX));
    peer_list[peer_num].last_seen = millis();

    SystemPacket sys_packet = {.cmd = cmd_add, .param = PEER_TIMEOUT};
    esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
  }
  if(validTimeFlag){
    SystemPacket sys_packet = { .cmd = cmd_time, .param = now };
    esp_now_send(incMAC, (uint8_t *)&sys_packet, sizeof(SystemPacket));
  }
}

// Lower level function meant to be called by other functions 
// Sends SystemPacket via ESP-NOW
esp_err_t sendESPNow(uint8_t *dest, SystemPacket *data) {
  esp_err_t sendResult;
  if (dest != nullptr && !esp_now_is_peer_exist(dest))
  {
#ifdef ESP8266
    sendResult = esp_now_add_peer(dest, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
    if (sendResult != ESP_OK)
    {
      DBG("Failed to add peer");
      return sendResult;
    }
#endif
#if defined(ESP32)
    esp_now_peer_info_t peerInfo;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, dest, 6);
    sendResult = esp_now_add_peer(&peerInfo);
    if (sendResult != ESP_OK)
    {
      DBG("Failed to add peer");
      return sendResult;
    }
#endif
    sendResult = esp_now_send(dest, (uint8_t *)data, sizeof(SystemPacket));
    esp_now_del_peer(dest);
  }
  else
  {
    sendResult = esp_now_send(dest, (uint8_t *)data, sizeof(SystemPacket));
  }
  return sendResult;
}

// Sends ping reply to sender
esp_err_t pingback_espnow()
{
  SystemPacket sys_packet = { .cmd = cmd_ping, .param = 1 };
  esp_err_t result;
  
  DBG("ESP-NOW Ping back to sender 0x" + String(incMAC[5], HEX));
  result = sendESPNow(incMAC, &sys_packet);
  return result;
}

// Sends time to both neighbors and all peers
esp_err_t sendTimeESPNow() {
  
  esp_err_t result1 = ESP_OK, result2 = ESP_OK, result3 = ESP_OK;
  SystemPacket sys_packet = { .cmd = cmd_time, .param = now };

  if((timeMaster.tmAddress != ESPNOW1[4] << 8 | ESPNOW1[5]) && ESPNOW1[5] != 0x00) {
    DBG("Sending time to ESP-NOW Peer 1");
    result1 = sendESPNow(ESPNOW1, &sys_packet);
  }
  if((timeMaster.tmAddress != ESPNOW2[4] << 8 | ESPNOW2[5]) && ESPNOW2[5] != 0x00) {
    DBG("Sending time to ESP-NOW Peer 2");
    result2 = sendESPNow(ESPNOW2, &sys_packet);
  }
  DBG("Sending time to ESP-NOW registered peers");
  result3 = sendESPNow(nullptr, &sys_packet);

  if(result1 != ESP_OK || result2 != ESP_OK || result3 != ESP_OK){
    return ESP_FAIL;
  }
  else {
    return ESP_OK;
  }
}

// Lower level function meant to be called by other functions 
// Sends DataReading via ESP-NOW
esp_err_t sendESPNow(uint8_t *dest, DataReading *data) {
  esp_err_t sendResult;
  bool tempPeerFlag = false;


  if (dest != nullptr && !esp_now_is_peer_exist(dest))
  {
    tempPeerFlag = true;
#ifdef ESP8266
    sendResult = esp_now_add_peer(dest, ESP_NOW_ROLE_COMBO, 0, NULL, 0);
    if (sendResult != ESP_OK)
    {
      DBG("Failed to add peer");
      return sendResult;
    }
  }
#endif
#if defined(ESP32)
    esp_now_peer_info_t peerInfo;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    memcpy(peerInfo.peer_addr, dest, 6);
    sendResult = esp_now_add_peer(&peerInfo);
    if (sendResult != ESP_OK)
    {
      DBG("Failed to add peer");
      return sendResult;
    }
  }
#endif // ESP32
    for(int i = 0; i < ln; ) {
      if(ln > espnow_size) {
        sendResult = esp_now_send(dest, (uint8_t *)&data[i], espnow_size * sizeof(DataReading));
        if(sendResult == ESP_OK) {
          i += espnow_size;
        }
        else {
          // Send failed!
          delay(10);
          return sendResult;
        }
      }
      else {
        sendResult = esp_now_send(dest, (uint8_t *)&data[i], ln * sizeof(DataReading));
        if(sendResult == ESP_OK) {
          ln = 0;
        }
        else {
          // Send Failed!
          delay(10);
          return sendResult;
        }
      }
    } 
    if(tempPeerFlag) {
      esp_now_del_peer(dest);
    }
    return sendResult;
}

// Action used in current version - not to be removed
esp_err_t sendESPNowNbr(uint8_t interface) {
  esp_err_t result;

  switch (interface)
  {
  case 1:
  { // These brackets are required!
    DBG("Sending to ESP-NOW Neighbor #1: 0x" + String(ESPNOW_NEIGHBOR_1, HEX));
    result = sendESPNow(ESPNOW1, theData);
    esp_now_del_peer(ESPNOW1);
    break;
  }
  case 2:
  { // These brackets are required!
    DBG("Sending to ESP-NOW Neighbor #2: 0x" + String(ESPNOW_NEIGHBOR_2, HEX));
    result = sendESPNow(ESPNOW2, theData);
    esp_now_del_peer(ESPNOW2);
    break;
  }
  default:
    result = ESP_FAIL;
    break;
  }
  return result;
}

// Action used in current version - not to be removed
esp_err_t sendESPNowPeers() {
  esp_err_t result;
  DBG("Sending to ESP-NOW peers.");
  result = sendESPNow(nullptr, theData);
  return result;
}

esp_err_t sendESPNowTempPeer(uint8_t *dest) {
  esp_err_t result;
  DBG("Sending ESP-NOW temp peer: 0x" + String(*dest, HEX));
  result = sendESPNow(dest, theData);
  esp_now_del_peer(dest);
  return result;
}

// Legacy Action used in previous versions - not to be removed
esp_err_t sendESPNow(uint8_t address) {
  esp_err_t result;
  uint8_t temp_peer[] = {MAC_PREFIX, address};
  result = sendESPNowTempPeer(temp_peer);
  return result;
}

// FDRS node pings gateway and listens for a defined amount of time for a reply
// Blocking function for timeout amount of time (up to timeout time waiting for reply)(IE no callback)
// Returns the amount of time in ms that the ping takes or predefined value if ping fails within timeout
uint32_t pingFDRSEspNow(uint8_t *address, uint32_t timeout) {
    SystemPacket sys_packet = {.cmd = cmd_ping, .param = 0};
    
    esp_now_send(address, (uint8_t *)&sys_packet, sizeof(SystemPacket));
    DBG(" ESP-NOW ping sent.");


    //  Should the code below be removed in place of pingback_espnow????
    // pingFlagEspNow is never set to true so this wouldn't run anyways???
    uint32_t ping_start = millis();
    pingFlagEspNow = false;
    while ((millis() - ping_start) <= timeout)
    {
        yield(); // do I need to yield or does it automatically?
        if (pingFlagEspNow)
        {
            DBG("ESP-NOW Ping Reply in " + String(millis() - ping_start) + "ms from 0x" + String(address[5], HEX));
            return (millis() - ping_start);
        }
    }
    DBG("No ESP-NOW ping returned within " + String(timeout) + "ms.");
    return UINT32_MAX;
    //  ?????
}