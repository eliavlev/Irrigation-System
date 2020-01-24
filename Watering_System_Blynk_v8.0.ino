
/*
#####################################################################################
#   File:               Arduino_Watering_sustem.ino                                             
#       Processor:          Arduino UNO      
#   Language:     Wiring / C /Processing /Fritzing / Arduino IDE          
#           
# Objectives:         Watering System - Irrigation using Blynk and NodeMCU (ESP8266-12E)
#                     
# Behavior:     Based on Blynk time schedule
#                           
#     
#   Author:                 Eliav Levinson 
#   Date:                   30/12/16  
#   place:                   
#         
#####################################################################################
 
  This project contains public domain code.
  The modification is allowed without notice.
  
 // libraries definition

/* BLYNK Definitions & Libraries */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
//#include <BlynkSimpleEsp8266_SSL.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <SoftwareSerial.h>  
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SimpleTimer.h>
#include "FS.h"
#include <ArduinoOTA.h>

// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
char auth[] = "Blynktoken"; //enter blynk token

// Your WiFi credentials.
// Set password to "" for open networks.
char ssid[] = "SSID";
char pass[] = "Password";
char server[]= "blynk-cloud.com";
unsigned int port = 8442;

#define RELAY1 12  //D6
#define RELAY2 13  //D7
#define RELAY3 2  //D4
#define ON 0
#define OFF 1
//#define BLYNK_PRINT Serial
#define OLED_RESET LED_BUILTIN


//#define OLED_RESET LED_BUILTIN
Adafruit_SSD1306 display(OLED_RESET);



// A UDP instance to let us send and receive packets over UDP
WiFiUDP Udp;

// Init NTP time 
//----------------------------------------------------------------------------------
unsigned int localPort = 8888;      // local port to listen for UDP packets
//String timeString=String();
//IPAddress timeServerIP; // This will store the timeserver ip address
static const char ntpServerName[] = "0.europe.pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
time_t prevDisplay = 0; // when the digital clock was displayed
int timeZoneOffset = 3; // set this to the offset in hours to your local time;


// the timer object
SimpleTimer timer;  

//--------------------------------------------------------------------------------------
// pins definition
int levelSensorPin = 0;
int moistureSensorPin= A0;
int moistureSensorValue =0;
int sensorValue=0;
int timeVal;  

String soilMoist;
char phrase[20] ="WiFi connected";
char digitalTime[25];  // The current time in ISO format is being stored here
uint8_t WeekDays_Valve1[8]={1,0,0,0,0,0,0,0}; 
uint8_t WeekDays_Valve2[8]={2,0,0,0,0,0,0,0};
uint8_t WeekDays_Valve3[8]={3,0,0,0,0,0,0,0};
int StartTime_Valve1=0;
int StartTime_Valve2=0;
int StartTime_Valve3=0;
int StopTime_Valve1=0;
int StopTime_Valve2=0;
int StopTime_Valve3=0;
bool spiffsActive = false;

long ManualWaterDuration = 1L;  //Set manual watering duration in minutes
int RainModeState=0;   //Rain Mode Button
int Relay1_buttonState=0;  //Set default Virtual Button state to OFF by default
int Relay2_buttonState=0;
int Relay3_buttonState=0;
int Relay4_buttonState=0;

  
void setup() {
// serial initialization
  Serial.begin(9600);
  Serial.println(); 

pinMode(RELAY1, OUTPUT);
pinMode(RELAY2, OUTPUT);
pinMode(RELAY3, OUTPUT);
digitalWrite(RELAY1, OFF);
digitalWrite(RELAY2, OFF);
digitalWrite(RELAY3, OFF);
//digitalWrite(RELAY4, LOW);

   
// Start filing subsystem
  if (SPIFFS.begin()) {
      Serial.println("SPIFFS Active");
      Serial.println();
      spiffsActive = true;
  } else {
      Serial.println("Unable to activate SPIFFS");
  }

/*
// Port defaults to 8266
  ArduinoOTA.setPort(8277);
//Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("WaterESP8266");

// No authentication by default
  ArduinoOTA.setPassword((const char *)"1234zooz");
 
  ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();
  Serial.println("OTA Success");
*/
  Wire.begin();
// initialize Oled display with the I2C addr 0x3D (for the 128x64)
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false);  
// Show image buffer on the display hardware.
// Since the buffer is intialized with an Adafruit splashscreen
// internally, this will display the splashscreen.
  display.display();
  delay(500);

