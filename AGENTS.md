# AGENTS.md — RFID Kapı Erişim Kontrol Sistemi

Bu dosya, projeye yardım eden AI asistanlar için rehber belgedir.
Her yeni oturumda bu dosyayı oku ve ona göre davran.

---

## Proje Kimliği

**Proje:** ESP32 tabanlı RFID kapı erişim kontrol sistemi  
**Framework:** ESP-IDF v5.x (Arduino framework kullanılmıyor)  
**Dil:** C (C++ kullanılmıyor)  
**Hedef:** Tek kart okuyuculu, web yönetim panelli, OTA destekli, internetsiz çalışabilen kapı sistemi

---

## Donanım

| Bileşen | Model / Detay |
|---|---|
| MCU | ESP32 (dual-core, 240MHz, 4MB flash) |
| RFID Okuyucu | RC522 — SPI (VSPI) |
| MOSFET | N-kanal MOSFET — GPIO 16, aktif-HIGH |
| Yeşil LED | Kapı açık göstergesi — MOSFET çıkışı üzerinden |
| Kırmızı LED | Sürekli yanık (standby), erişim verilince söner — doğrudan GPIO 14 |

### Pin Haritası

| Sinyal | GPIO |
|---|---|
| RC522 CS | 5 |
| RC522 SCK | 18 |
| RC522 MOSI | 23 |
| RC522 MISO | 19 |
| RC522 RST | 22 |
| Röle tetikleme (IN) | 16 |
| Kırmızı LED | 14 |

**MOSFET devre bağlantısı:**
```
ESP GPIO 16     → MOSFET Gate (direnç ile)
MOSFET Drain    → Yeşil LED(+) → [direnç] → VCC
MOSFET Source   → GND
```
GPIO 16 HIGH olduğunda MOSFET iletime geçer, yeşil LED yanar.

Kırmızı LED doğrudan GPIO 14'ten sürülür. Sistem açıldığında sürekli yanık kalır (standby göstergesi). Erişim verildiğinde söner, kapı kapandığında tekrar yanar.

---

## Yazılım Mimarisi

### Dizin Yapısı
```
project/
├── AGENTS.md
├── PROGRESS.md
├── CMakeLists.txt
├── partitions.csv
├── sdkconfig.defaults
├── main/
│   ├── main.c
│   └── CMakeLists.txt
└── components/
    ├── nvs_store/        — Kart listesi + config (NVS)
    ├── auth_manager/     — Admin oturum yönetimi (SHA-256 + session token)
    ├── rfid_manager/     — RC522 sürücüsü + kart okuma
    ├── door_controller/  — Kapı mantığı + LittleFS log sistemi
    ├── wifi_manager/     — AP+STA Wi-Fi yönetimi
    ├── web_server/       — HTTP server + REST API + embedded HTML
    └── ota_manager/      — OTA güncelleme + rollback
```

### FreeRTOS Task Yapısı

| Task | Öncelik | Core | Açıklama |
|---|---|---|---|
| rfid_task | 5 | 1 | RC522 poll, UID → Queue |
| door_task | 4 | 0 | Queue dinle, kapı kontrol, log yaz |
| web_server | 3 | 0 | HTTP isteklerini işle |
| ota_task | 2 | 0 | OTA yazma (tetiklenince başlar) |

**Task haberleşmesi:** Sadece FreeRTOS Queue. Global değişken yasak.

### Depolama

| Veri | Nerede | Detay |
|---|---|---|
| Kart listesi | NVS (`cards`) | UID hex string + isim |
| Config | NVS (`config`) | door_delay, ap_pass, wifi_ssid, wifi_pass |
| Admin auth | NVS (`auth`) | username, SHA-256 hash, session token, expiry |
| Erişim logları | LittleFS (`storage` partition) | Günlük .log dosyaları, 14 gün saklanır |
| Firmware | OTA_0 / OTA_1 | Rollback destekli çift slot |

### Partition Tablosu
```csv
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x5000
otadata,  data, ota,      0xe000,   0x2000
ota_0,    app,  ota_0,    0x10000,  0x1D0000
ota_1,    app,  ota_1,    0x1E0000, 0x1D0000
storage,  data, spiffs,   0x3B0000, 0x40000
```
> OTA slot: 1856KB | storage: 256KB | Güvenlik payı: 64KB | Flash sonu: 0x3F0000

### Log Formatı
```
/littlefs/logs/YYYY-MM-DD.log
```
Her satır bağımsız JSON objesi (array değil):
```json
{"ts":1741651200,"uid":"A3F2C1B0","name":"Umur","result":"OK"}
{"ts":1741651260,"uid":"DEADBEEF","name":"unknown","result":"DENY"}
```
14 günden eski dosyalar `door_task` içinde otomatik silinir.

