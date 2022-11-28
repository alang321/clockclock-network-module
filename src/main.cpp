#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RP2040_RTC.h>

void startCaptivePortal();
void handleCredentials();
void handleCaptive();

//i2c handlers
void i2c_receive(int numBytesReceived);
void i2c_request();

const int I2C_SDA_PIN = 15;
const int I2C_SCL_PIN = 16;
const int I2C_ADDRESS = 40;

const String ACCESS_POINT_NAME = "ClockClock";
const String ACCESS_POINT_PASSWORD = "vierundzwanzig";

String wifiNetworkName = "momak_2.4";
String wifiNetworkPassword = "et970004";
bool isProtected = true;

const byte DNS_PORT = 53;
IPAddress apIP(172, 217, 28, 1);
DNSServer dnsServer;
WebServer webServer(80);

cmd_enable_ap_data enable_ap_data;
cmd_poll_ntp_struct_data poll_ntp_data;

datetime_t currTime = { 2022, 1, 21, 5, 5, 0, 0 };

#pragma region i2c command datastructs

enum cmd_identifier {enable_ap = 0, poll_ntp = 1};

struct cmd_enable_ap_data {
  bool enable; //1bytes # true enables the acces point for configuration, false disables it
};

struct cmd_poll_ntp_data {
  uint16_t ntp_timeout; //2bytes, timeout in s
};

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
      <input type="submit" value="Anpassen">
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
  Serial.begin(9600);
  Serial.println("test");
  startCaptivePortal();

  //Initialize as i2c slave
  Wire.setSCL(I2C_SCL_PIN);
  Wire.setSDA(I2C_SDA_PIN);  
  //Wire.setClock(100000);  breaks the i2c bus for some reason
  Wire.begin(I2C_ADDRESS); 
  Wire.onReceive(i2c_receive);
  Wire.onRequest(i2c_request);

  rtc_init();
  rtc_set_datetime(&currTime);
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
}

#pragma endregion

#pragma region i2c handler

void i2c_receive(int numBytesReceived) {
  uint8_t cmd_id = 0;
  Wire.readBytes((byte*) &cmd_id, 1);

  byte i2c_buffer[numBytesReceived - 1];
  
  Wire.readBytes((byte*) &i2c_buffer, numBytesReceived - 1);
  if (cmd_id == 0){
    enable_ap_data = &i2c_buffer;
  }else{
    poll_ntp_data = &i2c_buffer;
  }
}

void i2c_request() {
  rtc_get_datetime(&currTime);
  
  Wire.write(1);
}

#pragma endregion

#pragma region captive portal

void startCaptivePortal() {
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

void stopCaptivePortal() {
  WiFi.mode(WIFI_OFF);
  dnsServer.stop();
  webServer.stop();
}

void handleCredentials(){
  String msg = CAPTIVE_SUCCESS_HTML;

  if (webServer.hasArg("wifissid") && webServer.hasArg("wifipass")){
    Serial.println(webServer.arg("wifissid"));
    Serial.println(webServer.arg("wifipass"));
    Serial.println(webServer.arg("is_protected"));

    if((webServer.hasArg("is_protected") && webServer.arg("wifipass").length() >= 8) || !webServer.hasArg("is_protected")){
      if(webServer.arg("wifissid").length() <= 32){
        isProtected = webServer.hasArg("wifipass");
        wifiNetworkName = webServer.arg("wifissid");
        wifiNetworkPassword = webServer.arg("is_protected");

        if (webServer.hasArg("is_protected")){
          msg.replace("*<*PROT*>*", "Ja");
        }else{
          msg.replace("*<*PROT*>*", "Nein");
          String a = R"rawliteral(<p>Passwort: *<*PASS*>*</p>)rawliteral";
          msg.replace(a, "");
        }
        msg.replace("*<*SSID*>*", webServer.arg("wifissid"));
        msg.replace("*<*PASS*>*", webServer.arg("wifipass"));
      }
      else{
        msg = CAPTIVE_ERROR_HTML;
        msg.replace("*<*Error*>*", "SSID sollte weniger als 32 Zeichen haben");
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