// Clear the display buffer.
  display.clearDisplay();
  
// Connecting to a WiFi network
//--------------------------------------------------------------------
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
   }
  Serial.println("WiFi connected");
  Serial.print("IP number assigned by DHCP is:  ");
  Serial.println(WiFi.localIP());
  PrintToLCD("WiFi connected",1,20);
  delay(2500);
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());

  // Set the sync provider & timesync interval
  setSyncProvider(getNtpTime);
  setSyncInterval(300); //Sync to NTP server every 5 minutes
  
  
// Read default watering time from FLASH
  StartTime_Valve1=ReadFromFlash("Start_v1");
  StopTime_Valve1=ReadFromFlash("Stop_v1");
  StartTime_Valve2=ReadFromFlash("Start_v2");
  StopTime_Valve2=ReadFromFlash("Stop_v2");
  StartTime_Valve3=ReadFromFlash("Start_v3");
  StopTime_Valve3=ReadFromFlash("Stop_v3");
  ReadFromFlash2("WeekV1",WeekDays_Valve1);
  for(int i=0;i<8;i++){
   Serial.print(WeekDays_Valve1[i]);
  };
  Serial.println();
  ReadFromFlash2("WeekV2",WeekDays_Valve2);
  for(int i=0;i<8;i++){
   Serial.print(WeekDays_Valve2[i]);
  };
  Serial.println();
  ReadFromFlash2("WeekV3",WeekDays_Valve3);
for(int i=0;i<8;i++){
   Serial.print(WeekDays_Valve3[i]);
  };
  Serial.println();



  
// Setup BLYNK code
 // Blynk.begin(auth, ssid, pass);
 Blynk.config(auth, server, port);
 Blynk.connect(10000);
 Serial.println("Connected to Blynk server"); 
 Blynk.syncAll();  
 //Blynk.syncVirtual(V1);
 //delay (1000);
 //Blynk.syncVirtual(V2);
 //delay(1000);
 //Blynk.syncVirtual(V3);
 //delay(1000);
 //Blynk.syncVirtual(V4);
 


/*  This replaces NTP sync procedure
// Attach to Real Time Clock Widget
BLYNK_ATTACH_WIDGET(rtc, V10);
// Begin synchronizing time
  rtc.begin();
*/

timer.setInterval(5007, WateringCheck);
timer.setInterval(32005,CheckMoisture);
timer.setInterval(10009,checkWifiStatus);
timer.setInterval(30003, CheckConnection); // check if still connected 
timer.setInterval(600000, SetTimeZone);

}


//---------------------------------------------------------------------------------------
//  Loop
//---------------------------------------------------------------------------------------
void loop() {
  
 timer.run();
 
 // Runs the Blynk interface for remote control
  Blynk.run();
  
 // Get UTC time
 if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      prevDisplay = now();
      }
  }
  //ArduinoOTA.handle();
  
 }


//---------------------------------------------------------------------------------
// Function Declerations
//---------------------------------------------------------------------------------
void CheckConnection(){    // check every 11s if connected to Blynk server
  if(!Blynk.connected()){
    Serial.println("Not connected to Blynk server"); 
    Blynk.connect();  // try to connect to server with default timeout
    Blynk.syncAll();
  }
  else{
    Serial.println("Connected to Blynk server");
       
  }
}

void CheckMoisture(){

  // reads the sensors
  moistureSensorValue = analogRead(moistureSensorPin);
  soilMoist="Moisture: "+ moistureSensorValue;
  Serial.print("Moisture: ");
  Serial.println(moistureSensorValue);
  Blynk.virtualWrite(V0, moistureSensorValue);
  
}

