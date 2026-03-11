# RFID Kapı Erişim Kontrol Sistemi

ESP32 tabanlı, RFID kart okuyuculu, web yönetim panelli kapı erişim kontrol sistemi.

## Özellikler

- **RFID Kart Okuma** — RC522 modülü ile temassız kart tanıma
- **Web Yönetim Paneli** — Telefondan veya bilgisayardan kart ekleme/silme, log görüntüleme, sistem ayarları
- **Oturum Güvenliği** — SHA-256 şifre hash, 24 saat geçerli session token, HttpOnly cookie
- **Çevrimdışı Çalışma** — İnternet bağlantısı gerektirmez, kendi Wi-Fi AP'sini açar
- **Erişim Logları** — LittleFS üzerinde günlük JSON log dosyaları, 14 gün saklama
- **OTA Güncelleme** — Wi-Fi üzerinden kablosuz firmware güncelleme, rollback desteği
- **Saat Senkronizasyonu** — Web panelinden otomatik (tarayıcı saati) + SNTP (internet varsa)
- **Hızlı Yetkilendirme** — Log ekranından tek tuşla reddedilen kartı yetkilendirme

## Donanım

### Gerekli Bileşenler

| Bileşen | Model / Detay |
|---|---|
| MCU | ESP32 (dual-core, 240MHz, 4MB flash) |
| RFID Okuyucu | RC522 — SPI bağlantılı |
| Röle Modülü | 3.3V veya 5V DC, HIGH-trigger |
| Kırmızı LED | Erişim reddedildi göstergesi |
| Direnç | 220Ω (kırmızı LED için) |

### Pin Bağlantıları

#### RC522 RFID Okuyucu (SPI — VSPI)

| RC522 Pin | ESP32 GPIO |
|---|---|
| SDA (CS) | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 22 |
| 3.3V | 3.3V |
| GND | GND |

#### Röle Modülü

| Röle Pin | Bağlantı |
|---|---|
| IN | GPIO 26 |
| VCC | 3.3V (veya 5V, modüle göre) |
| GND | GND |

Röle kontaklarına (COM ve NO) kapı kilidi veya test LED'i bağlanır:

```
3.3V ── [220Ω] ── LED(+) ── LED(−) ── COM
                                        │
                                NO ─── GND
```

Röle çektiğinde COM-NO arası kapanır, LED yanar (veya kapı kilidi açılır).

#### Kırmızı LED (Erişim Reddedildi)

| Bağlantı | Pin |
|---|---|
| GPIO 14 → 220Ω → LED(+) | Anot |
| LED(−) → GND | Katot |

## Yazılım Mimarisi

### Teknoloji

- **Framework:** ESP-IDF v5.x
- **Dil:** C
- **RTOS:** FreeRTOS (ESP-IDF dahili)
- **Web:** Embedded HTML/CSS/JS (SPA)

### Dizin Yapısı

```
kapi/
├── CMakeLists.txt              # Root CMake
├── partitions.csv              # Özel partition tablosu
├── sdkconfig.defaults          # ESP-IDF yapılandırma varsayılanları
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # Uygulama giriş noktası, bileşen başlatma
└── components/
    ├── nvs_store/              # Kart listesi + sistem ayarları (NVS)
    ├── auth_manager/           # Admin oturum yönetimi (SHA-256 + session token)
    ├── rfid_manager/           # RC522 sürücüsü + kart okuma
    ├── door_controller/        # Kapı mantığı + LittleFS log sistemi
    ├── wifi_manager/           # AP+STA Wi-Fi yönetimi + SNTP
    ├── web_server/             # HTTP server + REST API + embedded HTML
    └── ota_manager/            # OTA güncelleme + rollback
```

### Bileşenler

