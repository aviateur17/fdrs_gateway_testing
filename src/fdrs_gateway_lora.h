#include <RadioLib.h>

#define GLOBAL_ACK_TIMEOUT 400 // LoRa ACK timeout in ms. (Minimum = 200) 
#define GLOBAL_LORA_RETRIES 2  // LoRa ACK automatic retries [0 - 3]
#define GLOBAL_LORA_TXPWR 17   // LoRa TX power in dBm (: +2dBm - +17dBm (for SX1276-7) +20dBm (for SX1278))

// select LoRa band configuration
#if defined(LORA_FREQUENCY)
#define FDRS_LORA_FREQUENCY LORA_FREQUENCY
#else
#define FDRS_LORA_FREQUENCY GLOBAL_LORA_FREQUENCY
#endif // LORA_FREQUENCY

// select LoRa SF configuration
#if defined(LORA_SF)
#define FDRS_LORA_SF LORA_SF
#else
#define FDRS_LORA_SF GLOBAL_LORA_SF
#endif // LORA_SF

// select LoRa ACK configuration
#if defined(LORA_ACK) || defined(GLOBAL_LORA_ACK)
#define FDRS_LORA_ACK
#endif // LORA_ACK

// select LoRa ACK Timeout configuration
#if defined(LORA_ACK_TIMEOUT)
#define FDRS_ACK_TIMEOUT LORA_ACK_TIMEOUT
#else
#define FDRS_ACK_TIMEOUT GLOBAL_ACK_TIMEOUT
#endif // LORA_ACK_TIMEOUT

// select LoRa Retry configuration
#if defined(LORA_RETRIES)
#define FDRS_LORA_RETRIES LORA_RETRIES
#else
#define FDRS_LORA_RETRIES GLOBAL_LORA_RETRIES
#endif // LORA_RETRIES

// select  LoRa Tx Power configuration
#if defined(LORA_TXPWR)
#define FDRS_LORA_TXPWR LORA_TXPWR
#else
#define FDRS_LORA_TXPWR GLOBAL_LORA_TXPWR
#endif // LORA_TXPWR

// select  LoRa BANDWIDTH configuration
#if defined(LORA_BANDWIDTH)
#define FDRS_LORA_BANDWIDTH LORA_BANDWIDTH
#else
#define FDRS_LORA_BANDWIDTH GLOBAL_LORA_BANDWIDTH
#endif // LORA_BANDWIDTH

// select  LoRa Coding Rate configuration
#if defined(LORA_CR)
#define FDRS_LORA_CR LORA_CR
#else
#define FDRS_LORA_CR GLOBAL_LORA_CR
#endif // LORA_CR

// select  LoRa SyncWord configuration
#if defined(LORA_SYNCWORD)
#define FDRS_LORA_SYNCWORD LORA_SYNCWORD
#else
#define FDRS_LORA_SYNCWORD GLOBAL_LORA_SYNCWORD
#endif // LORA_SYNCWORD

// select  LoRa Release Interval configuration
#if defined(LORA_INTERVAL)
#define FDRS_LORA_INTERVAL LORA_INTERVAL
#else
#define FDRS_LORA_INTERVAL GLOBAL_LORA_INTERVAL
#endif // LORA_INTERVAL

#ifndef LORA_BUSY
#define LORA_BUSY RADIOLIB_NC
#endif

const uint8_t lora_size = 250 / sizeof(DataReading);

#ifdef CUSTOM_SPI
#ifdef ARDUINO_ARCH_RP2040
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY, SPI1);
#endif  // RP2040
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY, SPI);
#else
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY);
#endif  // CUSTOM_SPI


#ifndef USE_ESPNOW   // mac_prefix used for both ESP-NOW and LoRa - avoid redefinition warnings
const uint8_t mac_prefix[] = {MAC_PREFIX};
const uint8_t selfAddress[] = {MAC_PREFIX, UNIT_MAC};
#endif

bool pingFlagLoRa = false;
bool transmitFlag = false;            // flag to indicate transmission or reception state
volatile bool enableInterrupt = true; // disable interrupt when it's not needed
volatile bool operationDone = false;  // flag to indicate that a packet was sent or received

