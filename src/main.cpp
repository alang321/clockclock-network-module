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
void PrintTime();
void saveDataEEPROM();
void readDataEEPROM();
void resetData();

#define DEBUG false

//#define MAX_HOTSPOT_ON_TIME_M 30 //max time in seconds the hotspot will be on, in case the turn off command is missed by chance

#define MAX_NTP_TIMEOUT 1800 //max timeout in seconds
#define MAX_NTP_TIME_VALIDITY 3600 //max time validity in seconds

#define MAX_COMMAND_LENGTH 5 //max length of a command data in bytes, used for checksum buffer
#define REPLY_LENGTH 5 //length of the reply in bytes

//i2c handlers
void i2c_receive(int numBytesReceived);
void i2c_request();

bool verifyChecksum(byte (&buffer)[MAX_COMMAND_LENGTH + 1], uint8_t bufferLength);
uint8_t getChecksum(byte (&buffer)[REPLY_LENGTH]);

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
bool webServer_started = false;

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

enum connection_feedback {success = 0, fail = 1, not_yet_attempted = 2};
uint8_t ntp_feedback = not_yet_attempted;
uint8_t wifi_feedback = not_yet_attempted;

//idk how to name, keeps track during one ntp cycle, this value gets passed on to wifi_feedback on timeout, but NOT on cancel
uint8_t wifi_feedback_2 = not_yet_attempted; 

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


enum timezone_identifier {cet = 0, uk = 1, jaudling = 2, uset = 3, usct = 4, usmt = 5, uspt = 6, mellau = 7, auset = 8};

Timezone timezones[] = {CE, UK, CE, usET, usCT, usMT, usPT, CE, ausET};

char timezoneNames[9][18] = {"Central European", "United Kingdom", "Jaudling", "US Eastern", "US Central", "US Mountain", "US Pacific", "Mellau" , "Australia Eastern"};

const int NUM_TIMEZONES = 9;

#pragma endregion

#pragma region i2c command datastructs

enum cmd_identifier {enable_ap = 0, poll_ntp = 1, reset_data = 2};

#pragma pack(push, 1) // exact fit - no padding

struct cmd_enable_ap_data {
  uint8_t cmd_id;
  bool enable; //1bytes # true enables the acces point for configuration, false disables it
};

struct cmd_poll_ntp_data {
  uint8_t cmd_id;
  uint16_t ntp_timeout; //2bytes, timeout in s
  uint16_t ntp_time_validity; //2bytes, how long the retrieved time will be valid
};

#pragma pack(pop)

cmd_enable_ap_data enable_ap_data;
cmd_poll_ntp_data poll_ntp_data;

#pragma endregion

#pragma region html

