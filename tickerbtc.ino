#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include "pin_config.h"
#include <time.h>
#include "btc_logo.h"
#include "logo.h"
#include <EEPROM.h>

#define BUTTON_A     0
#define BUTTON_B     14
#define EEPROM_SIZE 512

// --- Struttura EEPROM ---
struct Config {
  char altcoin1[20], altcoin2[20], altcoin3[20], altcoin4[20], altcoin5[20];
  char ticker1 [6],  ticker2 [6],  ticker3 [6],  ticker4 [6],  ticker5 [6];
};

// --- Costanti schermate ---
const int NUM_TOTAL_SCREENS   = 9;
const int BTC_PRICE_INDEX     = 0;
const int BTC_STATS_INDEX     = 1;
const int BTC_CHART_INDEX     = 2;
const int ALT_SUMMARY_INDEX   = 3;
const int ALT1_PRICE_INDEX    = 4;
const int ALT2_PRICE_INDEX    = 5;
const int ALT3_PRICE_INDEX    = 6;
const int ALT4_PRICE_INDEX    = 7;
const int ALT5_PRICE_INDEX    = 8;

String labels[NUM_TOTAL_SCREENS] = {
  "BTC/USD","BTC Stats","BTC Chart","All Alts",
  "ALT1/USD","ALT2/USD","ALT3/USD","ALT4/USD","ALT5/USD"
};

// --- Kraken BTC config ---
const String krakenBtcPair    = "XBTUSD";
const String krakenBtcJsonKey = "XXBTZUSD";

// --- Default altcoins & ticker ---
String customAltcoins[5]       = {"ethereum","ripple","kaspa","dogecoin","cardano"};
String customAltcoinTickers[5] = {"ETH","XRP","KAS","DOGE","ADA"};

// --- WiFiManager parameters ---
WiFiManagerParameter custom_altcoin1("alt1","Altcoin 1 ID",customAltcoins[0].c_str(),20);
WiFiManagerParameter custom_altcoin2("alt2","Altcoin 2 ID",customAltcoins[1].c_str(),20);
WiFiManagerParameter custom_altcoin3("alt3","Altcoin 3 ID",customAltcoins[2].c_str(),20);
WiFiManagerParameter custom_altcoin4("alt4","Altcoin 4 ID",customAltcoins[3].c_str(),20);
WiFiManagerParameter custom_altcoin5("alt5","Altcoin 5 ID",customAltcoins[4].c_str(),20);
WiFiManagerParameter custom_ticker1("tick1","Ticker 1",customAltcoinTickers[0].c_str(),6);
WiFiManagerParameter custom_ticker2("tick2","Ticker 2",customAltcoinTickers[1].c_str(),6);
WiFiManagerParameter custom_ticker3("tick3","Ticker 3",customAltcoinTickers[2].c_str(),6);
WiFiManagerParameter custom_ticker4("tick4","Ticker 4",customAltcoinTickers[3].c_str(),6);
WiFiManagerParameter custom_ticker5("tick5","Ticker 5",customAltcoinTickers[4].c_str(),6);

// --- Chart timeframes ---
const int NUM_TIMEFRAMES = 3;
const String timeframeLabels[] = {"24 Hours","7 Days","30 Days"};
const String binanceIntervals[] = {"1h","4h","1d"};
const int    chartLimits[]     = {24,42,30};

// --- Globals ---
int currentChartTimeframeIndex = 0;
int currentIndex = 0;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
uint16_t btcOrange = tft.color565(247,147,26);
bool darkMode = false;
unsigned long lastButtonPress  = 0;
unsigned long lastButtonBPress = 0;
unsigned long lastUpdate       = 0;
const unsigned long debounceDelay  = 300;
const unsigned long updateInterval = 30000;
unsigned long buttonADownTime = 0, buttonBDownTime = 0;
bool buttonAWasPressed = false, buttonBWasPressed = false;
WiFiManager wm;