uint16_t LoRa1 = ((mac_prefix[4] << 8) | LORA_NEIGHBOR_1); // Use 2 bytes for LoRa addressing instead of previous 3 bytes
uint16_t LoRa2 = ((mac_prefix[4] << 8) | LORA_NEIGHBOR_2);
uint16_t loraGwAddress = ((selfAddress[4] << 8) | selfAddress[5]); // last 2 bytes of gateway address
uint16_t loraBroadcast = 0xFFFF;
unsigned long receivedLoRaMsg = 0; // Number of total LoRa packets received, destined for us and of valid size
unsigned long ackOkLoRaMsg = 0;    // Number of total LoRa packets received with valid CRC
unsigned long transmitLoRaMsgwAck = 0; // Number of total LoRa packets transmitted with ACK enabled
unsigned long txMsgAckOk = 0; // Number of total LoRa packets where successful ACK was received 
unsigned long lastTimeMasterPing = 0;
extern time_t now;
time_t netTimeOffset = UINT32_MAX;  // One direction of LoRa Ping time in units of seconds (1/2 full ping time)


typedef struct DataBuffer
{
  DataReading buffer[256];
  uint8_t len = 0;
} DataBuffer;

DataBuffer LORA1Buffer;
DataBuffer LORA2Buffer;
DataBuffer LORABBuffer;

enum
{
  TxLoRa1,
  TxLoRa2,
  TxLoRaB,
  TxIdle
} TxStatus = TxIdle;

uint8_t tx_buffer_position = 0;
uint32_t tx_start_time;

// Function prototypes
crcResult transmitLoRa(uint16_t *, DataReading *, uint8_t);
crcResult transmitLoRa(uint16_t *, SystemPacket *, uint8_t);
static uint16_t crc16_update(uint16_t, uint8_t);
crcResult handleLoRa();

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
  // check if the interrupt is enabled
  if (!enableInterrupt)
  {
    return;
  }
  // we sent or received  packet, set the flag
  operationDone = true;
}

// crc16_update used by both LoRa and filesystem

// CRC16 from https://github.com/4-20ma/ModbusMaster/blob/3a05ff87677a9bdd8e027d6906dc05ca15ca8ade/src/util/crc16.h#L71

/** @ingroup util_crc16
    Processor-independent CRC-16 calculation.
    Polynomial: x^16 + x^15 + x^2 + 1 (0xA001)<br>
    Initial value: 0xFFFF
    This CRC is normally used in disk-drive controllers.
    @param uint16_t crc (0x0000..0xFFFF)
    @param uint8_t a (0x00..0xFF)
    @return calculated CRC (0x0000..0xFFFF)
*/

static uint16_t crc16_update(uint16_t crc, uint8_t a)
{
  int i;

  crc ^= a;
  for (i = 0; i < 8; ++i)
  {
    if (crc & 1)
      crc = (crc >> 1) ^ 0xA001;
    else
      crc = (crc >> 1);
  }

  return crc;
}

// For debugging purposes
void printLoraPacket(uint8_t *p, int size)
{
  printf("Printing packet of size %d.", size);
  for (int i = 0; i < size; i++)
  {
    if (i % 2 == 0)
      printf("\n%02d: ", i);
    printf("%02X ", p[i]);
  }
  printf("\n");
}

