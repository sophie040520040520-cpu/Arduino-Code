#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h> 
#include <SoftwareSerial.h>

SoftwareSerial unoSerial(D6, D5);

const char* ssid = "KT_WiFi_7_CE90";
const char* password = "Angel21992";

// 서버 원본 AWS IP + HTTP 주소 설정
const char* verifyStep1Url = "http://34.239.136.133:8080/api/verify-step1";
const char* verifyStep2Url = "http://34.239.136.133:8080/api/verify-step2";
const char* sensorsLogUrl = "http://34.239.136.133:8080/api/sensors/logs";

int currentVerificationStep = 1; 
int currentCabinetSlot = 1; // 💡 [추가] 우노에서 받아온 최신 수납장 번호를 저장할 변수

void setup() {
  Serial.begin(9600);
  unoSerial.begin(9600);
  unoSerial.setTimeout(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP8266 시작 (동적 슬롯 연동 버전)");
  Serial.println("==============================");

  WiFi.begin(ssid, password);
  Serial.print("Wi-Fi 연결 중");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi 연결 완료!");
  Serial.print("IP 주소: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (unoSerial.available() > 0) {
    String receivedData = unoSerial.readStringUntil('\n');
    receivedData.trim();

    if (receivedData.length() == 0) return;

    Serial.print("[수신 원문] ");
    Serial.println(receivedData);

    // 💡 [추가] 우노가 보낸 상태 JSON에서 cabinetSlot 번호를 실시간으로 추출하여 저장
    if (receivedData.startsWith("{") && receivedData.endsWith("}")) {
      int slotIndex = receivedData.indexOf("\"cabinetSlot\":");
      if (slotIndex >= 0) {
        int commaIndex = receivedData.indexOf(",", slotIndex);
        if (commaIndex > 0) {
          String slotStr = receivedData.substring(slotIndex + 14, commaIndex);
          slotStr.trim();
          currentCabinetSlot = slotStr.toInt();
          Serial.print("[동적 슬롯 감지] 현재 책 구역 번호: ");
          Serial.println(currentCabinetSlot);
        }
      }
      // 서버로 센서 상태 전송
      sendServerLog(receivedData);
    }
    else if (receivedData.startsWith("KEY:")) {
      String keyData = receivedData.substring(4);
      keyData.trim();
      
      if (currentVerificationStep == 1) {
        Serial.println("--- [1단계] 인증 요청 시도 ---");
        sendKeypadToServer(keyData, verifyStep1Url, 1);
      } else {
        Serial.println("--- [2단계] 최종 인증 요청 시도 ---");
        sendKeypadToServer(keyData, verifyStep2Url, 2);
      }
    }
  }
  yield();
}

// 서버로 센서 상태 전송
void sendServerLog(String payload) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, sensorsLogUrl)) {
    http.addHeader("Content-Type", "application/json");
    int responseCode = http.POST(payload);
    http.end();
  }
}

// 서버 가이드에 맞춰 전송 및 응답 판별하는 함수 (동적 슬롯 적용)
void sendKeypadToServer(String authCode, const char* targetUrl, int step) {
  if (WiFi.status() != WL_CONNECTED) return;

  authCode.trim();

  WiFiClient client;
  HTTPClient http;

  Serial.print("접속 시도 주소: ");
  Serial.println(targetUrl);

  if (http.begin(client, targetUrl)) {
    http.addHeader("Content-Type", "application/json");

    // 💡 [수정 완료] 하드코딩된 1 대신 우노에서 동적으로 넘어온 currentCabinetSlot 사용!
    String jsonPayload = "{\"cabinetSlot\":" + String(currentCabinetSlot) + ", \"authCode\":\"" + authCode + "\"}";
    
    Serial.print("전송할 페이로드: ");
    Serial.println(jsonPayload);

    int responseCode = http.POST(jsonPayload);
    Serial.print("비밀번호 검증 요청 응답 코드: ");
    Serial.println(responseCode);

    if (responseCode > 0) {
      String responseString = http.getString();
      Serial.println(responseString);

      bool isSuccess = (responseString.indexOf("\"open\":false") < 0) && 
                       (responseString.indexOf("\"open\": true") >= 0 || responseString.indexOf("\"open\":true") >= 0);

      if (isSuccess) {
        if (step == 1) {
          Serial.println("[1단계 성공] 서버 승인! 다음 2단계 입력으로 넘어갑니다.");
          currentVerificationStep = 2;
        } else {
          Serial.println("[2단계 최종 성공] 문 열기(open: true) 승인!");
          unoSerial.println("UNLOCK");
          currentVerificationStep = 1;
        }
      } else {
        Serial.println("[인증 실패] 일치하지 않거나 거부됨. 단계를 1단계로 초기화합니다.");
        currentVerificationStep = 1;
      }
    } else {
      Serial.print("통신 실패 에러 내용: ");
      Serial.println(http.errorToString(responseCode).c_str());
    }
    http.end();
  } else {
    Serial.println("HTTP Begin 실패 (주소 형식 확인 필요)");
  }
}