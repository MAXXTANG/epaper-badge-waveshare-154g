#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <atomic>
#include "EPD_1in54g.h"
#include "GUI_Paint.h"
#include "ImageReceiver.h"
#include "Palette.h"
#include "WebAssets.h"
#include "DeviceSecrets.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);
static const char* kName="Paper154G";
static constexpr char kWifiPassword[]=EPAPER_WIFI_PASSWORD;
static_assert(sizeof(kWifiPassword)>=9 && sizeof(kWifiPassword)<=64,
              "Set an 8-63 character Wi-Fi password in DeviceSecrets.h.");
static const char* kService="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* kRx="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char* kTx="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static SemaphoreHandle_t receiverLock,statusLock,refreshSignal;
static ImageReceiver* receiver;
static BLECharacteristic* tx;
static uint8_t frame[epaper::kFrameBytes],uploadBuffer[epaper::kFrameBytes];
static std::atomic<bool> connected{false},connectPending{false},disconnectPending{false},rendering{false};
static std::atomic<int> transferEvent{0};
static std::atomic<uint32_t> jobId{0};
static std::atomic<bool> jobOk{true};
static std::atomic<bool> imageKnown{false};
static char lastStatus[40]="READY";
static uint32_t lastWriteMs=0,lastRefreshMs=0;
static bool hasRefreshed=false;
static Preferences storage;
static WebServer web(80);
static bool uploadAccepted=false,uploadQueued=false;
static size_t uploadBytes=0;
static String uploadError;

static void report(const char* message) {
  xSemaphoreTake(statusLock,portMAX_DELAY);
  snprintf(lastStatus,sizeof(lastStatus),"%s",message);
  Serial.printf("[STATUS] %s\n",message);
  tx->setValue(message);
  if(connected)tx->notify();
  xSemaphoreGive(statusLock);
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {connected=true;connectPending=true;}
  void onDisconnect(BLEServer*) override {connected=false;disconnectPending=true;}
};
class ReceiveCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    if(xSemaphoreTake(receiverLock,pdMS_TO_TICKS(20))!=pdTRUE)return;
    // Recheck after locking: an HTTP upload can claim the frame concurrently.
    if(rendering){xSemaphoreGive(receiverLock);return;}
    const String data=characteristic->getValue();
    if(!data.length()){xSemaphoreGive(receiverLock);return;}
    if(receiver->active()&&uint32_t(millis()-lastWriteMs)>30000)receiver->reset();
    const bool wasActive=receiver->active();
    const auto result=receiver->feed(reinterpret_cast<const uint8_t*>(data.c_str()),data.length());
    lastWriteMs=millis();
    if(result==ImageReceiver::Result::Complete)transferEvent=2;
    else if(result==ImageReceiver::Result::BadHeader)transferEvent=-1;
    else if(result==ImageReceiver::Result::BadChecksum)transferEvent=-2;
    else if(!wasActive&&receiver->active())transferEvent=1;
    xSemaphoreGive(receiverLock);
  }
};

static bool displayFrame() {
  EPD_1IN54G_Init();EPD_1IN54G_Display(frame);EPD_1IN54G_Sleep();
  lastRefreshMs=millis();hasRefreshed=true;
  return EPD_1IN54G_Ok();
}

