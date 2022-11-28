#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>

void startCaptivePortal();
void handleCredentials();
void handleCaptive();


const String apName = "ClockClock";
const String apPassword = "vierundzwanzig";

String wifiNetworkName = "momak_2.4";
String wifiNetworkPassword = "et970004";
bool isProtected = true;

const byte DNS_PORT = 53;
IPAddress apIP(172, 217, 28, 1);
DNSServer dnsServer;
WebServer webServer(80);

const String styleHTML = R"rawliteral(
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

const String captiveFormHTML = R"rawliteral(<body>
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

const String captiveSuccessHTML = R"rawliteral(<body>
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

const String captiveErrorHTML = R"rawliteral(<body>
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

void setup() {
  Serial.begin(9600);
  Serial.println("test");
  startCaptivePortal();
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
}



void startCaptivePortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apName, apPassword);

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
  String msg = captiveSuccessHTML;

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
        msg = captiveErrorHTML;
        msg.replace("*<*Error*>*", "SSID sollte weniger als 32 Zeichen haben");
      }
    }
    else{
      msg = captiveErrorHTML;
      msg.replace("*<*Error*>*", "Passwort muss mehr als 7 Zeichen haben");
    }
  }
  else{
    msg = captiveErrorHTML;
    msg.replace("*<*Error*>*", "Unbekannter Fehler");
  }

  webServer.send(200, "text/html", (styleHTML + msg));
}

void handleCaptive(){
  webServer.send(200, "text/html", (styleHTML + captiveFormHTML));
}

//three modes
//acces point
//ntp sync
//off


//turn off all wifi things

//connect to hotspot

//synchronise ntp
//connects to hotspot if not



//i2c set mode command
//i2c maybe set 