// --- Utility ---
String getTimeString(){
  time_t now = time(nullptr);
  if(now < 1672531200) return "--:--:--";
  struct tm *t = localtime(&now);
  char buf[16];
  sprintf(buf,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
  return String(buf);
}

void reconnectWiFi(){
  if(WiFi.status()!=WL_CONNECTED){
    WiFi.reconnect();
    unsigned long start=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-start<5000) delay(200);
  }
}

void showError(const char* msg){
  sprite.fillSprite(TFT_RED);
  sprite.setTextColor(TFT_WHITE,TFT_RED);
  sprite.setTextSize(2);
  int w=sprite.textWidth(msg);
  sprite.setCursor((320-w)/2,60);
  sprite.println(msg);
  sprite.pushSprite(0,0);
}

void updateTheme(){ darkMode=true; }
void updateAltcoinLabels(){
  for(int i=0;i<5;i++){
    labels[ALT1_PRICE_INDEX+i] = customAltcoinTickers[i] + "/USD";
  }
}

// --- EEPROM handling ---
void saveConfig(){
  Config cfg;
  strncpy(cfg.altcoin1, customAltcoins[0].c_str(), sizeof(cfg.altcoin1));
  strncpy(cfg.altcoin2, customAltcoins[1].c_str(), sizeof(cfg.altcoin2));
  strncpy(cfg.altcoin3, customAltcoins[2].c_str(), sizeof(cfg.altcoin3));
  strncpy(cfg.altcoin4, customAltcoins[3].c_str(), sizeof(cfg.altcoin4));
  strncpy(cfg.altcoin5, customAltcoins[4].c_str(), sizeof(cfg.altcoin5));
  strncpy(cfg.ticker1 , customAltcoinTickers[0].c_str(), sizeof(cfg.ticker1));
  strncpy(cfg.ticker2 , customAltcoinTickers[1].c_str(), sizeof(cfg.ticker2));
  strncpy(cfg.ticker3 , customAltcoinTickers[2].c_str(), sizeof(cfg.ticker3));
  strncpy(cfg.ticker4 , customAltcoinTickers[3].c_str(), sizeof(cfg.ticker4));
  strncpy(cfg.ticker5 , customAltcoinTickers[4].c_str(), sizeof(cfg.ticker5));
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, cfg);
  EEPROM.commit();
  EEPROM.end();
}

void loadConfig(){
  Config cfg;
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  EEPROM.end();
  customAltcoins[0]       = String(cfg.altcoin1);
  customAltcoins[1]       = String(cfg.altcoin2);
  customAltcoins[2]       = String(cfg.altcoin3);
  customAltcoins[3]       = String(cfg.altcoin4);
  customAltcoins[4]       = String(cfg.altcoin5);
  customAltcoinTickers[0] = String(cfg.ticker1);
  customAltcoinTickers[1] = String(cfg.ticker2);
  customAltcoinTickers[2] = String(cfg.ticker3);
  customAltcoinTickers[3] = String(cfg.ticker4);
  customAltcoinTickers[4] = String(cfg.ticker5);
}

void initDefaultConfigIfNeeded(){
  EEPROM.begin(EEPROM_SIZE);
  byte first = EEPROM.read(0);
  EEPROM.end();
  if(first == 0xFF){
    saveConfig();
  }
}

// --- Draw functions (definite *prima* dei fetch) ---

