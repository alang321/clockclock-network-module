#include <DNSServer.h>
#include <WiFi.h>
#include <WebServer.h>

void startCaptivePortal();
void handleCredentials();
void handleCaptive();


const String apName = "ClockClock";
const String apPassword = "et970004";

String wifiNetworkName = "momak_2.4";
String wifiNetworkPassword = "et970004";

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
  label{text-align:left; padding:2%;}
  input{border-radius: 4px; border: 2px solid #0056a8; width:90%; padding:10px; display:block; margin-right:auto; margin-left:auto;}
  input[type="submit"]{font-size: 25px; background-color:#0056a8; color:white; border-radius:8px; height:50px; width:95%;}
  </style>
  </head>)rawliteral";

const String captiveFormHTML = R"rawliteral(<body>
  <div>
    <h1>ClockClock</h1>
    <p>Geben sie die folgenden Daten ein um Ihre Uhr mit einem 2.4GHz WLAN-Netzwerk für Internet-Zeitsynchronisierung zu verbinden:</p>
    <form action="/credentials" method="POST">
      <label>Wi-Fi Daten</label>
      <br>
      <input type="text" name="wifissid" id="wifissid" placeholder="Wi-Fi SSID">
      <br>
      <input type="text" name="wifipass" id="wifipass" placeholder="Wi-Fi Passwort">
      <br>
      <input type="submit" value="Submit">
    </form><br>
  </div>
</body></html>)rawliteral";


const String captiveRedoHTML = R"rawliteral(<body>
  <div>
    <h1>ClockClock</h1>
    <p>Daten Erfolgreich gepeichert, zum erneuten ändern den untensteheden Knoopf drücken:</p>
    <form action="/" method="POST">
      <input type="submit" value="Anpassen">
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

void handleCredentials(){
  Serial.println("I got dems credentials");

  if (webServer.hasArg("wifissid") && webServer.hasArg("wifipass")){
    Serial.println(webServer.arg("wifissid"));
    Serial.println(webServer.arg("wifipass"));

    //display a webpage with a confirmation that shows the set wifi ssid and password
    //and a button to change them again

    //make a string with style that get combined with a body so less space is used
  }
  else{
    Serial.println("nopers");
  }

  webServer.send(200, "text/html", (styleHTML + captiveRedoHTML));
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