# SFT Health

**Solunum basıncı, nabız ve ortam verilerini bir arada izleyen donanım + web prototipi.** Bu depo, `SFT_Healt` adıyla projenin V17 mikroişlemci C kaynaklarını ve tasarlanan web panelini toplar.

| Bölüm | İçerik |
| --- | --- |
| [01-Mikroislemci](01-Mikroislemci/README.md) | Zephyr tabanlı V17 C kodları; sensörler, OLED, enkoder, RTC, kayıtlar ve BLE |
| [02-Web-Sitesi](02-Web-Sitesi/README.md) | React/vinext web paneli; Bluetooth, kişi/ölçüm yönetimi, nefes egzersizi ve hastane demo akışı |

## Sistem ne yapıyor?

Donanım; optik nabız sensörünü, basınç arayüzünü, sıcaklık/nem sensörünü ve gerçek zaman saatini okur. OLED ve döner enkoder üzerinden cihazın kendi menülerinde gezinilir. Kullanıcı profilleri ve ölçümler cihazda saklanabilir. Web paneli Nordic UART Service üzerinden Bluetooth bağlantısı kurarak profilleri, canlı sonuçları ve geçmiş kayıtları alır.

Kaynaklarda **MEP/MIP basınç denemeleri**, BPM ve sıcaklık/nem verileri bulunur. Bu aktarımda SpO₂ hesaplaması veya FEV1/FVC akım-hacim ölçümü tespit edilmedi; MAX30102 bulunması bu özelliklerin uygulandığı anlamına gelmez. Klinik doğrulama sonucu sağlanmadı; paneldeki referans yüzdeleri yazılımın prototip gösterimidir.

```mermaid
flowchart LR
  A[MAX30102 • nabız] --> M[nRF52840 tabanlı sistem / Zephyr]
  B[Basınç sensörü + HX711] --> M
  C[DHT11 • sıcaklık/nem] --> M
  D[DS1302 • saat] --> M
  E[Döner enkoder ve buton] --> M
  M --> O[128×64 OLED]
  M <--> F[Cihaz profilleri ve kayıtlar]
  M <-->|BLE / Nordic UART Service| W[SFT Health web paneli]
  W <-->|Oturumlu API| DB[Cloudflare D1 / Drizzle]
```

Kart **Pro Micro nRF52840**, işlemci **Nordic nRF52840**, SDK **nRF Connect SDK v3.4.0**, Zephyr hedefi **`promicro_nrf52840/nrf52840/uf2`** ve bootloader türü **UF2** olarak kullanıcı tarafından doğrulandı. Bluetooth adı `nRF52840_fy` olarak `prj.conf` içinde tanımlıdır. Tam proje dosyaları ve [kesin pin tablosu](01-Mikroislemci/docs/DONANIM_VE_PINLER.md) eklendi.

## Görseller

### Fiziksel prototip

![Kullanıcı tarafından sağlanan breadboard prototipi](01-Mikroislemci/docs/images/donanim-prototipi.png)

### Web paneli

![Web genel bakış](02-Web-Sitesi/docs/images/01-genel-bakis.jpg)

![Nefes egzersizi ekranı](02-Web-Sitesi/docs/images/02-nefes-egzersizi.jpg)

![Hastane demo ekranı](02-Web-Sitesi/docs/images/03-hastane-demo.jpg)

Web görüntüleri kaynak uygulamanın yerel önizlemesinden alınır; bağlı cihaz veya gerçek hasta verisi içermez.

## Başlangıç

Web için [yerel kurulum rehberini](02-Web-Sitesi/README.md#yerel-kurulum), cihaz için [SDK ve derleme adımlarını](01-Mikroislemci/README.md#derleme-için-gerekenler) izleyin. Tam firmware arşivindeki `src/*.c`, `src/*.h`, `app.overlay`, `prj.conf` ve `CMakeLists.txt` korunmuştur. Firmware SDK derlemesi ve fiziksel donanım denemesi bu aktarımın parçası değildir.

[BLE protokolü](01-Mikroislemci/docs/BLE_PROTOKOLU.md) · [Kaynak aktarım kaydı](02-Web-Sitesi/docs/AKTARIM.md)

Bu aktarım yeni bir açık kaynak lisansı atamaz. Sağlanan kodlara ve bağımlılıklara ait mevcut lisans koşulları korunur.
