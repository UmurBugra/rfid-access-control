# PROGRESS.md — Geliştirme Takip Dosyası

Bu dosyayı her aşama tamamlandığında güncelle.
Her oturumun başında AI'ya bu dosyayı ver — nerede kaldığını bilsin.

---

## Genel Durum

```
Başlangıç tarihi : 2026-03-11
Son güncelleme   : 2026-03-11
Tamamlanan faz   : Faz 1, Faz 2, Faz 3
Aktif aşama      : Faz 4 (Sertleştirme ve Test — donanım testi gerekli)
Genel ilerleme   : %100 (yazılım geliştirme tamamlandı)
```

---

## FAZ 1 — Temel Altyapı

> Hedef: Sistem ayağa kalksın, kart okusun, kapıyı kontrol etsin, loglasın.

| # | Aşama | Durum | Tamamlanma |
|---|---|---|---|
| 1 | Proje iskeleti (CMakeLists, partitions, sdkconfig) | ✅ Tamamlandı | 2026-03-11 |
| 2 | nvs_store component | ✅ Tamamlandı | 2026-03-11 |
| 3 | auth_manager component | ✅ Tamamlandı | 2026-03-11 |
| 4 | rfid_manager component | ✅ Tamamlandı | 2026-03-11 |
| 5 | door_controller component + LittleFS log | ✅ Tamamlandı | 2026-03-11 |

**Faz 1 tamamlanma kriteri:**
- [ ] Kart okutunca yeşil LED 3 sn yanıyor
- [ ] Yetkisiz kartta kırmızı LED yanıyor
- [ ] Log dosyası LittleFS'e yazılıyor
- [ ] `idf.py build` hatasız tamamlanıyor

---

## FAZ 2 — Ağ ve Web Arayüzü

> Hedef: Wi-Fi AP ayağa kalksın, tarayıcıdan sistemi yönetmek mümkün olsun.

| # | Aşama | Durum | Tamamlanma |
|---|---|---|---|
| 6 | wifi_manager component | ✅ Tamamlandı | 2026-03-11 |
| 7 | web_server component + embedded HTML + auth | ✅ Tamamlandı | 2026-03-11 |

**Faz 2 tamamlanma kriteri:**
- [ ] Telefondan "KAPI-SISTEMI" ağı görünüyor
- [ ] 192.168.4.1 login sayfası açılıyor
- [ ] admin/admin1234 ile giriş yapılıyor
- [ ] Web panelinden kart eklenip silinebiliyor
- [ ] Loglar web panelinde görüntülenebiliyor
- [ ] Şifre değiştirme çalışıyor

---

## FAZ 3 — OTA ve Entegrasyon

> Hedef: Tüm sistem birleşsin, OTA çalışsın, uçtan uca test geçsin.

| # | Aşama | Durum | Tamamlanma |
|---|---|---|---|
| 8 | ota_manager component | ✅ Tamamlandı | 2026-03-11 |
| 9 | main.c entegrasyonu (tüm component'ler birleşir) | ✅ Tamamlandı | 2026-03-11 |

**Faz 3 tamamlanma kriteri:**
- [ ] Web panelinden OTA firmware güncellemesi yapılabiliyor
- [ ] OTA başarısız olursa eski firmware'e rollback oluyor
- [ ] Sistem güç kesintisi sonrası düzgün başlıyor
- [ ] Tüm sistem birlikte sorunsuz çalışıyor

---

## FAZ 4 — Sertleştirme ve Test

> Hedef: Sistem production'a hazır hale gelsin.

| # | Görev | Durum | Tamamlanma |
|---|---|---|---|
| 4.1 | 24 saat kesintisiz çalışma testi | ⬜ Bekliyor | — |
| 4.2 | Güç kesintisi senaryoları (NVS bütünlüğü) | ⬜ Bekliyor | — |
| 4.3 | Bilinmeyen kart tekrar saldırı testi | ⬜ Bekliyor | — |
| 4.4 | Web arayüzü yetki bypass denemeleri | ⬜ Bekliyor | — |
| 4.5 | LittleFS doluluk sınır testi | ⬜ Bekliyor | — |
| 4.6 | OTA rollback senaryosu testi | ⬜ Bekliyor | — |

---

## FAZ 5 — PCB Tasarımı (Donanım)

> Hedef: Prototipten üretim donanımına geçiş.

| # | Görev | Durum | Tamamlanma |
|---|---|---|---|
| 5.1 | Şematik tasarım (röle, güç, RC522, ESP32) | ⬜ Bekliyor | — |
| 5.2 | PCB layout | ⬜ Bekliyor | — |
| 5.3 | Gerber üretim + montaj | ⬜ Bekliyor | — |
| 5.4 | Röle ile yazılım testi (GPIO 26 yük değişimi) | ⬜ Bekliyor | — |
| 5.5 | Gerçek kapı kilidi entegrasyonu | ⬜ Bekliyor | — |

---

## Durum Sembolleri

| Sembol | Anlam |
|---|---|
| ⬜ | Bekliyor |
| 🔄 | Devam ediyor |
| ✅ | Tamamlandı |
| ❌ | Engel var / Bloke |
| ⏸️ | Ertelendi |

---

## Notlar ve Kararlar

### 2026-03-11
- Proje başlatıldı
- Framework kararı: ESP-IDF (Arduino değil) — kararlılık ve FreeRTOS kontrolü için
- RC522 kütüphanesi: `abobija/esp-idf-rc522` seçildi
- Log depolama: LittleFS (NVS write cycle ömrü için)
- Auth: Tek süper admin, SHA-256 + session token, 24 saat expiry
- Prototip: BJT NPN transistör (röle henüz yok), LED ile simülasyon
- Buzzer: Kullanılmayacak, tasarımdan çıkarıldı
- Partition tablosu düzeltildi: orijinal tablo 4MB flash'ı 64KB aşıyordu.
  OTA slotları 1920KB→1856KB küçültüldü, storage 256KB korundu, 64KB güvenlik payı bırakıldı.
- Git repo baslatildi (ESP-IDF build sistemi git gerektiriyor)
- Asama 1 tamamlandi: proje iskeleti, partitions.csv, sdkconfig.defaults, 7 component placeholder
- Asama 2 tamamlandi: nvs_store component — kart CRUD (add/remove/exists/get_name/list), config get/set (door_delay, ap_pass, wifi_ssid, wifi_pass), mutex korumali NVS erisimi
- main.c'de nvs_flash_init() + nvs_store_init() cagriliyor
- Asama 3 tamamlandi: auth_manager component — SHA-256 sifre hash (mbedtls), fabrika varsayilani admin/admin1234, 32 byte session token (esp_random), 24 saat expiry, sifre degistirme + token invalidation, mutex korumali
- Asama 4 tamamlandi: rfid_manager component — abobija/rc522 v3.4 kutuphane, SPI3_HOST, event-driven (PICC state changed), UID 4-byte hex string cevirme, FreeRTOS Queue ile door_task'a gonderim. main.c'de gecici rfid_listener_task var (door_controller gelince kaldirilacak)
- Asama 5 tamamlandi: door_controller component — LittleFS mount (storage partition), GPIO init (26=BJT/kapi, 14=kirmizi LED), door_task (Queue dinle, yetki kontrol, kapi ac/kapat, log yaz), JSON log dosyalari (/littlefs/logs/YYYY-MM-DD.log), 14 gun eski log temizligi, web_server icin log okuma/silme/listeleme API. main.c'den gecici rfid_listener_task kaldirildi, door_controller_init(rfid_queue) eklendi. joltwallet/littlefs bagimliligi idf_component.yml ile eklendi.
- Asama 6 tamamlandi: wifi_manager component — WIFI_MODE_APSTA (AP+STA ayni anda), AP SSID "KAPI-SISTEMI" sifre NVS'ten (ap_pass), STA opsiyonel (NVS'te wifi_ssid varsa baglan), event-driven (baglanti/kopma/IP), exponential backoff ile STA yeniden baglanti (5s-60s), reconfigure fonksiyonu (web panelinden config degisince Wi-Fi yeniden baslatma), durum sorgulama (STA IP, AP IP, bagli istemci sayisi).