void drawDisplay(const String& label, float price, float change, float low, float high, const String& timeStr){
  updateTheme();
  uint16_t bg = TFT_BLACK, fg = btcOrange;
  uint16_t ccol = change>=0 ? TFT_GREEN : TFT_RED;
  sprite.fillSprite(bg);

  bool isBTC = (currentIndex == BTC_PRICE_INDEX);
  sprite.setTextSize(3);
  sprite.setTextColor(fg,bg);
  int labelW = sprite.textWidth(label);
  int iconW  = isBTC?30:0;
  int x = (320 - (labelW+iconW))/2;
  if(isBTC){
    sprite.drawBitmap(x,10,epd_bitmap_bitcoin_logo_crypto_currency_symbol_free_vector__1_,24,24,fg);
    sprite.setCursor(x+iconW,12);
  } else {
    sprite.setCursor(x,12);
  }
  sprite.println(label);

  sprite.setTextSize(4);
  String pstr = "$" + String(price, price<1.0?4:2);
  int pw = sprite.textWidth(pstr);
  sprite.setTextColor(ccol,bg);
  sprite.setCursor((320-pw)/2,70);
  sprite.println(pstr);

  sprite.setTextSize(isBTC?2:3);
  String vstr = (change>=0?"+":"") + String(change,2) + "%";
  int vsW = sprite.textWidth(vstr);
  sprite.setTextColor(ccol);
  sprite.setCursor((320-vsW)/2, isBTC?115:110);
  sprite.print(vstr);

  if(isBTC && low>=0){
    sprite.setTextSize(1);
    sprite.setTextColor(fg,bg);
    String lows  = "Low: "  + String(low,2);
    String highs = "High: " + String(high,2);
    String lh    = lows+"   "+highs;
    int lw       = sprite.textWidth(lh);
    sprite.setCursor((320-lw)/2,145);
    sprite.println(lh);
  }

  sprite.setTextSize(1);
  sprite.setTextColor(TFT_DARKGREY,bg);
  String ts = "Upd: "+timeStr;
  int tw = sprite.textWidth(ts);
  sprite.setCursor((320-tw)/2,160);
  sprite.print(ts);

  sprite.pushSprite(0,0);
}

void drawBitcoinChart(float prices[], int count, const String& tfLabel){
  updateTheme();
  uint16_t bg=TFT_BLACK, fg=btcOrange;
  sprite.fillSprite(bg);

  sprite.setTextSize(1);
  sprite.setTextColor(TFT_WHITE,bg);
  String title="BTC Chart - "+tfLabel;
  int tw = sprite.textWidth(title);
  sprite.setCursor((320-tw)/2,5);
  sprite.println(title);

  if(count<2){
    sprite.setTextSize(2);
    sprite.setTextColor(TFT_WHITE,bg);
    sprite.setCursor(20,60);
    sprite.println("Not enough data");
    sprite.pushSprite(0,0);
    return;
  }

  float minY=prices[0], maxY=prices[0];
  for(int i=1;i<count;i++){ minY=min(minY,prices[i]); maxY=max(maxY,prices[i]); }
  if(minY==maxY) maxY+=1.0;

  int chartX=10, chartY=20, chartW=300, chartH=120, grid=4;
  int labelX=chartX+chartW+5;
  sprite.setTextSize(1);
  sprite.setTextColor(TFT_WHITE,bg);
  for(int i=0;i<=grid;i++){
    int y=chartY + i*(chartH/grid);
    sprite.drawFastHLine(chartX,y,chartW,TFT_DARKGREY);
    float v = maxY - i*(maxY-minY)/grid;
    String vs = String((int)v);
    int w = sprite.textWidth(vs);
    sprite.setCursor(labelX - w, y - 4);
    sprite.print(vs);
  }
  for(int i=1;i<count;i++){
    int x0 = chartX + (chartW*(i-1))/(count-1);
    int x1 = chartX + (chartW*i)/(count-1);
    int y0 = chartY + chartH - ((prices[i-1]-minY)*chartH/(maxY-minY));
    int y1 = chartY + chartH - ((prices[i]-minY)*chartH/(maxY-minY));
    sprite.drawLine(x0,y0,x1,y1,fg);
  }
  sprite.setTextSize(1);
  sprite.setTextColor(TFT_WHITE,bg);
  String bottom="Min:$"+String(minY,2)+"   Max:$"+String(maxY,2);
  int bw=sprite.textWidth(bottom);
  sprite.setCursor((320-bw)/2,chartY+chartH+5);
  sprite.print(bottom);

  sprite.pushSprite(0,0);
}

