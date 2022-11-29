#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <PicoEspTime.h>
#include <lwip/apps/sntp.h>
#include <EEPROM.h>
#include <Timezone.h>

void startCaptivePortal();
void handleCredentials();
void handleCaptive();
void startNtpPoll();
void stopCaptivePortal();
void cancelNtpPoll();
void begin_ntp(IPAddress s1, IPAddress s2, int timeout = 3600);
void PrintTime();
void writeStringToEEPROM(int addrOffset, const String &strToWrite);
String readStringFromEEPROM(int addrOffset);
void saveDataEEPROM();
void readDataEEPROM();
void resetData();
void readRules(Timezone& tz, int address);
void writeRules(Timezone tz, int address);

//i2c handlers
void i2c_receive(int numBytesReceived);
void i2c_request();

const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;
const int I2C_ADDRESS = 40;

const String ACCESS_POINT_NAME = "ClockClock";
const String ACCESS_POINT_PASSWORD = "vierundzwanzig";

const byte DNS_PORT = 53;
IPAddress apIP(172, 217, 28, 1);
DNSServer dnsServer;
WebServer webServer(80);

bool start_ap_flag = false;
bool stop_ap_flag = false;
bool ap_enabled = false;

NTPClass ntp_service;
bool start_poll_flag = false;
bool polling_ntp = false;
bool cancel_ntp_poll_flag = false;
bool poll_successfull = false;
time_t poll_starttime = 0;
uint16_t poll_timeout = 60;
uint16_t ntp_time_validity = 60;
long expiry_time;

bool reset_data_flag = false;

PicoEspTime rtc;

#pragma region settings

const int ADDRESS_SETTINGS = 1; //32 bytes

struct settings {
  uint8_t ssidLength; //1 byte
  uint8_t passLength; //1 byte
  char ssid[33] = {}; //33 bytes
  char pass[33] = {}; //33 bytes
  bool isProtected; //1 byte
  bool useGmtOffset; //1 byte
  int8_t gmtOffset; //1 byte
  uint8_t timezoneIdx; //1 byte

  settings(String ssidStr, String passStr, bool isPr, bool uGO, int8_t gO, uint8_t tzIDX)
  {
    ssidLength = ssidStr.length();
    passLength = passStr.length();
    
    setSSIDString(ssidStr);
    setPassString(passStr);

    isProtected = isPr;
    useGmtOffset = uGO;
    gmtOffset = gO;
    timezoneIdx = tzIDX;
  }

  void setSSIDString(String ssidStr){
    ssidLength = ssidStr.length();

    for (int i = 0; i < ssidLength; i++) {
      ssid[i] = ssidStr[i];
    }
  }

  void setPassString(String passStr){
    passLength = passStr.length();

    for (int i = 0; i < passLength; i++) {
      pass[i] = passStr[i];
    }
  }

  String getSSIDString(){
    char ssid_str[ssidLength + 1];

    for (int i = 0; i < ssidLength; i++) {
      ssid_str[i] = ssid[i];
    }

    ssid_str[ssidLength] = '\0';

    return String(ssid_str);
  }

  String getPassString(){
    char pass_str[passLength + 1];

    for (int i = 0; i < passLength; i++) {
      pass_str[i] = pass[i];
    }

    pass_str[passLength] = '\0';

    return String(pass_str);
  }

  String getPrintableString(){
    String ssid = getSSIDString();
    String pass = getPassString();
    return "SSID:" + ssid + " Pass:" + pass + " isProtected:" + isProtected + " useGmtOffset:" + useGmtOffset + " gmtOffset:" + gmtOffset + " timezoneIdx:" + timezoneIdx;
  }
};

settings DEFAULT_SETTINGS = settings("Wifi", "12345678", false, false, 0, 0);
settings current_settings = DEFAULT_SETTINGS;

#pragma endregion

#pragma region timezone data

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);

// United Kingdom (London, Belfast)
TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};        // British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};         // Standard Time
Timezone UK(BST, GMT);

// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);

// US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};
Timezone usCT(usCDT, usCST);

// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};
Timezone usMT(usMDT, usMST);

// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};
Timezone usPT(usPDT, usPST);

// Australia Eastern Time Zone (Sydney, Melbourne)
TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660};    // UTC + 11 hours
TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600};    // UTC + 10 hours
Timezone ausET(aEDT, aEST);


enum timezone_identifier {cet = 0, uk = 1, jaudling = 2, uset = 3, usct = 4, usmt = 5, uspt = 6, mellau = 8, auset = 7};