const String STYLE_HTML = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>UhrUhr24</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
  body{color:white; background-color:#141414; font-family: Helvetica, Verdana, Arial, sans-serif}
  h1{text-align:center;}
  p{text-align:center;}
  div{margin: 5%; background-color:#242424; padding:10px; border-radius:14px;}
  br{display: block; margin: 10px 0; line-height:22px; content: " ";}
  .biglabel{font-size:20px; border-radius: 8px; width:90%; padding:10px; display:block; margin-right:auto; margin-left:auto;}
  .smalllabel{border-radius: 8px; width:90%; padding:10px; display:block; margin-right:auto; margin-left:auto;}
  .textbox{border-radius: 8px; border: 2px solid #0056a8; width:90%; padding:10px; display:block; margin-right:auto; margin-left:auto;}
  .numbox{border-radius: 8px; border: 2px solid #0056a8; width:15%; padding:10px; }
  input[type="submit"]{font-size: 23px; background-color:#0056a8; color:white; padding 10px; border-radius:8px; height:50px; width:93%;display:block; margin-top:5%; margin-right:auto; margin-left:auto;}
  .disable_section {pointer-events: none;opacity: 0.4;}
  .hor {margin:2%;}
  input[type="checkbox"]{width: 20px;height: 20px; vertical-align: middle;position: relative;bottom: 1px;}
  </style>
  </head>)rawliteral";

const String CAPTIVE_FORM_HTML = R"rawliteral(<body>
    <body>
  <div>
    <h1>UhrUhr24</h1>
    <br>
    <br>
    <form action="/credentials" method="POST">
      <label class="biglabel" >Wi-Fi Daten:</label>
      <br>
      <input class="textbox" type="text" name="wifissid" id="wifissid" placeholder="Wi-Fi SSID" value="*<*SSID*>*">
      <br>
      <input class="textbox" type="text" name="wifipass" id="wifipass" placeholder="Passwort unverändert">
      <label class="smalllabel"><input type="checkbox" name="is_protected" id="is_protected" onclick="enableFields()" *<*IS_PROT*>*/> Geschütztes Netzwerk</label>
      <br>
      <hr class="hor">
      <label class="biglabel">Zeitzone:</label>
      
      <select class="textbox" id="timezone" name="timezone">
          *<*TZ_LIST*>*
        </select>
      <label class="smalllabel"><input type="checkbox" name="gmt_offset_enabled" id="gmt_offset_enabled" onclick="enableFields()" *<*USE_GMT_OFFS*>*/> Stattdessen GMT-Abweichung verwenden</label>
      <label class="smalllabel" id="gmtOffsetLabel"> GMT-Abweichung: GMT <input class="numbox" type="number" id="gmtOffset" name="gmtOffset" min="-12" max="12" value="*<*GMT_OFFS*>*"></label>
      

      <input type="submit" value="Änderungen Speichern">
    </form><br>
  </div>
  <div>
      <p style="color:grey">Letzte Wifi-Verbindung erfolgreich: <span style="color:*<*WIFI_COL*>*">*<*WIFI*>*</span></p>
      <p style="color:grey">Letzte Internet-Zeitabfrage erfolgreich: <span style="color:*<*NTP_COL*>*">*<*NTP*>*</span></p>
  </div>
</body>
<script>
function enableFields() {
  if (document.getElementById("is_protected").checked) {
    document.getElementById("wifipass").classList.remove('disable_section')
  } else {
    document.getElementById("wifipass").classList.add('disable_section')
  }
  if (document.getElementById("gmt_offset_enabled").checked) {
    document.getElementById("gmtOffsetLabel").classList.remove('disable_section')
    document.getElementById("gmtOffset").classList.remove('disable_section')
    document.getElementById("timezone").classList.add('disable_section')
  } else {
    document.getElementById("gmtOffsetLabel").classList.add('disable_section')
    document.getElementById("gmtOffset").classList.add('disable_section')
    document.getElementById("timezone").classList.remove('disable_section')
  }
}

window.onload = enableFields;
</script>
</html>)rawliteral";

const String CAPTIVE_SUCCESS_HTML = R"rawliteral(<body>
  <div>
    <h1>UhrUhr24</h1>
    <p>Daten Erfolgreich gepeichert, zum erneuten ändern den "Anpassen" Knopf drücken:</p>
    <p></p>
    <p>SSID: *<*SSID*>*</p>
    <p>Passwort: *<*PASS*>*</p>
    <p>Geschützt: *<*PROT*>*</p>
    <p>Zeitzone: *<*TZ*>*</p>
    <p></p>
    <form action="/" method="POST">
      <input type="submit" value="Anpassen">
    </form><br>
  </div>
</body></html>)rawliteral";

const String CAPTIVE_ERROR_HTML = R"rawliteral(<body>
  <div>
    <h1>UhrUhr24</h1>
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
  if(DEBUG){
    Serial.begin(9600);
    Serial.println("test");
  }

  delay(4000);

  EEPROM.begin(256);
  readDataEEPROM();
  
  rtc.adjust(1, 0, 0, 2010, 1,1); //some random date

  //Initialize as i2c slave
  Wire.setSCL(I2C_SCL_PIN);
  Wire.setSDA(I2C_SDA_PIN);  
  Wire.setClock(25000); 
  Wire.begin(I2C_ADDRESS); 
  Wire.onReceive(i2c_receive);
  Wire.onRequest(i2c_request);
}

void loop() {
  delay(1);

  if(reset_data_flag){
    reset_data_flag = false;
    resetData();

    //if ap is enabled, restart it
    if(ap_enabled){
      stopCaptivePortal();
      startCaptivePortal();
    }

    //if ntp is polling, restart it
    if(polling_ntp){
      cancelNtpPoll();
      startNtpPoll();
    }
  }
  
  if(cancel_ntp_poll_flag){
    cancel_ntp_poll_flag = false;
    cancelNtpPoll();
  }

  if(start_poll_flag){
    stopCaptivePortal();
    startNtpPoll();
    start_poll_flag = false;
  }

  if(stop_ap_flag){
    stop_ap_flag = false;
    stopCaptivePortal();
  }
  
  if (start_ap_flag){
    if(polling_ntp){
      cancelNtpPoll();
    }
    start_ap_flag = false;
    startCaptivePortal();
  }

  if(webServer_started){
    webServer.handleClient();
  }

  if (ap_enabled){
    dnsServer.processNextRequest();
  }

  if (polling_ntp){
    //todo, handle timeouts, overflow save millis webpage
    if(WiFi.status() == WL_CONNECTED){
      wifi_feedback_2 = success;
      if(!ntp_service.running()){
        ntp_service.begin("pool.ntp.org", "time.nist.gov");
        if(DEBUG) Serial.println("starting sntp service");
      }
      
      if(time(nullptr) > (poll_starttime + poll_timeout + 31536000)){ //if timesetting has occured, one year after 2010 or smth
        ntp_feedback = success;
        poll_successfull = true;
        if(DEBUG) Serial.println(("Succesfully polled, time will be valid for (s)" + ntp_time_validity));
        rtc.read();
        PrintTime();
        expiry_time = time(nullptr) + ntp_time_validity;
      }
    }
    if(time(nullptr) > (poll_starttime + poll_timeout) || poll_successfull){ //also called after time is set succesfully
      if(!poll_successfull){
        ntp_feedback = fail;
      }
      wifi_feedback = wifi_feedback_2;

      if(DEBUG) Serial.println("ntp ended");
      cancelNtpPoll();
    }
  }

  if(poll_successfull && (time(nullptr) > expiry_time)){
    if(DEBUG) Serial.println("Invalidating ntp time since timeout has been reached");
    poll_successfull = false;
    if(polling_ntp){
      cancelNtpPoll();
    }
  }
}

void PrintTime()
{ 
  if(DEBUG) Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
} 

#pragma endregion

#pragma region i2c handler

void i2c_receive(int numBytesReceived) {
  //verify correct number of bytes received
  if(numBytesReceived >= 2 && numBytesReceived <= MAX_COMMAND_LENGTH + 1){
    //verify checksum
    byte buffer[MAX_COMMAND_LENGTH + 1];
    Wire.readBytes((byte*) &buffer, numBytesReceived);
    if(verifyChecksum(buffer, numBytesReceived)){
      //verify command id
      uint8_t cmd_id = 0;
      cmd_id = static_cast<uint8_t>(buffer[0]);

      if (cmd_id == enable_ap && numBytesReceived == sizeof(enable_ap_data) + 1){
        memcpy(&enable_ap_data, buffer, sizeof(enable_ap_data));
        if (enable_ap_data.enable)
        {
          start_ap_flag = true;
        }else{
          stop_ap_flag = true;
        }
      }else if (cmd_id == poll_ntp && numBytesReceived == sizeof(poll_ntp_data) + 1){
        memcpy(&poll_ntp_data, buffer, sizeof(poll_ntp_data));

        if(poll_ntp_data.ntp_timeout > MAX_NTP_TIMEOUT){
          poll_ntp_data.ntp_timeout = MAX_NTP_TIMEOUT;
        }
        if(poll_ntp_data.ntp_time_validity > MAX_NTP_TIME_VALIDITY){
          poll_ntp_data.ntp_time_validity = MAX_NTP_TIME_VALIDITY;
        }

        poll_timeout = poll_ntp_data.ntp_timeout;
        ntp_time_validity = poll_ntp_data.ntp_time_validity;
        cancel_ntp_poll_flag = true;
        start_poll_flag = true;
      }else if (cmd_id == reset_data && numBytesReceived == 2){
        reset_data_flag = true;
      }
    }

  }
  else{
    //clear the bytes form the buffer
    byte discard_buffer[numBytesReceived];
    Wire.readBytes((byte *)&discard_buffer, numBytesReceived);
    return;
  }
}

void i2c_request() {
  byte buffer[REPLY_LENGTH];
  rtc.read();

  time_t t = 0;

  if (current_settings.useGmtOffset){
    TimeChangeRule utcRule = {"UTC", Last, Sun, Mar, 1, current_settings.gmtOffset * 60};     // UTC
    Timezone UTC(utcRule);
    t = UTC.toLocal(rtc.getEpoch());
  }else{
    t = timezones[current_settings.timezoneIdx].toLocal(rtc.getEpoch());
  }

  uint8_t combined_bool = poll_successfull + (polling_ntp * 2); //LSB poll suiccesfull, next from the right is wether polling_ntp is active

  buffer[0] = (byte)combined_bool;
  buffer[1] = (byte)hour(t);
  buffer[2] = (byte)minute(t);
  buffer[3] = (byte)second(t);
  uint8_t checksum = getChecksum(buffer);
  buffer[4] = checksum;

  Wire.write(buffer, REPLY_LENGTH);
}

bool verifyChecksum(byte (&buffer)[MAX_COMMAND_LENGTH + 1], uint8_t bufferLength){
    uint8_t checksum = 0;
    for(int i = 0; i < bufferLength - 1; i++){
        checksum += buffer[i];
    }
    return checksum == buffer[bufferLength - 1];
}

uint8_t getChecksum(byte (&buffer)[REPLY_LENGTH]){
    uint8_t checksum = 0;
    for(int i = 0; i < REPLY_LENGTH - 1; i++){
        checksum += buffer[i];
    }
    return checksum;
}

#pragma endregion

#pragma region captive portal

void startCaptivePortal() {
  if(!ap_enabled){
    if(DEBUG) Serial.println("start ap");
    ap_enabled = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ACCESS_POINT_NAME, ACCESS_POINT_PASSWORD);

    dnsServer.start(DNS_PORT, "*", apIP);

    if(!webServer_started){
      // reply to requests
      webServer.on("/credentials", HTTP_POST, handleCredentials);
      webServer.onNotFound(handleCaptive);
      webServer.begin();
      webServer_started = true;
    }
  }
  else{
    if(DEBUG) Serial.println("ap already running");
  }
}

