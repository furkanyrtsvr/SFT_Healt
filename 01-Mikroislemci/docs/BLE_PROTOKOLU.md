# BLE / web haberleşmesi

Firmware `ble_health.c` ve web `app/health-dashboard.tsx` birlikte incelenerek hazırlanmıştır.

## Nordic UART Service

| Alan | UUID |
| --- | --- |
| Servis | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| RX — web’den cihaza | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` |
| TX — cihazdan web’e bildirim | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |

Satırlar `\n` ile sonlanır ve `|` ile alanlara ayrılır. Aktarım 20 baytlık parçalar kullanır; bir BLE bildirimi bir tam satır olmak zorunda değildir. Web, parçaları biriktirip tamamlanan satırları ayrıştırır. Firmware adı `CONFIG_BT_DEVICE_NAME` üzerinden gelir; web’de varsayılan ad `nRF52840_fy` olarak yazılmıştır.

## Web → cihaz

| Komut | İşlev |
| --- | --- |
| `SYNC` / `GET ALL` | Profil ve geçmiş aktarım isteği |
| `PROFILE_SET|index|name|age|gender|height|weight` | Profil güncelleme; ad en fazla 8 karakter, gender M/W |
| `PROFILE_SELECT|index` | Aktif profil seçimi |
| `PROFILE_DELETE|index` | Profil silme |
| `SESSION_END` | Ölçüm oturumunu bitirme |
| `TIME|year|month|day|hour|minute|second|weekday` | Firmware’in desteklediği saat komutu; mevcut web istemcisi bunu göndermiyor |

## Cihaz → web

| Başlangıç | Veri |
| --- | --- |
| `P|...` | Profil alanları ve cihaz profil durumu |
| `M|HR|profile|bpm` | Canlı nabız sonucu |
| `M|MEP|profile|value_x100|predicted_x100|percent` | Ekspiratuvar basınç; web kPa için 100’e böler |
| `M|MIP|profile|value_x100|predicted_x100|percent` | İnspiratuvar basınç; web kPa için 100’e böler |
| `M|ENV|temperature_x10|humidity_x10|quality` | Ortam sıcaklığı, nemi ve kalite kodu |
| `R|id|type|profile|value_x100|predicted_x100|percent|year|month|day|hour|minute|second` | Kaydedilmiş ölçüm; değerler 100 ölçeğiyle taşınır |
| `STATUS|...` | Firmware durum metni |
| `DONE` | Senkronizasyonun tamamlandığı bildirimi |

Mevcut web ayrıştırıcısı profil, HR/MEP/MIP/ENV, kayıt ve DONE satırlarını işler; her firmware durum mesajı için özel bir ekran davranışı tanımlı değildir. Gerçek BLE bağlantısı bu yayın sırasında donanımla sınanmadı.

[Mikroişlemci](../README.md) · [Web rehberi](../../02-Web-Sitesi/README.md)
