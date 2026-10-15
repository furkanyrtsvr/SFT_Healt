# SFT Health — Mikroişlemci kaynakları V17

`SFT_V17_Elektronik_Cihaz_Tam_Kaynak.zip` içinden aktarılan tam V17 uygulama kaynakları. `src/` altında 13 C dosyası ve 13 başlık; kökte `app.overlay`, `prj.conf`, `CMakeLists.txt` bulunur. İlk C arşiviyle ortak 13 dosyanın içerikleri aynıdır. Zephyr cihaz modeli, devicetree, GPIO/I²C, settings depolaması ve Nordic UART Service kullanır.

![Breadboard üzerinde SFT Health prototipi](docs/images/donanim-prototipi.png)

Fotoğraf kullanıcı tarafından sağlanmıştır. Bileşen yerleşimini gösterir; kablo renklerinden fiziksel pin numarası çıkarılmamıştır.

## Kart ve modüller

| Bileşen | Görevi | Kaynaktaki dayanak |
| --- | --- | --- |
| Pro Micro nRF52840 | Nordic nRF52840 ile ana kontrol ve BLE | Kullanıcı kart teyidi; `prj.conf`: `nRF52840_fy`; Zephyr/NUS |
| MAX30102 | Optik örneklerden nabız/BPM | `max30102.c`, `heart_rate.c`; I²C adresi `0x57` |
| Basınç modülü + HX711 arayüzü | Solunum basıncı ham verisi, MEP/MIP | `pressure_sensor.c`; 24 bit okuma, Kanal A ×64 |
| DHT11 | Ortam sıcaklığı ve bağıl nem | `environment_sensor.c`, devicetree `dht0` |
| DS1302 RTC | Tarih/saat ve ölçüm zamanları | `rtc_clock.c`, CE/CLK/I/O hatları |
| SSD1306 128×64 OLED | Menü, nabız dalgası, sonuç ve durum ekranları | `app.overlay`: `solomon,ssd1306`, adres `0x3C` |
| Döner enkoder + anahtarı | Menü gezinmesi, kısa/uzun basış | `input_encoder.c`, 900 ms uzun basış |
| Breadboard, USB, jumper, RTC pili | Prototip bağlantısı ve besleme | Sağlanan donanım fotoğrafı |

Kart hedefi `promicro_nrf52840/nrf52840/uf2`, bootloader UF2 ve SDK nRF Connect SDK v3.4.0’dır. Basınç transdüserinin tam üretici/modeli belirtilmedi; kaynakta HX711 arayüzü kullanılır.

## Pinler ve bağlantılar

Fiziksel bağlantılar kullanıcı tarafından doğrulandı; GPIO tanımları `app.overlay` içindedir. Ortak I²C SDA/SCL kart pinleri kullanıcı eşlemesinden gelir; overlay I²C pinctrl’ü yeniden tanımlamaz, seçilen board hedefini devralır.

| Modül hattı | Kart pini | nRF52840 GPIO |
| --- | --- | --- |
| OLED + MAX30102 SDA | D6 | P1.00 |
| OLED + MAX30102 SCL | D7 | P0.11 |
| Enkoder A | D8 | P1.04 |
| Enkoder B | D9 | P1.06 |
| Enkoder butonu / diğer uç GND | D10 | P0.09 |
| HX711 PD_SCK | D2 | P0.17 |
| HX711 DOUT | D3 | P0.20 |
| DHT11 DATA | D4 | P0.22 |
| DS1302 CLK | D5 | P0.24 |
| DS1302 DATA | D15/A0 | P0.02 |
| DS1302 CE/RST | D12 | P1.11 |

Modüllerin GND uçları ortak, beslemeleri kullanıcının bildirdiği şekilde **3V3** olmalıdır. DHT11 DATA hattında 3V3’e **4,7–10 kΩ pull-up** kullanılır. OLED `0x3C`, MAX30102 `0x57` adresindedir; MAX30102 INT pini kullanılmaz.

[Ayrıntılı donanım ve pin belgesi](docs/DONANIM_VE_PINLER.md). C makrolarında bu property adlarının tireleri alt çizgiye dönüşür; örneğin `pressure_sck_gpios`.