BLYNK_WRITE(V1) {  // Get watering scheduling for valve1
  TimeInputParam t(param);

  // Process start time
  if (t.hasStartTime())
  {
   if (t.getStartHour()==0){
    StartTime_Valve1=24*100+ t.getStartMinute();
   }
   else {
   StartTime_Valve1=t.getStartHour()*100+ t.getStartMinute();
   }
   Serial.print("Start V1: " );
   Serial.println (StartTime_Valve1);
  
  // Write START TIME to Flash
   WriteToFlash("Start_v1",StartTime_Valve1);
   
  }
  // Process stop time
  if (t.hasStopTime())
  {
   if(t.getStopHour()==0){
    StopTime_Valve1=24*100+ t.getStopMinute();
   }
   else {
      StopTime_Valve1=t.getStopHour()*100+t.getStopMinute();
   }
   Serial.print("Stop V1: ");
   Serial.println(StopTime_Valve1);
   
   // Write STOP TIME to Flash
   WriteToFlash("Stop_v1",StopTime_Valve1);
    
  }
 
  // Process weekdays (1. Mon, 2. Tue, 3. Wed, ...)
  for (int i = 1; i <= 6; i++) {
    if (t.isWeekdaySelected(i)) {
      WeekDays_Valve1[i+1]=1;
      Serial.println(String("Day ") + (i+1) + " is selected");
      }
        
    else{
      WeekDays_Valve1[i+1]=0;
    }
  }
  if (t.isWeekdaySelected(7)){     //Blynk Sunday enumeration is 7. Convert to 1.
      WeekDays_Valve1[1]=1;
      Serial.println(String("Day ") + (1) + " is selected");
    }

  WriteToFlash2("WeekV1", WeekDays_Valve1); ////////////////////////////////////////////
  
  Serial.println();
}
//-----------------------
BLYNK_WRITE(V2) {  // Get watering scheduling for valve2
  TimeInputParam t(param);

  // Process start time
  if (t.hasStartTime())
  {
    if (t.getStartHour()==0){
    StartTime_Valve2=24*100+ t.getStartMinute();
   }
   else {
   StartTime_Valve2=t.getStartHour()*100+ t.getStartMinute();
   }
   Serial.print("Start V2: " );
   Serial.println (StartTime_Valve2);
  
  // Write START TIME to Flash
   WriteToFlash("Start_v2",StartTime_Valve2);
   
  }
  // Process stop time
  if (t.hasStopTime())
    {
    if(t.getStopHour()==0){
    StopTime_Valve2=24*100+ t.getStopMinute();
    }
   else {
      StopTime_Valve2=t.getStopHour()*100+t.getStopMinute();
     }
   Serial.print("Stop V2: ");
   Serial.println(StopTime_Valve2);

   // Write STOP TIME to Flash
   WriteToFlash("Stop_v2",StopTime_Valve2);
   }
   
  // Process weekdays (1. Mon, 2. Tue, 3. Wed, ...)
  for (int i = 1; i <= 6; i++) {
    if (t.isWeekdaySelected(i)) {
      WeekDays_Valve2[i+1]=1;
      Serial.println(String("Day ") + (i+1) + " is selected");
    }
    else{
      WeekDays_Valve2[i+1]=0;
    }
  }
  if (t.isWeekdaySelected(7)){     //Blynk Sunday enumeration is 7. Convert to 1.
      WeekDays_Valve2[1]=1;
      Serial.println(String("Day ") + (1) + " is selected");
    }

  WriteToFlash2("WeekV2", WeekDays_Valve2); ////////////////////////////////////////////
  Serial.println();
}
//---------------------------------------------

BLYNK_WRITE(V3) {  // Get watering scheduling for valve3
  TimeInputParam t(param);

  // Process start time
  if (t.hasStartTime())
  {
   if (t.getStartHour()==0){
    StartTime_Valve3=24*100+ t.getStartMinute();
   }
   else {
   StartTime_Valve3=t.getStartHour()*100+ t.getStartMinute();
   }
   Serial.print("Start V3: " );
   Serial.println (StartTime_Valve3);
  
  // Write START TIME to Flash
   WriteToFlash("Start_v3",StartTime_Valve3);
   
  }
  // Process stop time
  if (t.hasStopTime())
  {
   if(t.getStopHour()==0){
    StopTime_Valve3=24*100+ t.getStopMinute();
   }
   else {
      StopTime_Valve3=t.getStopHour()*100+t.getStopMinute();
   }
   Serial.print("Stop V3: ");
   Serial.println(StopTime_Valve3);
  
  // Write STOP TIME to Flash
   WriteToFlash("Stop_v3",StopTime_Valve3);
  
  }
 
  // Process weekdays (1. Mon, 2. Tue, 3. Wed, ...)
  for (int i = 1; i <= 6; i++) {
    if (t.isWeekdaySelected(i)) {
      WeekDays_Valve3[i+1]=1;
      Serial.println(String("Day ") + (i+1) + " is selected");
    }
    else{
      WeekDays_Valve3[i+1]=0;
    }
  }
  if (t.isWeekdaySelected(7)){     //Blynk Sunday enumeration is 7. Convert to 1.
      WeekDays_Valve3[1]=1;
      Serial.println(String("Day ") + (1) + " is selected");
    }
  WriteToFlash2("WeekV3", WeekDays_Valve3); ////////////////////////////////////////////
  Serial.println();
}

