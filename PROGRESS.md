# PROGRESS.md — Geliştirme Takip Dosyası

Bu dosyayı her aşama tamamlandığında güncelle.
Her oturumun başında AI'ya bu dosyayı ver — nerede kaldığını bilsin.

---

## Genel Durum

```
Başlangıç tarihi : 2026-03-11
Son güncelleme   : 2026-03-11
Tamamlanan faz   : —
Aktif aşama      : Faz 1 — Aşama 2
Genel ilerleme   : %11
```

---

## FAZ 1 — Temel Altyapı

> Hedef: Sistem ayağa kalksın, kart okusun, kapıyı kontrol etsin, loglasın.

| # | Aşama | Durum | Tamamlanma |
|---|---|---|---|
| 1 | Proje iskeleti (CMakeLists, partitions, sdkconfig) | ✅ Tamamlandı | 2026-03-11 |
| 2 | nvs_store component | 🔄 Devam ediyor | — |
| 3 | auth_manager component | ⬜ Bekliyor | — |
| 4 | rfid_manager component | ⬜ Bekliyor | — |
| 5 | door_controller component + LittleFS log | ⬜ Bekliyor | — |

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
| 6 | wifi_manager component | ⬜ Bekliyor | — |
| 7 | web_server component + embedded HTML + auth | ⬜ Bekliyor | — |

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
| 8 | ota_manager component | ⬜ Bekliyor | — |
| 9 | main.c entegrasyonu (tüm component'ler birleşir) | ⬜ Bekliyor | — |

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

---

## Bilinen Sorunlar / Engeller

*Henüz yok.*

---

## Sonraki Adım

**Başlanacak aşama:** Faz 1 — Aşama 2 (nvs_store component)

Yapılacak: nvs_store componentini yaz — kart CRUD + config get/set fonksiyonlari, NVS mutex koruması.