| Bileşen | Sorumluluk |
|---|---|
| **nvs_store** | Kart CRUD (UID + isim), sistem ayarları (kapı süresi, AP şifresi, Wi-Fi bilgileri). Mutex korumalı NVS erişimi. |
| **auth_manager** | Tek admin hesabı, SHA-256 şifre hash, 32 byte random session token, 24 saat expiry, şifre değiştirme. |
| **rfid_manager** | RC522 SPI sürücüsü (abobija/rc522 v3.4), event-driven kart algılama, UID → hex string, FreeRTOS Queue ile iletişim. |
| **door_controller** | LittleFS mount, GPIO kontrol (röle + kırmızı LED), door_task (Queue'dan kart oku → yetki kontrol → kapı aç/kapat → log yaz), 14 gün log temizliği. |
| **wifi_manager** | AP+STA eşzamanlı mod, AP: "KAPI-SISTEMI", STA: opsiyonel (NVS'ten), exponential backoff ile yeniden bağlanma, SNTP saat senkronizasyonu. |
| **web_server** | HTTP server (port 80), 15 REST endpoint, session cookie doğrulama, embedded HTML (login + dashboard SPA), cJSON ile JSON işleme. |
| **ota_manager** | Çift OTA slot (ota_0/ota_1), streaming firmware upload, otomatik rollback, firmware bilgi sorgulama. |

### FreeRTOS Task Yapısı

| Task | Öncelik | Core | Açıklama |
|---|---|---|---|
| rfid_task | 5 | 1 | RC522 poll (100ms aralık), kart algılama |
| door_task | 4 | 0 | Queue dinle, yetki kontrol, kapı kontrol, log yaz |
| web_server | 3 | 0 | HTTP isteklerini işle |

Task'lar arası iletişim yalnızca FreeRTOS Queue ile yapılır.

### Depolama

| Veri | Konum | Format |
|---|---|---|
| Kart listesi | NVS (`nvs` partition) | UID hex string → isim |
| Sistem ayarları | NVS | door_delay, ap_pass, wifi_ssid, wifi_pass |
| Admin bilgileri | NVS | username, SHA-256 hash, session token, expiry |
| Erişim logları | LittleFS (`storage` partition) | Günlük `.log` dosyaları, satır satır JSON |
| Firmware | OTA_0 / OTA_1 | Çift slot, rollback destekli |

### Partition Tablosu

| Partition | Tür | Boyut |
|---|---|---|
| nvs | NVS | 20 KB |
| otadata | OTA data | 8 KB |
| ota_0 | OTA app | 1856 KB |
| ota_1 | OTA app | 1856 KB |
| storage | LittleFS | 256 KB |

Toplam: 4 MB flash

### Log Formatı

Dosya yolu: `/littlefs/logs/YYYY-MM-DD.log`

Her satır bağımsız bir JSON objesi:

```json
{"ts":1741651200,"uid":"A3F2C1B0","name":"Umur","result":"OK"}
{"ts":1741651260,"uid":"DEADBEEF","name":"unknown","result":"DENY"}
```

## Kurulum

### Gereksinimler

- [ESP-IDF v5.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/)
- ESP32 geliştirme kartı (4MB flash)
- RC522 RFID modülü
- USB kablosu

### Build ve Flash

```bash
# Projeyi klonla
git clone <repo-url>
cd kapi

# Build
idf.py build

# Flash ve seri monitör
idf.py flash monitor
```

İlk build'de bağımlılıklar (`abobija/rc522`, `joltwallet/littlefs`) otomatik indirilir.

### sdkconfig Değişikliği

`sdkconfig.defaults` değiştirildiğinde:

```bash
# Mevcut config ve build'i temizle
idf.py fullclean
rm sdkconfig

# Yeniden build
idf.py build
```

## Kullanım

### İlk Başlatma

1. Firmware flash'landıktan sonra ESP32 otomatik başlar
2. RC522 bağlı değilse sistem RFID'siz devam eder (web paneli çalışır)
3. "KAPI-SISTEMI" Wi-Fi ağı açılır

### Web Paneline Erişim

1. Telefondan veya bilgisayardan **KAPI-SISTEMI** ağına bağlan
   - Şifre: `kapi1234` (varsayılan)
2. Tarayıcıda `http://192.168.4.1` adresine git
3. Giriş bilgileri:
   - Kullanıcı adı: `admin`
   - Şifre: `admin1234` (varsayılan)

### Web Paneli Sekmeleri

#### Kartlar
- Kayıtlı kartları listeler
- UID ve isim girerek yeni kart ekle
- Mevcut kartları sil

#### Loglar
- Tarihe göre erişim loglarını görüntüle
- Her log girdisi: saat, sonuç (OK/DENY), UID, kart sahibi
- **DENY** loglarının yanında "Yetki Ver" butonu — tıkla, isim gir, kart anında yetkilendirilsin
- Log dosyalarını sil

#### Ayarlar

**Genel Ayarlar:**
- Kapı açık süresi (1-30 saniye, varsayılan: 3)
- AP şifresi (min 8 karakter)
- Wi-Fi SSID ve şifre (STA modu — opsiyonel)

**Şifre Değiştir:**
- Admin panel şifresini değiştirir
- Değişiklik sonrası yeniden giriş gerekir

**OTA Firmware Güncelleme:**
1. `idf.py build` ile yeni firmware oluştur
2. `build/kapi.bin` dosyasını seç
3. "Güncellemeyi Başlat" butonuna bas
4. İlerleme çubuğu tamamlanınca sistem yeniden başlar
5. Başarısız olursa otomatik eski firmware'e döner (rollback)

#### Durum
- Çalışma süresi, boş bellek
- Wi-Fi durumu (STA ve AP)
- Depolama kullanımı
- Kayıtlı kart sayısı
- Firmware bilgileri (versiyon, derleme tarihi, aktif partition)

### REST API

**Korumasız (oturum gerektirmez):**

| Yöntem | Endpoint | Açıklama |
|---|---|---|
| GET | `/login` | Login sayfası |
| POST | `/login` | Giriş `{username, password}` |
| POST | `/logout` | Çıkış |

**Korumalı (geçerli session cookie gerekli):**

| Yöntem | Endpoint | Açıklama |
|---|---|---|
| GET | `/` | Dashboard (SPA) |
| GET | `/api/cards` | Kart listesi |
| POST | `/api/cards` | Kart ekle `{uid, name}` |
| DELETE | `/api/cards?uid=XX` | Kart sil |
| GET | `/api/logs?date=YYYY-MM-DD` | Log görüntüle |
| GET | `/api/logs/dates` | Mevcut log tarihleri |
| DELETE | `/api/logs?date=YYYY-MM-DD` | Log sil |
| GET | `/api/status` | Sistem durumu |
| POST | `/api/config` | Ayar güncelle |
| POST | `/api/auth/password` | Şifre değiştir |
| POST | `/api/ota` | OTA firmware upload |
| POST | `/api/time` | Sistem saatini ayarla `{timestamp}` |

## Güvenlik

- Şifreler düz metin saklanmaz — SHA-256 hash (mbedTLS)
- Session token: 32 byte, `esp_random()` ile üretilir, 24 saat geçerli
- Cookie: `HttpOnly` — JavaScript'ten erişilemez
- Kart sahibi ve web admin tamamen bağımsız sistemler
- Şifre değiştirildiğinde mevcut oturum iptal edilir
- OTA başarısız olursa otomatik rollback

## Varsayılan Değerler

| Ayar | Değer |
|---|---|
| AP SSID | KAPI-SISTEMI |
| AP Şifre | kapi1234 |
| Admin Kullanıcı | admin |
| Admin Şifre | admin1234 |
| Kapı Açık Süresi | 3 saniye |
| Log Saklama | 14 gün |
| Session Süresi | 24 saat |

## Lisans

Bu proje özel kullanım amaçlı geliştirilmiştir.