BLYNK_WRITE(V19){  //Read manual watering duration
    ManualWaterDuration=param.asInt()*1000*60;
    }

BLYNK_WRITE(V20){  //Read Rain Mode state from Blynk App
    RainModeState=param.asInt();
    }

BLYNK_WRITE(V21){
    Relay1_buttonState=param.asInt();
    timer.setTimeout(ManualWaterDuration, ShutOff21);
   }

BLYNK_WRITE(V22){
    Relay2_buttonState=param.asInt();
    timer.setTimeout(ManualWaterDuration, ShutOff22);
   }
BLYNK_WRITE(V23){
    Relay3_buttonState=param.asInt();
    timer.setTimeout(ManualWaterDuration, ShutOff23);
   }

void ShutOff21(){
  Blynk.virtualWrite(21, 0);
  Relay1_buttonState=0;
  }

void ShutOff22(){
  Blynk.virtualWrite(22, 0);
  Relay2_buttonState=0;
  }

void ShutOff23(){
  Blynk.virtualWrite(23, 0);
  Relay3_buttonState=0;
  }
 

//---------------------------------------------
  
//  Date & Time Based watering logic
void WateringCheck(){
  
  display.clearDisplay();
  
 if (hour()==0){
    timeVal=24*100+minute(); //if watering is around midnight
    }
  else {
    timeVal=hour()*100+minute();
  }
  Serial.println("timeval: ");
  Serial.println (timeVal);
  Serial.println("Watering Check Running");
  // if valve 1=True & current weekday = Active watering days & current time within watering time interval or Manual Button Pressed 
  if (((WeekDays_Valve1[0]==1 && WeekDays_Valve1[weekday()]==1 && timeVal >=StartTime_Valve1 && timeVal<=StopTime_Valve1)and (RainModeState==0)) or (Relay1_buttonState==1)){
    Water(RELAY1,"Valve1");
    Serial.print("Valve 1 Weekday: ");
    Serial.println(weekday());
    Serial.println (timeVal);
    PrintToLCD("Valve1 Watering",0,20);
    WidgetLED led1(V5); // Valve 1 register to virtual pin 5
    led1.on();
    }
  else {
    digitalWrite(RELAY1,OFF);           // Turns OFF Relay
    PrintToLCD("Valve1 STBY",0,20);
    WidgetLED led1(V5); // Valve 1 register to virtual pin 5
    led1.off();
  }
  if ((WeekDays_Valve2[0]==2 && WeekDays_Valve2[weekday()]==1 && timeVal >=StartTime_Valve2 && timeVal<=StopTime_Valve2 and (RainModeState==0))or (Relay2_buttonState==1)){
    Serial.print("Valve 2 Watering: ");
    WidgetLED led2(V6); // Valve 2 register to virtual pin 6
    led2.on();
    Water(RELAY2,"Valve2");
    PrintToLCD("Valve2 Watering",0,30);
    }
  else{
    digitalWrite(RELAY2,OFF);           // Turns OFF Relay
    PrintToLCD("Valve2 STBY",0,30);
    WidgetLED led2(V6); // Valve 2 register to virtual pin 6
    led2.off();
    }
  if ((WeekDays_Valve3[0]==3 && WeekDays_Valve3[weekday()]==1 && timeVal >=StartTime_Valve3 && timeVal<=StopTime_Valve3 and (RainModeState==0))or (Relay3_buttonState==1)){
    Serial.print("Valve 3 Watering: ");
    WidgetLED led3(V7); // Valve 2 register to virtual pin 7
    led3.on();
    Water(RELAY3,"Valve3");
    PrintToLCD("Valve3 Watering",0,40);
    }
  else {
    digitalWrite(RELAY3,OFF);           // Turns OFF Relay
    WidgetLED led3(V7); // Valve 2 register to virtual pin 6
    led3.off();
    PrintToLCD("Valve3 STBY",0,40);
   
    }
  
  display.display();
  }

  

