//------是否开启打印-----------------
#define Use_Serial Serial

/*
   给stc12的命令
   1.wifi连接成功：“WOk”
   2.wifi断开 ：“Wfa”
   3.http发送成功：“ZOk”
*/
//-----------------------------A-1 库引用开始-----------------//
#include <Arduino.h>
#include <string.h>
#include <math.h>  // 数学工具库
#include <EEPROM.h>// eeprom  存储库

// WIFI库
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
//MQTT库
#include <PubSubClient.h>
//-------客户端---------
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
//---------服务器---------
#include <ESP8266WebServer.h>
#include <FS.h>

//ESP获取自身ID
#ifdef ESP8266
extern "C" {
#include "user_interface.h"   //含有system_get_chip_id（）的库
}
#endif

//-----------------------------A-1 库引用结束-----------------//




//-----------------------------A-2 变量声明开始-----------------//
int PIN_Led = D4;  //D4 是8266上的led灯的引脚 对应IDE：17号引脚
bool PIN_Led_State = 0; //
ESP8266WebServer server ( 80 );
ESP8266WiFiMulti WiFiMulti;
//储存SN号
String SN; //system_get_chip_id()的返回值

/*----------------WIFI账号和密码--------------*/
char ssid[50] = "";    // Enter SSID here
char password[50] = "";  //Enter Password here

#define DEFAULT_STASSID "bu_neng_yong"
#define DEFAULT_STAPSW  "yu721215"
#define DEFAULT_IP_DOMAIN ""
#define mqtt_server "mqtt.iotly.cn"
#define mqtt_sub_topic "yuhaodong"  //订阅和发布的topic一般不同，要不然会自己发的自己也接
#define mqtt_publish_topic "yunduan"
#define mqtt_publish "ok"
int port = 1883;

// 用于存上次的WIFI和密码
#define MAGIC_NUMBER 0xAA
struct config_type
{
  char stassid[50];
  char stapsw[50];
  char ip_domain[50];
  uint8_t magic;
};
config_type config_wifi;


//--------------HTTP请求------------------
struct http_request {
  String  Referer;
  char host[20];
  int httpPort = 80;
  String host_ur ;

  String usr_name;//账号
  String usr_pwd;//密码

  String postDate;

};

String ApiKey = "";
String dataRequestAddress = "/index/Index/swipe";     /*   /Controller/function  */
/*
  组成http的地址 访问到哪个控制器 方法 http://ip/Controller/function?machine=*********&id=********&apikey=**********
  例子：GET 请求 ：  http://127.0.0.1/Index/swipe?machine=*********&id=********&apikey=**********
*/

String SerialInputString = "";         //串口通讯 a string to hold incoming data
boolean SerialStringComplete = false;  // 串口数据是否传输完成 whether the string is complete
//-----------------------------A-2 变量声明结束-----------------//


//-----------------------------A-3 函数声明开始-----------------//
void get_espid();
void LED_Int();
void wifi_Init();
void saveConfig();
void loadConfig();
void SET_AP();
void Server_int();
void handleMain();
void handleWifi();
void handleTest();
void handleNotFound();
String getContentType(String filename);
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
//mqtt客户端初始化
WiFiClient espClient;
PubSubClient client(espClient);

//---------------------------A-3 函数声明结束------------------------//


//-----------------------------A-3 函数-----------------//
//3-1管脚初始化
void get_espid() {
  SN = (String )system_get_chip_id();  //查询芯片ID
  Use_Serial.println(SN);  //串口输出芯片出场ID

}
void LED_Int() {
  pinMode(PIN_Led, OUTPUT); //D4 是8266上的led灯的引脚 对应IDE：17号引脚
}

//3-2WIFI初始化
// 连接wifi
// D4 或 esp8266自带led 慢闪(每秒一次) 连不上就一直连 当通过AP接入更换密码后将重新启动wifi_Init()   不知道在连接wifi过程中是否还提供AP服务
void wifi_Init() {
  Use_Serial.println("Connecting to ");

  Use_Serial.println(config_wifi.stassid);//

  WiFi.begin(config_wifi.stassid, config_wifi.stapsw);
  int cycleTimes = 0;
  while (WiFi.status() != WL_CONNECTED) {
    server.handleClient();

    ESP.wdtFeed(); //喂狗,资源释放  ESP.wdtDisable();
    delay(500);

    Use_Serial.print(".");
    PIN_Led_State = !PIN_Led_State;  //使状态灯D4闪烁
    digitalWrite(PIN_Led, PIN_Led_State); //( D4,状态);
    cycleTimes++;
    if (cycleTimes == 20) {
      Use_Serial.println("");//换一个行方便串口调试 一行10s
      cycleTimes = 0;
    }

  }

  Use_Serial.println("--------------WIFI CONNECT!-------------  ");
  Use_Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str()); //c_str() 函数返回一个指向正规C字符串的指针 返回字串地址
  Use_Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());//////////////////////////////////////////////////////////////////////
  Use_Serial.print("Use this URL to connect: ");
  Use_Serial.println("http://" + WiFi.localIP());
  Use_Serial.println("----------------------------------------  ");
  digitalWrite(PIN_Led, LOW); //( D4,LOW); 板载LED电平拉低才会亮  连接成功wifi常亮

  //告诉stc12 wifi连接成功：“WOk”
  Serial.println("WOk");
  Serial.println("ZOk");

}

