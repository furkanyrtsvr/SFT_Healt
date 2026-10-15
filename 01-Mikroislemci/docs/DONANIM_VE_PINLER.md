# Donanım ve pin eşlemesi

![Fiziksel prototip](images/donanim-prototipi.png)

## Doğrulama durumu

Web kaynakları sistem kartını **nRF52840** olarak adlandırır. Fotoğraf USB ile beslenen küçük kartı, sensör modüllerini, OLED’i, enkoderi ve RTC kartını gösterir. Ticari kart adı, MCU üzerindeki tam işaret ve GPIO numaraları fotoğraftan güvenilir biçimde okunamıyor. Fiziksel pin numarası ile breadboard satır numarası aynı şey değildir.

## Yazılımdan çıkarılan bağlantı sözleşmesi

| Modül / sinyal | Zephyr referansı | Fiziksel pin |
| --- | --- | --- |
| MAX30102 SDA / SCL | `main.c`: `i2c0`; `max30102.c`: `0x57` | Overlay/pinctrl gerekli |
| OLED veri yolu | `display_ui.c`: `DT_CHOSEN(zephyr_display)` | Overlay gerekli; denetleyici ve adres bilinmiyor |
| DHT11 DATA | `environment_sensor.c`: `DT_ALIAS(dht0)` | `dht0` düğümü gerekli |
| HX711 PD_SCK | `pressure_sck_gpios` | Overlay gerekli |
| HX711 DOUT | `pressure_out_gpios` | Overlay gerekli |
| DS1302 CLK / I/O / CE | `rtc_clk_gpios`, `rtc_data_gpios`, `rtc_ce_gpios` | Overlay gerekli |
| Enkoder A / B / SW | `encoder_a_gpios`, `encoder_b_gpios`, `encoder_sw_gpios` | Overlay gerekli |
| Besleme / GND | Fotoğrafta USB ve breadboard hatları | Gerilimler ve kesin dağıtım şeması sağlanmadı |
| Fotoğraftaki ayrı buton | Ayrı bir GPIO tanımı C arşivinde tespit edilmedi | Kullanım/bağlantı teyidi gerekli |

HX711, bu kaynakta Kanal A ×64 olarak 27 saat darbesiyle seçiliyor. Basınç algılayıcısının üretici/modeli kaynakta belirtilmiyor; HX710B adı yalnızca ölçek karşılaştırması açıklamasında geçiyor. Bu nedenle fiziksel modül HX710B diye etiketlenmedi.

## Tam şema için eklenecek dosyalar

Kartın tam adı ve pinout’u, kullanılan `.overlay`, `prj.conf`, proje başlıkları ve mümkünse şema/bağlantı tablosu eklendiğinde bu belge gerçek GPIO numaralarıyla tamamlanabilir. Özellikle SDA/SCL pinctrl seçimi ve `zephyr,user` özellikleri esas alınmalıdır. Fotoğraf montaj referansıdır; elektrik şeması yerine geçmez.

[Mikroişlemci README](../README.md)
