/*
  new:
  Press cd1 or cd2 to change bluetooth volume.
  Cd change is resetting track number.


  By Thomas Landahl, 2017-04-25
  27-aug-2017 Modified by Vincent Gijsen, to incorporate text-sending to display


*/
//#define MELBUS_CLOCK_INTERRUPT 1
#define MELBUS_CLOCKBIT (byte)3 //Pin D2  - CLK
#define MELBUS_DATA (byte)2     //Pin D3  - Data
#define MELBUS_BUSY (byte)4     //Pin D4  - Busy

const byte playPin = 10;
const byte prevPin = 11;
const byte nextPin = 12;
const byte downPin = 13;  //volume down
const byte upPin = A0;    //volume up

//volatile variables used inside and outside of ISP
volatile byte melbus_ReceivedByte = 0;
volatile byte melbus_Bitposition = 7;
volatile bool byteIsRead = false;


byte byteToSend = 0;  //global to avoid unnecessary overhead
unsigned long Connected = 0;

//preset parameters
byte track = 0x01; //Display show HEX value, not DEC. (A-F not "allowed")
byte cd = 0x01; //1-10 is allowed (in HEX. 0A-0F and 1A-1F is not allowed)
byte powerup_ack = 0x00;

//Base adresses.
//const byte MD_ADDR = 0x70;  //internal
//const byte CD_ADDR = 0x80;  //internal
//const byte TV_ADDR = 0xA8;  //might be A9 during main init sequence
//const byte DAB_ADDR = 0xB8;
//const byte SAT_ADDR = 0xC0;
//const byte MDC_ADDR = 0xD8;
//const byte CDC_ADDR = 0xE8;
//const byte RESPONSE = 0x06; //add this to base adress when responding to HU
//const byte MASTER = 0x07;   //add this to base adress when requesting/beeing master

//change theese definitions if you wanna emulate another device.
//My HU-650 don't accept anything but a CD-C (so far).
/*
  #define RESPONSE_ID 0xEE  //ID while responding to init requests (which will use base_id)
  #define MASTER_ID 0xEF    //ID while requesting/beeing master
  #define BASE_ID 0xE8      //ID when getting commands from HU
  #define ALT_ID 0xE9       //Alternative ID when getting commands from HU

*/
#define RESPONSE_ID 0xC6  //ID while responding to init requests (which will use base_id)
#define MASTER_ID 0xC7    //ID while requesting/beeing master
#define BASE_ID 0xC0      //ID when getting commands from HU
#define ALT_ID 0xC1       //Alternative ID when getting commands from HU


byte textPayload[] = {  0x8F,  0xFF, 0xC7, 0xC7,
                        0xFC, 0xC6, 0x73, 0x01, 0x04, ' ', ' ',
                        'b', 'i', 't', 'c',
                        'h', 'e', 's', 0x00, 0x00,
                     };

const byte c1Init[] = { 0x10, 0x10, 0xc3, 0x01,
                        0x00, 0x81, 0x01, 0xff,
                        0x00
                      };

const byte c3init0[] = {0x10, 0x00, 0xfe, 0xff,
                        0xff, 0xdf, 0x3f, 0x29, 0x2c,
                        0xf0,  0xde, 0x2f, 0x61, 0xf4,
                        0xf4, 0xdf, 0xdd, 0xbf, 0xff,
                        0xbe, 0xff, 0xff, 0x03, 0x00,
                        0xe0, 0x05, 0x40, 0x0, 0x0, 0x0
                       } ;

const byte c3init1[] = {
  0x10, 0x01, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff , 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff
};




const byte c3init2[] = {
  0x10, 0x02, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff , 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff,
  0xff, 0xff
};

const byte case12Threebyte[] = {0x00, 0x01, 0x08};