void drawBitcoinStats(float price, const String& hash, float diff, int height, int toHalving, int days, float dom, const String& timeStr){
  updateTheme();
  uint16_t bg=TFT_BLACK, fg=btcOrange;
  sprite.fillSprite(bg);

  // Titolo + logo
  sprite.setTextSize(3);
  sprite.setTextColor(fg,bg);
  String title="BTC Stats";
  int logoW=24, logoH=24;
  int titleW=sprite.textWidth(title);
  int totalW=logoW+6+titleW;
  int x0=(320-totalW)/2, y0=5;
  sprite.drawBitmap(x0,y0,epd_bitmap_bitcoin_logo_crypto_currency_symbol_free_vector__1_,logoW,logoH,fg);
  sprite.setCursor(x0+logoW+6, y0 + (logoH - sprite.fontHeight())/2);
  sprite.print(title);

  // Statistiche
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE,bg);
  const char* statLabels[] = {"Price:","Hash:","Diff:","Blk:","Days:","Dom:"};
  String statValues[6] = {
    "$"+String((int)price),
    hash,
    String(diff,1)+"T",
    String(height),
    String(days),
    String(dom,1)+"%"
  };

  int labelX = 10;
  // Calcolo larghezza label con if
  int maxLabelW=0;
  for(int i=0;i<6;i++){
    int w = sprite.textWidth(statLabels[i]);
    if(w>maxLabelW) maxLabelW=w;
  }
  int valueX = labelX + maxLabelW + 8;
  int y = y0 + logoH + 8;
  int lineH = sprite.fontHeight()+2;
  for(int i=0;i<6;i++){
    sprite.setCursor(labelX, y);
    sprite.print(statLabels[i]);
    sprite.setCursor(valueX, y);
    sprite.print(statValues[i]);
    y += lineH;
  }

  // Timestamp
  sprite.setTextSize(1);
  sprite.setTextColor(TFT_DARKGREY,bg);
  String ts="Upd: "+timeStr;
  int tw2=sprite.textWidth(ts);
  sprite.setCursor((320-tw2)/2,162);
  sprite.print(ts);

  sprite.pushSprite(0,0);
}

// --- Fetch dati ---

void fetchKrakenBtcPrice(){
  reconnectWiFi();
  if(WiFi.status()!=WL_CONNECTED){ showError("No WiFi"); return; }
  String url="https://api.kraken.com/0/public/Ticker?pair="+krakenBtcPair;
  HTTPClient http; http.begin(url);
  int code = http.GET();
  if(code==HTTP_CODE_OK){
    DynamicJsonDocument d(2048);
    if(!deserializeJson(d, http.getStream()) && d["error"].is<JsonArray>() && d["error"].size()==0){
      auto r = d["result"][krakenBtcJsonKey];
      float price = r["c"][0].as<float>();
      float open  = r["o"].as<float>();
      float low   = r["l"][1].as<float>();
      float high  = r["h"][1].as<float>();
      float change = open!=0 ? ((price-open)/open)*100.0 : 0;
      drawDisplay(labels[BTC_PRICE_INDEX], price, change, low, high, getTimeString());
    } else {
      showError("API/JSON Err");
    }
  } else {
    showError(("HTTP Err "+String(code)).c_str());
  }
  http.end();
}
// --- Fetch Bitcoin Stats ---
void fetchBitcoinStats(){
  reconnectWiFi();
  if(WiFi.status() != WL_CONNECTED){
    showError("No WiFi");
    return;
  }

  float price = 0, diff = 0, dom = 0;
  String hash = "N/A";
  int height = 0, toHalv = -1, days = -1;

  // 1) Prezzo da Kraken
  {
    HTTPClient http;
    http.begin("https://api.kraken.com/0/public/Ticker?pair=XBTUSD");
    if(http.GET() == HTTP_CODE_OK){
      DynamicJsonDocument d(2048);
      if(!deserializeJson(d, http.getString())){
        price = d["result"]["XXBTZUSD"]["c"][0].as<float>();
      }
    }
    http.end();
  }

  // 2) Statistiche da blockchain.info
  {
    HTTPClient http;
    http.begin("https://blockchain.info/stats?format=json");
    if(http.GET() == HTTP_CODE_OK){
      DynamicJsonDocument d(4096);
      if(!deserializeJson(d, http.getString())){
        double hr = d["hash_rate"];
        if(hr >= 1e6)      hash = String(hr/1e6,1) + "EH/s";
        else if(hr >= 1e3) hash = String(hr/1e3,1) + "PH/s";
        else               hash = String(hr,1)   + "TH/s";
        diff   = d["difficulty"].as<float>() / 1e12;
        height = d["n_blocks_total"];
      }
    }
    http.end();
  }

  // 3) Dominio di mercato da CoinGecko global
  {
    HTTPClient http;
    http.begin("https://api.coingecko.com/api/v3/global");
    if(http.GET() == HTTP_CODE_OK){
      DynamicJsonDocument d(4096);
      if(!deserializeJson(d, http.getString())){
        dom = d["data"]["market_cap_percentage"]["btc"].as<float>();
      }
    }
    http.end();
  }

  // Calcoli halving
  toHalv = 210000 - (height % 210000);
  days   = round(toHalv / 144.0);

  // Disegna
  drawBitcoinStats(price, hash, diff, height, toHalv, days, dom, getTimeString());
}

