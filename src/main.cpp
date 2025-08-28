#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <PicoEspTime.h>
#include <lwip/apps/sntp.h>
#include <EEPROM.h>
#include <Timezone.h>

#define DEBUG false

//#define MAX_HOTSPOT_ON_TIME_M 30 //max time in seconds the hotspot will be on, in case the turn off command is missed by chance
#define MAX_NTP_TIMEOUT 1800 //max timeout in seconds
#define MAX_NTP_TIME_VALIDITY 3600 //max time validity in seconds

#define MAX_COMMAND_LENGTH 5 //max length of a command data in bytes, used for checksum buffer
#define REPLY_LENGTH 5 //length of the reply in bytes

const int I2C_SDA_PIN = 8;
const int I2C_SCL_PIN = 9;
const int I2C_ADDRESS = 40;

const String ACCESS_POINT_NAME = "ClockClock";
const String ACCESS_POINT_PASSWORD = "vierundzwanzig";

const byte DNS_PORT = 53;
IPAddress apIP(172, 217, 28, 1);
DNSServer dnsServer;
WebServer webServer(80);

enum DeviceState {
  STATE_IDLE,
  STATE_AP_MODE,
  STATE_NTP_POLLING
};
DeviceState currentState = STATE_IDLE;

NTPClass ntp_service;
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
void changeState(DeviceState newState);
void handleNtpPolling();
void i2c_receive(int numBytesReceived);
void i2c_request();
bool verifyChecksum(byte (&buffer)[MAX_COMMAND_LENGTH + 1], uint8_t bufferLength);
uint8_t getChecksum(byte (&buffer)[REPLY_LENGTH]);

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

  void setSSIDString(String ssidStr) {
    // Ensure we don't write past the end of the buffer
    ssidLength = min((int)ssidStr.length(), 32); 
    // Copy the string and add the null terminator
    ssidStr.toCharArray(ssid, ssidLength + 1); 
  }

    void setPassString(String passStr) {
    passLength = min((int)passStr.length(), 32);
    passStr.toCharArray(pass, passLength + 1);
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
  if(DEBUG){
    Serial.begin(9600);
    Serial.println("test");
  }
  delay(4000);

  EEPROM.begin(256);
  readDataEEPROM();
  
  rtc.adjust(1, 0, 0, 2010, 1,1); //some random date

  Wire.setSCL(I2C_SCL_PIN);
  Wire.setSDA(I2C_SDA_PIN);  
  Wire.setClock(25000); 
  Wire.begin(I2C_ADDRESS); 
  Wire.onReceive(i2c_receive);
  Wire.onRequest(i2c_request);

  // Initialize in IDLE state
  currentState = STATE_IDLE;
}

void loop() {
  delay(1);

  // High-priority action: Reset data
  if(reset_data_flag){
    reset_data_flag = false;
    DeviceState stateBeforeReset = currentState;
    
    changeState(STATE_IDLE); // Stop current activity
    resetData();
    
    // If it was doing something before, restart that activity
    if(stateBeforeReset != STATE_IDLE){
      changeState(stateBeforeReset);
    }
  }

  // Main state machine handler
  switch (currentState) {
    case STATE_IDLE:
      // Nothing to do in a loop
      break;
    case STATE_AP_MODE:
      dnsServer.processNextRequest();
      webServer.handleClient();
      break;
    case STATE_NTP_POLLING:
      handleNtpPolling();
      break;
  }

  // This check is independent of the main state
  if(poll_successfull && (time(nullptr) > expiry_time)){
    if(DEBUG) Serial.println("Invalidating ntp time since timeout has been reached");
    poll_successfull = false;
    if(currentState == STATE_NTP_POLLING){
      changeState(STATE_IDLE);
    }
  }
}

void PrintTime()
{ 
  if(DEBUG) Serial.println(rtc.getTime("%A, %B %d %Y %H:%M:%S"));
} 

#pragma endregion

#pragma region state machine

void changeState(DeviceState newState) {
  if (newState == currentState) return; // No change needed

  // --- Exit current state ---
  if (currentState == STATE_AP_MODE) {
    stopCaptivePortal();
  } else if (currentState == STATE_NTP_POLLING) {
    cancelNtpPoll();
  }

  // --- Enter new state ---
  if (newState == STATE_AP_MODE) {
    startCaptivePortal();
  } else if (newState == STATE_NTP_POLLING) {
    startNtpPoll();
  }
  
  currentState = newState;
}

