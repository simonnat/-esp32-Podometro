/*
  SDWebServer - Example WebServer with SD Card backend for esp8266

  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Have a FAT Formatted SD Card connected to the SPI port of the ESP8266
  The web root is the SD Card root folder
  File extensions with more than 3 charecters are not supported by the SD Library
  File Names longer than 8 charecters will be truncated by the SD library, so keep filenames shorter
  index.htm is the default index (works on subfolders as well)

  upload the contents of SdRoot to the root of the SDcard and access the editor by going to http://esp8266sd.local/edit

*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include "FS.h"


#include"index.h"



File myFile;
const char* ssid = "Podometro";
const char* password = "Podometro";
const char* host = "esp32sd";

//////////////////Variabili globali////////////////
WebServer server(80);
int sensoriMap[]={33,32,35,34,39,36};
long peso[6]={0};
int prevTime=0, dayprev=0;
int passo=0;
static bool hasSD = false;
File uploadFile;
bool stato=false;
//////////////////////////Fine/////////////////////////////


void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  }

  File dataFile = SD.open(path.c_str());
  if (dataFile.isDirectory()) {
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile) {
    return false;
  }

  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    Serial.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void handleFileUpload() {
  if (server.uri() != "/edit") {
    return;
  }
 
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (SD.exists((char *)upload.filename.c_str())) {
      SD.remove((char *)upload.filename.c_str());
    }
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    Serial.print("Upload: START, filename: "); Serial.println(upload.filename);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
    Serial.print("Upload: WRITE, Bytes: "); Serial.println(upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    Serial.print("Upload: END, Size: "); Serial.println(upload.totalSize);
  }
}

void deleteRecursive(String path) {
  File file = SD.open((char *)path.c_str());
  if (!file.isDirectory()) {
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) {
    File entry = file.openNextFile();
    if (!entry) {
      break;
    }
    String entryPath = path + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
}

void handleCreate() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }

  if (path.indexOf('.') > 0) {
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if (file) {
      file.write(0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}

void printDirectory() {
  if (!server.hasArg("dir")) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg("dir");
  if (path != "/" && !SD.exists((char *)path.c_str())) {
    return returnFail("BAD PATH");
  }
  File dir = SD.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String output;
    if (cnt > 0) {
      output = ',';
    }

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.name();
    output += "\"";
    output += "}";
    server.sendContent(output);
    entry.close();
  }
  server.sendContent("]");
  dir.close();
}

void handleNotFound() {
  if (hasSD && loadFromSdCard(server.uri())) {
    return;
  }
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.print(message);
}


///////////////////Funzione che invia pagina scansione in tempo reale///////////////////////////////
void handleScan(){
  String s=scansione;
  server.send(200, "text/html",s);
  
  }


///////////////////Funzione che invia pagina iniziale/////////////////////////////////
void handleRoot(){
  String s=page;
  server.send(200, "text/html",s);
  
  }


///////////////////////Funzione che aggiorna vista in tempo reale///////////////////////////
void handleUp(){
  String s=sendTemplate;
  s.replace("@@peso1@@",String(peso[0]));
  s.replace("@@peso2@@",String(peso[1]));
  s.replace("@@peso3@@",String(peso[2]));
  s.replace("@@peso4@@",String(peso[3]));
  s.replace("@@peso5@@",String(peso[4]));
  s.replace("@@peso6@@",String(peso[5]));
  server.send(200, "text/XML",s);
  
  }

void setup(void) {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.print("\n");
  WiFi.softAP(ssid, password);
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
 
  uint8_t i = 0;

    if(!SD.begin(5)){
        Serial.println("Card Mount Failed");
        return;
    }
  
  if(SD.remove("/DATA.txt"))
    Serial.println("File deleted ok");
  myFile=SD.open("/DATA.txt", FILE_WRITE);
if (myFile) {
    Serial.println("File opened ok");
    // print the headings for our data
    myFile.println("Giorno,Passi,Sens1,Sens2,Sens3,Sens4,Sens5,Sens6,Sens7,Sens8");
  }else
 Serial.println("File not open");
  myFile.close();

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
  }


  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);
  //////////////////////definizione di link interni///////////////////////////
  server.on("/",handleRoot);
  server.on("/Scansione",handleScan);
  server.on("/Up",handleUp);
  /////////////////////////Fine///////////////////////////////////
  
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  if (SD.begin(SS)) {
    Serial.println("SD Card initialized.");
    hasSD = true;
  }
}


void loop(void) {
  server.handleClient();
  
 sensoriPeso();
   
 int now=millis();
  if(now-prevTime>=500){ //////////////////Ogni 500ms scrivo log su sd///////////////////////
  loggingTime();
  loggingWeight();
  prevTime=millis();
  }
  
  contapassi();
}




void sensoriPeso(){
     int i,j, media[6]={0};
  
   int sensore[6];
  // read the input on analog pin 0:
  for(i=0;i<100;i++){
    for(j=0;j<6;j++){
      sensore[j] = analogRead(sensoriMap[j]);
      
      media[j]+=sensore[j];
    }
  
  }
  for(j=0;j<6;j++)
    sensore[j]=media[j]/100;

   
  for(j=0;j<6;j++){
    peso[j]=taratura(sensore[j]);
 
}
  }




void contapassi(){
  if(stato==false&&peso[0]>1000&&peso[1]>1000&&peso[2]>1000&&peso[3]>1000){
  stato=true;
  passo++;
  }
  if(stato==true&&peso[0]<100&&peso[1]<100&&peso[2]<100&&peso[3]<100)
   stato=false;
  }

//////////////////////////funzione che calibra sensori peso/////////////////////
float taratura(float x)
{
  float y;
  if(x<1300.0) y=(0.38)*x + (6.0);
  if(x>=1300.0 && x<2600.0) y=(0.38)*x + (12.0);
  if(x>=2600.0 && x<3100.0) y=(1.0)*x + (-1600.0);
  if(x>=3100.0 && x<3200.0) y=(5.0)*x + (-14000.0);
  if(x>=3200.0 && x<3500.0) y=(1.67)*x + (-3345.0);
  if(x>=3500.0 && x<3600.0) y=(5.0)*x + (-15000.0);
  if(x>=3600.0 && x<3700.0) y=(10.0)*x + (-33000.0);
  if(x>=3700.0) y=(53.33)*x + (-193320.0);
  return y;
}


//////////////////log dei giorni//////////////////////////////////
void loggingTime() {
  myFile = SD.open("/DATA.txt", FILE_APPEND);
  if (myFile) {
    long seconds = millis() / 1000;
    long minutes = seconds / 60;
    long hours = minutes / 60;
    int days = floor(hours / 24);
    myFile.print(days, DEC);
    myFile.print(',');
    if(days!=dayprev){
  passo=0;
  dayprev=days;
 }
}
  myFile.close();  
}


////////////////////////log dei pesi//////////////////////////////
void loggingWeight() {
 int i=0;
  
  myFile = SD.open("/DATA.txt", FILE_APPEND);
  if (myFile) {
    Serial.println("open with success");
    myFile.print(passo);
    for(i=0;i<6;i++){
      myFile.print(",");
      myFile.print(peso[i]);
    }
    myFile.println(" ");
  }
  myFile.close();
}






   