static void makeReadyCard() {
  Paint_NewImage(frame,200,200,0,EPD_1IN54G_WHITE);Paint_SetScale(4);
  Paint_SelectImage(frame);Paint_Clear(EPD_1IN54G_WHITE);
  Paint_DrawString_EN(12,16,"Wi-Fi + BLE",&Font16,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(12,48,"Paper154G",&Font16,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(12,82,"Wi-Fi: Paper154G",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(12,104,"Use your Wi-Fi key",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(12,130,"http://192.168.4.1",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawString_EN(12,151,"BLE: Bluefruit Connect",&Font12,EPD_1IN54G_WHITE,EPD_1IN54G_BLACK);
  Paint_DrawRectangle(12,180,65,192,EPD_1IN54G_BLACK,DOT_PIXEL_1X1,DRAW_FILL_FULL);
  Paint_DrawRectangle(73,180,126,192,EPD_1IN54G_RED,DOT_PIXEL_1X1,DRAW_FILL_FULL);
  Paint_DrawRectangle(134,180,187,192,EPD_1IN54G_YELLOW,DOT_PIXEL_1X1,DRAW_FILL_FULL);
}

static void queueFrame() {jobId++;jobOk=false;report("QUEUED");xSemaphoreGive(refreshSignal);}

static void displayTask(void*) {
  for(;;){
    xSemaphoreTake(refreshSignal,portMAX_DELAY);
    if(hasRefreshed){const uint32_t elapsed=millis()-lastRefreshMs;if(elapsed<5000)vTaskDelay(pdMS_TO_TICKS(5000-elapsed));}
    report("REFRESHING");
    const bool ok=displayFrame();
    if(ok){
      // The physical e-paper retains its image without power. Persist only the
      // first-image marker, avoiding repeated 10 KB blob updates in a small NVS.
      const bool saved=imageKnown || storage.putBool("has-image",true)>0;
      imageKnown=saved;jobOk=true;report(saved?"DONE":"DONE MARKER FAILED");
    }
    else{jobOk=false;report("ERR DISPLAY");}
    // Publish completion after all frame reads, storage writes and state updates.
    rendering=false;
  }
}

static String statusJson() {
  xSemaphoreTake(statusLock,portMAX_DELAY);const String state=lastStatus;xSemaphoreGive(statusLock);
  String result="{\"busy\":";result+=rendering?"true":"false";
  result+=",\"ok\":";result+=jobOk?"true":"false";
  result+=",\"job\":"+String(jobId.load())+",\"status\":\""+state+"\"";
  result+=",\"ble\":";result+=connected?"true":"false";
  result+=",\"clients\":"+String(WiFi.softAPgetStationNum());
  result+=",\"error\":\"";
  if(!jobOk&&!rendering)result+="螢幕刷新失敗，請確認供電後重試。";
  result+="\",\"has_image\":";result+=imageKnown?"true":"false";
  result+=",\"version\":\"2.1\"}";return result;
}

static void configureWeb() {
  web.on("/",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send_P(200,"text/html; charset=utf-8",WEB_INDEX);});
  web.on("/style.css",HTTP_GET,[]{web.send_P(200,"text/css; charset=utf-8",WEB_STYLE);});
  web.on("/app.mjs",HTTP_GET,[]{web.send_P(200,"text/javascript; charset=utf-8",WEB_APP);});
  web.on("/image.mjs",HTTP_GET,[]{web.send_P(200,"text/javascript; charset=utf-8",WEB_IMAGE);});
  web.on("/api/status",HTTP_GET,[]{web.sendHeader("Cache-Control","no-store");web.send(200,"application/json; charset=utf-8",statusJson());});
  web.on("/api/frame",HTTP_POST,[]{
    if(uploadQueued){uploadQueued=false;uploadAccepted=false;web.send(202,"application/json","{\"job\":"+String(jobId.load())+"}");}
    else{
      if(uploadAccepted){rendering=false;uploadAccepted=false;}
      web.send(uploadError=="BUSY"?409:400,"application/json; charset=utf-8",
        uploadError=="BUSY"?"{\"error\":\"裝置正在接收或刷新，請稍後重試。\"}":"{\"error\":\"圖片資料不完整，請重新傳送。\"}");
      uploadError="";
    }
  },[]{
    HTTPUpload& upload=web.upload();
    if(upload.status==UPLOAD_FILE_START){
      uploadBytes=0;uploadAccepted=false;uploadQueued=false;uploadError="";
      xSemaphoreTake(receiverLock,portMAX_DELAY);
      if(receiver->active()&&uint32_t(millis()-lastWriteMs)>30000)receiver->reset();
      if(rendering||receiver->active()||receiver->ready())uploadError="BUSY";
      else{rendering=true;uploadAccepted=true;}
      xSemaphoreGive(receiverLock);
    }else if(upload.status==UPLOAD_FILE_WRITE&&uploadAccepted){
      if(upload.currentSize>sizeof(uploadBuffer)-uploadBytes){uploadError="SIZE";uploadAccepted=false;rendering=false;}
      else{memcpy(uploadBuffer+uploadBytes,upload.buf,upload.currentSize);uploadBytes+=upload.currentSize;}
    }else if(upload.status==UPLOAD_FILE_END&&uploadAccepted){
      if(uploadBytes!=sizeof(frame)||upload.totalSize!=sizeof(frame)){uploadError="SIZE";uploadAccepted=false;rendering=false;}
      else{memcpy(frame,uploadBuffer,sizeof(frame));uploadQueued=true;queueFrame();Serial.printf("Wi-Fi image accepted: %u bytes, job %u\n",unsigned(uploadBytes),unsigned(jobId.load()));}
    }else if(upload.status==UPLOAD_FILE_ABORTED){
      if(uploadAccepted&&!uploadQueued)rendering=false;
      uploadAccepted=false;uploadError="ABORTED";
    }
  });
  web.onNotFound([]{web.send(404,"text/plain; charset=utf-8","找不到此頁面，請開啟 http://192.168.4.1");});web.begin();
}

void setup() {
  DEV_Module_Init();delay(1000);
  Serial.println("\nPaper154G Wi-Fi + BLE photo receiver v2.1");Serial.printf("PSRAM: %u bytes\n",ESP.getPsramSize());
  uint8_t* pixels=static_cast<uint8_t*>(ps_malloc(ImageReceiver::kCapacity));
  receiverLock=xSemaphoreCreateMutex();statusLock=xSemaphoreCreateMutex();refreshSignal=xSemaphoreCreateBinary();
  if(!pixels||!receiverLock||!statusLock||!refreshSignal){Serial.println("ERROR: memory allocation failed");while(true)delay(1000);}
  receiver=new ImageReceiver(pixels);
  BLEDevice::init(kName);BLEDevice::setMTU(247);
  BLEServer* server=BLEDevice::createServer();server->setCallbacks(new ServerCallbacks());
  BLEService* service=server->createService(kService);
  tx=service->createCharacteristic(kTx,BLECharacteristic::PROPERTY_NOTIFY|BLECharacteristic::PROPERTY_READ);
  BLECharacteristic* rx=service->createCharacteristic(kRx,BLECharacteristic::PROPERTY_WRITE|BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new ReceiveCallbacks());service->start();
  BLEAdvertising* advertising=BLEDevice::getAdvertising();advertising->addServiceUUID(kService);
  advertising->setScanResponse(true);advertising->setMinPreferred(0x06);advertising->setMaxPreferred(0x12);BLEDevice::startAdvertising();
  WiFi.mode(WIFI_AP);WiFi.softAPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  const bool apOk=WiFi.softAP(kName,kWifiPassword);Serial.printf("Wi-Fi AP: %s, IP: %s\n",apOk?"ready":"FAILED",WiFi.softAPIP().toString().c_str());
  configureWeb();storage.begin("epaper-ble",false);
  imageKnown=storage.getBool("has-image",false);
  if(!imageKnown&&storage.getBytesLength("frame")==sizeof(frame)){
    imageKnown=storage.putBool("has-image",true)>0;
    if(imageKnown)storage.remove("frame");
  }
  if(xTaskCreate(displayTask,"epaper_display",8192,nullptr,1,nullptr)!=pdPASS){Serial.println("ERROR: display task allocation failed");while(true)delay(1000);}
  if(!imageKnown){rendering=true;makeReadyCard();queueFrame();}else report("READY");
  Serial.println("Wi-Fi http://192.168.4.1 | BLE Paper154G | Commands: STATUS, DEMO, CLEAR");
}

void loop() {
  web.handleClient();
  if(connectPending.exchange(false))report("CONNECTED");
  if(disconnectPending.exchange(false)){
    xSemaphoreTake(receiverLock,portMAX_DELAY);if(!receiver->ready())receiver->reset();xSemaphoreGive(receiverLock);
    BLEDevice::startAdvertising();Serial.println("BLE disconnected; advertising restarted");
  }
  const int event=transferEvent.exchange(0);
  if(event==1)report("RECEIVING BLE");if(event==-1)report("ERR SIZE OR FORMAT");if(event==-2)report("ERR CHECKSUM RETRY");
  xSemaphoreTake(receiverLock,portMAX_DELAY);
  if(!rendering&&receiver->active()&&uint32_t(millis()-lastWriteMs)>30000){receiver->reset();report("ERR TIMEOUT RETRY");}
  if(!rendering&&receiver->ready()){
    rendering=true;Serial.printf("BLE complete: %ux%u RGB%u\n",receiver->width(),receiver->height(),receiver->depth());
    epaper::convert(*receiver,frame);receiver->reset();queueFrame();
  }
  xSemaphoreGive(receiverLock);
  static char line[32];static uint8_t n=0;
  while(Serial.available()){
    const char c=Serial.read();
    if(c=='\n'||c=='\r'){
      line[n]=0;
      if(!strcmp(line,"STATUS"))Serial.println(statusJson());
      else if(!strcmp(line,"DEMO")||!strcmp(line,"CLEAR")){
        xSemaphoreTake(receiverLock,portMAX_DELAY);
        if(!rendering&&!receiver->active()&&!receiver->ready()){
          rendering=true;if(!strcmp(line,"DEMO"))makeReadyCard();else memset(frame,0x55,sizeof(frame));queueFrame();
        }
        xSemaphoreGive(receiverLock);
      }
      n=0;
    }else if(n<sizeof(line)-1)line[n++]=c;
  }
  delay(2);
}