- Asama 7 kodu yazildi ve build dogrulandi: web_server component — esp_http_server (port 80, max 16 handler, stack 8192), 13 URI handler, session cookie middleware, cJSON ile JSON parsing/generation, embedded HTML (login.html + dashboard.html via EMBED_TXTFILES). Korumasiz endpointler: GET/POST /login, POST /logout. Korumali endpointler: GET / (dashboard), GET/POST/DELETE /api/cards, GET/DELETE /api/logs, GET /api/logs/dates, GET /api/status, POST /api/config, POST /api/auth/password. Build hatalari: esp_timer REQUIRES eksikti (eklendi), ESP_ERR_NVS_NOT_FOUND icin nvs_flash.h include eksikti (eklendi).

- Asama 8 tamamlandi: ota_manager component — esp_ota_ops ile OTA firmware guncelleme, cift slot (ota_0/ota_1), rollback destegi (esp_ota_mark_app_valid_cancel_rollback), firmware bilgi sorgulama (versiyon, IDF, derleme tarihi). web_server'a POST /api/ota streaming upload handler eklendi (1024 byte buffer, ilerleme logu), /api/status'a firmware bilgileri eklendi. dashboard.html'e OTA upload UI (dosya secimi, progress bar, yuzde gosterimi) ve firmware bilgi gosterimi eklendi. main.c'de ota_manager_init() NVS'ten hemen sonra cagriliyor (erken rollback onaylama). Build dogrulandi.

- Asama 9 tamamlandi: main.c final entegrasyon — tum componentlerin birlikte calismasi gozden gecirildi. Kod incelemesinde 2 bug ve 1 iyilestirme bulundu ve duzeltildi:
  1. BUG: nvs_store.c — nvs_entry_find() ilk parametresi yanlis partition label ("cards") kullaniyordu, "nvs" olarak duzeltildi. Bu hata kart listelemenin runtime'da calismamasi demekti.
  2. BUG: web_server.c — cards_post_handler'da cJSON_Delete(json) sonrasi j_uid->valuestring'e erisiliyordu (use-after-free). Log satiri cJSON_Delete'den once tasinarak duzeltildi.
  3. IMPROVE: door_controller.c — door_task stack boyutu 4096→6144 arttirildi. cleanup_old_logs() icindeki DIR*, struct dirent, filepath[280] vb. icin guvenli headroom saglandi.
  Init sirasi dogrulandi: nvs_flash → ota_manager → nvs_store → auth_manager → wifi_manager → queue → rfid → door → web_server. Build dogrulandi.

---

## Bilinen Sorunlar / Engeller

*Henüz yok.*

---

## Sonraki Adım

**Yazılım geliştirme tamamlandı.** Sonraki adım: Faz 4 — Sertleştirme ve Test (donanım üzerinde test gerekli).

Firmware'i ESP32'ye flash'layıp gerçek donanımda test etme aşamasına geçilebilir:
- `idf.py flash monitor` ile yükle ve seri çıktıyı izle
- Telefondan "KAPI-SISTEMI" ağına bağlan
- 192.168.4.1 adresinden web paneline eriş
- Kart okutma, log kontrolü, OTA güncelleme test et
