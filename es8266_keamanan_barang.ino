#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Servo.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

/* Wi-Fi */

#define WIFI_SSID "SengganiAtas" // ubahen
#define WIFI_PASSWORD "Bismillah" // ubahen

/* Waktu */

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

/* Firebase */

#define API_KEY "AIzaSyCSLBFbS1xg3o5CbvS8eaUEiFJjJ_oxJVE" // firebase web api key // ubahen

#define DATABASE_URL "iot-keamanan-barang-default-rtdb.asia-southeast1.firebasedatabase.app" // firebase realtime database url [..firebase.com/..firebasedatabase.app] // ubahen

FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;

/* Inisialisasi pin nodemcu */

#define TRIG_PIN D4
#define ECHO_PIN D3
#define LED_PIN D0
#define SERVO_PIN D5

/* Nilai awal servo dan sensor ultrasonik */

#define SOUND_VELOCITY 0.034
#define CM_TO_INCH 0.393701

Servo servo;

unsigned int servoMinDegree = 90; // sudut minimal servo menutup
unsigned int servoMaxDegree = 180; // sudut maksimal servo membuka
unsigned int servoPauseTime = 0; // waktu jeda servo dalam milidetik.

long duration;
float distanceInCM = 0;
float distanceInInch = 0;

unsigned int nearestDistance = 50; // jarak terdekat sensor mendeteksi objek dalam CM

/* Status led, servo, dan sensor jarak */

bool freshStart = true;
bool objectIsFound = false;

bool ledIsEnabled = false;
bool servoIsEnabled = true;

void initWiFi() {
  Serial.println("Menghubungkan ke Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println();
  Serial.println("Terhubung ke jaringan\n");
  Serial.println("Alamat IP: " + WiFi.localIP().toString());
  Serial.println("Alamat MAC: " + WiFi.macAddress());
  Serial.println("Kekuatan sinyal: " +String(WiFi.RSSI()) + " dBm");
  Serial.println();
}

void initFirebase() {
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  config.timeout.serverResponse = 10 * 1000;
  config.signer.test_mode = true; // tanpa login

  fbdo.setResponseSize(2048); // ukuran payload yand dikumpulkan dari objek FirebaseData

  Firebase.reconnectWiFi(true);
  Firebase.setDoubleDigits(5);
  Firebase.begin(&config, &auth); // inisialisasi library firebase
}

void initialize() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);

  servo.attach(SERVO_PIN);
  servo.write(servoMinDegree);
}

void setup() {
  Serial.begin(115200);

  initWiFi();
  initFirebase();
  initialize();

  timeClient.begin();
  timeClient.setTimeOffset(25200);
}

void loop() {
  timeClient.update();

  if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();
    checkVariableUpdates(); // perbarui nilai yang tersimpan saat ini dengan yg ada di database
  }

  // cek sensor
  
  String status = "initial";
  String message = "";
  bool updateCurrentLed = false;

  readDistance();

  if (distanceInCM <= nearestDistance) {
    status = "object-detected";
    message = "Paket sedang memasuki gudang";
    objectIsFound = true;

    ledIsEnabled = true;
    updateCurrentLed = true;

    digitalWrite(LED_PIN, LOW);
    if (servoIsEnabled) servo.write(servoMaxDegree);
    if (servoPauseTime > 0) delay(servoPauseTime);
    printMessage(message);
  } else if (distanceInCM > nearestDistance && distanceInCM <= (nearestDistance*2)) {
    status = "object-in-range";
    message = "Terdeteksi objek dalam jarak " + String(distanceInCM) + " cm" + ", atau " + String(distanceInInch) + " inch";
    objectIsFound = true;

    ledIsEnabled = false;

    digitalWrite(LED_PIN, HIGH);
    if (servoIsEnabled) servo.write(servoMaxDegree);
    printMessage(message);
  } else {
    if (objectIsFound) {
      status = "object-away";
      message = "Objek telah menjauh dari jangkauan";

      printMessage(message);
    }

    objectIsFound = false;

    ledIsEnabled = false;
    updateCurrentLed = true;

    digitalWrite(LED_PIN, HIGH);
    if (servoIsEnabled) servo.write(servoMinDegree);
  }

  // simpan data yang diterima

  updateDistance();
  if (!status.isEmpty() && !message.isEmpty()) updateHistory(status, message);    
  if (updateCurrentLed) updateLED();

  delay(1000);
}

/* Fungsi utama */

void readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH);

  distanceInCM = duration * SOUND_VELOCITY/2;
  distanceInInch = distanceInCM * CM_TO_INCH;
}

void printMessage(String newMessage) {
  String time = getFormattedTime();
  
  Serial.println(time);
  Serial.println("pesan: " + newMessage + "\n");
}

