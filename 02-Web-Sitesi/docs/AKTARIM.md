# Kaynak ve aktarım kaydı

## Teslim edilenler

- `SFT_Son_Surum_C_Dosyalari_V17.zip`: 13 C dosyası; `01-Mikroislemci/` altında içerik değiştirilmeden korunur.
- `SFT_Health_Web_Sitesi_Tam_Kaynak.zip`: 35 web kaynağı; `02-Web-Sitesi/` altında.
- Donanım fotoğrafı: kullanıcı tarafından mesajda sağlanan breadboard prototipi.

Web arşivinin özgün README’si [ORIGINAL_STARTER.md](ORIGINAL_STARTER.md) olarak korunur. Bu dosya genel starter belgesidir; güncel SFT kurulumunda bölüm README’sini esas alın. Projenin kaynak fotoğrafından veya dosya adlarından eksik GPIO numarası üretilmedi.

## Yayın için uyarlamalar

1. `vite.config.ts`, arşivde eksik olan `./build/sites-vite-plugin` import ve çağrısı kaldırılarak bağımsız yerel önizlemeye uyarlandı. vinext ve Cloudflare eklentileri korundu.
2. `.openai/hosting.json` içindeki eski proje kimliği boşaltıldı; `DB` bağlaması korundu. Canlı Sites projesine işlem yapılmadı.
3. `package.json` ve kilit dosyasının proje adı `sft-health-web` oldu. npm’nin temiz kurulumda bildirdiği eksik `@emnapi/runtime` girdisi kilit dosyasına tamamlandı.
4. Ana ve bölüm README’leri, pin tablosu, modül/kod haritası ve BLE protokolü eklendi.
5. Yerel web uygulamasından üç gerçek ekran görüntüsü alındı. Ölçüm/hasta verisi üretilmedi.

## Kontrollerin kapsamı

- Web: temiz npm kurulumu, üretim derlemesi, arşivde gelen iki test ve yerel tarayıcı önizlemesi başarılı.
- Firmware: `.c` dosyaları ve dış bağımlılıkları incelendi. `.h`, `.overlay`, `prj.conf` ve `CMakeLists.txt` bulunmadığı için firmware derlemesi yapılmadı.
- Fiziksel sensör, GPIO eşlemesi, BLE bağlantısı, kamera, D1 oturumlu yazma ve sağlık ölçüm doğruluğu bu aktarımda sınanmadı.

Eksik pin/model bilgileri [donanım belgesinde](../../01-Mikroislemci/docs/DONANIM_VE_PINLER.md) açıkça işaretlidir.
