#include <WiFi.h>
#include <HTTPClient.h>

// ======================================================
// WIFI CONFIG
// ======================================================
const char* ssid = "****";
const char* password = "****";

// ======================================================
// SERVER FLASK
// ======================================================
String serverURL = "http://10.114.218.154:5000/api/sensor";

// ======================================================
// PIN SENSOR
// ======================================================
// Gunakan pin sesuai wiring kamu.
// Kalau wiring kamu TRIG ke GPIO 5 dan ECHO ke GPIO 18, biarkan seperti ini.
#define TRIG_PIN 26
#define ECHO_PIN 27

// Sensor hujan YL-83 / raindrop sensor AO ke GPIO 34
#define HUJAN_PIN 34

// ======================================================
// KONFIGURASI WADAH DAN WARNING SYSTEM
// ======================================================
// Tinggi toples = 21 cm
// Sensor dinaikkan 15 cm dari bibir toples
// Jadi jarak sensor ke dasar toples = 21 + 15 = 36 cm
float tinggiWadah = 36.0;

// Berdasarkan hasil uji, sensor tidak stabil jika jarak < 20 cm
float jarakMinimumValid = 20.0;

// Karena tinggiWadah 36 cm dan jarak minimum valid 20 cm,
// tinggi air maksimum yang masih valid dibaca adalah 36 - 20 = 16 cm
float tinggiMaksValid = 16.0;

// Ambang status untuk rentang tinggi air valid 0-16 cm
float batasNormal = 8.0;
float batasWaspada = 13.0;

// Kirim data setiap 30 detik
unsigned long intervalKirim = 30000;
unsigned long waktuSebelumnya = 0;

// ======================================================
// FUNGSI BACA JARAK SEKALI
// ======================================================
float bacaJarakSekali() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(5);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  // Timeout 100000 us agar sensor punya waktu cukup membaca echo
  long durasi = pulseIn(ECHO_PIN, HIGH, 100000);

  if (durasi == 0) {
    return -1;
  }

  float jarak = durasi * 0.0343 / 2.0;

  if (jarak <= 0 || jarak > 400) {
    return -1;
  }

  return jarak;
}

// ======================================================
// FUNGSI BACA JARAK RATA-RATA
// ======================================================
float bacaJarakUltrasonik() {
  float total = 0;
  int jumlahValid = 0;

  // Ambil 7 pembacaan agar nilai lebih stabil
  for (int i = 0; i < 7; i++) {
    float jarak = bacaJarakSekali();

    if (jarak > 0 && jarak <= 400) {
      total += jarak;
      jumlahValid++;
    }

    delay(100);
  }

  if (jumlahValid == 0) {
    return -1;
  }

  return total / jumlahValid;
}

// ======================================================
// FUNGSI STATUS KETINGGIAN AIR
// ======================================================
String tentukanStatus(float jarakSensor, float tinggiAir) {
  // Jika jarak sensor ke permukaan air kurang dari 20 cm,
  // nilai dianggap sudah berada di luar rentang pembacaan stabil.
  // Maka langsung diberi status BAHAYA.
  if (jarakSensor < jarakMinimumValid) {
    return "BAHAYA";
  }

  if (tinggiAir < batasNormal) {
    return "NORMAL";
  } else if (tinggiAir < batasWaspada) {
    return "WASPADA";
  } else {
    return "SIAGA";
  }
}

// ======================================================
// FUNGSI KONDISI HUJAN
// ======================================================
String tentukanKondisiHujan(int nilaiHujan) {
  // Pada banyak modul YL-83:
  // nilai kecil = basah/hujan
  // nilai besar = kering/tidak hujan
  if (nilaiHujan < 2000) {
    return "HUJAN";
  } else {
    return "TIDAK HUJAN";
  }
}