String getFormattedTime() {
  time_t epochTime = timeClient.getEpochTime();

  struct tm *ptm = gmtime ((time_t *)&epochTime); 

  int monthDay = ptm->tm_mday;
  String dayNumber = monthDay < 10 ? "0" + String(monthDay) : String(monthDay);

  int currentMonth = ptm->tm_mon+1;
  String monthNumber = currentMonth < 10 ? "0" + String(currentMonth) : String(currentMonth);

  int currentYear = ptm->tm_year+1900;

  String dateFormat = String(currentYear) + "-" + monthNumber + "-" + dayNumber + " " + timeClient.getFormattedTime();

  return dateFormat;
}

/* Fungsi pendukung */

void updateLED() {
  String time = getFormattedTime(); // dapatkan tanggal dan waktu sekarang

  if (!Firebase.RTDB.setString(&fbdo, "led_status/date", time)) printMessage(fbdo.errorReason().c_str());
  if (!Firebase.RTDB.setBool(&fbdo, "led_status/status", ledIsEnabled)) printMessage(fbdo.errorReason().c_str());
}

void updateDistance() {
  String time = getFormattedTime(); // dapatkan tanggal dan waktu sekarang

  if (!Firebase.RTDB.setFloat(&fbdo, "distance/cm", distanceInCM)) printMessage(fbdo.errorReason().c_str());
  if (!Firebase.RTDB.setFloat(&fbdo, "distance/inch", distanceInInch)) printMessage(fbdo.errorReason().c_str());
  if (!Firebase.RTDB.setFloat(&fbdo, "distance/detection_distance", (nearestDistance*2))) printMessage(fbdo.errorReason().c_str());
}

void updateHistory(String status, String message) {
  String time = getFormattedTime(); // dapatkan tanggal dan waktu sekarang
  String getDate = time.substring(0, time.indexOf(' ')); // pecah bagian tanggal dari spasi (format: 16-May-2023 14:01:00)
  String getTime = timeClient.getFormattedTime();

  if (!Firebase.RTDB.setString(&fbdo, "/system_notification/" + getDate + "/" + getTime + "/" + "status", status)) printMessage(fbdo.errorReason().c_str());
  if (!Firebase.RTDB.setString(&fbdo, "/system_notification/" + getDate + "/" + getTime + "/" + "message", message)) printMessage(fbdo.errorReason().c_str());
}

void checkVariableUpdates() {
  if (Firebase.RTDB.getString(&fbdo, "/configuration")) {
    printMessage("Updating variables.");
    servoMaxDegree = Firebase.RTDB.getInt(&fbdo, "/configuration/servo_max_degree") ? fbdo.to<int>() : servoMaxDegree;
    servoMinDegree = Firebase.RTDB.getInt(&fbdo, "/configuration/servo_min_degree") ? fbdo.to<int>() : servoMinDegree;
    servoPauseTime = Firebase.RTDB.getInt(&fbdo, "/configuration/servo_pause_time") ? fbdo.to<int>() : servoPauseTime;

    nearestDistance = Firebase.RTDB.getInt(&fbdo, "/configuration/nearest_distance_sensor") ? fbdo.to<int>() : nearestDistance;

    servoIsEnabled = Firebase.RTDB.getBool(&fbdo, "/configuration/servo_enabled") ? fbdo.to<bool>() : servoIsEnabled;

    if (freshStart) {
      updateLED();
      updateHistory("initial", "Perangkat berhasil disiapkan.");
      printMessage("Perangkat berhasil disiapkan.");
    }
  } else {
    printMessage("Setting up variables.");
    if (!Firebase.RTDB.setInt(&fbdo, "/configuration/servo_max_degree", servoMaxDegree)) printMessage(fbdo.errorReason().c_str());
    if (!Firebase.RTDB.setInt(&fbdo, "/configuration/servo_min_degree", servoMinDegree)) printMessage(fbdo.errorReason().c_str());
    if (!Firebase.RTDB.setInt(&fbdo, "/configuration/servo_pause_time", servoPauseTime)) printMessage(fbdo.errorReason().c_str());

    if (!Firebase.RTDB.setInt(&fbdo, "/configuration/nearest_distance_sensor", nearestDistance)) printMessage(fbdo.errorReason().c_str());

    if (!Firebase.RTDB.setInt(&fbdo, "/configuration/servo_enabled", servoIsEnabled)) printMessage(fbdo.errorReason().c_str());

    updateLED();
    updateHistory("initial", "Perangkat berhasil disiapkan.");
    printMessage("Perangkat berhasil disiapkan.");
  }

  freshStart = false;
}