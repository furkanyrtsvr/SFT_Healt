# Donanım ve kesin pin eşlemesi

![Fiziksel prototip](images/donanim-prototipi.png)

## Kart ve geliştirme ortamı

| Alan | Değer |
| --- | --- |
| Kart | Pro Micro nRF52840 |
| MCU | Nordic nRF52840 |
| SDK | nRF Connect SDK v3.4.0 |
| Zephyr hedefi | `promicro_nrf52840/nrf52840/uf2` |
| Bootloader | UF2 |
| BLE adı | `nRF52840_fy` |
| Besleme | Modüller için 3V3; ortak GND |

Kart, SDK ve pin eşlemesi proje sahibi tarafından doğrulandı. GPIO özellikleri tam kaynak arşivindeki [app.overlay](../app.overlay) ile karşılaştırıldı. Ortak I²C SDA/SCL eşlemesi kullanıcı bilgisidir; overlay kendi pinctrl atamasını yapmaz, board hedefinin I²C pinctrl tanımını devralır.

## Bağlantı tablosu

| Donanım | Kart pini | nRF52840 GPIO | Kaynak / not |
| --- | --- | --- | --- |
| OLED + MAX30102 SDA | D6 | P1.00 | Ortak I²C veri hattı; board pinctrl |
| OLED + MAX30102 SCL/SCK | D7 | P0.11 | Ortak I²C saat hattı; board pinctrl |
| Enkoder A | D8 | P1.04 | `encoder-a-gpios = <&gpio1 4 ...>` |
| Enkoder B | D9 | P1.06 | `encoder-b-gpios = <&gpio1 6 ...>` |
| Enkoder butonu | D10 | P0.09 | `encoder-sw-gpios`; diğer uç GND |
| HX711 SCK | D2 | P0.17 | `pressure-sck-gpios` |
| HX711 DOUT/OUT | D3 | P0.20 | `pressure-out-gpios` |
| DHT11 DATA | D4 | P0.22 | `dht0`; 3V3’e 4,7–10 kΩ pull-up |
| DS1302 CLK | D5 | P0.24 | `rtc-clk-gpios` |
| DS1302 DATA | D15/A0 | P0.02 | `rtc-data-gpios`; çift yönlü |
| DS1302 RST/CE | D12 | P1.11 | `rtc-ce-gpios`; haberleşmeyi etkinleştirir |

Modüllerin GND uçları ortak olmalıdır. Kullanıcının bildirdiği bağlantıda modüller 3V3 ile beslenir. GPIO tarafına 5 V lojik uygulanacak bir bağlantı bu tabloda tanımlı değildir.

## Modül ayarları

- **OLED:** SSD1306, 128×64, I²C `0x3C`, `zephyr,display` seçimi.
- **MAX30102:** I²C `0x57`; INT kullanılmaz. Yazılım nabız/BPM için optik örnekleri okur.
- **HX711:** PD_SCK/DOUT bit-bang erişimi; 24 bit okuma, 27 darbe ile Kanal A ×64. Basınç transdüserinin tam modeli belirtilmedi. HX710B adı yalnızca kaynak yorumundaki ölçek karşılaştırmasında geçer.
- **DHT11:** Zephyr `aosong,dht` sürücüsü; sıcaklık ve nem.
- **DS1302:** CE/CLK/DATA üzerinden üç telli GPIO erişimi; tarih/saat kaydı.
- **Enkoder:** A/B/SW pull-up ve active-low tanımları; uzun basış 900 ms.

Fotoğraf montaj referansıdır. Bu yayın sırasında bağlantılar sökülüp ölçülmedi ve karta firmware yüklenmedi.

[Mikroişlemci README](../README.md)