void fetchBitcoinChart(){
  reconnectWiFi();
  if(WiFi.status()!=WL_CONNECTED){ showError("No WiFi"); return; }
  String url = String("https://api.binance.com/api/v3/klines?symbol=BTCUSDT") +
               "&interval=" + binanceIntervals[currentChartTimeframeIndex] +
               "&limit="    + String(chartLimits[currentChartTimeframeIndex]);
  HTTPClient http; http.begin(url);
  if(http.GET()==HTTP_CODE_OK){
    DynamicJsonDocument d(16384);
    if(!deserializeJson(d, http.getStream())){
      JsonArray arr=d.as<JsonArray>();
      int n=min((int)arr.size(),50);
      static float data[50];
      for(int i=0;i<n;i++) data[i]=arr[i][4].as<float>();
      drawBitcoinChart(data,n,timeframeLabels[currentChartTimeframeIndex]);
    }
  }
  http.end();
}

void fetchAllAltPrices(){
  reconnectWiFi();
  if(WiFi.status()!=WL_CONNECTED){ showError("No WiFi"); return; }
  String ids="";
  for(int i=0;i<5;i++){
    if(i) ids+=",";
    ids+=customAltcoins[i];
  }
  String url=String("https://api.coingecko.com/api/v3/simple/price?ids=")+ids+
             "&vs_currencies=usd&include_24hr_change=true";
  HTTPClient http; http.begin(url);
  http.addHeader("User-Agent","ESP32-Crypto-Ticker");
  int code=http.GET();
  if(code==HTTP_CODE_OK){
    DynamicJsonDocument d(4096);
    if(!deserializeJson(d, http.getString())){
      updateTheme();
      sprite.fillSprite(TFT_BLACK);
      // Titolo
      sprite.setTextSize(2);
      sprite.setTextColor(btcOrange);
      String title="All Alts";
      sprite.setCursor((320-sprite.textWidth(title))/2,5);
      sprite.println(title);

      // Colonne
      sprite.setTextSize(2);
      int marginLeft=10;
      int maxTickerW=0;
      for(int i=0;i<5;i++){
        int w=sprite.textWidth(customAltcoinTickers[i]);
        if(w>maxTickerW) maxTickerW=w;
      }
      int priceColX=marginLeft+maxTickerW+12;
      int pctEnd=320-22;
      int y=40;
      // Dati
      for(int i=0;i<5;i++){
        String id=customAltcoins[i];
        if(!d.containsKey(id)){
          sprite.setTextColor(TFT_YELLOW);
          sprite.setCursor(marginLeft,y);
          sprite.print(customAltcoinTickers[i]+": N/A");
          y+=20; continue;
        }
        float price = d[id]["usd"].as<float>();
        float chg   = d[id]["usd_24h_change"].as<float>();
        uint16_t col = chg>=0?TFT_GREEN:TFT_RED;
        // Ticker
        sprite.setTextColor(TFT_WHITE);
        sprite.setCursor(marginLeft,y);
        sprite.print(customAltcoinTickers[i]);
        // Prezzo
        String pstr="$"+String(price, price<1?6:2);
        sprite.setTextColor(col);
        sprite.setCursor(priceColX,y);
        sprite.print(pstr);
        // %chg
        String cstr=(chg>=0?"+":"")+String(chg,2)+"%";
        int cw=sprite.textWidth(cstr);
        sprite.setCursor(pctEnd-cw,y);
        sprite.print(cstr);
        y+=20;
      }
      // Timestamp
      String ts="Upd: "+getTimeString();
      sprite.setTextSize(1);
      sprite.setTextColor(TFT_DARKGREY);
      int tw2=sprite.textWidth(ts);
      sprite.setCursor((320-tw2)/2,160);
      sprite.print(ts);
      sprite.pushSprite(0,0);
    } else {
      showError("JSON Err");
    }
  } else if(code==HTTP_CODE_TOO_MANY_REQUESTS){
    showError("API Limit");
  } else {
    showError(("HTTP Err "+String(code)).c_str());
  }
  http.end();
  delay(500);
}

