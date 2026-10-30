#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <atomic>
#include "EPD_1in54g.h"
#include "GUI_Paint.h"
#include "ImageReceiver.h"
#include "Tone.h"
#include "ButtonInput.h"
#include "SourceFormat.h"
#include "WebAssets.h"
#include "DeviceSecrets.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);
static const char* kName="Paper154G";
static constexpr char kWifiPassword[]=EPAPER_WIFI_PASSWORD;
static_assert(sizeof(kWifiPassword)>=9&&sizeof(kWifiPassword)<=64,"Set an 8-63 character Wi-Fi password in DeviceSecrets.h.");
static const char* kService="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* kRx="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char* kTx="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr int kBootPin=0,kPwrPin=18,kBatteryHoldPin=17;
static SemaphoreHandle_t receiverLock,statusLock,refreshSignal;
static ImageReceiver* receiver;
static BLECharacteristic* tx;
static uint8_t frame[epaper::kFrameBytes];
static uint8_t *sourceRgb,*uploadBuffer;
static std::atomic<bool> connected{false},connectPending{false},disconnectPending{false},rendering{false};
static std::atomic<bool> jobOk{true},imageKnown{false},sourceReady{false},sourceSaved{false};
static std::atomic<bool> ditherEnabled{true},saveSourcePending{false};
static std::atomic<int> transferEvent{0},brightness{0},appliedBrightness{0};
static std::atomic<uint32_t> jobId{0},sourceId{0};
static char lastStatus[40]="READY";
static uint32_t lastWriteMs=0,lastRefreshMs=0,lastToneChange=0;
static bool hasRefreshed=false,fsReady=false,tonePending=false,powerOffPending=false;
static Preferences storage;
static WebServer web(80);
static bool uploadAccepted=false,uploadComplete=false;
static size_t uploadBytes=0;
static String uploadError;
static ButtonInput bootButton(1000),pwrButton(2200);

