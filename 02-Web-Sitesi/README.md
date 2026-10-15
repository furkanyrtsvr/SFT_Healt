# SFT Health — Web paneli

Donanımdaki nabız, MEP/MIP basıncı, kişi profilleri ve geçmiş kayıtları tek arayüzde toplamak için hazırlanmış web uygulaması. Kaynak, `SFT_Health_Web_Sitesi_Tam_Kaynak.zip` arşivinden aktarılmıştır.

![SFT Health genel bakış](docs/images/01-genel-bakis.jpg)

## Özellikler

- Genel bakış: son nabız, ölçülen/tahmini MEP ve MIP, aktif profil, sıcaklık/nem.
- Web Bluetooth ile Nordic UART Service’e bağlantı ve otomatik `SYNC`.
- Kişi bilgilerini düzenleme, profil seçme/silme ve bağlı cihaza iletme.
- Nabız/MEP/MIP kayıtlarını tarih ve saatle listeleme; cihaz geçmişini alma.
- Sesli komut seçeneği bulunan yönlendirmeli nefes egzersizi arayüzü.
- Ana kişi listesinden ayrı hastane demo bölümü: barkod, hasta kaydı ve ölçüm oturumu.
- Desteklenen tarayıcılarda kamera/BarcodeDetector ile barkod okuma; elle kod girişi.
- Oturumlu kullanımda Cloudflare D1 üzerinde kullanıcıya göre ayrılan cihaz/profil/ölçüm kayıtları.

## Yerel kurulum

Node.js **24** önerilir; paketin beyan ettiği alt sınır `22.13.0`’dır. Komutları bu klasörde çalıştırın:

```sh
npm ci
npm run dev -- --port 5192
```

Terminalde yazan yerel adresi açın; bu aktarımda **http://localhost:5192/** kullanıldı. Sunucu IPv6 localhost’a bağlanabildiği için `127.0.0.1` aynı makinede farklı davranabilir.

Derleme ve mevcut testler:

```sh
npm test
```

Bu komut vinext üretim derlemesini, sunucu HTML oluşturma testini ve BLE/profil entegrasyonunu kaynak düzeyinde denetleyen testi çalıştırır. Gerçek sensör, Bluetooth, kamera veya bulut oturum kabul testi yerine geçmez.

## Cihaza bağlanma

1. NUS servisi açık SFT donanımını çalıştırın.
2. Web Bluetooth destekleyen bir tarayıcı ve güvenli bağlam kullanın; uygulama desteklenmediğinde durumu arayüzde bildirir.
3. **Cihaz ekle** ile tarayıcının cihaz seçim penceresinden SFT cihazını seçin.
4. Panel RX/TX karakteristiklerini bulur, bildirimleri açar ve `SYNC` gönderir.
5. Gelen profiller/kayıtlar paneli doldurur. Profil güncellemeleri ve oturum komutları cihaza iletilebilir.

[UUID, mesaj ve komut ayrıntıları](../01-Mikroislemci/docs/BLE_PROTOKOLU.md). Fiziksel BLE cihazı bu yayın sırasında bağlanmadı.

## Yerel önizleme ve kalıcı kayıt farkı

Oturumsuz geliştirme önizlemesinde panel boş kayıtlarla açılır; kaynak gerçek hasta veya örnek sağlık verisiyle doldurulmadı. Geliştirme modundaki `/api/health` GET’i oturumsuzken boş diziler döndürür. Yazma/silme API’leri kullanıcı oturumu ister; yerelde çalışan ekran tek başına kalıcı veritabanı işlevini doğrulamaz. Üretimde oturumsuz istekler 401 döndürür.

`app/chatgpt-auth.ts`, barındırma katmanının verdiği `oai-authenticated-user-*` başlıklarını okur. Bu başlıkları doğrudan internete açık, güvenilmeyen istemcilerden kabul eden bir sunucu kimlik doğrulaması sağlamaz. Canlı barındırmada başlıkları güvenilir oturum katmanı üretmelidir. Kaynaktaki özgün Sites oturum açıklaması [ORIGINAL_STARTER.md](docs/ORIGINAL_STARTER.md) içinde korunmuştur.

## Veri tabanı

| Tablo | İçerik |
| --- | --- |
| `health_devices` | Kullanıcının cihazları ve son görülme zamanı |
| `health_profiles` | Cihaz profilleri, fiziksel bilgiler ve varsa hasta eşlemesi |
| `health_measurements` | Nabız/MEP/MIP sonuçları, birim, referans/yüzde, ölçüm zamanı |
| `health_hospital_patients` | Demo hasta kimliği, barkod ve profil bilgileri |
| `health_exercise_sessions` | Egzersiz oturumları |

Şema `db/schema.ts`, SQL geçişleri `drizzle/` altındadır. Yerel D1 bağlaması `.openai/hosting.json` içinde `DB` olarak tanımlıdır. Hazır veritabanı dosyası veya üretim kimliği depoya alınmadı. Canlı kullanım için uygun D1 kurulumu, SQL geçişlerinin uygulanması ve güvenilir oturum katmanı gerekir; bu aktarım canlı dağıtım yapmaz. `npm run db:generate` şema değişikliklerinden yeni geçiş dosyaları üretir, mevcut veritabanına geçiş uygulamaz.

## Görseller

### Nefes egzersizi

![Nefes egzersizi bölümü](docs/images/02-nefes-egzersizi.jpg)

### Hastane demo akışı

![Hasta/barkod bölümü](docs/images/03-hastane-demo.jpg)

Görüntüler yerel uygulamadan alınmıştır. Boş alanlar cihaz ve oturum bağlanmadığı için boş gösterilir; gerçek ölçüm veya hasta verisi gibi sunulmaz.

## Dosya düzeni

| Yol | İçerik |
| --- | --- |
| `app/health-dashboard.tsx` | Panel, BLE istemcisi, profil ve demo akışları |
| `app/globals.css` | Arayüz ve duyarlı görünüm |
| `app/api/health/` | Kullanıcıya göre ayrılan sağlık API’leri |
| `app/chatgpt-auth.ts` | Barındırma oturum başlıklarını okuyan yardımcılar |
| `db/`, `drizzle/` | Veri modeli ve SQL geçişleri |
| `worker/` | Cloudflare Worker giriş noktası |
| `tests/` | Arşivle gelen iki web testi |
| `docs/` | Görseller, özgün README ve aktarım notları |

## Aktarımda yapılan düzenlemeler

Arşivde bulunmayan `build/sites-vite-plugin` modülüne ait import/çağrı yerel yapılandırmadan çıkarıldı. Barındırma projesinin kimliği boş şablona çevrildi, paket adı `sft-health-web` oldu ve eksik `@emnapi/runtime` kilit girdisi npm ile tamamlandı. Donanım C kaynakları değiştirilmedi. [Ayrıntılı kayıt](docs/AKTARIM.md).

[Mikroişlemci kodları](../01-Mikroislemci/README.md) · [Proje tanıtımı](../README.md)