crcResult transmitLoRa(uint16_t *destMac, DataReading *packet, uint8_t len)
{
  crcResult crcReturned = CRC_NULL;
  uint16_t calcCRC = 0x0000;
  bool ack = false;

  uint8_t pkt[6 + (len * sizeof(DataReading))];

  pkt[0] = (*destMac >> 8);                           // high byte of destination MAC
  pkt[1] = (*destMac & 0x00FF);                       // low byte of destination MAC
  pkt[2] = selfAddress[4];                            // high byte of source MAC (ourselves)
  pkt[3] = selfAddress[5];                            // low byte of source MAC
  memcpy(&pkt[4], packet, len * sizeof(DataReading)); // copy data portion of packet
  // Calculate CRC for ACKs
  for (int i = 0; i < (sizeof(pkt) - 2); i++)
  { // Last 2 bytes are CRC so do not include them in the calculation itself
    // printf("CRC: %02X : %d\n",calcCRC, i);
    calcCRC = crc16_update(calcCRC, pkt[i]);
  }
  // Do we require ACKs?
#ifdef LORA_ACK
  ack = true;
#else
  ack = false;
#endif // LORA_ACK
  if(*destMac == 0xFFFF) {
    ack = false;
  }
  if(!ack) {
    calcCRC = crc16_update(calcCRC, 0xA1); // Recalculate CRC for No ACK
  }
  pkt[(len * sizeof(DataReading) + 4)] = (calcCRC >> 8); // Append calculated CRC to the last 2 bytes of the packet
  pkt[(len * sizeof(DataReading) + 5)] = (calcCRC & 0x00FF);
  if(ack) {
    int retries = FDRS_LORA_RETRIES + 1;
    while (retries != 0)
    {
      if (transmitLoRaMsgwAck != 0)
      {
          DBGF("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to gateway 0x" + String(*destMac, HEX) + ". Retries remaining: " + String(retries - 1) + ", Ack Ok " + String((float)txMsgAckOk / transmitLoRaMsgwAck * 100) + "%");
      }
      else
      {
          DBGF("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to gateway 0x" + String(*destMac, HEX) + ". Retries remaining: " + String(retries - 1));
      }
      // printLoraPacket(pkt,sizeof(pkt));
      int state = radio.transmit(pkt, sizeof(pkt));
      transmitFlag = true;
      if (state == RADIOLIB_ERR_NONE)
      {
      }
      else
      {
          DBG(" LoRa xmit failed, code " + String(state));
          // JL - should we restart the controller?  ESP.restart()? if ESP based
          while (true)
              ;
      }
      transmitLoRaMsgwAck++;
      unsigned long loraAckTimeout = millis() + FDRS_ACK_TIMEOUT;
      retries--;
      delay(10);
      while (crcReturned == CRC_NULL && (millis() < loraAckTimeout))
      {
          crcReturned = handleLoRa();
      }
      if (crcReturned == CRC_OK)
      {
          DBGFST("LoRa ACK Received! CRC OK");
          txMsgAckOk++;
          return CRC_OK; // we're done
      }
      else if (crcReturned == CRC_BAD)
      {
          DBGFST("LoRa ACK Received! CRC BAD");
          //  Resend original packet again if retries are available
      }
      else
      {
          DBG("LoRa Timeout waiting for ACK!");
          // resend original packet again if retries are available
      }
    }
  }
  // No ACK
  else {
    DBGF("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to LoRa MAC 0x" + String(*destMac, HEX));
    // printLoraPacket(pkt,sizeof(pkt));
    int state = radio.startTransmit(pkt, sizeof(pkt));
    transmitFlag = true;
    if (state == RADIOLIB_ERR_NONE)
    {
    }
    else
    {
      DBG(" LoRa xmit failed, code " + String(state));
      while (true)
        ;
    }
  }
  return crcReturned;
}

crcResult transmitLoRa(uint16_t *destMac, SystemPacket *packet, uint8_t len)
{
  crcResult crcReturned = CRC_NULL;
  uint16_t calcCRC = 0x0000;

  uint8_t pkt[6 + (len * sizeof(SystemPacket))];

  pkt[0] = (*destMac >> 8);                            // high byte of destination MAC
  pkt[1] = (*destMac & 0x00FF);                        // low byte of destination MAC
  pkt[2] = selfAddress[4];                             // high byte of source MAC (ourselves)
  pkt[3] = selfAddress[5];                             // low byte of source MAC
  memcpy(&pkt[4], packet, len * sizeof(SystemPacket)); // copy data portion of packet
  for (int i = 0; i < (sizeof(pkt) - 2); i++)
  { // Last 2 bytes are CRC so do not include them in the calculation itself
    // printf("CRC: %02X : %d\n",calcCRC, i);
    calcCRC = crc16_update(calcCRC, pkt[i]);
  }
  calcCRC = crc16_update(calcCRC, 0xA1);                  // No ACK for SystemPacket messages so generate new CRC with 0xA1
  pkt[(len * sizeof(SystemPacket) + 4)] = (calcCRC >> 8); // Append calculated CRC to the last 2 bytes of the packet
  pkt[(len * sizeof(SystemPacket) + 5)] = (calcCRC & 0x00FF);
  DBGF("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to LoRa MAC 0x" + String(*destMac, HEX));
  // printLoraPacket(pkt,sizeof(pkt));
  int state = radio.startTransmit(pkt, sizeof(pkt));
  transmitFlag = true;
  if (state == RADIOLIB_ERR_NONE)
  {
  }
  else
  {
    DBG(" LoRa xmit failed, code " + String(state));
    while (true)
      ;
  }
  return crcReturned;
}

void begin_lora()
{
#ifdef CUSTOM_SPI
#ifdef ESP32
  SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);