static void report(const char* message){
  xSemaphoreTake(statusLock,portMAX_DELAY);
  snprintf(lastStatus,sizeof(lastStatus),"%s",message);Serial.printf("[STATUS] %s\n",message);
  tx->setValue(message);if(connected)tx->notify();
  xSemaphoreGive(statusLock);
}
class ServerCallbacks:public BLEServerCallbacks{
  void onConnect(BLEServer*)override{connected=true;connectPending=true;}
  void onDisconnect(BLEServer*)override{connected=false;disconnectPending=true;}
};
class ReceiveCallbacks:public BLECharacteristicCallbacks{
  void onWrite(BLECharacteristic* characteristic)override{
    if(xSemaphoreTake(receiverLock,pdMS_TO_TICKS(20))!=pdTRUE)return;
    if(rendering){xSemaphoreGive(receiverLock);return;}
    const String data=characteristic->getValue();
    if(!data.length()){xSemaphoreGive(receiverLock);return;}
    if(receiver->active()&&uint32_t(millis()-lastWriteMs)>30000)receiver->reset();
    const bool wasActive=receiver->active();
    const auto result=receiver->feed(reinterpret_cast<const uint8_t*>(data.c_str()),data.length());lastWriteMs=millis();
    if(result==ImageReceiver::Result::Complete)transferEvent=2;
    else if(result==ImageReceiver::Result::BadHeader)transferEvent=-1;
    else if(result==ImageReceiver::Result::BadChecksum)transferEvent=-2;
    else if(!wasActive&&receiver->active())transferEvent=1;
    xSemaphoreGive(receiverLock);
  }
};
static bool loadSource(){
  if(!fsReady)return false;
  File file=LittleFS.open("/source.rgb","r");
  if(!file||file.size()!=epaper::kRgbBytes+epaper::kSourceHeaderBytes)return false;
  uint8_t header[epaper::kSourceHeaderBytes];
  const bool read=file.read(header,sizeof(header))==sizeof(header)&&file.read(sourceRgb,epaper::kRgbBytes)==epaper::kRgbBytes;file.close();
  return read&&epaper::validSource(header,sourceRgb,epaper::kRgbBytes)&&
    (!storage.isKey("source-crc")||storage.getUInt("source-crc",0)==epaper::get32(header+8));
}
static bool saveSource(){
  if(!fsReady)return false;
  uint8_t header[epaper::kSourceHeaderBytes];epaper::sourceHeader(header,sourceRgb,epaper::kRgbBytes);
  File file=LittleFS.open("/source.tmp","w");if(!file)return false;
  const bool written=file.write(header,sizeof(header))==sizeof(header)&&file.write(sourceRgb,epaper::kRgbBytes)==epaper::kRgbBytes;
  file.flush();file.close();
  if(!written){LittleFS.remove("/source.tmp");return false;}
  return LittleFS.rename("/source.tmp","/source.rgb");
}
static bool displayFrame(){
  EPD_1IN54G_Init();EPD_1IN54G_Display(frame);EPD_1IN54G_Sleep();
  lastRefreshMs=millis();hasRefreshed=true;return EPD_1IN54G_Ok();
}
static void makeReadyCard(){
  Paint_NewImage(frame,200,200,0,EPD_1IN54G_WHITE);Paint_SetScale(4);Paint_SelectImage(frame);Paint_Clear(EPD_1IN54G_WHITE);
  Paint_DrawString_EN(10,14,"epaper badge",&Font16,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,44,"Wi-Fi: Paper154G",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,66,"http://192.168.4.1",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,100,"BOOT: lighter",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,122,"PWR: darker",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,144,"Hold BOOT: normal",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(10,173,"Wi-Fi + BLE ready",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
}
static void queueFrame(){tonePending=false;jobId++;jobOk=false;report("QUEUED");xSemaphoreGive(refreshSignal);}
static void displayTask(void*){
  for(;;){
    xSemaphoreTake(refreshSignal,portMAX_DELAY);
    if(hasRefreshed){const uint32_t elapsed=millis()-lastRefreshMs;if(elapsed<5000)vTaskDelay(pdMS_TO_TICKS(5000-elapsed));}
    const int level=brightness.load();const bool dither=ditherEnabled.load();
    if(sourceReady)epaper::quantizeRgb(sourceRgb,frame,level,dither);
    report("REFRESHING");const bool ok=displayFrame();
    if(ok){
      imageKnown=imageKnown||storage.putBool("has-image",true)>0;
      if(saveSourcePending){sourceSaved=saveSource();storage.putUInt("source-crc",epaper::crc32(sourceRgb,epaper::kRgbBytes));saveSourcePending=false;}
      appliedBrightness=level;storage.putInt("brightness",level);storage.putBool("dither",dither);
      jobOk=true;report(sourceReady&&!sourceSaved?"DONE RAM ONLY":"DONE");
    }else{jobOk=false;report("ERR DISPLAY");}
    rendering=false;
  }
}
static String statusJson(){
  xSemaphoreTake(statusLock,portMAX_DELAY);const String state=lastStatus;xSemaphoreGive(statusLock);
  String out="{\"busy\":";out+=rendering?"true":"false";
  out+=",\"ok\":";out+=jobOk?"true":"false";
  out+=",\"job\":"+String(jobId.load())+",\"status\":\""+state+"\"";
  out+=",\"ble\":";out+=connected?"true":"false";
  out+=",\"clients\":"+String(WiFi.softAPgetStationNum());
  out+=",\"has_image\":";out+=imageKnown?"true":"false";
  out+=",\"has_source\":";out+=sourceReady?"true":"false";
  out+=",\"source_saved\":";out+=sourceSaved?"true":"false";
  out+=",\"source_id\":"+String(sourceId.load());
  out+=",\"brightness\":"+String(brightness.load())+",\"applied_brightness\":"+String(appliedBrightness.load());
  out+=",\"pending\":";out+=tonePending?"true":"false";
  out+=",\"dither\":";out+=ditherEnabled?"true":"false";
  out+=",\"boot_pressed\":";out+=digitalRead(kBootPin)==LOW?"true":"false";
  out+=",\"pwr_pressed\":";out+=digitalRead(kPwrPin)==LOW?"true":"false";
  out+=",\"error\":\"";if(!jobOk&&!rendering)out+="螢幕刷新失敗，請確認供電後重試。";
  out+="\",\"version\":\"3.0\"}";return out;
}
static bool parseTone(const String& value,int& level){
  if(value!="-40"&&value!="-20"&&value!="0"&&value!="20"&&value!="40")return false;
  level=value.toInt();return true;
}
static void replyError(int code,const char* error){web.send(code,"application/json; charset=utf-8",String("{\"error\":\"")+error+"\"}");}
static void configureWeb(){
  web.on("/",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/html; charset=utf-8",WEB_INDEX);});
  web.on("/style.css",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/css; charset=utf-8",WEB_STYLE);});
  web.on("/app.mjs",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/javascript; charset=utf-8",WEB_APP);});
  web.on("/image.mjs",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/javascript; charset=utf-8",WEB_IMAGE);});
  web.on("/api/status",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send(200,"application/json; charset=utf-8",statusJson());});
  web.on("/api/source",HTTP_GET,[]{
    if(!sourceReady){replyError(404,"請先重新傳一張圖片，以啟用明暗調整。");return;}
    web.sendHeader("Cache-Control","no-store");web.send_P(200,"application/octet-stream",reinterpret_cast<const char*>(sourceRgb),epaper::kRgbBytes);
  });
  web.on("/api/brightness",HTTP_POST,[]{
    int value=0;if(!parseTone(web.arg("value"),value)){replyError(400,"明暗值必須為 -40、-20、0、20 或 40。");return;}
    xSemaphoreTake(receiverLock,portMAX_DELAY);
    if(!sourceReady){xSemaphoreGive(receiverLock);replyError(409,"請先重新傳一張圖片，以啟用明暗調整。");return;}
    if(rendering||receiver->active()||receiver->ready()){xSemaphoreGive(receiverLock);replyError(409,"裝置正在接收或刷新，請稍後再調整。");return;}
    rendering=true;brightness=value;if(web.hasArg("dither"))ditherEnabled=web.arg("dither")!="0";queueFrame();xSemaphoreGive(receiverLock);
    web.send(202,"application/json","{\"job\":"+String(jobId.load())+"}");
  });
  web.on("/api/frame",HTTP_POST,[]{
    int value=brightness.load();const bool valid=!web.hasArg("brightness")||parseTone(web.arg("brightness"),value);
    if(uploadComplete&&uploadAccepted&&valid){
      if(uploadBytes==epaper::kRgbBytes)memcpy(sourceRgb,uploadBuffer,epaper::kRgbBytes);
      else epaper::unpackRgb(uploadBuffer,sourceRgb); // Compatibility with old 10 KB clients.
      brightness=value;ditherEnabled=!web.hasArg("dither")||web.arg("dither")!="0";
      sourceReady=true;sourceSaved=false;sourceId++;saveSourcePending=true;queueFrame();
      Serial.printf("Wi-Fi source accepted: %u bytes, job %u, brightness %d\n",unsigned(uploadBytes),unsigned(jobId.load()),value);
      uploadComplete=false;uploadAccepted=false;web.send(202,"application/json","{\"job\":"+String(jobId.load())+"}");
    }else{
      if(uploadAccepted)rendering=false;
      uploadAccepted=false;uploadComplete=false;
      replyError(uploadError=="BUSY"?409:400,uploadError=="BUSY"?"裝置正在接收或刷新，請稍後重試。":"圖片或明暗資料不完整，請重新傳送。");uploadError="";
    }
  },[]{
    HTTPUpload& u=web.upload();
    if(u.status==UPLOAD_FILE_START){
      uploadBytes=0;uploadAccepted=false;uploadComplete=false;uploadError="";
      xSemaphoreTake(receiverLock,portMAX_DELAY);
      if(receiver->active()&&uint32_t(millis()-lastWriteMs)>30000)receiver->reset();
      if(rendering||receiver->active()||receiver->ready())uploadError="BUSY";else{rendering=true;uploadAccepted=true;}
      xSemaphoreGive(receiverLock);
    }else if(u.status==UPLOAD_FILE_WRITE&&uploadAccepted){
      if(u.currentSize>epaper::kRgbBytes-uploadBytes){uploadError="SIZE";uploadAccepted=false;rendering=false;}
      else{memcpy(uploadBuffer+uploadBytes,u.buf,u.currentSize);uploadBytes+=u.currentSize;}
    }else if(u.status==UPLOAD_FILE_END&&uploadAccepted){
      uploadComplete=(uploadBytes==epaper::kRgbBytes||uploadBytes==sizeof(frame))&&u.totalSize==uploadBytes;
      if(!uploadComplete){uploadError="SIZE";uploadAccepted=false;rendering=false;}
    }else if(u.status==UPLOAD_FILE_ABORTED){if(uploadAccepted)rendering=false;uploadAccepted=false;uploadError="ABORTED";}
  });
  web.onNotFound([]{web.send(404,"text/plain; charset=utf-8","找不到此頁面，請開啟 http://192.168.4.1");});web.begin();
}
static void changeBrightness(int value){
  if(!sourceReady){Serial.println("BUTTON: upload an image first");return;}
  value=epaper::clampTone(value);if(value==brightness)return;
  brightness=value;tonePending=true;lastToneChange=millis();Serial.printf("BUTTON brightness=%d\n",value);
}
static void handleButtons(){
  static bool armed=false;static uint32_t releasedAt=0;
  const bool boot=digitalRead(kBootPin)==LOW,pwr=digitalRead(kPwrPin)==LOW;
  if(!armed){if(boot||pwr)releasedAt=millis();else if(uint32_t(millis()-releasedAt)>=100)armed=true;return;}
  const auto b=bootButton.update(boot,millis()),p=pwrButton.update(pwr,millis());
  if(b==ButtonInput::ShortPress)changeBrightness(brightness+epaper::kToneStep);
  if(b==ButtonInput::LongPress)changeBrightness(0);
  if(p==ButtonInput::ShortPress)changeBrightness(brightness-epaper::kToneStep);
  if(p==ButtonInput::LongPress)powerOffPending=true;
  if(powerOffPending&&!rendering){digitalWrite(kBatteryHoldPin,LOW);powerOffPending=false;Serial.println("Battery power hold released");}
  if(tonePending&&!rendering&&uint32_t(millis()-lastToneChange)>=600){
    xSemaphoreTake(receiverLock,portMAX_DELAY);
    if(!receiver->active()&&!receiver->ready()){
      if(brightness==appliedBrightness)tonePending=false;
      else{rendering=true;queueFrame();}
    }
    xSemaphoreGive(receiverLock);
  }
}
void setup(){
  digitalWrite(kBatteryHoldPin,HIGH);pinMode(kBatteryHoldPin,OUTPUT);
  pinMode(kBootPin,INPUT_PULLUP);pinMode(kPwrPin,INPUT_PULLUP);
  DEV_Module_Init();delay(1000);Serial.println("\nepaper badge Wi-Fi + BLE v3.0");
  uint8_t* pixels=static_cast<uint8_t*>(ps_malloc(ImageReceiver::kCapacity));
  sourceRgb=static_cast<uint8_t*>(ps_malloc(epaper::kRgbBytes));uploadBuffer=static_cast<uint8_t*>(ps_malloc(epaper::kRgbBytes));
  receiverLock=xSemaphoreCreateMutex();statusLock=xSemaphoreCreateMutex();refreshSignal=xSemaphoreCreateBinary();
  if(!pixels||!sourceRgb||!uploadBuffer||!receiverLock||!statusLock||!refreshSignal){Serial.println("ERROR: memory allocation failed");while(true)delay(1000);}
  receiver=new ImageReceiver(pixels);BLEDevice::init(kName);BLEDevice::setMTU(247);
  BLEServer* server=BLEDevice::createServer();server->setCallbacks(new ServerCallbacks());BLEService* service=server->createService(kService);
  tx=service->createCharacteristic(kTx,BLECharacteristic::PROPERTY_NOTIFY|BLECharacteristic::PROPERTY_READ);
  auto* rx=service->createCharacteristic(kRx,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);rx->setCallbacks(new ReceiveCallbacks());service->start();
  auto* advertising=BLEDevice::getAdvertising();advertising->addServiceUUID(kService);advertising->setScanResponse(true);advertising->setMinPreferred(0x06);advertising->setMaxPreferred(0x12);BLEDevice::startAdvertising();
  storage.begin("epaper-ble",false);imageKnown=storage.getBool("has-image",false);
  if(!imageKnown&&storage.getBytesLength("frame")==sizeof(frame)){imageKnown=storage.putBool("has-image",true)>0;if(imageKnown)storage.remove("frame");}
  const int savedTone=storage.getInt("brightness",0);brightness=epaper::validTone(savedTone)?savedTone:0;appliedBrightness=brightness.load();ditherEnabled=storage.getBool("dither",true);
  fsReady=LittleFS.begin(true);sourceReady=loadSource();sourceSaved=sourceReady.load();if(sourceReady)sourceId=1;
  Serial.printf("Source storage: mounted=%d loaded=%d, brightness=%d\n",fsReady,sourceReady.load(),brightness.load());
  WiFi.mode(WIFI_AP);WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));WiFi.softAP(kName,kWifiPassword);configureWeb();
  if(xTaskCreate(displayTask,"epaper_display",12*1024,nullptr,1,nullptr)!=pdPASS){Serial.println("ERROR: display task allocation failed");while(true)delay(1000);}
  if(!imageKnown){rendering=true;makeReadyCard();queueFrame();}else report("READY");
  Serial.println("Wi-Fi http://192.168.4.1 | BLE Paper154G | BOOT lighter / PWR darker | USB STATUS, DEMO, CLEAR, TONE n");
}
void loop(){
  web.handleClient();handleButtons();
  if(connectPending.exchange(false))report("CONNECTED");
  if(disconnectPending.exchange(false)){xSemaphoreTake(receiverLock,portMAX_DELAY);if(!receiver->ready())receiver->reset();xSemaphoreGive(receiverLock);BLEDevice::startAdvertising();}
  const int event=transferEvent.exchange(0);if(event==1)report("RECEIVING BLE");if(event==-1)report("ERR SIZE OR FORMAT");if(event==-2)report("ERR CHECKSUM RETRY");
  xSemaphoreTake(receiverLock,portMAX_DELAY);
  if(!rendering&&receiver->active()&&uint32_t(millis()-lastWriteMs)>30000){receiver->reset();report("ERR TIMEOUT RETRY");}
  if(!rendering&&receiver->ready()){
    rendering=true;epaper::resizeRgb(*receiver,sourceRgb);receiver->reset();sourceReady=true;sourceSaved=false;sourceId++;saveSourcePending=true;ditherEnabled=true;queueFrame();
  }
  xSemaphoreGive(receiverLock);
  static char line[32];static uint8_t n=0;
  while(Serial.available()){
    const char c=Serial.read();
    if(c=='\n'||c=='\r'){
      line[n]=0;
      if(!strcmp(line,"STATUS"))Serial.println(statusJson());
      else if(!strncmp(line,"TONE ",5)){int value=0;if(parseTone(String(line+5),value))changeBrightness(value);}
      else if(!strcmp(line,"DEMO")||!strcmp(line,"CLEAR")){
        xSemaphoreTake(receiverLock,portMAX_DELAY);
        if(!rendering&&!receiver->active()&&!receiver->ready()){
          rendering=true;sourceReady=false;sourceSaved=false;sourceId++;saveSourcePending=false;LittleFS.remove("/source.rgb");
          if(!strcmp(line,"DEMO"))makeReadyCard();else memset(frame,0x55,sizeof(frame));queueFrame();
        }
        xSemaphoreGive(receiverLock);
      }
      n=0;
    }else if(n<sizeof(line)-1)line[n++]=c;
  }
  delay(2);
}
