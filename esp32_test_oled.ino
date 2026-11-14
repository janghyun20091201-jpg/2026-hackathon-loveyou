// ===== ESP32 하드웨어 테스트: RGB / MPU6050 / 부저 / OLED =====
// 보드: ESP32 Dev Module (업로드 속도 115200)
// 필요한 라이브러리: Adafruit SSD1306 (설치할 때 "Install all" → Adafruit GFX도 같이 설치됨)
// 접속: 와이파이 CPR-Test (비번 12345678) → 브라우저에서 http://192.168.4.1
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* AP_SSID = "CPR-Test";
const char* AP_PASS = "12345678";   // 8자 이상

// ===== 핀 (830홀 최종 배치 기준) =====
const int BUZ_A    = 25;   // 부저 +
const int BUZ_B    = 26;   // 부저 −
const int MPU_SDA  = 27;   // MPU6050 → Wire
const int MPU_SCL  = 33;
const int OLED_SDA = 32;   // OLED → Wire1 (MPU와 따로)
const int OLED_SCL = 14;
const int LED_R    = 13;   // RGB 모듈 R
const int LED_G    = 12;   // RGB 모듈 G (B는 연결 안 함)
const uint8_t MPU  = 0x68;

WebServer server(80);
Adafruit_SSD1306 oled(128, 64, &Wire1, -1, 400000UL, 400000UL);

bool mpuOk   = false;
bool oledOk  = false;
bool oledMpu = false;      // OLED에 MPU 실시간 표시 중인지
uint32_t lastOledMs = 0;

// ===== 웹페이지 =====
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 테스트</title>
<style>
body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}
h3{margin:22px 0 8px}
button{font-size:17px;padding:12px 0;margin:3px 0;width:100%;border:1px solid #999;border-radius:8px;background:#f4f4f4}
input{flex:1;min-width:0;font-size:17px;padding:10px;margin:3px 0;border:1px solid #999;border-radius:8px}
.row{display:flex;gap:6px}
pre{background:#f4f4f4;padding:10px;border-radius:8px;font-size:15px;white-space:pre-wrap}
#st{color:#666;font-size:14px}
</style></head><body>
<h2>ESP32 테스트</h2>
<div id="st">대기 중</div>

<h3>RGB</h3>
<div class="row">
<button onclick="go('/rgb?c=r')">빨강</button>
<button onclick="go('/rgb?c=g')">초록</button>
<button onclick="go('/rgb?c=y')">노랑</button>
<button onclick="go('/rgb?c=off')">끄기</button>
</div>

<h3>부저</h3>
<button onclick="go('/beep')">삐 (0.3초)</button>

<h3>OLED</h3>
<div class="row">
<input id="tx" maxlength="80" placeholder="영어·숫자만 (한글 X)">
<button style="width:80px;flex:none" onclick="go('/text?t='+encodeURIComponent($('tx').value))">표시</button>
</div>
<div class="row">
<button onclick="go('/oled?m=fill')">전체 켜기</button>
<button onclick="go('/oled?m=clear')">지우기</button>
<button onclick="go('/oled?m=mpu')">MPU 표시</button>
</div>

<h3>MPU6050</h3>
<button id="mb" onclick="mpu()">실시간 보기 시작</button>
<pre id="mv">-</pre>

<script>
const $=id=>document.getElementById(id);
let t=null;
function go(u){
  fetch(u).then(r=>r.text()).then(s=>$('st').textContent=s)
          .catch(()=>$('st').textContent='연결 끊김');
}
function mpu(){
  if(t){clearInterval(t);t=null;$('mb').textContent='실시간 보기 시작';return;}
  $('mb').textContent='정지';
  t=setInterval(()=>{
    fetch('/mpu').then(r=>r.json()).then(d=>{
      $('mv').textContent=d.ok
        ? 'ax '+d.ax+'   ay '+d.ay+'   az '+d.az+'   (g)\n'+
          'gx '+d.gx+'   gy '+d.gy+'   gz '+d.gz+'   (deg/s)'
        : 'MPU 응답 없음 → SDA(27)·SCL(33)·VCC·GND 배선 확인';
    }).catch(()=>$('mv').textContent='연결 끊김');
  },300);
}
</script></body></html>
)HTML";

// ===== 부저 (두 핀을 번갈아 → 더 크게) =====
void beep(int hz, int ms) {
  uint32_t half = 500000UL / hz;
  uint32_t end  = millis() + ms;
  while ((int32_t)(millis() - end) < 0) {
    digitalWrite(BUZ_A, HIGH); digitalWrite(BUZ_B, LOW);  delayMicroseconds(half);
    digitalWrite(BUZ_A, LOW);  digitalWrite(BUZ_B, HIGH); delayMicroseconds(half);
  }
  digitalWrite(BUZ_A, LOW);
  digitalWrite(BUZ_B, LOW);
}

// ===== MPU6050 =====
bool mpuInit() {
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);   // PWR_MGMT_1
  Wire.write(0);      // 슬립 해제
  return Wire.endTransmission() == 0;
}

bool mpuRead(float v[6]) {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);   // ACCEL_XOUT_H부터
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU, (size_t)14, true) != 14) return false;

  uint8_t b[14];
  for (int i = 0; i < 14; i++) b[i] = Wire.read();
  int16_t r[7];
  for (int i = 0; i < 7; i++) r[i] = (int16_t)((b[i * 2] << 8) | b[i * 2 + 1]);

  // r[3]은 온도라 건너뜀
  v[0] = r[0] / 16384.0;  v[1] = r[1] / 16384.0;  v[2] = r[2] / 16384.0;   // ±2g
  v[3] = r[4] / 131.0;    v[4] = r[5] / 131.0;    v[5] = r[6] / 131.0;     // ±250°/s
  return true;
}

