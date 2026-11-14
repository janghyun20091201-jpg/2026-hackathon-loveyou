// ===== ESP32 하드웨어 테스트: RGB / MPU6050 / 부저 =====
// 보드: ESP32 Dev Module (업로드 속도 115200)
// 추가 라이브러리 필요 없음
// 접속: 와이파이 CPR-Test (비번 12345678) → 브라우저에서 http://192.168.4.1
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>

const char* AP_SSID = "CPR-Test";
const char* AP_PASS = "12345678";   // 8자 이상

// ===== 핀 (830홀 최종 배치 기준) =====
const int BUZ_A   = 25;   // 부저 +
const int BUZ_B   = 26;   // 부저 −
const int MPU_SDA = 27;
const int MPU_SCL = 33;
const int LED_R   = 13;   // RGB 모듈 R
const int LED_G   = 12;   // RGB 모듈 G (B는 연결 안 함)
const uint8_t MPU = 0x68;

WebServer server(80);
bool mpuOk = false;

// ===== 웹페이지 =====
const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 테스트</title>
<style>
body{font-family:sans-serif;max-width:420px;margin:20px auto;padding:0 12px}
h3{margin:22px 0 8px}
button{font-size:17px;padding:12px 0;margin:3px 0;width:100%;border:1px solid #999;border-radius:8px;background:#f4f4f4}
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

void setup() {
  Serial.begin(115200);

  pinMode(LED_R, OUTPUT); digitalWrite(LED_R, LOW);
  pinMode(LED_G, OUTPUT); digitalWrite(LED_G, LOW);
  pinMode(BUZ_A, OUTPUT); digitalWrite(BUZ_A, LOW);
  pinMode(BUZ_B, OUTPUT); digitalWrite(BUZ_B, LOW);

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.setTimeOut(50);   // MPU 선 빠져도 사이트가 안 멈추게
  mpuOk = mpuInit();
  Serial.println(mpuOk ? "MPU OK" : "MPU 응답 없음");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 6);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);   // 배터리 구동 시 리셋 방지
  Serial.print("접속 주소: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/rgb", handleRgb);
  server.on("/beep", handleBeep);
  server.on("/mpu", handleMpu);
  server.begin();
}

void loop() {
  server.handleClient();
}