### Auth Sistemi
- Tek süper admin hesabı
- Fabrika varsayılanı: `admin / admin1234`
- Şifre SHA-256 hash olarak NVS'te saklanır (düz metin asla)
- Session token: 32 byte hex, `esp_random()` ile üretilir, 24 saat geçerli
- Cookie: `session=TOKEN; HttpOnly; Path=/`
- Kart sahibi ≠ web admin — tamamen bağımsız sistemler

### REST API

**Korumasız:**
- `GET  /login` — login sayfası
- `POST /login` — giriş `{username, password}`
- `POST /logout` — çıkış

**Korumalı (geçerli session cookie zorunlu):**
- `GET  /` — yönetim paneli
- `GET/POST /api/cards` — kart listesi yönetimi
- `DELETE /api/cards?uid=XX` — kart silme
- `GET  /api/logs?date=YYYY-MM-DD` — log görüntüleme
- `GET  /api/logs/dates` — mevcut log tarihleri
- `DELETE /api/logs?date=YYYY-MM-DD` — log silme
- `GET  /api/status` — sistem durumu
- `POST /api/config` — ayar güncelleme
- `POST /api/auth/password` — şifre değiştirme
- `POST /api/ota` — OTA güncelleme

---

## Kod Yazma Kuralları

Bu projeye kod yazarken aşağıdaki kurallara MUTLAKA uy:

1. **Hata yönetimi:** Her ESP-IDF fonksiyon dönüşü `ESP_ERROR_CHECK()` veya `if (ret != ESP_OK)` ile kontrol edilmeli
2. **Loglama:** `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE` kullan — `printf` kullanma
3. **Bellek:** Stack allocation tercih et; heap kullanıyorsan `free()` unutma
4. **Task haberleşmesi:** Task'lar arası veri sadece FreeRTOS Queue ile — global değişken kullanma
5. **Mutex:** Paylaşılan kaynaklara (NVS, LittleFS) erişimde mutex kullan
6. **Watchdog:** Her task'ta `vTaskDelay` ile watchdog beslenmeli
7. **Dosya yapısı:** Her component kendi `component_name.h` ve `component_name.c` dosyasına sahip olmalı
8. **Dil:** Sadece C — C++ kullanılmıyor
9. **SHA-256:** `mbedtls/sha256.h` kullan (ESP-IDF içinde gelir)
10. **Tek sorumluluk:** Her component sadece kendi işini yapmalı, başka component'in işine karışmamalı

---

## Bağımlılıklar

| Kütüphane | Kaynak | Kurulum |
|---|---|---|
| esp-idf-rc522 | github.com/abobija/esp-idf-rc522 | `idf.py add-dependency abobija/esp-idf-rc522` |
| LittleFS | github.com/joltwallet/esp_littlefs | `idf.py add-dependency joltwallet/littlefs` |
| mbedTLS | ESP-IDF built-in | Ek kurulum gerekmez |
| esp_http_server | ESP-IDF built-in | Ek kurulum gerekmez |
| esp_ota_ops | ESP-IDF built-in | Ek kurulum gerekmez |

---

## Geliştirme Durumu ve Dosya Yönetimi

Güncel geliştirme durumu için `PROGRESS.md` dosyasına bak.

### Her Oturumun Başında Yap
1. Bu dosyayı (`AGENTS.md`) oku — proje bağlamını kavra
2. `PROGRESS.md` dosyasını oku — aktif aşamayı ve nerede kalındığını anla
3. Tamamlanan aşamaların kodlarını bağlam olarak al

### Her Aşama Tamamlandığında Yap
1. `PROGRESS.md` güncelle:
   - Aşamanın durumunu `⬜ → ✅` yap, tamamlanma tarihini yaz
   - Faz bittiyse tamamlanma kriterlerini işaretle
   - `Aktif aşama`, `Genel ilerleme` ve `Sonraki Adım` bölümlerini güncelle
   - Önemli karar alındıysa `Notlar ve Kararlar` bölümüne ekle
2. `AGENTS.md` güncelle (gerekiyorsa):
   - Mimari değişiklik → ilgili bölümü güncelle
   - Yeni component → dizin yapısına ekle
   - API veya pin değişikliği → ilgili tabloyu güncelle

---

## Önemli Notlar

- ESP-IDF versiyonu **5.x** — eski API kullanma
- `esp_wifi_set_mode(WIFI_MODE_APSTA)` ile AP+STA aynı anda aktif
- OTA partition tablosu değiştirilmemeli
- NVS'te binary değil, her zaman string (hex) sakla
- Log dosyaları JSON array değil, satır satır JSON objesi (append performansı için)
- Cookie'de `HttpOnly` zorunlu — JS'den token erişimi engellenmeli
- Şifre değiştirildiğinde mevcut token NVS'ten silinmeli
