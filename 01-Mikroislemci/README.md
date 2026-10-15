# SFT Health — Mikroişlemci kaynakları V17

`SFT_Son_Surum_C_Dosyalari_V17.zip` içinden aktarılan 13 C dosyası. Zephyr cihaz modeli, devicetree, GPIO/I²C, settings depolaması ve Nordic UART Service kullanır.

![Breadboard üzerinde SFT Health prototipi](docs/images/donanim-prototipi.png)

Fotoğraf kullanıcı tarafından sağlanmıştır. Bileşen yerleşimini gösterir; kablo renklerinden fiziksel pin numarası çıkarılmamıştır.

## Kart ve modüller

| Bileşen | Görevi | Kaynaktaki dayanak |
| --- | --- | --- |
| nRF52840 tabanlı geliştirme kartı | Ana kontrol ve BLE | Web `health-dashboard.tsx` içinde model ve `nRF52840_fy` cihaz adı; C tarafı Zephyr/NUS |
| MAX30102 | Optik örneklerden nabız/BPM | `max30102.c`, `heart_rate.c`; I²C adresi `0x57` |
| Basınç modülü + HX711 arayüzü | Solunum basıncı ham verisi, MEP/MIP | `pressure_sensor.c`; 24 bit okuma, Kanal A ×64 |
| DHT11 | Ortam sıcaklığı ve bağıl nem | `environment_sensor.c`, devicetree `dht0` |
| DS1302 RTC | Tarih/saat ve ölçüm zamanları | `rtc_clock.c`, CE/CLK/I/O hatları |
| 128×64 OLED | Menü, nabız dalgası, sonuç ve durum ekranları | `display_ui.c`, `zephyr,display` |
| Döner enkoder + anahtarı | Menü gezinmesi, kısa/uzun basış | `input_encoder.c`, 900 ms uzun basış |
| Breadboard, USB, jumper, RTC pili | Prototip bağlantısı ve besleme | Sağlanan donanım fotoğrafı |

Kartın tam model/revizyonu, basınç transdüserinin modeli ve OLED denetleyici modeli kesinleştirilemedi. OLED için SSD1306 gibi bir model adı bu arşivden doğrulanamaz.

## Pinler ve bağlantılar

**Fiziksel GPIO numaraları teslim edilen dosyalarda bulunmuyor.** Kod, bunları dışarıda kalan devicetree `.overlay` dosyasından alıyor. Kullanılacak bağlantılar:

| İşlev | Devicetree kaynağı | Modül hattı |
| --- | --- | --- |
| Nabız sensörü I²C | `DT_NODELABEL(i2c0)` | MAX30102 SDA / SCL; adres `0x57` |
| OLED | `DT_CHOSEN(zephyr_display)` | Ekran veri yolu ve adresi overlay ile belirlenir |
| DHT11 | `DT_ALIAS(dht0)` | DATA |
| Basınç saati | `/zephyr,user` → `pressure-sck-gpios` | HX711 PD_SCK |
| Basınç verisi | `/zephyr,user` → `pressure-out-gpios` | HX711 DOUT |
| RTC saati | `/zephyr,user` → `rtc-clk-gpios` | DS1302 SCLK |
| RTC verisi | `/zephyr,user` → `rtc-data-gpios` | DS1302 I/O |
| RTC seçimi | `/zephyr,user` → `rtc-ce-gpios` | DS1302 CE/RST |
| Enkoder A / B | `encoder-a-gpios`, `encoder-b-gpios` | A/CLK, B/DT |
| Enkoder butonu | `encoder-sw-gpios` | SW |

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

Arşiv tam Zephyr projesi değildir. Şunlar ayrıca gerekir:

- Kaynakların `#include` ettiği 13 proje `.h` dosyası.
- `CMakeLists.txt` ve sürümü belirli `prj.conf`.
- Kesin kart hedefi, `.overlay`, pinctrl/I²C ve ekran/sensör tanımları.
- Uyumlu Zephyr / nRF Connect SDK sürümü, NUS ve settings/flash yapılandırması.
- Bootloader, programlayıcı/yükleme yolu ve varsa flash bölüm tanımları.

Bu bilgiler gelmeden rastgele `west build -b ...` hedefi veya pin yerleşimi önerilmiyor. C kaynakları içerik değişikliği yapılmadan korunmuştur; firmware derlemesi veya karta yazma yapılmadı.

[BLE protokolü](docs/BLE_PROTOKOLU.md) · [Web tarafı](../02-Web-Sitesi/README.md) · [Ana tanıtım](../README.md)