void callback(char* topic, byte* payload, unsigned int length) {////用于接收数据
  Serial.print("Message arrived [");
  Serial.print(topic);   // 打印主题信息
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]); // 打印主题内容
  }
  Serial.println();

}
void reconnect() {//等待，直到连接上mqtt服务器
  while (!client.connected()) {//如果没有连接上
    if (client.connect("yunduan") + random(999999999)) { //接入时的用户名，尽量取一个很不常用的用户名
      client.subscribe(mqtt_sub_topic);//接收外来的数据时的intopic
    } else {
      Serial.print("failed, rc=");//连接失败
      Serial.print(client.state());//重新连接
      Serial.println(" try again in 2 seconds");//延时5秒后重新连接
      delay(2000);
    }
  }
}
/*
  3-/2 保存参数到EEPROM(存入)
*/
void saveConfig()
{
  Use_Serial.println("Save config!");
  Use_Serial.print("stassid:");
  Use_Serial.println(config_wifi.stassid);
  Use_Serial.print("stapsw:");
  Use_Serial.println(config_wifi.stapsw);
  Use_Serial.print("ip or domain:");
  Use_Serial.println(config_wifi.ip_domain);
  EEPROM.begin(1024); //ROM开始1024字节
  uint8_t *p = (uint8_t*)(&config_wifi);
  for (int i = 0; i < sizeof(config_wifi); i++)


  {
    EEPROM.write(i, *(p + i));
  }
  EEPROM.commit();//提交
}