#endif  // ESP32
#ifdef ARDUINO_ARCH_RP2040
  SPI1.setRX(LORA_SPI_MISO);
  SPI1.setTX(LORA_SPI_MOSI);
  SPI1.setSCK(LORA_SPI_SCK);
  SPI1.begin(false);
#endif  //ARDUINO_ARCH_RP2040
#endif  // CUSTOM_SPI

#ifdef USE_SX126X
  int state = radio.begin(FDRS_LORA_FREQUENCY, FDRS_LORA_BANDWIDTH, FDRS_LORA_SF, FDRS_LORA_CR, FDRS_LORA_SYNCWORD, FDRS_LORA_TXPWR);
#else
  int state = radio.begin(FDRS_LORA_FREQUENCY, FDRS_LORA_BANDWIDTH, FDRS_LORA_SF, FDRS_LORA_CR, FDRS_LORA_SYNCWORD, FDRS_LORA_TXPWR, 8, 0);
#endif
  if (state == RADIOLIB_ERR_NONE)
  {
    DBG("RadioLib initialization successful!");
  }
  else
  {
    DBG("RadioLib initialization failed, code " + String(state));
    while (true)
      ;
  }
#ifdef USE_SX126X
  radio.setDio1Action(setFlag);
#else
  radio.setDio0Action(setFlag, RISING);
#endif

  radio.setCRC(false);
  DBG("LoRa Initialized. Frequency: " + String(FDRS_LORA_FREQUENCY) + "  Bandwidth: " + String(FDRS_LORA_BANDWIDTH) + "  SF: " + String(FDRS_LORA_SF) + "  CR: " + String(FDRS_LORA_CR) + "  SyncWord: " + String(FDRS_LORA_SYNCWORD) + "  Tx Power: " + String(FDRS_LORA_TXPWR) + "dBm");
  state = radio.startReceive(); // start listening for LoRa packets
  if (state == RADIOLIB_ERR_NONE)
  {
  }
  else
  {
    DBG(" LoRa rcv failed, code " + String(state));
    while (true)
      ;
  }
}

