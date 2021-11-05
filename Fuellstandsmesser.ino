/*
  Pneumatische Zisternen-Füllstandsmessung.
  Start der Messung und Übertragung der Ergebnise über WLAN.
  
  Arduino-Bord: "NodeMCU 1.0(ESP-12E Module)"
  Autor Wolfgang Neußer
  Stand: 24.10.2021

  Hardware:
  DOIT ESP12E Motor Shield mit L293D Motortreiber
  Amica NODE MCU ESP8266 12E
  SparkFun Qwiic MicroPressure Sensor
  Druckpumpe und Entlüftungsventil aus Oberarm-Blutdruckmesser
 
  Messablauf:
  1. Abluftventil schließen, Druckpumpe einschalten
  2. Druck kontinuierlich messen
     Wenn Druckanstieg beendet -> Pumpe ausschalten
  3. Beruhigungszeit
  4. Aktueller Druck - atmosphärischen Druck = Messdruck
     Beispiel: 29810 Pa = 3040 mmH2O = 100% Füllstand
  5. Abluftventil öffnen

*/

// Bibliothek für WLAN
#include <ESP8266WiFi.h>

// Bibliothek für die I2C-Schnittstelle
#include <Wire.h>

// Bibliothek für den Sensor (Im Bibliotheksverwalter unter "MicroPressure" suchen
// oder aus dem GitHub-Repository https://github.com/sparkfun/SparkFun_MicroPressure_Arduino_Library )
#include <SparkFun_MicroPressure.h>

// Server an Port 80 initialisieren
WiFiServer server(80);

// Konstruktor initialisieren
// Ohne Parameter werden Default Werte verwendet
SparkFun_MicroPressure mpr; 

// Bibliothek für das Flashen über WLAN
#include <ArduinoOTA.h>

// Zuordnung der Ein- Ausgänge
#define VENTIL          5                 // GPIO5 (PWM MotorA)
#define DA              0                 // GPIO0 (Richtung MotorA)
#define PUMPE           4                 // GPIO4 (PWM MotorB)
#define DB              2                 // GPIO2 (Richtung MotorB)
#define SDA            12                 // GPIO12 I2C
#define SCL            13                 // GPIO13 I2C
#define AUF             LOW               // Ventil öffnen
#define AUS             LOW               // Pumpe ausschalten
#define ZU              HIGH              // Ventil schliessen
#define EIN             HIGH              // Pumpe einschalten

// Heimnetz Parameter (an eigenes Netz anpassen)
const char* ssid = "Heimnetz-Name";
const char* pass = "Heimnetz-Passwort";

// An eigene Zisterne anpassen (zur Berechnung der Füllmenge)
const int A = 3140;                       // Grundfläche der Zisterne in cm^2 (d * d * 3,14 / 4)
const int maxFuellhoehe = 3040;           // Füllhöhe der Zisterne in mm

int atmDruck, messDruck, vergleichswert;
int messSchritt, wassersaeule;
String hoehe = " - - ";
String volumen = "- - ";
String fuellstand = " - - "; 
unsigned long messung, messTakt;

// **************************************************************************************
// State-Machine Füllstandsmessung
//
void messablauf() {
   switch (messSchritt) {
      case 0:  // Regelmäßig aktuellen atmosphärischen Druck erfassen
         if (!digitalRead(VENTIL) && !digitalRead(PUMPE)) {
            atmDruck = messDruck;      
         }
         break;

      case 1:  // Messung gestartet
         vergleichswert = messDruck;
         digitalWrite(VENTIL, ZU);
         digitalWrite(PUMPE, EIN);
         messung = millis() + 2000;
         messSchritt = 2;
         break;
      
      case 2:  // warten solange Druck steigt
         if (messDruck > vergleichswert + 10) {
            vergleichswert = messDruck;
            messung = millis() + 1000;
         }
         if (wassersaeule > (maxFuellhoehe + 200)) {
            Serial.println("Fehler: Messleitung verstopft!");
            messSchritt = 4;
         }
         else if (messung < millis()) {
            digitalWrite(PUMPE, AUS);
            messung = millis() + 100;
            messSchritt = 3;
         }
         break;

      case 3:  // Beruhigungszeit abgelaufen, Messwert ermitteln
         if (messung < millis()) {
            hoehe = String(wassersaeule / 10) + "cm";
            volumen = String((wassersaeule / 10) * A / 100) + "L";
            // Umrechnung Wassersäule in 0 - 100%
            fuellstand = String(map(wassersaeule, 0, maxFuellhoehe, 0, 100)) + "%";
            Serial.println("Füllhöhe: "+ hoehe);
            Serial.println("Volumen: " + volumen);
            Serial.println("Füllstand: " + fuellstand);
            Serial.println();
            messSchritt = 4;
         }
         break;

      case 4:  // Ablauf beenden
         digitalWrite(VENTIL, AUF);
         digitalWrite(PUMPE, AUS); 
         messSchritt = 0;
         break;
         
      default:
         messSchritt = 0;
         break;
   }
}