// ===== OLED =====
bool oledInit() {
  const uint8_t addrs[] = {0x3C, 0x3D};   // 보통 0x3C
  for (uint8_t a : addrs) {
    Wire1.beginTransmission(a);
    if (Wire1.endTransmission() == 0) {
      if (!oled.begin(SSD1306_SWITCHCAPVCC, a, true, false)) return false;
      oled.setTextColor(SSD1306_WHITE);
      return true;
    }
  }
  return false;
}

void oledBoot() {
  if (!oledOk) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("ESP32 TEST");
  oled.println();
  oled.println("WiFi: CPR-Test");
  oled.println("PW  : 12345678");
  oled.println("http://192.168.4.1");
  oled.println();
  oled.println(mpuOk ? "MPU : OK" : "MPU : NO RESPONSE");
  oled.display();
}

// 한글 같은 비ASCII 글자는 OLED 기본 폰트에 없어서 '?'로 바꿈
String toAscii(const String& s) {
  String out;
  for (unsigned i = 0; i < s.length() && out.length() < 80; i++) {
    uint8_t ch = s[i];
    if (ch >= 32 && ch < 127) out += (char)ch;
    else if (ch >= 0xC0)      out += '?';   // UTF-8 글자 시작 바이트만 '?' 하나로
  }
  return out;
}

void oledText(const String& msg) {
  oled.clearDisplay();
  oled.setTextSize(msg.length() <= 40 ? 2 : 1);   // 짧으면 크게
  oled.setCursor(0, 0);
  oled.print(msg);
  oled.display();
}

void oledShowMpu() {
  if (!mpuOk) mpuOk = mpuInit();
  float v[6];
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  if (mpuOk && mpuRead(v)) {
    oled.println("MPU6050");
    oled.println();
    oled.printf("ax %7.2f g\n",   v[0]);
    oled.printf("ay %7.2f g\n",   v[1]);
    oled.printf("az %7.2f g\n",   v[2]);
    oled.printf("gx %7.1f d/s\n", v[3]);
    oled.printf("gy %7.1f d/s\n", v[4]);
    oled.printf("gz %7.1f d/s",   v[5]);
  } else {
    mpuOk = false;
    oled.println("MPU6050");
    oled.println();
    oled.println("NO RESPONSE");
    oled.println("check SDA27 SCL33");
  }
  oled.display();
}