crcResult getLoRa()
{

  int packetSize = radio.getPacketLength();
  if ((((packetSize - 6) % sizeof(DataReading) == 0) || ((packetSize - 6) % sizeof(SystemPacket) == 0)) && packetSize > 0)
  { // packet size should be 6 bytes plus multiple of size of DataReading
    uint8_t packet[packetSize];
    uint16_t packetCRC = 0x0000; // CRC Extracted from received LoRa packet
    uint16_t calcCRC = 0x0000;   // CRC calculated from received LoRa packet
    uint16_t sourceMAC = 0x0000;
    uint16_t destMAC = 0x0000;

    radio.readData((uint8_t *)&packet, packetSize);

    destMAC = (packet[0] << 8) | packet[1];
    sourceMAC = (packet[2] << 8) | packet[3];
    packetCRC = ((packet[packetSize - 2] << 8) | packet[packetSize - 1]);
    DBGF("Packet Address: 0x" + String(packet[0], HEX) + String(packet[1], HEX) + " Self Address: 0x" + String(selfAddress[4], HEX) + String(selfAddress[5], HEX));
    if (destMAC == (selfAddress[4] << 8 | selfAddress[5]))
    { // Check if addressed to this device (2 bytes, bytes 1 and 2)
      // printLoraPacket(packet,sizeof(packet));
      // JL - Maybe move these down to the bottom to print after we know if we've successfully received the data
      if (receivedLoRaMsg != 0)
      { // Avoid divide by 0
        DBGF("Incoming LoRa. Size: " + String(packetSize) + " Bytes, RSSI: " + String(radio.getRSSI()) + "dBm, SNR: " + String(radio.getSNR()) + "dB, PacketCRC: 0x" + String(packetCRC, HEX) + ", Total LoRa received: " + String(receivedLoRaMsg) + ", CRC Ok Pct " + String((float)ackOkLoRaMsg / receivedLoRaMsg * 100) + "%");
      }
      else
      {
        DBGF("Incoming LoRa. Size: " + String(packetSize) + " Bytes, RSSI: " + String(radio.getRSSI()) + "dBm, SNR: " + String(radio.getSNR()) + "dB, PacketCRC: 0x" + String(packetCRC, HEX) + ", Total LoRa received: " + String(receivedLoRaMsg));
      }
      receivedLoRaMsg++;
      // JL - end potential move
      // Evaluate CRC
      for (int i = 0; i < (packetSize - 2); i++)
      { // Last 2 bytes of packet are the CRC so do not include them in calculation
        // printf("CRC: %02X : %d\n",calcCRC, i);
        calcCRC = crc16_update(calcCRC, packet[i]);
      }
      if ((packetSize - 6) % sizeof(DataReading) == 0)
      { // DataReading type packet
        DBG("Incoming LoRa DataReading.");
        if (calcCRC == packetCRC)
        {
          SystemPacket ACK = {.cmd = cmd_ack, .param = CRC_OK};
          DBGF("CRC Match, sending ACK packet to 0x" + String(sourceMAC, HEX));
          transmitLoRa(&sourceMAC, &ACK, 1); // Send ACK back to source
        }
        else if (packetCRC == crc16_update(calcCRC, 0xA1))
        { // Sender does not want ACK and CRC is valid
          DBGF("Address 0x" + String(sourceMAC, HEX) + " does not want ACK");
        }
        else
        {
          SystemPacket NAK = {.cmd = cmd_ack, .param = CRC_BAD};
          // Send NAK packet to sensor
          DBG("CRC Mismatch! Packet CRC is 0x" + String(packetCRC, HEX) + ", Calculated CRC is 0x" + String(calcCRC, HEX) + " Sending NAK packet to sensor 0x" + String(sourceMAC, HEX));
          transmitLoRa(&sourceMAC, &NAK, 1); // CRC did not match so send NAK to source
          newData = event_clear;             // do not process data as data may be corrupt
          return CRC_BAD;                    // Exit function and do not update newData to send invalid data further on
        }
        memcpy(&theData, &packet[4], packetSize - 6); // Split off data portion of packet (N - 6 bytes (6 bytes for headers and CRC))
        ln = (packetSize - 6) / sizeof(DataReading);
        ackOkLoRaMsg++;
        if (memcmp(&sourceMAC, &LoRa1, 2) == 0)
        { // Check if it is from a registered sender
          newData = event_lora1;
          return CRC_OK;
        }
        if (memcmp(&sourceMAC, &LoRa2, 2) == 0)
        {
          newData = event_lora2;
          return CRC_OK;
        }
        newData = event_lorag;
        return CRC_OK;
      }
      else if ((packetSize - 6) % sizeof(SystemPacket) == 0)
      {
        DBG("Incoming LoRa SystemPacket.");
        unsigned int ln = (packetSize - 6) / sizeof(SystemPacket);
        SystemPacket receiveData[ln];
        // SystemPacket data type do not require ACKs so we don't care which of the two CRC calculations are used
        if (packetCRC == calcCRC || packetCRC == crc16_update(calcCRC, 0xA1))
        {
          memcpy(receiveData, &packet[4], packetSize - 6); // Split off data portion of packet (N bytes)
          if (ln == 1 && receiveData[0].cmd == cmd_ack)
          {
            DBGF("ACK Received - CRC Match");
          }
          else if (ln == 1 && receiveData[0].cmd == cmd_ping)
          { // We have received a ping request or reply??
            if (receiveData[0].param == 1)
            { // This is a reply to our ping request
              pingFlagLoRa = true;
              DBGF("We have received a ping reply via LoRa from address 0x" + String(sourceMAC, HEX));
            }
            else if (receiveData[0].param == 0)
            {
              DBGF("We have received a ping request from 0x" + String(sourceMAC, HEX) + ", Replying.");
              SystemPacket pingReply = {.cmd = cmd_ping, .param = 1};
              transmitLoRa(&sourceMAC, &pingReply, 1);
            }
          }
          else if (ln == 1 && receiveData[0].cmd == cmd_time) {
            if(timeMaster.tmType == TM_NONE || timeMaster.tmType != TM_ESPNOW || (timeMaster.tmType == TM_LORA && timeMaster.tmAddress == sourceMAC)) {
              DBG("Time rcv from LoRa 0x" + String(sourceMAC, HEX));
              if(timeMaster.tmType == TM_NONE) {
                timeMaster.tmType = TM_LORA;
                timeMaster.tmAddress = sourceMAC;
                DBGF("Time master is LoRa 0x" + String(sourceMAC, HEX));
              }
              setTime(receiveData[0].param);
              adjTimeforNetDelay(netTimeOffset);
              timeMaster.tmLastTimeSet = millis();
            }
            else {
              DBGF("LoRa 0x" + String(sourceMAC, HEX) + " is not time master, discarding request");
            }
          }
          else
          { // data we have received is not yet programmed.  How we handle is future enhancement.
            DBGF("Received some LoRa SystemPacket data that is not yet handled.  To be handled in future enhancement.");
            DBGF("ln: " + String(ln) + "data type: " + String(receiveData[0].cmd));
          }
          ackOkLoRaMsg++;
          return CRC_OK;
        }
        else
        {
          DBGF("ACK Received CRC Mismatch! Packet CRC is 0x" + String(packetCRC, HEX) + ", Calculated CRC is 0x" + String(calcCRC, HEX));
          return CRC_BAD;
        }
      }
    }
    else
    {
      DBGF("Incoming LoRa packet of " + String(packetSize) + " bytes received from address 0x" + String(sourceMAC, HEX) + " destined for node address 0x" + String(destMAC, HEX));
      //  printLoraPacket(packet,sizeof(packet));
      return CRC_NULL;
    }
  }
  else
  {
    if (packetSize != 0)
    {
      DBGF("Incoming LoRa packet of " + String(packetSize) + " bytes not processed.");
      //  uint8_t packet[packetSize];
      //  radio.readData((uint8_t *)&packet, packetSize);
      //  printLoraPacket(packet,sizeof(packet));
      return CRC_NULL;
    }
  }
  return CRC_NULL;
}