void setup() {
   // Motortreiber-Signale

   // Richtung Motor A
   pinMode(DA, OUTPUT);
   digitalWrite(DA, HIGH);
   // PWM Motor A
   pinMode(VENTIL, OUTPUT);
   digitalWrite(VENTIL, AUF);
   
   // Richtung Motor B
   pinMode(DB, OUTPUT);
   digitalWrite(DB, HIGH);
   // PWM Motor B
   pinMode(PUMPE, OUTPUT);
   digitalWrite(PUMPE, AUS); 
   
   Serial.begin(115200);                     
   delay(10);
   Serial.println();
   
   // I2C initialisieren mit 400 kHz
   Wire.begin(SDA, SCL, 400000);            

   // Drucksensor initialisieren
   // Die Default-Adresse des Sensors ist 0x18
   // Für andere Adresse oder I2C-Bus: mpr.begin(ADRESS, Wire1)
   if(!mpr.begin()) {
      Serial.println("Keine Verbindung zum Drucksensor.");
      while(1);
   }

   // WiFi initialisieren
   WiFi.mode(WIFI_STA);
   Serial.println("Verbindung zu " + String(ssid) + " wird hergestellt");
   WiFi.begin(ssid, pass);
   while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
   }
   Serial.println();
   server.begin();
   Serial.println("Server ist gestartet");
   Serial.print("IP-Adresse: ");
   Serial.println(WiFi.localIP());
   
   ArduinoOTA.onStart([]() {        // Pumpe und Ventil ausschalten beim Flashen
      digitalWrite(VENTIL, AUF);
      digitalWrite(PUMPE, AUS);
      messSchritt = 0;
   });
   // Passwort zum Flashen
   ArduinoOTA.setPassword((const char *)"esp8266");
   // OTA initialisieren
   ArduinoOTA.begin();              

   messTakt = 0;
   messSchritt = 0;
   atmDruck = 97400.0;                    // Augangswert Atmosphärendruck in Pa 
}
                                          
void loop() {
   static String inputString;
   
   // OTA-Service bedienen                                                                                                                              
   ArduinoOTA.handle();                   
   yield();

   // Kommandos über serielle Schnittstelle
   if (Serial.available()) {
      char inChar = (char)Serial.read();
      if ((inChar == '\r') || (inChar == '\n')) {
         if (inputString == "?") {
            Serial.println("Kommandos: ");
            Serial.println("p1 = Pumpe EIN");
            Serial.println("p0 = Pumpe AUS");
            Serial.println("v1 = Ventil ZU");
            Serial.println("v0 = Ventil AUF");
            Serial.println("start = Messung starten");
            Serial.println();
         }
         else if (inputString == "p1") {
            Serial.println("Pumpe EIN");
            digitalWrite(PUMPE, EIN);
         }
         else if (inputString == "p0") {
            Serial.println("Pumpe AUS");
            digitalWrite(PUMPE, AUS);
         }
         else if (inputString == "v1") {
            Serial.println("Ventil ZU");
            digitalWrite(VENTIL, ZU);
         }
         else if (inputString == "v0") {
            Serial.println("Ventil AUF");
            digitalWrite(VENTIL, AUF);
         }
         else if (inputString == "start") {
            if (messSchritt == 0) {
               Serial.println("Messung gestartet");
               messSchritt = 1;
            }
         }
         inputString = "";
      } else inputString += inChar;
   } 

   // Alle 10 ms Sensorwert auslesen                                          
   if (messTakt < millis()) {
      // Messwert in Pascal auslesen und filtern
      messDruck = ((messDruck * 50) + int(mpr.readPressure(PA))) / 51;
      // Umrechnung Pa in mmH2O   
      wassersaeule = (messDruck - atmDruck) * 10197 / 100000;
      if (wassersaeule < 0) wassersaeule = 0;
      messTakt = millis() + 10;
   }

   // Sicherheitsabschaltung der Pumpe bei Überdruck
   if ((messSchritt == 0) && (wassersaeule > (maxFuellhoehe + 300))) {
      digitalWrite(PUMPE, AUS);
      Serial.println("Überdruck. Messleitung verstopft!");
   }
                                          
   // State-Machine 
   messablauf();

   // Start der Messung und Übergabe des letzten Ergebnisses bei jeder Client-Anfrage
   WiFiClient client = server.available();
   if (client) {
      client.print("<head><title>Fuellstandsmesser</title>");
      client.print("<meta charset=\"utf-8\" http-equiv='refresh' content='10'>");
      client.print("<meta name='viewport' content='width=device-width, initial-scale=1.0' /></head>");
      client.print("<h1>Füllstand Zisterne</h1><br>");
      client.print("<table>");
      client.print("<tr><td><b>Füllhöhe:</b> </td><td>"); client.print(hoehe); client.print("<br></td></tr>");
      client.print("<tr><td><b>Volumen:</b> </td><td>"); client.print(volumen); client.print("<br></td></tr>");
      client.print("<tr><td><b>Füllstand:</b> </td><td>"); client.print(fuellstand); client.print("<br></td></tr>");
      client.print("</table>");
      if (messSchritt == 0) messSchritt = 1;
   }
} 