//This list can't be too long. We only have so much time between the received bytes. (approx 500 us)
const byte commands[][8] = {
  {0xC1, 0x1D, 0x73, 0x01, 0x81,  0x10}, // 0
  {0xC3, 0x1F, 0x7C, 0x00}, // 1 initdata(1)
  {0x00, 0x1E, 0xEC }, // 2 now we are master and can stufs (like text) to the display!
  {0x07, 0x1A, 0xEE}, // 3 main init
  {0x00, 0x00, 0x1C, 0xED}, // 4 sec init
  {0xC1, 0x1B, 0x7F, 0x01, 0x08},  // 5 C1 1B 7F 1 8 FF FF FF FF FF FF FF FF FF

  {0xC3, 0x1F, 0x7C, 0x0}, // 6
  {0xC3, 0x1F, 0x7C, 0x1}, // 7 c3init1 FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
  {0xC3, 0x1F, 0x7C, 0x2}, // 8 c3init2 FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF
  {0xC0, 0x1C, 0x70, 0x00, 0x80, 0x01 }, //9 C0 1C 70 0 80 1 FF (we insert 0x90)
  {0xC0, 0x1C, 0x70, 0x02, 0x80, 0x01 }, //10 C0 1C 70 0 80 1 FF (we respond 0x10)
  {0xC0, 0x1B, 0x76}, // 11 follows 0, 92, ff, OR 1,3 ff, OR 2, 5 FF
  {0x00, 0x1C, 0xEC, 0x8F, 0xA8, 0x8C, 0xAF, 0xCC} //12 follows FF FF FF
  /*
    {0xC0, 0x1C, 0x70, 0x02, 0x80, 0x01, 0x10},
    {0xC0, 0x1B, 0x76, 0x00, 0x92, 0x10
    {0xC0, 0x1B, 0x76, 0x01, 0x03, 0x10
    {0xC0, 0x1B, 0x76, 0x02, 0x05, 0x10
    {0xC0, 0x1D, 0x76, 0x80, 0x00, 0x10, 0x80, 0x90
    {0xC1, 0x1B, 0x7f, 0x01, 0x08, 0x10, 0x10, 0xc3, c01, 0x00, 0x81, 0x01, 0xff, 0x00
    {0xC2, 0x1D, 0x73, 0x00, 0x20, 0x10, 0x00, 0x20, 0x00, 0x00, 0x20, 0x00, 0x20, 0x020

    {BASE_ID, 0x1E, 0xEF},             //0, Cartridge info request. Respond with 6 bytes (confirmed)
    /
    {ALT_ID, 0x1B, 0xE0, 0x01, 0x08}, //1, track info req. resp 9 bytes
    {BASE_ID, 0x1B, 0x2D, 0x40, 0x01}, //2, next track.
    {BASE_ID, 0x1B, 0x2D, 0x00, 0x01}, //3, prev track
    {BASE_ID, 0x1A, 0x50},       //4, next cd, not verified (what buttons on HU trigger this command?)
    {BASE_ID, 0x1A, 0x50},       //5, prev cd, not verified
    {BASE_ID, 0x19, 0x2F},            //6, power up. resp ack (0x00), 0x19 could be 0x49??
    {BASE_ID, 0x19, 0x22},            //7, power down. ack (0x00), not verified
    {BASE_ID, 0x19, 0x29},            //8, FFW. ack, not verified
    {BASE_ID, 0x19, 0x26},            //9, FRW. ack, not verified
    {BASE_ID, 0x19, 0x2E},            //10 scan mode. ack, cmd verified!
    {BASE_ID, 0x19, 0x52},            //11 random mode. ack, not verified
    {0x07, 0x1A, 0xEE},               //12 main init seq. wait for BASE_ID and respond with RESPONSE_ID.
    {0x00, 0x00, 0x1C, 0xED},         //13 secondary init req. wait for BASE_ID and respond with RESPONSE_ID.
    {0x00, 0x1C, 0xEC}                //14 master req broadcast. wait for MASTER_ID and respond with MASTER_ID.
  */
};
//keep track of the length of each command. (The two dimensional array above have fixed width (padded with 0x00))
const byte listLen = 13;
byte cmdLen[listLen] = {
  6,
  4 ,
  3,
  3,
  4,
  5,
  4,
  4 ,
  4,
  6,
  6,
  3,
  8,/*, 5, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3 */
};