## Kod haritası

| Dosya | İçerik |
| --- | --- |
| `main.c` | Ana ekran/durum makinesi; kişi seçimi, nabız, MEP/MIP, ortam, RTC ve kalibrasyon akışı |
| `display_ui.c` | OLED piksel/metin çizimi, menüler, sonuç ekranları ve nabız dalgası |
| `environment_sensor.c` | DHT11 ölçümleri ve ortam sınıflandırması |
| `max30102.c` | MAX30102 register/FIFO erişimi ve sensör ayarı |
| `heart_rate.c` | Örnek işleme, parmak algılama, atım aralığı/BPM hesabı |
| `pressure_sensor.c` | HX711 okuma, sıfırlama, kalibrasyon kaydı, doyum/sınır kontrolleri |
| `mep_test.c` | İnspirasyon/ekspirasyon denemesi; başlangıç/bitiş ve süre/pencere mantığı |
| `input_encoder.c` | Enkoder durum geçişleri ve olay kuyruğu |
| `rtc_clock.c` | DS1302 üç telli erişim, BCD tarih/saat dönüşümü |
| `profile_store.c` | Kişi profillerinin settings ile saklanması |
| `measurement_log.c` | Zaman damgalı ölçüm kayıtları |
| `ble_health.c` | BLE/NUS bağlantısı, profil/ölçüm aktarımı ve komut işleme |
| `storage_reset.c` | Depolama sürümü/sıfırlama işaretçisi |

## Kullanım akışı

1. Kart, ekran ve enkoder ile profil seçimi/menü akışını başlatır.
2. Nabız ekranı optik örnekleri işler; ortam ekranı sıcaklık/nem değerlerini gösterir.
3. MEP/MIP ekranları basınç denemelerini yönetir. Sonuç ve geçmiş kayıtlar cihazda tutulabilir.
4. RTC tarih/saat düzenlemesi cihaz menüsünden yapılabilir.
5. Web panelinden NUS destekli cihaza bağlanıldığında `SYNC` komutu gönderilir; profiller ve kayıtlar aktarılır.

`pressure_sensor.c` içinde kalibrasyonsuz prototip için nominal `355000 count/kPa` ölçeği vardır; kaynak bunun birim bazlı kalibrasyon olmadığını açıkça belirtir. 30/40 kPa kalibrasyon referansları kod parametreleridir; bunlar kullanıcıya uygulanacak nefes basıncı talimatı olarak yorumlanmamalıdır. Fiziksel doğruluk ve klinik kullanım bu aktarımda doğrulanmadı.

## Derleme için gerekenler

**nRF Connect SDK v3.4.0** ve ona ait toolchain/workspace etkin olmalıdır. `west` komutunu SDK terminalinde, bu `01-Mikroislemci/` klasöründe çalıştırın:

```sh
west build -p always -b promicro_nrf52840/nrf52840/uf2 .
```

`CMakeLists.txt` 13 `src/*.c` dosyasını derlemeye ekler. `prj.conf` GPIO, I²C, SSD1306, DHT, flash/NVS/settings ve BLE NUS ayarlarını içerir. `app.overlay` sensörleri, ekranı ve GPIO’ları tanımlar. Board hedefinin SDK ortamında mevcut olması gerekir.

UF2 yüklemesi için kartın UF2 bootloader akışını kullanın; SDK build çıktısında UF2 dosyası üretildiğinde bunu bootloader sürücüsüne aktarın. Bu aktarımda bootloader moduna geçiş veya karta program yazma işlemi yapılmadı. CMake proje adı arşivdeki `OLED_Merhaba` olarak korunmuştur. Tam kaynaklar eklendi; SDK derlemesinin bu makinede yapıldığı iddia edilmez.

Dosyalar: [src/](src/README.md), [app.overlay](app.overlay), [prj.conf](prj.conf), [CMakeLists.txt](CMakeLists.txt).

[BLE protokolü](docs/BLE_PROTOKOLU.md) · [Web tarafı](../02-Web-Sitesi/README.md) · [Ana tanıtım](../README.md)