/*
   从EEPROM加载参数(读出)
*/
void loadConfig()
{
  EEPROM.begin(1024); //ROM开始1024字节
  uint8_t *p = (uint8_t*)(&config_wifi);
  for (int i = 0; i < sizeof(config_wifi); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.commit();
  //出厂自带
  if (config_wifi.magic != MAGIC_NUMBER)
  {
    strcpy(config_wifi.stassid, DEFAULT_STASSID);
    strcpy(config_wifi.stapsw, DEFAULT_STAPSW);
    strcpy(config_wifi.ip_domain, DEFAULT_IP_DOMAIN);
    config_wifi.magic = MAGIC_NUMBER;
    saveConfig();
    Use_Serial.println("Restore config!");
  }
  Use_Serial.println(" ");
  Use_Serial.println("-----Read config-----");
  Use_Serial.print("stassid:");
  Use_Serial.println(config_wifi.stassid);
  Use_Serial.print("stapsw:");
  Use_Serial.println(config_wifi.stapsw);
  Use_Serial.print("ip or domain:");
  Use_Serial.println(config_wifi.ip_domain);
  Use_Serial.println("-------------------");

  //  ssid=String(config.stassid);
  //  password=String(config.stapsw);
}


//3-3ESP8266建立无线热点
void SET_AP() {
  Use_Serial.println("AP begin\n");
  // 设置内网
  IPAddress softLocal(192, 168, 4, 1); // 1 设置内网WIFI IP地址  (连接ESP热点后默认打开网页输入192.168.4.1即可配置WiFi)
  IPAddress softGateway(192, 168, 4, 1);
  IPAddress softSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(softLocal, softGateway, softSubnet);

  String apName = ("ESP_" + (String)ESP.getChipId()); // 2 设置WIFI热点名称 "ESP_"+(String)ESP.getChipId()
  const char *softAPName = apName.c_str();
  WiFi.softAP(softAPName, "12345678");      // 3创建wifi  名称 +密码 adminadmin

  Use_Serial.print("softAPName: ");  // 5输出WIFI 名称
  Use_Serial.println(apName);

  IPAddress myIP = WiFi.softAPIP();  // 4输出创建的WIFI IP地址
  Use_Serial.print("AP IP address: ");
  Use_Serial.println(myIP);

}

//3-4ESP建立网页服务器
void Server_int() {
  server.on ("/", handleMain); // 绑定‘/’地址到handleMain方法处理 ----  返回主页面 一键配网页面
  server.on ("/wifi", HTTP_GET, handleWifi); // 绑定‘/wifi’地址到handlePWIFI方法处理  --- 重新配网请求
  server.on ("/test", HTTP_GET, handleTest);
  server.onNotFound ( handleNotFound ); // NotFound处理
  server.begin();   //应该会有个server.close()  server.stop()
  Use_Serial.println ( "HTTP server started" );

}

//3-5-1 网页服务器主页
/*  返回信息给浏览器（状态码，Content-type， 内容）
    这里是访问当前设备ip直接返回一个String
  /*  test :   server.send(200, "text/html", "<h1>You are connected to handleMain </h1>");*/
void handleMain() {

  Use_Serial.print("\r\n handleMain GET file index.html");

  File file = SPIFFS.open("/index.html", "r");

  size_t sent = server.streamFile(file, "text/html");

  file.close();

  return;


}

//3-5-3 网页修改普通家庭WIFI连接账号密码 还有域名或ip
/* WIFI更改处理
   访问地址为htp://192.162.xxx.xxx/wifi?config=on&name=Testwifi&pwd=123456&ip_domain=medical.iotl.net
  根据wifi进入 WIFI数据处理函数
  根据config的值来进行 on
  根据name的值来进行  wifi名字传输
  根据pwd的值来进行   wifi密码传输
*/
void handleWifi() {

  if (server.hasArg("config")) { // 请求中是否包含有a的参数

    String config = server.arg("config"); // 获得a参数的值
    String wifiname;
    String wifipwd;
    String ipDomain;

    if (config == "on") { // action=on   " on config "

      if (server.hasArg("name")) { // 请求中是否包含有a的参数
        wifiname = server.arg("name"); // 获得a参数的值
      }
      if (server.hasArg("pwd")) { // 请求中是否包含有a的参数
        wifipwd = server.arg("pwd"); // 获得a参数的值
      }
      if (server.hasArg("ip_domain")) { // 请求中是否包含有a的参数
        ipDomain = server.arg("ip_domain"); // 获得a参数的值
      }

      String backSerial = "Set Wifiname: " + wifiname  + "/r/n wifipwd: " + wifipwd + "/r/n ip_domain: " + ipDomain; // 用于网页返回信息
      Use_Serial.println ( backSerial ); // 串口打印给电脑

      String backHtml = "<br>wifi设置成功！<br>wifi名称：" + wifiname  + "<br>wifi密码: " + wifipwd + "<br>域名或ip: " + ipDomain; // 用于串口返回信息
      server.send ( 200, "text/html", backHtml); // 网页返回给手机提示
      // wifi连接开始

      wifiname.toCharArray(config_wifi.stassid, 50);    // 从网页得到的 WIFI名
      wifipwd.toCharArray(config_wifi.stapsw, 50);  //从网页得到的 WIFI密码
      ipDomain.toCharArray(config_wifi.ip_domain, 50);  //从网页得到的 ip 域名

      saveConfig();
      wifi_Init();
      return;

    } else if (config == "off") { // a=off
      //网页请求主动关闭AP
      //server.send ( 200, "text/html", "Server closed");
      //server.stop();
      //Use_Serial.printf("Server Closed");
      //return;
    }  else if (config == "info") { //action is info 获取配置信息
      //准备返回信息
      String tempssid = "";
      String tempstapsw = "";
      String tempip = "";
      for (int i = 0; i < 50; i++) {
        if (config_wifi.stassid[i] == 0) {
          break;
        }
        tempssid += String(config_wifi.stassid[i]);
      }
      for (int i = 0; i < 50; i++) {
        if (config_wifi.stapsw[i] == 0) {
          break;
        }
        tempstapsw += String(config_wifi.stapsw[i]);
      }
      for (int i = 0; i < 50; i++) {
        if (config_wifi.ip_domain[i] == 0) {
          break;
        }
        tempip += String(config_wifi.ip_domain[i]);
      }
      String  backHtml = "wifi名称：" + tempssid  + "<br>wifi密码: " + tempstapsw + "<br>域名或ip: " + tempip;
      server.send ( 200, "text/html", backHtml );
      return;
    }
    server.send ( 200, "text/html", "unknown action"); return;
  }
  server.send ( 200, "text/html", "action no found");  return;

}

//3-5-4 网页测试
void handleTest() {
  server.send(200, "text/html", "ESP http test"); return ;
}

//3-5-5 网页没有对应请求如何处理
void handleNotFound() {
  String path = server.uri();
  Use_Serial.print("\r\n Error ! load url:");
  Use_Serial.println(path);
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
    return;
  }
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }
  server.send ( 404, "text/plain", message );
}