Timezone timezones[] = {CE, UK, CE, usET, usCT, usMT, usPT, CE, ausET};

char colour[9][18] = {"Central European", "United Kingdom", "Jaudling", "US Eastern", "US Central", "US Mountain", "US Pacific", "Mellau" , "Australia Eastern"};

int8_t DEFAULT_GMT_OFFSET = 0;
uint8_t DEFAULT_TZ_IDX = 0;
bool DEFAULT_USE_TZ = true;  //use timezone or gmt offset

int gmt_offset = DEFAULT_GMT_OFFSET;
int timezone_idx = DEFAULT_TZ_IDX;
int use_timezone = DEFAULT_USE_TZ;

#pragma endregion

#pragma region i2c command datastructs

enum cmd_identifier {enable_ap = 0, poll_ntp = 1, reset_data = 2};

struct cmd_enable_ap_data {
  bool enable; //1bytes # true enables the acces point for configuration, false disables it
};

struct cmd_poll_ntp_data {
  uint16_t ntp_timeout; //2bytes, timeout in s
  uint16_t ntp_time_validity; //2bytes, how long the retrieved time will be valid
};

cmd_enable_ap_data enable_ap_data;
cmd_poll_ntp_data poll_ntp_data;

#pragma endregion

#pragma region html

const String STYLE_HTML = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ClockClock</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
  body{color:white; background-color:#141414; font-family: Helvetica, Verdana, Arial, sans-serif}
  h1{text-align:center;}
  p{text-align:center;}
  div{margin: 5%; background-color:#242424; padding:10px; border-radius:8px;}
  br{display: block; margin: 10px 0; line-height:22px; content: " ";}
  label{margin-left: 4%; margin-top: 2%; font-size: 22px;display: block;}
  input[type="text"]{border-radius: 8px; border: 2px solid #0056a8; width:90%; padding:10px; display:block; margin-right:auto; margin-left:auto;}
  input[type="submit"]{font-size: 25px; background-color:#0056a8; color:white; padding 10px; border-radius:8px; height:50px; width:93%;display:block; margin-top:5%; margin-right:auto; margin-left:auto;}
  .disable_section {pointer-events: none;opacity: 0.4;}
  input[type="checkbox"]{width: 20px;height: 20px; vertical-align: middle;position: relative;bottom: 1px;}
  </style>
  </head>)rawliteral";

const String CAPTIVE_FORM_HTML = R"rawliteral(<body>
    <body>
  <div>
    <h1>ClockClock</h1>
    <p>Geben sie die folgenden Daten ein um Ihre Uhr mit einem 2.4GHz WLAN-Netzwerk für Internet-Zeitsynchronisierung zu verbinden:</p>
    <br>
    <br>
    <form action="/credentials" method="POST">
      <label>Wi-Fi Daten:</label>
      <br>
      <input type="text" name="wifissid" id="wifissid" placeholder="Wi-Fi SSID">
      <br>
      <input type="text" name="wifipass" id="wifipass" placeholder="Wi-Fi Passwort">
      <label style="font-size:16px;"><input type="checkbox" name="is_protected" id="is_protected" onclick="enablepassword()" checked/> Geschütztes Netzwerk?</label>
      <br>
      <input type="submit" value="Submit">
    </form><br>
  </div>
</body>
<script>
function enablepassword() {
  if (document.getElementById("is_protected").checked) {
    document.getElementById("wifipass").classList.remove('disable_section')
  } else {
    document.getElementById("wifipass").classList.add('disable_section')
  }
}
</script>
</html>)rawliteral";

const String CAPTIVE_SUCCESS_HTML = R"rawliteral(<body>
  <div>
    <h1>ClockClock</h1>
    <p>Daten Erfolgreich gepeichert, zum erneuten ändern den "Anpassen" Knopf drücken:</p>
    <p></p>
    <p>SSID: *<*SSID*>*</p>
    <p>Passwort: *<*PASS*>*</p>
    <p>Geschützt: *<*PROT*>*</p>
    <p></p>
    <form action="/" method="POST">
      <input type="submit" value="Startseite">
    </form><br>
  </div>
</body></html>)rawliteral";

const String CAPTIVE_ERROR_HTML = R"rawliteral(<body>
  <div>
    <h1>ClockClock</h1>
    <p>Beim speichern der Daten ist ein Fehler aufgetreten. (*<*Error*>*):</p>
    <p></p>
    <p></p>
    <form action="/" method="POST">
      <input type="submit" value="Erneut Versuchen">
    </form><br>
  </div>
</body></html>)rawliteral";