// ======================================================
// FUNGSI KONEKSI WIFI
// ======================================================
void konekWiFi() {
  Serial.println();
  Serial.println("Menghubungkan ke WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int percobaan = 0;

  while (WiFi.status() != WL_CONNECTED && percobaan < 40) {
    delay(500);
    Serial.print(".");
    percobaan++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi terhubung.");
    Serial.print("IP ESP32: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi gagal terhubung. Cek SSID/password/hotspot.");
  }
}

// ======================================================
// KIRIM DATA KE FLASK
// ======================================================
void kirimDataKeServer(float jarakSensor, float tinggiAir, String status, int nilaiHujan, String kondisiHujan) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung. Mencoba konek ulang...");
    konekWiFi();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi masih gagal. Data tidak dikirim.");
      return;
    }
  }

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String jsonData = "{";
  jsonData += "\"jarak_sensor\":" + String(jarakSensor, 2) + ",";
  jsonData += "\"tinggi_air\":" + String(tinggiAir, 2) + ",";
  jsonData += "\"status\":\"" + status + "\",";
  jsonData += "\"nilai_hujan\":" + String(nilaiHujan) + ",";
  jsonData += "\"kondisi_hujan\":\"" + kondisiHujan + "\"";
  jsonData += "}";

  Serial.println();
  Serial.println("Mengirim data ke Flask:");
  Serial.println(jsonData);

  int httpResponseCode = http.POST(jsonData);

  Serial.print("Kode respon server: ");
  Serial.println(httpResponseCode);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Respon server:");
    Serial.println(response);
  } else {
    Serial.println("Gagal kirim data ke Flask.");
    Serial.println("Cek IP laptop, endpoint Flask, firewall, dan koneksi WiFi.");
  }

  http.end();
}

// ======================================================
// SETUP
// ======================================================
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(HUJAN_PIN, INPUT);

  digitalWrite(TRIG_PIN, LOW);

  Serial.println();
  Serial.println("====================================");
  Serial.println("Sistem Monitoring Ketinggian Air");
  Serial.println("ESP32 + Ultrasonik + YL-83");
  Serial.println("====================================");
  Serial.println("Konfigurasi:");
  Serial.print("Tinggi acuan sensor ke dasar wadah: ");
  Serial.print(tinggiWadah);
  Serial.println(" cm");

  Serial.print("Jarak minimum valid sensor: ");
  Serial.print(jarakMinimumValid);
  Serial.println(" cm");

  Serial.print("Tinggi maksimum valid: ");
  Serial.print(tinggiMaksValid);
  Serial.println(" cm");

  konekWiFi();

  Serial.println("Sistem monitoring siap.");

  // Supaya data pertama langsung dikirim tanpa menunggu 30 detik
  waktuSebelumnya = millis() - intervalKirim;
}

// ======================================================
// LOOP
// ======================================================
void loop() {
  unsigned long waktuSekarang = millis();

  if (waktuSekarang - waktuSebelumnya >= intervalKirim) {
    waktuSebelumnya = waktuSekarang;

    float jarakSensor = bacaJarakUltrasonik();

    if (jarakSensor < 0) {
      Serial.println("Pembacaan ultrasonik gagal. Tidak ada data dikirim.");
      return;
    }

    float tinggiAir;

    // Jika jarak < 20 cm, data mentah tidak dipakai untuk hitungan normal.
    // Sistem langsung masuk status BAHAYA.
    // tinggiAir dibatasi ke tinggi maksimum valid agar tidak menampilkan angka ngaco.
    if (jarakSensor < jarakMinimumValid) {
      tinggiAir = tinggiMaksValid;
    } else {
      tinggiAir = tinggiWadah - jarakSensor;

      if (tinggiAir < 0) {
        tinggiAir = 0;
      }

      if (tinggiAir > tinggiMaksValid) {
        tinggiAir = tinggiMaksValid;
      }
    }

    int nilaiHujan = analogRead(HUJAN_PIN);
    String kondisiHujan = tentukanKondisiHujan(nilaiHujan);
    String status = tentukanStatus(jarakSensor, tinggiAir);

    Serial.println();
    Serial.println("====================================");
    Serial.print("Jarak sensor ke air : ");
    Serial.print(jarakSensor);
    Serial.println(" cm");

    Serial.print("Estimasi tinggi air : ");
    Serial.print(tinggiAir);
    Serial.println(" cm");

    Serial.print("Status              : ");
    Serial.println(status);

    Serial.print("Nilai AO hujan      : ");
    Serial.println(nilaiHujan);

    Serial.print("Kondisi hujan       : ");
    Serial.println(kondisiHujan);

    if (jarakSensor < jarakMinimumValid) {
      Serial.println("Keterangan          : Jarak < 20 cm, data mentah berada di luar rentang stabil sensor.");
    }

    Serial.println("====================================");

    kirimDataKeServer(jarakSensor, tinggiAir, status, nilaiHujan, kondisiHujan);
  }
}