// 4 解析请求的文件
/**
   根据文件后缀获取html协议的返回内容类型
*/
String getContentType(String filename) {

  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

//5串口 + http上传:
//5-1.串口信息获取
void nodeSerialEvent() {
  while (Use_Serial.available()) {
    // get the new byte:
    char inChar = (char)Use_Serial.read();
    // add it to the SerialInputString:
    SerialInputString += inChar;

    // serial: " your input string is:  daf4654asdf6546afsd\n "
    //串口通讯输出回车中只有一个\n  所以 以一个\n为 结束

    if (inChar == '\n') {
      SerialStringComplete = true;
    }
  }
}

//5-2.字符串HTTP发送
int httpSendStr(String str) {
  //此函数执行的条件是网络连接正常
  /*
     Return int httpCode    200 400 500 ...
  */
  Use_Serial.println("httpSend : " + str);

  //已经判断wifi连接成功：WiFiMulti.run() == WL_CONNECTED
  HTTPClient http;

  String tempIpStr = "";
  for (int i = 0; i < 50; i++) {
    if (config_wifi.ip_domain[i] == 0) {
      break;
    }
    tempIpStr += String(config_wifi.ip_domain[i]);
  }
  Use_Serial.print("[HTTP] begin...\n");
  Use_Serial.print("http://" + tempIpStr + str);
  // configure traged server and url
  http.begin("http://" + tempIpStr + str); //HTTP

  Use_Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    Use_Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Use_Serial.println(payload);
    }
  } else {
    Use_Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  return httpCode;

}

//5-3.串口参数提取并发送http
void receiveSendHttp() {
  //stc12 发给我的是：action-m=******;id=*****\n
  if (SerialStringComplete) {
    //Serial.println("Your input string is:" + SerialInputString); //输入字符串显示
    //处理字符串：
    //if有"action"字符串
    //在例子:action-m=5asdf4a6;id=3141\n  中 indexOf（m）=7  indexOf（id）=18  indexOf（\n）=25
    //Serial.print(SerialInputString); //其中有回车了

    if ( (SerialInputString.indexOf("action") != -1) || (SerialInputString.indexOf("ction") != -1) || (SerialInputString.indexOf("tion") != -1)  ) {
      delay(200);
      int indexOfm = SerialInputString.indexOf("m");
      int indexOfSemicolon = SerialInputString.indexOf(";"); //第一个分号的索引
      int indexOfId = SerialInputString.indexOf("id");
      int indexOfReturn = SerialInputString.indexOf("\n");
      //Serial.print("indexofM_ID_RETURN"+  String(indexOfm) + " " + String(indexOfId) + " " + String(indexOfReturn) + " index END");

      String machine = SerialInputString.substring( indexOfm + 2 , indexOfSemicolon ) ;
      String id = SerialInputString.substring( indexOfId + 3 , indexOfReturn );

      //发送http
      Use_Serial.println(machine + " " + id);
      if (WiFiMulti.run() != WL_CONNECTED) { //如果连接不正常 重新连接wifi
        Serial.println("Wfa"); //to stc12 wifi断开
        Use_Serial.print("Re");
        wifi_Init();
      }
      String mqtt_str="machine=" + machine + "id=" + id;
      char* http_mqtt_send = &mqtt_str[0];//在发送http的同时也发送到mqtt服务器
      client.publish(mqtt_publish_topic, http_mqtt_send);
      int getTimes = 3; //如果未发送成功 重试的次数
      while (getTimes) {
        int httpCode = httpSendStr( dataRequestAddress + "?machine=" + machine + "&id=" + id + "&apikey=" + ApiKey);
        if (httpCode == 200) {
          Serial.println("ZOk");
          break;
        }
        getTimes--;
      }

    } else {
      //Serial.println("espReturn");
    }
    // clear the string:
    SerialInputString = "";
    SerialStringComplete = false;
  }
  nodeSerialEvent();
}

//------------------------------------------- void setup() ------------------------------------------

void setup() {
  Use_Serial.begin(9600);
  SerialInputString.reserve(200);// 给string变量留出 200 Bytes 空间 reserve 200 bytes for the SerialInputString:

  get_espid();  //打印输出ESP芯片ID
  LED_Int();  // D4 pinMode set

  SET_AP(); // 建立WIFI (AP接入点)
  SPIFFS.begin(); //SPI文件操作
  loadConfig();// 读取信息 WIFI

  Server_int();  //3-4ESP建立网页服务器
  wifi_Init(); // wifi连接 如果没连上就一直重连

  client.setServer(mqtt_server, port); //mqtt服务器地址和端口
  client.setCallback(callback); //用于接收服务器接收的数据
}

//------------------------------------------- void loop()  ------------------------------------------

void loop()
{
  reconnect();//确保连上服务器，否则一直等待。
  client.loop();//MCU接收数据的主循环函数。
  server.handleClient();
  receiveSendHttp();
 // client.publish(mqtt_publish_topic, mqtt_publish);//发布函数 第一个参数为发布的topic第二个参数为要发送的字符串
  //delay(1000);//测试一秒发一次，上面程序已经完成HTTP和MQTT同时发送(在http发送函数中）
  
}