#pragma endregion

#pragma region setup and loop  

void setup() {
  //todo load ssid password and if protected from flash
  Serial.begin(9600);
  Serial.println("test");

  delay(4000);

  EEPROM.begin(256);
  readDataEEPROM();
  
  rtc.adjust(1, 0, 0, 2010, 1,1); //some random date

  //Initialize as i2c slave
  Wire.setSCL(I2C_SCL_PIN);
  Wire.setSDA(I2C_SDA_PIN);  
  Wire.setClock(100000); 
  Wire.begin(I2C_ADDRESS); 
  Wire.onReceive(i2c_receive);
  Wire.onRequest(i2c_request);
}

void loop() {
  delay(1);

  if(reset_data_flag){
    reset_data_flag = false;
    resetData();
  }
  
  if(cancel_ntp_poll_flag){
    cancel_ntp_poll_flag = false;
    start_poll_flag = false;
    cancelNtpPoll();
  }else if(start_poll_flag){
    if(!ap_enabled){
      startNtpPoll();
    }
    start_poll_flag = false;
  }

  if(stop_ap_flag){
    start_ap_flag = false;
    stop_ap_flag = false;
    stopCaptivePortal();
  }else if (start_ap_flag){
    start_ap_flag = false;
    startCaptivePortal();
  }

  if (ap_enabled){
    dnsServer.processNextRequest();
    webServer.handleClient();
  }
  else if (polling_ntp){
    //todo, handle timeouts, overflow save millis webpage
    if(WiFi.status() == WL_CONNECTED){
      if(!ntp_service.running()){
        ntp_service.begin("pool.ntp.org", "time.nist.gov");
        Serial.println("starting sntp service");
      }
      
      if(time(nullptr) > (poll_starttime + poll_timeout + 31536000)){ //if timesetting has occured, one year after 2010 or smth
        poll_successfull = true;
        Serial.println("Succesfully polled");
        rtc.read();
        PrintTime();
        expiry_time = time(nullptr) + ntp_time_validity;
      }
    }
    if(time(nullptr) > (poll_starttime + poll_timeout) || poll_successfull){ //also called after time is set succesfully
      Serial.println("ntp ended");
      cancelNtpPoll();
    }
  }

  if(poll_successfull && time(nullptr) > expiry_time){
    Serial.println("Invalidating ntp time since timeout has been reached");
    poll_successfull = false;
    if(polling_ntp){
      cancelNtpPoll();
    }
  }
}

void PrintTime()
{ 
  Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
} 

#pragma endregion

#pragma region i2c handler

void i2c_receive(int numBytesReceived) {
  uint8_t cmd_id = 0;
  Wire.readBytes((byte*) &cmd_id, 1);

  byte i2c_buffer[numBytesReceived - 1];

  if (cmd_id == enable_ap){
    Wire.readBytes((byte*) &enable_ap_data, numBytesReceived - 1);
    if (enable_ap_data.enable)
    {
      cancel_ntp_poll_flag = true;
      start_ap_flag = true;
    }else{
      stop_ap_flag = true;
    }
  }else if (cmd_id == poll_ntp){
    Wire.readBytes((byte*) &poll_ntp_data, numBytesReceived - 1);
    poll_timeout = poll_ntp_data.ntp_timeout;
    ntp_time_validity = poll_ntp_data.ntp_time_validity;
    start_poll_flag = true;
  }else if (cmd_id == reset_data){
    reset_data_flag = true;
  }
}

void i2c_request() {
  byte buffer[4];
  rtc.read();

  time_t t = 0;

  if (current_settings.useGmtOffset){
    TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, current_settings.gmtOffset * 60};     // UTC
    Timezone UTC(utcRule);
    t = UTC.toLocal(rtc.getEpoch());
  }else{
    t = timezones[current_settings.timezoneIdx].toLocal(rtc.getEpoch());
  }

  buffer[0] = (byte)poll_successfull;
  buffer[1] = (byte)hour(t);
  buffer[2] = (byte)minute(t);
  buffer[3] = (byte)second(t);

  Wire.write(buffer, 4);
}

#pragma endregion

#pragma region captive portal

void startCaptivePortal() {
  if(!ap_enabled){
    Serial.println("start ap");
    ap_enabled = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ACCESS_POINT_NAME, ACCESS_POINT_PASSWORD);

    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.start(DNS_PORT, "*", apIP);

    // replay to all requests with same HTML
    webServer.on("/credentials", HTTP_POST, handleCredentials);
    webServer.onNotFound(handleCaptive);
    webServer.begin();
  }
  else{
    Serial.println("ap already running");
  }
}

