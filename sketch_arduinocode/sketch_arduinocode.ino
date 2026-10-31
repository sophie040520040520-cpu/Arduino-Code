#include <Keypad.h>
#include <SoftwareSerial.h>

SoftwareSerial espSerial(A0, A1);

const byte DOOR_LOCK_PIN = 2;
const byte STATUS_LED_PIN = 13;
const byte DOOR_LIMIT_SWITCH_PIN = 10;
const byte BOOK_LIMIT_SWITCH_PIN = 11;

// 기본 수납장 구역 번호 (키패드 첫 글자로 1~5구역 동적 변경됨)
int cabinetSlot = 1; 

const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};

byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 입력 중인 비밀번호 저장을 위한 변수
String enteredPassword = "";

bool lastDoorClosed = false;
bool lastBookPresent = false;
bool isDoorUnlocked = false;

bool isDoorClosed();
bool isBookPresent();
void sendStateToESP();
void unlockDoor();
void lockDoor();
void handleKeypadInput(char key);

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);

  pinMode(DOOR_LOCK_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  pinMode(DOOR_LIMIT_SWITCH_PIN, INPUT_PULLUP);
  pinMode(BOOK_LIMIT_SWITCH_PIN, INPUT_PULLUP);

  digitalWrite(DOOR_LOCK_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);

  lastDoorClosed = isDoorClosed();
  lastBookPresent = isBookPresent();

  Serial.println("==============================");
  Serial.println("도어락 시스템 시작 (1~5구역 슬롯 선택 모드)");
  Serial.println("==============================");

  sendStateToESP();
}

void loop() {
  // A. 문 열림/닫힘 감지
  bool currentDoorClosed = isDoorClosed();
  if (currentDoorClosed != lastDoorClosed) {
    lastDoorClosed = currentDoorClosed;
    enteredPassword = "";

    if (currentDoorClosed) {
      lockDoor();
      Serial.println("[도어락] 문 닫힘 - 잠금 실행");
    } else {
      digitalWrite(STATUS_LED_PIN, LOW);
      Serial.println("[도어락] 문 열림 감지");
      sendStateToESP();
    }
  }

  // B. 책 유무 감지
  bool currentBookPresent = isBookPresent();
  if (currentBookPresent != lastBookPresent) {
    lastBookPresent = currentBookPresent;
    sendStateToESP();
  }

  // C. 키패드 입력 처리
  char key = keypad.getKey();
  if (key != NO_KEY) {
    Serial.print("[키패드 입력] ");
    Serial.println(key);

    if (currentDoorClosed && !isDoorUnlocked) {
      handleKeypadInput(key);
    } else if (!currentDoorClosed) {
      Serial.println("[안내] 문이 열린 상태입니다.");
    } else {
      Serial.println("[안내] 이미 잠금 해제된 상태입니다.");
    }
  }

  // D. 서버 검증을 통과해 ESP8266으로부터 UNLOCK 명령이 내려온 경우
  if (espSerial.available() > 0) {
    String cmd = espSerial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "UNLOCK") {
      Serial.println("[명령 수신] 서버 인증 완료 -> 문 열림 실행");
      unlockDoor();
    }
  }
}

// 키패드 입력 관리 및 서버 전송 트리거 (1~5구역 선택 기능 포함)
void handleKeypadInput(char key) {
  if (key == '*') {
    enteredPassword = "";
    Serial.println("[키패드] 입력 초기화");
  }
  else if (key == '#') {
    // #을 누를 때: 첫 글자는 구역 번호(1~5), 나머지는 비밀번호로 분리
    if (enteredPassword.length() > 1) {
      int selectedSlot = enteredPassword.charAt(0) - '0'; // 첫 글자를 숫자로 변환
      String authCode = enteredPassword.substring(1);   // 첫 글자 뗀 나머지가 비밀번호

      if (selectedSlot >= 1 && selectedSlot <= 5) {
        cabinetSlot = selectedSlot; // 수납장 구역 번호 동적 변경
        
        Serial.print("[선택된 수납장 구역] ");
        Serial.print(cabinetSlot);
        Serial.print("구역 | 입력된 비밀번호: ");
        Serial.println(authCode);

        // 변경된 구역 번호를 ESP8266에 먼저 동기화
        sendStateToESP();
        delay(50);

        // 키패드 인증 요청 전송
        espSerial.print("KEY:");
        espSerial.println(authCode);
      } else {
        Serial.println("[입력 오류] 첫 자리는 1~5구역 번호여야 합니다.");
      }
    } else {
      Serial.println("[입력 오류] 번호가 너무 짧습니다. (예: 14031#)");
    }
    enteredPassword = ""; // 입력 버퍼 초기화
  }
  else if (key >= '0' && key <= '9') {
    enteredPassword += key;
    Serial.print("[입력 중] ");
    for (unsigned int i = 0; i < enteredPassword.length(); i++) {
      if (i == 0) {
        Serial.print(enteredPassword.charAt(0));
        Serial.print(" (구역) ");
      } else {
        Serial.print("*");
      }
    }
    Serial.println();
  }
}

void unlockDoor() {
  isDoorUnlocked = true;
  digitalWrite(DOOR_LOCK_PIN, HIGH);
  digitalWrite(STATUS_LED_PIN, LOW);
  Serial.println("[도어락] 잠금 해제!");
  sendStateToESP();
}

void lockDoor() {
  digitalWrite(DOOR_LOCK_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, HIGH);
  isDoorUnlocked = false;
  Serial.println("[도어락] 잠금 상태");
  sendStateToESP();
}

bool isDoorClosed() {
  return digitalRead(DOOR_LIMIT_SWITCH_PIN) == LOW;
}

bool isBookPresent() {
  return digitalRead(BOOK_LIMIT_SWITCH_PIN) == LOW;
}

void sendStateToESP() {
  delay(50);
  String jsonPayload = "{";
  jsonPayload += "\"cabinetSlot\": " + String(cabinetSlot);
  jsonPayload += ", \"doorStatus\": " + String(lastDoorClosed ? "true" : "false");
  jsonPayload += ", \"limitSwitchStatus\": " + String(lastBookPresent ? "true" : "false");
  jsonPayload += "}";

  espSerial.println(jsonPayload);
  delay(50);
}