void fetchCoinGeckoPrice(int idx){
  reconnectWiFi();
  if(WiFi.status()!=WL_CONNECTED){ showError("No WiFi"); return; }
  int ai = idx - ALT1_PRICE_INDEX;
  if(ai<0||ai>=5) return;
  String url=String("https://api.coingecko.com/api/v3/simple/price?ids=")+customAltcoins[ai]+
             "&vs_currencies=usd&include_24hr_change=true";
  HTTPClient http; http.begin(url);
  if(http.GET()==HTTP_CODE_OK){
    DynamicJsonDocument d(1024);
    if(!deserializeJson(d, http.getString())){
      float price = d[customAltcoins[ai]]["usd"].as<float>();
      float chg   = d[customAltcoins[ai]]["usd_24h_change"].as<float>();
      drawDisplay(labels[idx], price, chg, -1, -1, getTimeString());
    }
  }
  http.end();
  delay(3000);
}

void updateDisplayData(){
  switch(currentIndex){
    case BTC_PRICE_INDEX:   fetchKrakenBtcPrice(); break;
    case BTC_STATS_INDEX:   fetchBitcoinStats();    break;
    case BTC_CHART_INDEX:   fetchBitcoinChart();    break;
    case ALT_SUMMARY_INDEX: fetchAllAltPrices();    break;
    default:
      if(currentIndex>=ALT1_PRICE_INDEX && currentIndex<=ALT5_PRICE_INDEX)
        fetchCoinGeckoPrice(currentIndex);
      else {
        currentIndex=0;
        fetchKrakenBtcPrice();
      }
  }
}

// --- Setup WiFi & Portal ---
void setupWiFi(){
  WiFi.mode(WIFI_STA);
  initDefaultConfigIfNeeded();
  loadConfig();
  // Imposto i default per portal
  custom_altcoin1.setValue(customAltcoins[0].c_str(),20);
  custom_altcoin2.setValue(customAltcoins[1].c_str(),20);
  custom_altcoin3.setValue(customAltcoins[2].c_str(),20);
  custom_altcoin4.setValue(customAltcoins[3].c_str(),20);
  custom_altcoin5.setValue(customAltcoins[4].c_str(),20);
  custom_ticker1 .setValue(customAltcoinTickers[0].c_str(),6);
  custom_ticker2 .setValue(customAltcoinTickers[1].c_str(),6);
  custom_ticker3 .setValue(customAltcoinTickers[2].c_str(),6);
  custom_ticker4 .setValue(customAltcoinTickers[3].c_str(),6);
  custom_ticker5 .setValue(customAltcoinTickers[4].c_str(),6);

  wm.addParameter(&custom_altcoin1);
  wm.addParameter(&custom_altcoin2);
  wm.addParameter(&custom_altcoin3);
  wm.addParameter(&custom_altcoin4);
  wm.addParameter(&custom_altcoin5);
  wm.addParameter(&custom_ticker1);
  wm.addParameter(&custom_ticker2);
  wm.addParameter(&custom_ticker3);
  wm.addParameter(&custom_ticker4);
  wm.addParameter(&custom_ticker5);

  wm.setConnectTimeout(20);
  wm.setConfigPortalTimeout(180);
  wm.setSaveConfigCallback([](){ saveConfig(); });

  if(!wm.autoConnect("TickerConfigAP","password")){
    showError("WiFi Fail"); delay(3000);
    ESP.restart();
  }

  // Rileggo dopo portal
  customAltcoins[0]       = custom_altcoin1.getValue();
  customAltcoins[1]       = custom_altcoin2.getValue();
  customAltcoins[2]       = custom_altcoin3.getValue();
  customAltcoins[3]       = custom_altcoin4.getValue();
  customAltcoins[4]       = custom_altcoin5.getValue();
  customAltcoinTickers[0] = custom_ticker1.getValue();
  customAltcoinTickers[1] = custom_ticker2.getValue();
  customAltcoinTickers[2] = custom_ticker3.getValue();
  customAltcoinTickers[3] = custom_ticker4.getValue();
  customAltcoinTickers[4] = custom_ticker5.getValue();
  updateAltcoinLabels();
}