void stopCaptivePortal() {
  if(ap_enabled){
    Serial.println("stop ap");
    ap_enabled = false;
    WiFi.mode(WIFI_OFF);
    dnsServer.stop();
    webServer.stop();
  }
  else{
    Serial.println("ap already stopped");
  }
}

void handleCredentials(){
  String msg = CAPTIVE_SUCCESS_HTML;

  if (webServer.hasArg("wifissid") && webServer.hasArg("wifipass")){
    Serial.println(webServer.arg("wifissid"));
    Serial.println(webServer.arg("wifipass"));
    Serial.println(webServer.hasArg("is_protected"));

    if((webServer.hasArg("is_protected") && webServer.arg("wifipass").length() >= 8) || !webServer.hasArg("is_protected")){
      if(webServer.arg("wifissid").length() <= 32 && webServer.arg("wifipass").length() <= 32){
        current_settings.isProtected = webServer.hasArg("is_protected");
        current_settings.setSSIDString(webServer.arg("wifissid"));
        current_settings.setPassString(webServer.arg("wifipass"));
        
        if (webServer.hasArg("is_protected")){
          msg.replace("*<*PROT*>*", "Ja");
        }else{
          msg.replace("*<*PROT*>*", "Nein");
          String a = R"rawliteral(<p>Passwort: *<*PASS*>*</p>)rawliteral";
          msg.replace(a, "");
        }
        msg.replace("*<*SSID*>*", webServer.arg("wifissid"));
        msg.replace("*<*PASS*>*", webServer.arg("wifipass"));

        saveDataEEPROM();
      }
      else{
        msg = CAPTIVE_ERROR_HTML;
        msg.replace("*<*Error*>*", "SSID und Passwort sollten je weniger als 33 Zeichen haben");
      }
    }
    else{
      msg = CAPTIVE_ERROR_HTML;
      msg.replace("*<*Error*>*", "Passwort muss mehr als 7 Zeichen haben");
    }
  }
  else{
    msg = CAPTIVE_ERROR_HTML;
    msg.replace("*<*Error*>*", "Unbekannter Fehler");
  }

  webServer.send(200, "text/html", (STYLE_HTML + msg));
}

void handleCaptive(){
  webServer.send(200, "text/html", (STYLE_HTML + CAPTIVE_FORM_HTML));
}

#pragma endregion

#pragma region ntp polling

void startNtpPoll() {
  Serial.println("start ntp");
  ntp_service = NTPClass();
  poll_successfull = false;
  polling_ntp = true;
  rtc.adjust(1, 0, 0, 2010, 1,1);
  poll_starttime = time(nullptr);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("ClockClockNTPService");

  char name[current_settings.ssidLength];
  for (int i = 0; i < current_settings.ssidLength; i++) {
    name[i] = current_settings.ssid[i];
  }

  char pass[current_settings.passLength];
  for (int i = 0; i < current_settings.passLength; i++) {
    pass[i] = current_settings.pass[i];
  }

  if(current_settings.isProtected){
    WiFi.begin(name, pass);
  }else{
    WiFi.begin(name);
  }
}

void cancelNtpPoll() {
  Serial.println("stop ntp");
  polling_ntp = false;
  sntp_stop(); //stops the sntp service used by NTPClass
  if(!ap_enabled){
    WiFi.mode(WIFI_OFF);
  }
}

#pragma endregion

#pragma region eeprom

void writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

String readStringFromEEPROM(int addrOffset)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0'; // !!! NOTE !!! Remove the space between the slash "/" and "0" (I've added a space because otherwise there is a display bug)
  return String(data);
}

void readRules(Timezone& tz, int address)
{
  EEPROM.get(address, tz);
}

void writeRules(Timezone tz, int address)
{
  EEPROM.put(address, tz);
}

void saveDataEEPROM()
{
  Serial.println(current_settings.getPrintableString());
  EEPROM.put(ADDRESS_SETTINGS, current_settings);

  EEPROM.commit();
}

void readDataEEPROM()
{
  EEPROM.get(ADDRESS_SETTINGS, current_settings);
  Serial.println(current_settings.getPrintableString());
}

void resetData()
{
  current_settings = DEFAULT_SETTINGS;
  saveDataEEPROM();
}

#pragma endregion