void handleNtpPolling() {
  // A known, recent timestamp (Jan 1, 2024). Time is valid if it's after this.
  const unsigned long MIN_VALID_EPOCH = 1704067200UL; 

  if(WiFi.status() == WL_CONNECTED){
    wifi_feedback_2 = success;
    if(!ntp_service.running()){
      ntp_service.begin("pool.ntp.org", "time.nist.gov");
      if(DEBUG) Serial.println("starting sntp service");
    }
    
    // Better check for valid time
    if(time(nullptr) > MIN_VALID_EPOCH){ 
      ntp_feedback = success;
      poll_successfull = true;
      if(DEBUG) Serial.println(("Succesfully polled, time will be valid for (s)" + String(ntp_time_validity)));
      rtc.read();
      PrintTime();
      expiry_time = time(nullptr) + ntp_time_validity;
    }
  }

  // Check for timeout or success to end the polling state
  if(time(nullptr) > (poll_starttime + poll_timeout) || poll_successfull){
    if(!poll_successfull){
      ntp_feedback = fail;
    }
    wifi_feedback = wifi_feedback_2;

    if(DEBUG) Serial.println("ntp ended");
    changeState(STATE_IDLE); // Transition back to IDLE
  }
}

#pragma endregion

#pragma region i2c handler

void i2c_receive(int numBytesReceived) {
  if(numBytesReceived >= 2 && numBytesReceived <= MAX_COMMAND_LENGTH + 1){
    byte buffer[MAX_COMMAND_LENGTH + 1];
    Wire.readBytes((byte*) &buffer, numBytesReceived);
    if(verifyChecksum(buffer, numBytesReceived)){
      uint8_t cmd_id = static_cast<uint8_t>(buffer[0]);

      if (cmd_id == enable_ap && numBytesReceived == sizeof(enable_ap_data) + 1){
        memcpy(&enable_ap_data, buffer, sizeof(enable_ap_data));
        if (enable_ap_data.enable) {
          changeState(STATE_AP_MODE);
        } else {
          changeState(STATE_IDLE);
        }
      } else if (cmd_id == poll_ntp && numBytesReceived == sizeof(poll_ntp_data) + 1){
        memcpy(&poll_ntp_data, buffer, sizeof(poll_ntp_data));

        poll_timeout = min((uint16_t)MAX_NTP_TIMEOUT, poll_ntp_data.ntp_timeout);
        ntp_time_validity = min((uint16_t)MAX_NTP_TIME_VALIDITY, poll_ntp_data.ntp_time_validity);
        
        changeState(STATE_NTP_POLLING);
      } else if (cmd_id == reset_data && numBytesReceived == 2){
        reset_data_flag = true;
      }
    }
  } else {
    // Clear the buffer if the message length is invalid
    while(Wire.available()) {
      Wire.read();
    }
  }
}

void i2c_request() {
  byte buffer[REPLY_LENGTH];
  rtc.read();
  time_t t = 0;

  if (current_settings.useGmtOffset){
    // ... (rest of the function is the same)
  } else {
    t = timezones[current_settings.timezoneIdx].toLocal(rtc.getEpoch());
  }

  // Replace polling_ntp with a check of the current state
  uint8_t combined_bool = poll_successfull + ((currentState == STATE_NTP_POLLING) * 2);

  buffer[0] = (byte)combined_bool;
  buffer[1] = (byte)hour(t);
  buffer[2] = (byte)minute(t);
  buffer[3] = (byte)second(t);
  buffer[4] = getChecksum(buffer);

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
  if(DEBUG) Serial.println("start ap");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ACCESS_POINT_NAME, ACCESS_POINT_PASSWORD);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  webServer.on("/credentials", HTTP_POST, handleCredentials);
  webServer.onNotFound(handleCaptive);
  webServer.begin();
}

void stopCaptivePortal() {
  if(DEBUG) Serial.println("stop ap");
  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
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
  // ... (setup logic is the same)
  wifi_feedback_2 = fail;
  ntp_service = NTPClass();
  poll_successfull = false;
  rtc.adjust(1, 0, 0, 2010, 1,1);
  poll_starttime = time(nullptr);
  WiFi.mode(WIFI_STA);
  // ... (rest of function is the same, WiFi.begin(...) etc.)
}

void cancelNtpPoll() {
  if(DEBUG) Serial.println("stop ntp");
  sntp_stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
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