//arrays to send to HU when requested

//TRACK INFO
/*According to internet resources, HEX 10 88 01 01 80 03 00 02 22 should be a valid answer.
  another source: 0x00, 0x02, 0x00, 0x01, 0x80, 0x01, 0xff, 0x60, 0x60
  All comments in HEX

  0.  10, 14, 20, 24, 30, 34, 80, 84, 90, 94 => HU is OK, else error
  1.  Status message
    01 = able to select cd by pushing HU-buttons but it resets to 01 after a couple of secs
    00, 10, 20, 40 = HU display: cd load cartridge (decimal numbers here??)
    22 = HU asking for status rapidly
    11 = HU display: CD cartridge empty
    44 = HU display: CD number is blinking
    81 = HU display: cd error

  2.  8 = HU display: random
  3.  HU display: CD number (1-10). 0x10 shows "CD  ". 0x0A (DEC 10) does not work?
        HU displays each nibble, and only values < A is valid.
  4.  Unknown meaning.
  5.  TRACK number
  6.  Hours     not displayed on HU-650
  7.  Minutes   not displayed on HU-650
  8.  Status Power?

*/
byte trackInfo[] = {0x00, 0x02, 0x00, cd, 0x80, track, 0xC7, 0x0A, 0x02}; //9 bytes
byte startByte = 0x08; //on powerup - change trackInfo[1] & [8] to this
byte stopByte = 0x02; //same on powerdown

//CARTRIDGE INFO
//According to internet resources, HEX 00 08 01 4A 0C CC is a valid answer.
//another source: 0x00, 0x0f, 0xff, 0x4a, 0xfc, 0xff
byte cartridgeInfo[] = {0x00, 0xFC, 0xFF, 0x4A, 0xFC, 0xFF};              //6 bytes

//MASTER MODE INFO
byte masterInfo[] = {0xF8, 0x85, 0xE2, 0x80, 0x03, 0x00, 0x02, 0x02};     //8 bytes



void setup() {
  //Disable timer0 interrupt. It's is only bogging down the system. We need speed!
  TIMSK0 &= ~_BV(TOIE0);

  //All lines are idle HIGH
  pinMode(MELBUS_DATA, INPUT_PULLUP);
  pinMode(MELBUS_CLOCKBIT, INPUT_PULLUP);
  pinMode(MELBUS_BUSY, INPUT_PULLUP);
  pinMode(nextPin, OUTPUT);
  pinMode(prevPin, OUTPUT);
  pinMode(playPin, OUTPUT);
  pinMode(upPin, OUTPUT);
  pinMode(downPin, OUTPUT);

  digitalWrite(nextPin, LOW);
  digitalWrite(prevPin, LOW);
  digitalWrite(playPin, LOW);
  digitalWrite(upPin, LOW);
  digitalWrite(downPin, LOW);

  //LED indicates connected status.
  //pinMode(13, OUTPUT);
  //digitalWrite(13, LOW);
  Serial.begin(230400);
  Serial.print("starting");
  Serial.println(sizeof(textPayload), DEC);


  //Activate interrupt on clock pin
  attachInterrupt(digitalPinToInterrupt(MELBUS_CLOCKBIT), MELBUS_CLOCK_INTERRUPT, RISING);

  //Initiate serial communication to debug via serial-usb (arduino)
  //For debugging purpose. Better off without it when things work.
  //Serial printing take a lot of time!!
  //Call function that tells HU that we want to register a new device
  melbusInitReq();
}