void stopCaptivePortal() {
  if(ap_enabled){
    if(DEBUG) Serial.println("stop ap");
    ap_enabled = false;
    WiFi.mode(WIFI_OFF);
    dnsServer.stop();
    //webServer.stop();
  }
  else{
    if(DEBUG) Serial.println("ap already stopped");
  }
}

void handleCredentials(){
  String msg = CAPTIVE_SUCCESS_HTML;

  if (webServer.hasArg("wifissid") && webServer.hasArg("wifipass") && webServer.hasArg("timezone") && webServer.hasArg("gmtOffset")){
    if(DEBUG){
      Serial.println(webServer.arg("wifissid"));
      Serial.println(webServer.arg("wifipass"));
      Serial.println(webServer.hasArg("is_protected"));
    }

    String wifipass;
    if(webServer.arg("wifipass") == ""){
      wifipass = current_settings.getPassString();
    }else{
      wifipass = webServer.arg("wifipass");
    }

    if((webServer.hasArg("is_protected") && wifipass.length() >= 8) || !webServer.hasArg("is_protected")){
      if(webServer.arg("wifissid").length() <= 32 && wifipass.length() <= 32){
        current_settings.isProtected = webServer.hasArg("is_protected");
        current_settings.setSSIDString(webServer.arg("wifissid"));
        current_settings.setPassString(wifipass);
        current_settings.timezoneIdx = max(0, min(webServer.arg("timezone").toInt(), NUM_TIMEZONES - 1));
        current_settings.useGmtOffset = webServer.hasArg("gmt_offset_enabled");
        current_settings.gmtOffset = max(-12, min(webServer.arg("gmtOffset").toInt(), 12));
        
        if (webServer.hasArg("is_protected")){
          msg.replace("*<*PROT*>*", "Ja");
        }else{
          msg.replace("*<*PROT*>*", "Nein");
          String a = R"rawliteral(<p>Passwort: *<*PASS*>*</p>)rawliteral";
          msg.replace(a, "");
        }
        msg.replace("*<*SSID*>*", webServer.arg("wifissid"));

        if(webServer.arg("wifipass") == ""){
          msg.replace("*<*PASS*>*", "unverändert");
        }else{
          msg.replace("*<*PASS*>*", webServer.arg("wifipass"));
        }

        if(current_settings.useGmtOffset){
          String symbol = "";
          if(current_settings.gmtOffset >= 0){
            symbol = "+";
          }
          msg.replace("*<*TZ*>*", ("GMT" + symbol + String(current_settings.gmtOffset)));
        }else{
          msg.replace("*<*TZ*>*", timezoneNames[current_settings.timezoneIdx]);
        }
        
        saveDataEEPROM();
      }
      else{
        msg = CAPTIVE_ERROR_HTML;
        msg.replace("*<*Error*>*", "SSID und Passwort müssen je weniger als 33 Zeichen haben");
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
  if(DEBUG) Serial.println("Serving Settings Page");
  String form = CAPTIVE_FORM_HTML;
  form.replace("*<*SSID*>*", current_settings.getSSIDString());
  if(current_settings.isProtected){
    form.replace("*<*IS_PROT*>*", "checked");
  }else{
    form.replace("*<*IS_PROT*>*", "");
  }

  String tmzListItems = "";
  for (int i = 0; i < NUM_TIMEZONES; i++){
    if(current_settings.timezoneIdx == i){
      tmzListItems += "<option value=\"" + String(i) + "\" selected>" + timezoneNames[i] + "</option>";
    }else{
      tmzListItems += "<option value=\"" + String(i) + "\">" + timezoneNames[i] + "</option>";
    }
  }
  form.replace("*<*TZ_LIST*>*", tmzListItems);
  
  if(current_settings.useGmtOffset){
    form.replace("*<*USE_GMT_OFFS*>*", "checked");
  }else{
    form.replace("*<*USE_GMT_OFFS*>*", "");
  }

  form.replace("*<*GMT_OFFS*>*", String(current_settings.gmtOffset));
  
  if(wifi_feedback == success){
    form.replace("*<*WIFI*>*", "Ja");
    form.replace("*<*WIFI_COL*>*", "green");
  }else if(wifi_feedback == fail){
    form.replace("*<*WIFI*>*", "Nein");
    form.replace("*<*WIFI_COL*>*", "red");
  }else{
    form.replace("*<*WIFI*>*", "Ungetestet");
    form.replace("*<*WIFI_COL*>*", "grey");
  }

  if(ntp_feedback == success){
    form.replace("*<*NTP*>*", "Ja");
    form.replace("*<*NTP_COL*>*", "green");
  }else if(ntp_feedback == fail){
    form.replace("*<*NTP*>*", "Nein");
    form.replace("*<*NTP_COL*>*", "red");
  }else{
    form.replace("*<*NTP*>*", "Ungetestet");
    form.replace("*<*NTP_COL*>*", "grey");
  }
  
  webServer.send(200, "text/html", (STYLE_HTML + form));
}

#pragma endregion

#pragma region ntp polling

void startNtpPoll() {
  if(DEBUG){
    Serial.println("");
    Serial.println("start ntp");
    Serial.println("polling timeout s: " + String(poll_timeout));
    Serial.println("ntp validity s: " + String(ntp_time_validity));
    Serial.println("");
  }
  wifi_feedback_2 = fail;
  ntp_service = NTPClass();
  poll_successfull = false;
  polling_ntp = true;
  rtc.adjust(1, 0, 0, 2010, 1,1);
  poll_starttime = time(nullptr);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("ClockClockNTPService");

  char name[current_settings.ssidLength + 1];
  for (int i = 0; i < current_settings.ssidLength; i++) {
    name[i] = current_settings.ssid[i];
  }
  name[current_settings.ssidLength] = '\0';

  char pass[current_settings.passLength + 1];
  for (int i = 0; i < current_settings.passLength; i++) {
    pass[i] = current_settings.pass[i];
  }
  pass[current_settings.passLength] = '\0';

  if(current_settings.isProtected){
    WiFi.begin(name, pass);
  }else{
    WiFi.begin(name);
  }
}

void cancelNtpPoll() {
  if (polling_ntp)
  {
    if(DEBUG) Serial.println("stop ntp");
    polling_ntp = false;
    sntp_stop(); //stops the sntp service used by NTPClass
    if(!ap_enabled){
      WiFi.mode(WIFI_OFF);
    }
  }
}

#pragma endregion

#pragma region eeprom

void saveDataEEPROM()
{
  if(DEBUG) Serial.println(current_settings.getPrintableString());
  EEPROM.put(ADDRESS_SETTINGS, current_settings);

  EEPROM.commit();
}

void readDataEEPROM()
{
  EEPROM.get(ADDRESS_SETTINGS, current_settings);
  if(DEBUG) Serial.println(current_settings.getPrintableString());
}

void resetData()
{
  current_settings = DEFAULT_SETTINGS;
  saveDataEEPROM();
}

#pragma endregion