void Water(int relay, String relayName){
      digitalWrite(relay,ON);           // Turns ON Relay 
       
    }
  
  


//---------------------------------------------------------------
// send an NTP request to the time server at the given address
//---------------------------------------------------------------

time_t getNtpTime(){
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZoneOffset * SECS_PER_HOUR;
  
    }
  }
  Serial.println("No NTP Response :-(");
    return 0; // return 0 if unable to get the time
}


// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

  

void digitalClockDisplay(){
  //sprintf(digitalTime, "%4d-%02d-%02d %02d:%02d:%02d\n", year(), month(), day(), hour(), minute());  //display second resolution
  sprintf(digitalTime, "%4d-%02d-%02d %02d:%02d\n", year(), month(), day(), hour(), minute());   //display minute resolution
  Blynk.virtualWrite(V10, digitalTime);
}

void displayTime()
{
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  digitalClockDisplay();
  display.println(digitalTime);
  display.display();
 }

 void PrintToLCD(const char* phrase, int XOffset, int YOffset)
{
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  digitalClockDisplay();
  display.println(digitalTime);
  display.println(" ");
  display.setCursor(XOffset,YOffset);
  //display.setTextSize(1);
  //display.setTextColor(WHITE);
  display.println(phrase);
  display.println(" ");
  //display.display();
  //display.clearDisplay(); 
 }

int ReadFromFlash(String filename){
  if (spiffsActive) {
    if (SPIFFS.exists(filename)) {
      File f = SPIFFS.open(filename, "r");
      if (!f) {
        Serial.print("Unable To Open '");
        Serial.print(filename);
        Serial.println(" for Reading");
        return 2500;
        } 
        else {
        String s;
        Serial.print("Contents of file ");
        Serial.print(filename);
        Serial.println("'");
        while (f.position()<f.size())
        {
          s=f.readStringUntil('\n');
          s.trim();
          Serial.println(s.toInt());
         } 
        f.close();
        delay(1000);
        return (s.toInt()); 
        }
          
        }
      } 
    }


// Write START TIME to Flash
void WriteToFlash(String filename, int timeValue){   

  File f = SPIFFS.open(filename, "w");
      if (!f) {
        Serial.print("Unable To Open for writing: ");
        Serial.println (filename);
      }
       else {
        Serial.println("Writing to file: ");
        Serial.println(filename);
        f.println(timeValue);
        f.close();
      }
}

// Wifi Status Check

void checkWifiStatus(){
if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    delay(500);
    WiFi.begin(ssid, pass);
    delay(800);
   }
}

// Write Weekdays to Flash
void WriteToFlash2(String filename, uint8_t WeekArray[]){   

  File f = SPIFFS.open(filename, "w");
      if (!f) {
        Serial.print("Unable To Open for writing: ");
        Serial.println (filename);
      }
       else {
        Serial.println("Writing to file: ");
        Serial.println(filename);
        f.write((uint8_t *)WeekArray, sizeof(WeekArray));
        f.close();
      }
}

int ReadFromFlash2(String filename, uint8_t WeekArray[]){
  if (spiffsActive) {
    if (SPIFFS.exists(filename)) {
      File f = SPIFFS.open(filename, "r");
      if (!f) {
        Serial.print("Unable To Open '");
        Serial.print(filename);
        Serial.println(" for Reading");
        return (0);
        } 
        else {
        Serial.print("Contents of file ");
        Serial.print(filename);
        Serial.println("'");
        f.read((uint8_t *)WeekArray, sizeof(WeekArray));
        f.close();
        delay(1000);
        return (1); 
        }
          
        }
      } 
    }

 void SetTimeZone(){
  if ((month()*100+day())<=323 or (month()*100+day())>=1029){
    timeZoneOffset =2;
   }
 }
 
