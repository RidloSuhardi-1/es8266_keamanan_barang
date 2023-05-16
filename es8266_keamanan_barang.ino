#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Firebase_ESP_Client.h>

#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

#include <Servo.h>

/* Wi-Fi */

#define WIFI_SSID "WIFI-SSID" // ubahen
#define WIFI_PASSWORD "WIFI-PASSWORD" // ubahen

/* Firebase */

#define API_KEY "" // firebase web api key // ubahen

#define DATABASE_URL "" // firebase realtime database url [..firebase.com/..firebasedatabase.app] // ubahen

FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
unsigned long count = 0;

/* Waktu */

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

String weekDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

String months[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

/* Inisialisasi pin NodeMCU */

#define SERVO_PIN D5
#define TRIG_PIN D4
#define ECHO_PIN D3
#define LED_PIN D0

Servo servo;  
int sudut= 180;
int sudut1= 0;
long duration, distance;

bool ledStatus = false;

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("connecting");

  while (WiFi.status() != WL_CONNECTED) {  
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("connected: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  
  config.api_key = API_KEY;

  config.database_url = DATABASE_URL;

  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  config.signer.test_mode = true; // tanpa login

  // if (Firebase.signUp(&config, &auth, "", "")) { // login sebagai anonymous
  //   Serial.println("ok");
  //   signUpOK = true;
  // } else {
  //   Serial.printf("%s\n", config.signer.signupError.message.c_str());
  // }

  fbdo.setResponseSize(2048); // ukuran payload yand dikumpulkan dari objek FirebaseData

  Firebase.begin(&config, &auth); // inisialisasi library firebase
  Firebase.reconnectWiFi(true);

  Firebase.setDoubleDigits(5);

  config.timeout.serverResponse = 10 * 1000;

  timeClient.begin();
  timeClient.setTimeOffset(25200); // default 3600 (GMT+1). Indonesia (GMT+7)

  // NodeMCU

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  servo.attach(SERVO_PIN);

  updateLEDStatus(ledStatus);
  addSystemStatus("Ready to use");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();

    if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)) {
      sendDataPrevMillis = millis();

      // cek jarak antara sensor dengan objek di depannya

      float distance = distanceInCM();

      if (distance > 0 && distance <= 50) {
        ledStatus = true;
        
        digitalWrite(LED_PIN, LOW);
        // delay(1000);

        servo.write(sudut);
        delay(1000);

        servo.write(sudut1);
        // delay(1000);

        updateLEDStatus(ledStatus);
        addSystemStatus("Lampu menyala, paket akan segera masuk.."); // yo ngeprint yo nyimpen ng firebase

      } else if (distance > 0 && distance <= 100) {
        addSystemStatus("Terdeteksi objek dalam jarak " + String(distance) + "cm");
      } else {
        ledStatus = false;
        digitalWrite(LED_PIN, HIGH);

        updateLEDStatus(ledStatus);
      }
    }
  } else {
    Serial.println("WiFi not connected");
  }
}

/* Fungsi tambahan */

// print pesan status ke terminal/output

void createStatus(String message) {
  String currentTime = timeToString();

  Serial.println(currentTime);
  Serial.println("status: " + message + "\n");
}

// konfigurasi waktu

String timeToString() { 
  time_t epochTime = timeClient.getEpochTime();

  struct tm *ptm = gmtime ((time_t *)&epochTime); 

  int monthDay = ptm->tm_mday; // tanggal

  int currentMonth = ptm->tm_mon+1; // bulan
  String currentMonthName = months[currentMonth-1];

  int currentYear = ptm->tm_year+1900; // tahun

  String currentDay = weekDays[timeClient.getDay()]; // hari

  String date = String(monthDay) + "-" + currentMonthName + "-" + String(currentYear) + " " + timeClient.getFormattedTime();

  return date;
}

// jarak dalam CM

float distanceInCM() { 
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2); 

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10); 

  digitalWrite(TRIG_PIN, LOW);   
  duration = pulseIn(ECHO_PIN, HIGH);

  /*
    Berfungsi untuk memperoleh jarak antara sensor dengan benda di depannya. 
    2.91 adalah kecepatan suara (29.1 uS/cm).
  */

  distance = (duration/2) / 29.1;

  return distance;
}

/* Fungsi query firebase */

void addSystemStatus(String message) {
  createStatus(message);

  String time = timeToString(); // dapatkan tanggal dan waktu sekarang

  String getDate = time.substring(0, time.indexOf(' ')); // pecah bagian tanggal dari spasi (format: 16-May-2023 14:01:00)
  String getTime = timeClient.getFormattedTime();

  Firebase.RTDB.setString(&fbdo, "/system_notification/" + getDate + "/" + getTime + "/" + "message", message) ? createStatus("Berhasil ditambahkan") : createStatus(fbdo.errorReason().c_str());
}

void updateLEDStatus(bool status) {
  String time = timeToString(); // dapatkan tanggal dan waktu sekarang

  Firebase.RTDB.setString(&fbdo, "/led_status/date", time) ? createStatus("Tanggal berhasil diperbarui") : createStatus(fbdo.errorReason().c_str());
  Firebase.RTDB.setBool(&fbdo, "/led_status/status", status) ? createStatus("Status berhasil diperbarui") : createStatus(fbdo.errorReason().c_str());
}