// Sends packet to any node that is paired to this gateway
void broadcastLoRa()
{
  DBG("Sending to LoRa broadcast buffer");

  for (int i = 0; i < ln; i++)
  {
    LORABBuffer.buffer[LORABBuffer.len + i] = theData[i];
  }
  LORABBuffer.len += ln;
}

// Sends packet to neighbor gateways
void sendLoRaNbr(uint8_t interface)
{
  DBG("Sending to LoRa neighbor buffer");
  switch (interface)
  {
    case 1:
      {
        for (int i = 0; i < ln; i++)
        {
          LORA1Buffer.buffer[LORA1Buffer.len + i] = theData[i];
        }
        LORA1Buffer.len += ln;
        break;
      }
    case 2:
      {
        for (int i = 0; i < ln; i++)
        {
          LORA2Buffer.buffer[LORA2Buffer.len + i] = theData[i];
        }
        LORA2Buffer.len += ln;
        break;
      }
  }
}

void asyncReleaseLoRa(bool first_run)
{
  delay(3);
  if (first_run)
  {
    if (LORA1Buffer.len > 0) {
      TxStatus = TxLoRa1;
    } else if (LORA2Buffer.len > 0) {
      TxStatus = TxLoRa2;
    } else if (LORABBuffer.len > 0) {
      TxStatus = TxLoRaB;
    } else {
      goto TxFin;
    }
    tx_start_time = millis();
  }
  switch (TxStatus)
  {
    case TxLoRa1:
      if (LORA1Buffer.len - tx_buffer_position > lora_size) {
        transmitLoRa(&LoRa1, &LORA1Buffer.buffer[tx_buffer_position], lora_size);
        tx_buffer_position += lora_size;
      } else {
        transmitLoRa(&LoRa1, &LORA1Buffer.buffer[tx_buffer_position], LORA1Buffer.len - tx_buffer_position);
        tx_buffer_position = 0;
        if (LORA2Buffer.len > 0) {
          TxStatus = TxLoRa2;
        } else if ((LORABBuffer.len > 0)) {
          TxStatus = TxLoRaB;
        } else {
          goto TxFin;
        }
      }
      break;
    case TxLoRa2:
      if (LORA2Buffer.len - tx_buffer_position > lora_size) {
        transmitLoRa(&LoRa2, &LORA2Buffer.buffer[tx_buffer_position], lora_size);
        tx_buffer_position += lora_size;
      } else {
        transmitLoRa(&LoRa2, &LORA2Buffer.buffer[tx_buffer_position], LORA2Buffer.len - tx_buffer_position);
        tx_buffer_position = 0;
        if (LORABBuffer.len > 0) {
          TxStatus = TxLoRaB;
        } else {
          goto TxFin;
        }
      }
      break;

    case TxLoRaB:
      if (LORABBuffer.len - tx_buffer_position > lora_size) {
        transmitLoRa(&loraBroadcast, &LORABBuffer.buffer[tx_buffer_position], lora_size);
        tx_buffer_position += lora_size;
      } else {
        transmitLoRa(&loraBroadcast, &LORABBuffer.buffer[tx_buffer_position], LORABBuffer.len - tx_buffer_position);
TxFin:
        if (LORABBuffer.len + LORA1Buffer.len + LORA2Buffer.len > 0) {
          LORABBuffer.len = 0;
          LORA1Buffer.len = 0;
          LORA2Buffer.len = 0;
          tx_buffer_position = 0;
          TxStatus = TxIdle;
        }
      }
      break;
  }
}