//Main loop
void loop() {
  static byte byteCounter = 0;  //keep track of how many bytes is sent in current command
  static byte lastByte = 0;     //used to copy volatile byte to register variable. See below
  static byte matching[listLen];     //Keep track of every matching byte in the commands array
  //static byte msgCount = 0;          //inc every time busy line goes idle.
  //static byte masterCount = 0;
  byte melbus_log[99];  //main init sequence is 61 bytes long...
  bool flag = false;
  bool text = false;
  bool BUSY = PIND & (1 << MELBUS_BUSY);
  int count = 0;

  Connected++;
  //check BUSY line active (active low)
  while (!BUSY) {
    //Transmission handling here...
    //debug
    //PORTB |= 1 << 5;
    if (byteIsRead) {
      //debug
      //PORTB &= ~(1 << 5);
      byteIsRead = false;
      lastByte = melbus_ReceivedByte; //copy volatile byte to register variable
      //Well, since HU is talking to us we might call it a conversation.
      Connected = 0;
      melbus_log[byteCounter] = lastByte;
      //Loop though every command in the array and check for matches. (No, don't go looking for matches now)
      for (byte cmd = 0; cmd < listLen; cmd++) {
        //check if this byte is matching
        if (lastByte == commands[cmd][byteCounter]) {
          matching[cmd]++;
          //check if a complete command is received, and take appropriate action
          if (matching[cmd] == cmdLen[cmd]) {
            switch (cmd) {
              //0, track info req. resp 9 bytes
              case 0:
                //send init data
                Serial.println("->0 ");
                //SendTrackInfo();
                //Serial.println("  trk info sent");
                break;

              case 1:
                SendInitData();
                Serial.println("->1 ");
                break;
              case 2:
                //we are writing as master to HU
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;
                    //should we send text?
                    if (text) {
                      //we fill in the blank with our own ID, and than start broadcasting payload
                      //byteToSend = MASTER_ID;
                      //SendByteToMelbus();
                      //do stuff here to send message to HU, like
                      masterSend();
                      text = false;
                    }
                    Serial.println("->2");

                    break;
                  }
                }

                break;
              case 3:
                //main init
                //wait for base_id and respond with response_id
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;
                    //debug get whole message
                    //byteCounter++;
                    //melbus_log[byteCounter] = melbus_ReceivedByte;
                    //end debug
                    if (melbus_ReceivedByte == BASE_ID) {
                      byteToSend = RESPONSE_ID;
                      SendByteToMelbus();
                      break;
                    }
                  }
                }
                Serial.println("->3");

                break;
              //secondary init
              case 4:
                //wait for base_id and respond response_id
                //digitalWrite(13, HIGH);
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;
                    //debug get whole message
                    //byteCounter++;
                    //melbus_log[byteCounter] = melbus_ReceivedByte;
                    //end debug
                    if (melbus_ReceivedByte == BASE_ID) {
                      byteToSend = RESPONSE_ID;
                      SendByteToMelbus();
                      break;
                    }
                  }
                }
                Serial.println("->4 ");

                break;

              case 5:
                sendC1Init();
                Serial.println("->5 ");

                break;

              case 6:
                SendC3init0();
                Serial.println("->6 ");
                break;

              case 7:
                SendC3init1();
                Serial.println("->7 ");
                break;

              case 8:
                SendC3init2();
                Serial.println("->8 ");
                break;
              //expect same resonse:?
              case 9:
                // {0xC0, 0x1C, 0x70, 0x00, 0x80, 0x01 }, //9 C0 1C 70 0 80 1 FF (we insert 0x90)
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;

                    noInterrupts();
                    byteToSend = 0x90;
                    SendByteToMelbus();
                    interrupts();
                    break;
                    Serial.println("->9 ");
                  }
                }
              case 10:
                // {0xC0, 0x1C, 0x70, 0x02, 0x80, 0x01 } we respond 0x10;
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;
                    noInterrupts();
                    byteToSend = 0x10;
                    SendByteToMelbus();
                    interrupts();
                    break;
                    Serial.println("->10 ");
                  }
                }
                break;

              case 11:
                // we read 3 different tuple bytes (0x00 92), (01,3) and (02,5), response is always 0x10;
                // int nByte = 0x00;
                while (!(PIND & (1 << MELBUS_BUSY))) {
                  if (byteIsRead) {
                    byteIsRead = false;
                    count++;
                    if (count == 1) {
                      // nByte = melbus_ReceivedByte;
                    }
                  }
                  if (count == 2) {
                    noInterrupts();
                    byteToSend = 0x10;
                    SendByteToMelbus();
                    interrupts();
                  }
                  break;
                  Serial.println("->10 ");
                }
                break;

              case 12:
                //  {0x00, 0x1C, 0xEC, 0x8F, 0xA8, 0x8C, 0xAF, 0xCC} //12 follows FF FF FF
                noInterrupts();
                for (int x = 0; x < sizeof(case12Threebyte); x++) {
                  byteToSend = case12Threebyte[x];
                  SendByteToMelbus();
                }
                interrupts();
                break;
                /*
                  track++;
                  fixTrack();
                  trackInfo[5] = track;
                  nextTrack();
                  break;
                  //3, prev track
                  case 3:
                  track--;
                  fixTrack();
                  trackInfo[5] = track;
                  prevTrack();
                  break;
                  //4, next cd
                  case 4:
                  //wait for next byte to get CD number
                  while (!(PIND & (1 << MELBUS_BUSY))) {
                    if (byteIsRead) {
                      byteIsRead = false;
                      switch (melbus_ReceivedByte) {
                        case 0x81:
                          cd = 1;
                          voice(); //internal to BT-module, not BT source
                          track = 1;
                          break;
                        case 0x82:
                          cd = 2;
                          volumeUp(); //internal to BT-module, not BT source
                          hangup();
                          track = 1;
                          break;
                        case 0x83:
                          cd = 3;
                          volumeDown();
                          track = 1;
                          break;
                        case 0x84:
                          cd = 4;
                          track = 1;
                          break;
                        case 0x85:
                          cd = 5;
                          track = 1;
                          break;
                        case 0x86:
                          cd = 6;
                          track = 1;
                          //here we mark for text-sending
                          text = 1;


                          break;
                        case 0x41:
                          cd++;
                          track = 1;
                          break;
                        case 0x01:
                          cd--;
                          track = 1;
                          break;
                        default:
                          track = 1;
                          break;
                      }
                    }
                  }
                  trackInfo[3] = cd;
                  trackInfo[5] = track;
                  break;
                  //5, not used
                  case 5:
                  break;
                  //6, power up. resp ack (0x00), not verified
                  case 6:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  trackInfo[1] = startByte;
                  trackInfo[8] = startByte;
                  break;
                  //7, power down. ack (0x00), not verified
                  case 7:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  trackInfo[1] = stopByte;
                  trackInfo[8] = stopByte;
                  melbusInitReq();
                  break;
                  //8, FFW. ack, not verified
                  case 8:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  break;
                  //9, FRW. ack, not verified
                  case 9:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  break;
                  //10 scan mode.
                  //Used as a PLAY button here
                  case 10:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  play();
                  //trackInfo[0]++; //debug
                  break;
                  //11 random mode.
                  //Used as a PLAY button here
                  case 11:
                  byteToSend = 0x00;
                  SendByteToMelbus();
                  play();
                  break;
                  //12 main init seq. wait for BASE_ID and respond with RESPONSE_ID.
                  case 12:
                  //wait for base_id and respond with response_id
                  while (!(PIND & (1 << MELBUS_BUSY))) {
                    if (byteIsRead) {
                      byteIsRead = false;
                      //debug get whole message
                      //byteCounter++;
                      //melbus_log[byteCounter] = melbus_ReceivedByte;
                      //end debug
                      if (melbus_ReceivedByte == BASE_ID) {
                        byteToSend = RESPONSE_ID;
                        SendByteToMelbus();
                        break;
                      }
                    }
                  }
                  break;
                  //13 secondary init req. wait for BASE_ID and respond with RESPONSE_ID.
                  case 13:
                  //wait for base_id and respond response_id
                  //digitalWrite(13, HIGH);
                  while (!(PIND & (1 << MELBUS_BUSY))) {
                    if (byteIsRead) {
                      byteIsRead = false;
                      //debug get whole message
                      //byteCounter++;
                      //melbus_log[byteCounter] = melbus_ReceivedByte;
                      //end debug
                      if (melbus_ReceivedByte == BASE_ID) {
                        byteToSend = RESPONSE_ID;
                        SendByteToMelbus();
                        break;
                      }
                    }
                  }
                  break;
                  //14 master req broadcast. wait for MASTER_ID and respond with MASTER_ID. (not used in this sketch)
                  case 14:
                  while (!(PIND & (1 << MELBUS_BUSY))) {
                    if (byteIsRead) {
                      byteIsRead = false;
                      if (melbus_ReceivedByte == MASTER_ID) {
                        byteToSend = MASTER_ID;
                        SendByteToMelbus();
                        //do stuff here to send message to HU, like
                        masterSend();
                        break;
                      }
                    }
                  }
                  break;
                */
                //              //15
                //              case 15:
                //                byteToSend = 0x00;
                //                SendByteToMelbus();
                //                break;
                //              //16
                //              case 16:
                //                byteToSend = 0x00;
                //                SendByteToMelbus();
                //                break;
                //              //17
                //              case 17:
                //                byteToSend = 0x00;
                //                SendByteToMelbus();
                //                break;
            }
            break;    //bail for loop. (Not meaningful to search more commands if one is already found)
          } //end if command found
        } //end if lastbyte matches
      }  //end for cmd loop
      byteCounter++;
    }  //end if byteisread
    //Update status of BUSY line, so we don't end up in an infinite while-loop.
    BUSY = PIND & (1 << MELBUS_BUSY);
    if (BUSY) {
      flag = true; //used to execute some code only once between transmissions
    }
  }

  //Do other stuff here if you want. MELBUS lines are free now. BUSY = IDLE (HIGH)
  //Don't take too much time though, since BUSY might go active anytime, and then we'd better be ready to receive.
  //Printing transmission log (incoming, before responses)
  if (flag) {
    for (byte b = 0; b < byteCounter; b++) {
      Serial.print(melbus_log[b], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }

  //Reset stuff
  byteCounter = 0;
  melbus_Bitposition = 7;
  for (byte i = 0; i < listLen; i++) {
    matching[i] = 0;
  }
  if (Connected > 2000000) {
    melbusInitReq();
    //Connected = 0;
  }

  if (Connected > 5000000) {
    Connected = 0;
    text = true;
  }

  //Incoming serial data is supposed to look like this:
  //index, databyte: "3, 5"
  //No error checking here since we're just hacking
  //  if (Serial.available() > 0) {
  //    int index = Serial.parseInt();
  //    trackInfo[index] = (byte) Serial.parseInt();
  //    Serial.readStringUntil('\n');
  //    for (byte b = 0; b < 9; b++) {
  //      Serial.print(trackInfo[b], HEX);
  //      Serial.print("-");
  //    }
  //    Serial.println();
  //  }

  // I haven't seen any advantages from sending messages to HU.
  //Therefore this section is disabled.
  //  if (flag) {
  //    if(some timed interval etc)
  //    reqMaster();
  //  }

  if (text || Serial.available() > 0) {
    Serial.read();
    Serial.println("\ns f");
    //next run, we want to send text!
    text = true;
    reqMaster();

  }

  flag = false; //don't print during next loop. Wait for new message to arrive first.
}

//Notify HU that we want to trigger the first initiate procedure to add a new device (CD-CHGR) by pulling BUSY line low for 1s
void melbusInitReq() {
  //Serial.println("conn");
  //Disable interrupt on INT0 quicker than: detachInterrupt(MELBUS_CLOCKBIT_INT);
  EIMSK &= ~(1 << INT1);

  // Wait until Busy-line goes high (not busy) before we pull BUSY low to request init
  while (digitalRead(MELBUS_BUSY) == LOW) {}
  delayMicroseconds(20);

  pinMode(MELBUS_BUSY, OUTPUT);
  digitalWrite(MELBUS_BUSY, LOW);
  //timer0 is off so we have to do a trick here
  for (unsigned int i = 0; i < 12000; i++) delayMicroseconds(100);

  digitalWrite(MELBUS_BUSY, HIGH);
  pinMode(MELBUS_BUSY, INPUT_PULLUP);
  //Enable interrupt on INT0, quicker than: attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, RISING);
  EIMSK |= (1 << INT1);
}


//This is a function that sends a byte to the HU - (not using interrupts)
//SET byteToSend variable before calling this!!
void SendByteToMelbus() {
  //Disable interrupt on INT0 quicker than: detachInterrupt(MELBUS_CLOCKBIT_INT);
  EIMSK &= ~(1 << INT1);

  //Convert datapin to output
  //pinMode(MELBUS_DATA, OUTPUT); //To slow, use DDRD instead:
  DDRD |= (1 << MELBUS_DATA);

  //For each bit in the byte
  for (int i = 7; i >= 0; i--)
  {
    while (PIND & (1 << MELBUS_CLOCKBIT)) {} //wait for low clock
    //If bit [i] is "1" - make datapin high
    if (byteToSend & (1 << i)) {
      PORTD |= (1 << MELBUS_DATA);
    }
    //if bit [i] is "0" - make datapin low
    else {
      PORTD &= ~(1 << MELBUS_DATA);
    }
    while (!(PIND & (1 << MELBUS_CLOCKBIT))) {}  //wait for high clock
  }

  //Reset datapin to high and return it to an input
  //pinMode(MELBUS_DATA, INPUT_PULLUP);
  PORTD |= 1 << MELBUS_DATA;
  DDRD &= ~(1 << MELBUS_DATA);

  //Enable interrupt on INT0, quicker than: attachInterrupt(MELBUS_CLOCKBIT_INT, MELBUS_CLOCK_INTERRUPT, RISING);
  EIMSK |= (1 << INT1);
}

//Global external interrupt that triggers when clock pin goes high after it has been low for a short time => time to read datapin
void MELBUS_CLOCK_INTERRUPT() {
  //Read status of Datapin and set status of current bit in recv_byte
  //if (digitalRead(MELBUS_DATA) == HIGH) {
  if ((PIND & (1 << MELBUS_DATA))) {
    melbus_ReceivedByte |= (1 << melbus_Bitposition); //set bit nr [melbus_Bitposition] to "1"
  }
  else {
    melbus_ReceivedByte &= ~(1 << melbus_Bitposition); //set bit nr [melbus_Bitposition] to "0"
  }

  //if all the bits in the byte are read:
  if (melbus_Bitposition == 0) {
    //set bool to true to evaluate the bytes in main loop
    byteIsRead = true;

    //Reset bitcount to first bit in byte
    melbus_Bitposition = 7;
  }
  else {
    //set bitnumber to address of next bit in byte
    melbus_Bitposition--;
  }
}

const byte initdata[] = {0x10, 0x0, 0xFE, 0xff,
                         0xff, 0xdf, 0x3f, 0x29,
                         0x2c, 0xf0, 0xde, 0x2f,
                         0x61, 0xf4, 0xf4, 0xdf,
                         0xdd, 0xbf, 0xff, 0xbe,
                         0xff, 0xff, 0x03, 0x00,
                         0xe0 , 0x05, 0x40, 0x00,
                         0x00, 0x00
                        };
void SendInitData() {
  noInterrupts();
  for (byte i = 0; i < sizeof(initdata); i++) {
    byteToSend = initdata[i];
    SendByteToMelbus();
  }
  interrupts();

}

void sendC1Init() {
  noInterrupts();
  for (byte i = 0; i < sizeof(c1Init); i++) {
    byteToSend = c1Init[i];
    SendByteToMelbus();
  }
  interrupts();
  Serial.println("c1i");

}

void SendC3init0() {
  noInterrupts();
  for (byte i = 0; i < sizeof(c3init0); i++) {
    byteToSend = c3init0[i];
    SendByteToMelbus();
  }
  interrupts();
}

void SendC3init1() {
  noInterrupts();
  for (byte i = 0; i < sizeof(c3init1); i++) {
    byteToSend = c3init1[i];
    SendByteToMelbus();
  }
  interrupts();
}


void SendC3init2() {
  noInterrupts();
  for (byte i = 0; i < sizeof(c3init2); i++) {
    byteToSend = c3init2[i];
    SendByteToMelbus();
  }
  interrupts();
}



void SendText() {
  noInterrupts();
  for (int i = 0; i < sizeof(textPayload); i++) {
    byteToSend = textPayload[i];
    SendByteToMelbus();
  }
  interrupts();
}

void SendTrackInfo() {
  noInterrupts();
  for (byte i = 0; i < 9; i++) {
    byteToSend = trackInfo[i];
    SendByteToMelbus();
  }
  interrupts();
}

void SendCartridgeInfo() {
  noInterrupts();
  for (byte i = 0; i < 6; i++) {
    byteToSend = cartridgeInfo[i];
    SendByteToMelbus();
  }
  interrupts();
}

void reqMaster() {
  DDRD |= (1 << MELBUS_DATA); //output
  PORTD &= ~(1 << MELBUS_DATA);//low
  delayMicroseconds(700);
  delayMicroseconds(700);
  delayMicroseconds(800);
  PORTD |= (1 << MELBUS_DATA);//high
  DDRD &= ~(1 << MELBUS_DATA); //back to input
}

void masterSend() {
  noInterrupts();
  for (byte i = 0; i < 8; i++) {
    byteToSend = masterInfo[i];
    SendByteToMelbus();
  }
  interrupts();
}

void fixTrack() {
  //cut out A-F in each nibble, and skip "00"
  byte hn = track >> 4;
  byte ln = track & 0xF;
  if (ln == 0xA) {
    ln = 0;
    hn += 1;
  }
  if (ln == 0xF) {
    ln = 9;
  }
  if (hn == 0xA) {
    hn = 0;
    ln = 1;
  }
  if ((hn == 0) && (ln == 0)) {
    ln = 0x9;
    hn = 0x9;
  }
  track = (hn << 4) + ln;
}

#define SHORT_DELAY 160
//Simulate button presses on the BT module. 200 ms works good. Less is not more in this case...
void nextTrack() {
  digitalWrite(nextPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(nextPin, LOW);
}

void prevTrack() {
  digitalWrite(prevPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(prevPin, LOW);
}

void play() {
  digitalWrite(playPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(playPin, LOW);
}

void volumeDown() {
  digitalWrite(downPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(downPin, LOW);

}

void volumeUp() {
  digitalWrite(upPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(upPin, LOW);

}

void voice() {
  digitalWrite(downPin, HIGH);
  for (byte i = 0; i < SHORT_DELAY; i++)
    delayMicroseconds(1000);
  digitalWrite(downPin, LOW);

}


void hangup() {
  digitalWrite(playPin, HIGH);
  for (byte i = 0; i < 1050; i++)
    delayMicroseconds(1000);
  digitalWrite(playPin, LOW);

}

//Happy listening, hacker!