// OLED 선 다시 꽂으면 자동 재연결
bool oledReady() {
  if (!oledOk) oledOk = oledInit();
  if (!oledOk) server.send(200, "text/plain; charset=utf-8",
                           "OLED 응답 없음 → SDA(32)·SCL(14)·VCC·GND 배선 확인");
  return oledOk;
}

// ===== 요청 처리 =====
void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", PAGE);
}

void handleRgb() {
  String c = server.arg("c");
  digitalWrite(LED_R, (c == "r" || c == "y") ? HIGH : LOW);
  digitalWrite(LED_G, (c == "g" || c == "y") ? HIGH : LOW);

  String name = (c == "r") ? "빨강" : (c == "g") ? "초록" : (c == "y") ? "노랑" : "꺼짐";
  server.send(200, "text/plain; charset=utf-8", "RGB → " + name);
}

void handleBeep() {
  beep(2700, 300);
  server.send(200, "text/plain; charset=utf-8", "부저 울림");
}

void handleMpu() {
  if (!mpuOk) mpuOk = mpuInit();   // 선 다시 꽂으면 자동 재연결

  float v[6];
  if (!mpuOk || !mpuRead(v)) {
    mpuOk = false;
    server.send(200, "application/json", "{\"ok\":false}");
    return;
  }
  char json[160];
  snprintf(json, sizeof(json),
           "{\"ok\":true,\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f}",
           v[0], v[1], v[2], v[3], v[4], v[5]);
  server.send(200, "application/json", json);
}

void handleText() {
  if (!oledReady()) return;
  oledMpu = false;
  String msg = toAscii(server.arg("t"));
  oledText(msg);
  server.send(200, "text/plain; charset=utf-8", "OLED → " + msg);
}

void handleOled() {
  if (!oledReady()) return;
  String m = server.arg("m");
  oledMpu = false;

  if (m == "fill") {
    oled.fillScreen(SSD1306_WHITE);   // 죽은 픽셀 확인용
    oled.display();
    server.send(200, "text/plain; charset=utf-8", "OLED 전체 켜짐");
  } else if (m == "mpu") {
    oledMpu = true;                   // loop에서 0.2초마다 갱신
    server.send(200, "text/plain; charset=utf-8", "OLED에 MPU 실시간 표시 중");
  } else {
    oled.clearDisplay();
    oled.display();
    server.send(200, "text/plain; charset=utf-8", "OLED 지움");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT); digitalWrite(LED_R, LOW);
  pinMode(LED_G, OUTPUT); digitalWrite(LED_G, LOW);
  pinMode(BUZ_A, OUTPUT); digitalWrite(BUZ_A, LOW);
  pinMode(BUZ_B, OUTPUT); digitalWrite(BUZ_B, LOW);

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setTimeOut(50);    // 선 빠져도 사이트가 안 멈추게
  mpuOk = mpuInit();
  Serial.println(mpuOk ? "MPU OK" : "MPU 응답 없음");

  Wire1.begin(OLED_SDA, OLED_SCL);
  Wire1.setTimeOut(50);
  oledOk = oledInit();
  Serial.println(oledOk ? "OLED OK" : "OLED 응답 없음");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 6);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // 배터리 구동 시 리셋 방지
  Serial.print("접속 주소: http://");
  Serial.println(WiFi.softAPIP());
  oledBoot();

  server.on("/", handleRoot);
  server.on("/rgb", handleRgb);
  server.on("/beep", handleBeep);
  server.on("/mpu", handleMpu);
  server.on("/text", handleText);
  server.on("/oled", handleOled);
  server.begin();
}

void loop() {
  server.handleClient();

  if (oledMpu && oledOk && millis() - lastOledMs >= 200) {
    lastOledMs = millis();
    oledShowMpu();
  }
}