void asyncReleaseLoRaFirst()
{
  asyncReleaseLoRa(true);
}

crcResult handleLoRa()
{
  crcResult crcReturned = CRC_NULL;
  if (operationDone) // the interrupt was triggered
  {
    DBGFST("Interrupt triggered");
    DBGFST("TxFlag: " + String(transmitFlag));
    DBGFST("TxStatus: " + String(TxStatus));

    enableInterrupt = false;
    operationDone = false;
    if (transmitFlag) // the previous operation was transmission
    {
      DBGF("LoRa airtime: " + String(millis() - tx_start_time) + "ms");
      radio.startReceive(); // return to listen mode
      enableInterrupt = true;
      transmitFlag = false;
    }
    else // the previous operation was reception
    {
      crcReturned = getLoRa();
      if (!transmitFlag) // return to listen if no transmission was begun
      {
        radio.startReceive();
      }
      enableInterrupt = true;
    }
  }
  return crcReturned;
}

void sendTimeLoRa() {

  DBGFST("Sending time via LoRa");
  SystemPacket spTimeLoRa = {.cmd = cmd_time, .param = now};
  DBG("Sending time to LoRa broadcast");
  transmitLoRa(&loraBroadcast, &spTimeLoRa, 1);
  // Do not send to LoRa peers if their address is 0x..00
  if(((LoRa1 & 0x00FF) != 0x0000) && (LoRa1 != timeMaster.tmAddress)) {
    DBG("Sending time to LoRa Neighbor 1");
    spTimeLoRa.param = now;
    // add LoRa neighbor 1
    transmitLoRa(&LoRa1, &spTimeLoRa, 1);
  }
  if(((LoRa2 & 0x00FF) != 0x0000) && (LoRa2 != timeMaster.tmAddress)) {
    DBG("Sending time to LoRa Neighbor 2");
    spTimeLoRa.param = now;
    // add LoRa neighbor 2
    transmitLoRa(&LoRa2, &spTimeLoRa, 1);
  }
}

// FDRS Sensor pings gateway and listens for a defined amount of time for a reply
// Blocking function for timeout amount of time (up to timeout time waiting for reply)(IE no callback)
// Returns the amount of time in ms that the ping takes or predefined value if ping fails within timeout
uint32_t pingFDRSLoRa(uint16_t *address, uint32_t timeout)
{
    SystemPacket sys_packet = {.cmd = cmd_ping, .param = 0};

    transmitLoRa(address, &sys_packet, 1);
    DBGF("LoRa ping sent to address: 0x" + String(*address, HEX));
    uint32_t ping_start = millis();
    pingFlagLoRa = false;
    // JL - future: figure out how to handle this asynchronously so we are not taking up processor time
    while ((millis() - ping_start) <= timeout)
    {
        handleLoRa();
        #ifdef ESP8266
            yield();
        #endif
        if (pingFlagLoRa)
        {   
            DBGF("LoRa Ping Returned: " + String(millis() - ping_start) + "ms.");
            pingFlagLoRa = false;
            return (millis() - ping_start);
        }
    }
    DBGF("No LoRa ping returned within " + String(timeout) + "ms.");
    return UINT32_MAX;
}

// Pings the LoRa time master periodically to calculate the time delay in the LoRa radio link
// Returns success or failure of the ping result
bool pingLoRaTimeMaster() {
  // ping the time master every 5 minutes
  if(millis() - lastTimeMasterPing > (5*60*1000 + random(0,2000))) {
    time_t pingTimeMs;
    pingTimeMs = pingFDRSLoRa(&timeMaster.tmAddress,4000);
    if(pingTimeMs != UINT32_MAX) {
      netTimeOffset = pingTimeMs/2/1000;
      adjTimeforNetDelay(netTimeOffset);
      lastTimeMasterPing = millis();
      return true;  
    }
    lastTimeMasterPing = millis();
  }
  return false;
}