void setupTime(){
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.nist.gov");
  time_t now = time(nullptr);
  unsigned long t0=millis();
  while(now<1672531200 && millis()-t0<15000){
    delay(200);
    now = time(nullptr);
  }
}

// --- Arduino setup & loop ---
void setup(){
  EEPROM.begin(EEPROM_SIZE);
  pinMode(PIN_POWER_ON,OUTPUT);
  digitalWrite(PIN_POWER_ON,HIGH);
  pinMode(BUTTON_A,INPUT_PULLUP);
  pinMode(BUTTON_B,INPUT_PULLUP);
  Serial.begin(115200);
  tft.init(); tft.setRotation(3);
  ledcSetup(0,5000,8);
  ledcAttachPin(PIN_LCD_BL,0);
  ledcWrite(0,255);
  sprite.createSprite(320,170);
  int lx=(tft.width()-LOGO_WIDTH)/2, ly=(tft.height()-LOGO_HEIGHT)/2;
  tft.pushImage(lx,ly,LOGO_WIDTH,LOGO_HEIGHT,bitmap);
  delay(1200);
  setupWiFi();
  setupTime();
  updateDisplayData();
  lastUpdate=millis();
}

void loop(){
  // Button A
  bool pa = digitalRead(BUTTON_A)==LOW;
  if(pa && !buttonAWasPressed){
    buttonADownTime=millis();
    buttonAWasPressed=true;
  } else if(pa && buttonAWasPressed){
    if(millis()-buttonADownTime>3000){
      currentIndex=BTC_PRICE_INDEX;
      updateDisplayData();
      lastUpdate=millis();
      buttonAWasPressed=false;
      lastButtonPress=millis();
    }
  } else if(!pa && buttonAWasPressed){
    if(millis()-buttonADownTime<3000 && millis()-lastButtonPress>debounceDelay){
      lastButtonPress=millis();
      currentIndex = (currentIndex+1)%NUM_TOTAL_SCREENS;
      updateDisplayData();
      lastUpdate=millis();
    }
    buttonAWasPressed=false;
  }

  // Button B
  bool pb = digitalRead(BUTTON_B)==LOW;
  if(pb && !buttonBWasPressed){
    buttonBDownTime=millis();
    buttonBWasPressed=true;
  } else if(pb && buttonBWasPressed){
    if(millis()-buttonBDownTime>3000){
      showError("Reset WiFi");
      wm.resetSettings();
      EEPROM.begin(EEPROM_SIZE);
      for(int i=0;i<EEPROM_SIZE;i++) EEPROM.write(i,0);
      EEPROM.commit();
      EEPROM.end();
      delay(1000);
      ESP.restart();
    }
  } else if(!pb && buttonBWasPressed){
    if(millis()-buttonBDownTime<3000 && millis()-lastButtonBPress>debounceDelay){
      if(currentIndex==BTC_CHART_INDEX){
        currentChartTimeframeIndex=(currentChartTimeframeIndex+1)%NUM_TIMEFRAMES;
        fetchBitcoinChart();
        lastUpdate=millis();
      }
    }
    buttonBWasPressed=false;
    lastButtonBPress=millis();
  }

  // Auto‐update
  if(millis()-lastUpdate>=updateInterval){
    updateDisplayData();
    lastUpdate=millis();
  }
  delay(10);
}
