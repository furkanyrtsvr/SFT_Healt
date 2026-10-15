"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";

const NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

type ConnectionState = "idle" | "connecting" | "connected" | "unsupported";
type MeasurementType = "heart_rate" | "mep" | "mip";
type DeviceRecord = { id: string; name: string; model: string; lastSeenAt: string };
type ProfileRecord = { id: string; deviceId: string; deviceProfileIndex: number; name: string; age: number; gender: "M" | "W"; heightCm: number; weightKg: number; hospitalPatientId?: string | null };
type MeasurementRecord = { id: string; deviceId: string; profileIndex: number; hospitalPatientId?: string | null; type: MeasurementType; value: number; unit: "BPM" | "kPa"; predicted?: number | null; percent?: number | null; status?: string | null; measuredAt: string };
type HospitalPatient = { id: string; barcode: string; name: string; age: number; gender: "M" | "W"; heightCm: number; weightKg: number; createdAt: string; updatedAt: string };
type HealthPayload = { devices: DeviceRecord[]; profiles: ProfileRecord[]; measurements: MeasurementRecord[]; hospitalPatients: HospitalPatient[] };
type EnvironmentReading = { temperature: number; humidity: number; quality: string; measuredAt: string };
type NusCharacteristic = { value?: DataView; startNotifications?: () => Promise<NusCharacteristic>; addEventListener: (type: "characteristicvaluechanged", listener: (event: Event) => void) => void; writeValueWithoutResponse?: (value: ArrayBuffer) => Promise<void>; writeValueWithResponse?: (value: ArrayBuffer) => Promise<void> };
type NusService = { getCharacteristic: (uuid: string) => Promise<NusCharacteristic> };
type GattServer = { connected: boolean; connect: () => Promise<GattServer>; disconnect: () => void; getPrimaryService: (uuid: string) => Promise<NusService> };
type BleDevice = { id?: string; name?: string; gatt?: GattServer; addEventListener: (type: "gattserverdisconnected", listener: () => void) => void };
type BluetoothApi = { requestDevice: (options: { filters: Array<{ services: string[] }> }) => Promise<BleDevice> };
type ExercisePhase = "ready" | "inhale" | "exhale" | "rest" | "done";

function emptyHospitalPatient(): HospitalPatient {
  const now = new Date().toISOString();
  return { id: makeId("patient"), barcode: "", name: "", age: 18, gender: "M", heightCm: 170, weightKg: 70, createdAt: now, updatedAt: now };
}

function deviceSafeName(name: string) {
  return name.toLocaleUpperCase("tr-TR").replace(/İ/g, "I").replace(/[^A-Z0-9]/g, "").slice(0, 8) || "HASTA";
}

function Icon({ name }: { name: "heart" | "lungs" | "user" | "device" | "activity" | "breath" }) {
  const paths = {
    heart: <path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1.1-1.1a5.5 5.5 0 0 0-7.8 7.8l1.1 1.1L12 21l7.8-7.5 1.1-1.1a5.5 5.5 0 0 0-.1-7.8Z" />,
    lungs: <><path d="M12 4v8" /><path d="M11 12c-1.5-3.7-2.6-6.4-4.2-6.1C4.8 6.3 3 11.2 3 15.2 3 18 4.6 20 7.2 20c2.9 0 3.8-3 3.8-5.3V12Z" /><path d="M13 12c1.5-3.7 2.6-6.4 4.2-6.1 2 .4 3.8 5.3 3.8 9.3 0 2.8-1.6 4.8-4.2 4.8-2.9 0-3.8-3-3.8-5.3V12Z" /></>,
    user: <><circle cx="12" cy="8" r="4" /><path d="M4.5 21a7.5 7.5 0 0 1 15 0" /></>,
    device: <><rect x="5" y="2.5" width="14" height="19" rx="3" /><path d="M9 6h6M10 18h4" /></>,
    activity: <path d="M3 12h4l2.2-6 4.1 12 2.2-6H21" />,
    breath: <><path d="M3 9h8a3 3 0 1 0-3-3" /><path d="M3 15h12a3 3 0 1 1-3 3" /></>,
  };
  return <svg className="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{paths[name]}</svg>;
}

function formatDate(value?: string) {
  if (!value) return "Henüz ölçüm yok";
  return new Intl.DateTimeFormat("tr-TR", { day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit" }).format(new Date(value));
}
function makeId(prefix: string) { return `${prefix}-${Date.now()}-${Math.random().toString(16).slice(2)}`; }
function deviceMeasurementTime(fields: string[]) {
  const year = Number(fields[7]); const month = Number(fields[8]); const day = Number(fields[9]);
  const hour = Number(fields[10]); const minute = Number(fields[11]); const second = Number(fields[12]);
  const value = new Date(year, month - 1, day, hour, minute, second);
  return Number.isNaN(value.getTime()) ? new Date().toISOString() : value.toISOString();
}
function statusForPercent(percent?: number | null) { if (!percent) return "Referans yok"; if (percent < 80) return "Düşük"; if (percent <= 120) return "Beklenen aralık"; return "Yüksek"; }
function environmentLabel(quality?: string) {
  if (quality === "GOOD") return "Ortam uygun";
  if (quality === "DRY") return "Ortam kuru";
  if (quality === "HUMID") return "Ortam nemli";
  if (quality === "COLD") return "Ortam soğuk";
  if (quality === "HOT") return "Ortam sıcak";
  return "Sensör bekleniyor";
}

export default function HealthDashboard({ viewerName, signedIn }: { viewerName: string; signedIn: boolean }) {
  const [connection, setConnection] = useState<ConnectionState>("idle");
  const [deviceName, setDeviceName] = useState("Cihaz bağlı değil");
  const [deviceId, setDeviceId] = useState("sft-health-local");
  const [devices, setDevices] = useState<DeviceRecord[]>([]);
  const [profiles, setProfiles] = useState<ProfileRecord[]>([]);
  const [measurements, setMeasurements] = useState<MeasurementRecord[]>([]);
  const [environment, setEnvironment] = useState<EnvironmentReading | null>(null);
  const [hospitalPatients, setHospitalPatients] = useState<HospitalPatient[]>([]);
  const [editingProfile, setEditingProfile] = useState<ProfileRecord | null>(null);
  const [hospitalForm, setHospitalForm] = useState<HospitalPatient | null>(null);
  const [hospitalCode, setHospitalCode] = useState("");
  const [selectedHospitalId, setSelectedHospitalId] = useState<string | null>(null);
  const [hospitalSession, setHospitalSession] = useState<{ patientId: string; profileIndex: number } | null>(null);
  const [scannerActive, setScannerActive] = useState(false);
  const [selectedProfile, setSelectedProfile] = useState<number | "all">("all");
  const [notice, setNotice] = useState("Cihazını bağladığında ölçümler otomatik senkronlanır.");
  const [exercisePhase, setExercisePhase] = useState<ExercisePhase>("ready");
  const [exerciseSecond, setExerciseSecond] = useState(2);
  const [exerciseBreath, setExerciseBreath] = useState(0);
  const [voiceEnabled, setVoiceEnabled] = useState(true);
  const deviceRef = useRef<BleDevice | null>(null);
  const rxRef = useRef<NusCharacteristic | null>(null);
  const decoderRef = useRef(new TextDecoder());
  const incomingRef = useRef("");
  const deviceIdRef = useRef(deviceId);
  const profilesRef = useRef(profiles);
  const hospitalSessionRef = useRef(hospitalSession);
  const scannerVideoRef = useRef<HTMLVideoElement | null>(null);

  useEffect(() => { deviceIdRef.current = deviceId; }, [deviceId]);
  useEffect(() => { profilesRef.current = profiles; }, [profiles]);
  useEffect(() => { hospitalSessionRef.current = hospitalSession; }, [hospitalSession]);
  useEffect(() => {
    if (!scannerActive || !scannerVideoRef.current) return;
    let stopped = false; let stream: MediaStream | null = null;
    const video = scannerVideoRef.current;
    const start = async () => {
      const Detector = (window as unknown as { BarcodeDetector?: new (options: { formats: string[] }) => { detect: (source: HTMLVideoElement) => Promise<Array<{ rawValue: string }>> } }).BarcodeDetector;
      if (!Detector || !navigator.mediaDevices?.getUserMedia) { setNotice("Bu tarayıcı kamera ile barkod okumayı desteklemiyor. Kodu elle yazabilirsin."); setScannerActive(false); return; }
      try {
        stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment" } });
        video.srcObject = stream; await video.play(); const detector = new Detector({ formats: ["qr_code", "code_128", "ean_13", "ean_8"] });
        const scan = async () => {
          if (stopped) return;
          const found = await detector.detect(video);
          if (found[0]?.rawValue) { setHospitalCode(found[0].rawValue); setSelectedHospitalId(hospitalPatients.find((item) => item.barcode === found[0].rawValue)?.id ?? null); setScannerActive(false); return; }
          window.setTimeout(() => void scan(), 180);
        };
        await scan();
      } catch { setNotice("Kamera açılamadı. Barkod kodunu elle yazabilirsin."); setScannerActive(false); }
    };
    void start();
    return () => { stopped = true; stream?.getTracks().forEach((track) => track.stop()); video.srcObject = null; };
  }, [hospitalPatients, scannerActive]);
  useEffect(() => {
    void fetch("/api/health", { cache: "no-store" }).then(async (response) => {
      if (!response.ok) throw new Error("Kayıt servisi hazır değil");
      return response.json() as Promise<HealthPayload>;
    }).then((payload) => {
      setDevices(payload.devices ?? []); setProfiles(payload.profiles ?? []); setMeasurements(payload.measurements ?? []); setHospitalPatients(payload.hospitalPatients ?? []);
    }).catch(() => setNotice("Yerel önizleme açık. Yayında kayıtlar hesabında saklanacak."));
  }, []);

  const persist = useCallback(async (path: string, body: unknown) => {
    try { await fetch(path, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(body) }); } catch { /* bağlantı sonrası yeni kayıtlar tekrar saklanır */ }
  }, []);
  const removePersisted = useCallback(async (path: string, body: unknown) => {
    const response = await fetch(path, { method: "DELETE", headers: { "content-type": "application/json" }, body: JSON.stringify(body) });
    if (!response.ok) throw new Error("Silme işlemi tamamlanamadı");
  }, []);
  const addMeasurement = useCallback((record: MeasurementRecord) => {
    setMeasurements((current) => [record, ...current.filter((item) => item.id !== record.id)].slice(0, 250));
    void persist("/api/health/measurements", record);
  }, [persist]);

  const processLine = useCallback((line: string) => {
    const fields = line.trim().split("|");
    if (fields[0] === "P" && fields.length >= 11) {
      const id = `${deviceIdRef.current}-${fields[1]}`;
      const existing = profilesRef.current.find((item) => item.id === id);
      const profile: ProfileRecord = { id, deviceId: deviceIdRef.current, deviceProfileIndex: Number(fields[1]), name: fields[2], age: Number(fields[3]), gender: fields[4] === "W" ? "W" : "M", heightCm: Number(fields[5]), weightKg: Number(fields[6]), hospitalPatientId: existing?.hospitalPatientId ?? null };
      setProfiles((current) => [profile, ...current.filter((item) => item.id !== profile.id)]);
      void persist("/api/health/profiles", profile);
    } else if (fields[0] === "M" && fields[1] === "HR" && fields.length >= 4) {
      const profileIndex = Number(fields[2]); const hospitalPatientId = profilesRef.current.find((item) => item.deviceId === deviceIdRef.current && item.deviceProfileIndex === profileIndex)?.hospitalPatientId ?? hospitalSessionRef.current?.patientId ?? null;
      addMeasurement({ id: makeId("hr"), deviceId: deviceIdRef.current, profileIndex, hospitalPatientId, type: "heart_rate", value: Number(fields[3]), unit: "BPM", measuredAt: new Date().toISOString() });
      setNotice("Yeni nabız ölçümü kaydedildi.");
    } else if (fields[0] === "M" && fields[1] === "MEP" && fields.length >= 6) {
      const percent = Number(fields[5]);
      const profileIndex = Number(fields[2]); const hospitalPatientId = profilesRef.current.find((item) => item.deviceId === deviceIdRef.current && item.deviceProfileIndex === profileIndex)?.hospitalPatientId ?? hospitalSessionRef.current?.patientId ?? null;
      addMeasurement({ id: makeId("mep"), deviceId: deviceIdRef.current, profileIndex, hospitalPatientId, type: "mep", value: Number(fields[3]) / 100, unit: "kPa", predicted: Number(fields[4]) / 100, percent, status: statusForPercent(percent), measuredAt: new Date().toISOString() });
      setNotice("Yeni MEP ölçümü kaydedildi.");
    } else if (fields[0] === "M" && fields[1] === "MIP" && fields.length >= 6) {
      const percent = Number(fields[5]);
      const profileIndex = Number(fields[2]); const hospitalPatientId = profilesRef.current.find((item) => item.deviceId === deviceIdRef.current && item.deviceProfileIndex === profileIndex)?.hospitalPatientId ?? hospitalSessionRef.current?.patientId ?? null;
      addMeasurement({ id: makeId("mip"), deviceId: deviceIdRef.current, profileIndex, hospitalPatientId, type: "mip", value: Number(fields[3]) / 100, unit: "kPa", predicted: Number(fields[4]) / 100, percent, status: statusForPercent(percent), measuredAt: new Date().toISOString() });
      setNotice("Yeni MIP ölçümü kaydedildi.");
    } else if (fields[0] === "M" && fields[1] === "ENV" && fields.length >= 5) {
      setEnvironment({ temperature: Number(fields[2]) / 10, humidity: Number(fields[3]) / 10, quality: fields[4], measuredAt: new Date().toISOString() });
    } else if (fields[0] === "R" && fields.length >= 13) {
      const type: MeasurementType = fields[2] === "MEP" ? "mep" : fields[2] === "MIP" ? "mip" : "heart_rate";
      const pressure = type !== "heart_rate"; const percent = Number(fields[6]);
      const profileIndex = Number(fields[3]); const hospitalPatientId = profilesRef.current.find((item) => item.deviceId === deviceIdRef.current && item.deviceProfileIndex === profileIndex)?.hospitalPatientId ?? null;
      addMeasurement({ id: `${deviceIdRef.current}-record-${fields[1]}`, deviceId: deviceIdRef.current, profileIndex, hospitalPatientId, type, value: Number(fields[4]) / 100, unit: pressure ? "kPa" : "BPM", predicted: pressure ? Number(fields[5]) / 100 : null, percent: pressure ? percent : null, status: pressure ? statusForPercent(percent) : "Cihazda kaydedildi", measuredAt: deviceMeasurementTime(fields) });
    } else if (fields[0] === "DONE") setNotice("Cihaz kayıtları senkronlandı.");
  }, [addMeasurement, persist]);

  const handleNotification = useCallback((event: Event) => {
    const value = (event.target as NusCharacteristic | null)?.value;
    if (!value) return;
    incomingRef.current += decoderRef.current.decode(new Uint8Array(value.buffer, value.byteOffset, value.byteLength), { stream: true });
    const lines = incomingRef.current.split(/\r?\n/);
    incomingRef.current = lines.pop() ?? "";
    lines.filter(Boolean).forEach(processLine);
  }, [processLine]);

  const sendCommand = useCallback(async (command: string) => {
    const characteristic = rxRef.current;
    if (!characteristic) return;
    const payload = new TextEncoder().encode(`${command}\n`);
    for (let offset = 0; offset < payload.length; offset += 20) {
      const chunk = Uint8Array.from(payload.slice(offset, offset + 20)).buffer;
      if (characteristic.writeValueWithoutResponse) await characteristic.writeValueWithoutResponse(chunk);
      else if (characteristic.writeValueWithResponse) await characteristic.writeValueWithResponse(chunk);
    }
  }, []);

  function disconnected() { deviceRef.current = null; rxRef.current = null; setConnection("idle"); setDeviceName("Cihaz bağlı değil"); setNotice("Bluetooth bağlantısı kesildi."); }
  async function connect() {
    const bluetooth = (navigator as Navigator & { bluetooth?: BluetoothApi }).bluetooth;
    if (!bluetooth || !window.isSecureContext) { setConnection("unsupported"); setNotice("Chrome veya Edge ile HTTPS üzerinden açmalısın."); return; }
    try {
      setConnection("connecting"); setNotice("Yakındaki SFT cihazları aranıyor...");
      const device = await bluetooth.requestDevice({ filters: [{ services: [NUS_SERVICE] }] });
      if (!device.gatt) throw new Error("Bluetooth servisine ulaşılamadı.");
      device.addEventListener("gattserverdisconnected", disconnected);
      const server = await device.gatt.connect();
      const service = await server.getPrimaryService(NUS_SERVICE);
      const rx = await service.getCharacteristic(NUS_RX);
      const tx = await service.getCharacteristic(NUS_TX);
      await tx.startNotifications?.(); tx.addEventListener("characteristicvaluechanged", handleNotification);
      const id = device.id || `sft-${device.name || "health"}`;
      const connectedDevice: DeviceRecord = { id, name: device.name || "nRF52840_fy", model: "SFT · nRF52840 · HX711 · MAX30102 · DHT11 · DS1302", lastSeenAt: new Date().toISOString() };
      deviceRef.current = device; rxRef.current = rx; setDeviceId(id); deviceIdRef.current = id; setDeviceName(connectedDevice.name);
      setDevices((current) => [connectedDevice, ...current.filter((item) => item.id !== id)]);
      setConnection("connected"); setNotice("Bağlandı. Profil ve son ölçümler alınıyor...");
      void persist("/api/health/devices", connectedDevice);
      await sendCommand("SYNC");
    } catch (error) { setConnection("idle"); setNotice(error instanceof Error ? error.message : "Bağlantı kurulamadı."); }
  }
  function disconnect() { if (deviceRef.current?.gatt?.connected) deviceRef.current.gatt.disconnect(); else disconnected(); }

  async function saveProfile(profile: ProfileRecord) {
    const updated = { ...profile, name: deviceSafeName(profile.name) };
    setProfiles((current) => [updated, ...current.filter((item) => item.id !== updated.id)]);
    setEditingProfile(null);
    await persist("/api/health/profiles", updated);
    if (connection === "connected" && updated.deviceId === deviceIdRef.current) {
      await sendCommand(`PROFILE_SET|${updated.deviceProfileIndex}|${updated.name}|${updated.age}|${updated.gender}|${updated.heightCm}|${updated.weightKg}`);
    }
    setNotice(`${updated.name} profili güncellendi.`);
  }

  async function deleteProfile(profile: ProfileRecord) {
    if (!window.confirm(`${profile.name} profili silinsin mi?`)) return;
    if (connection === "connected" && profile.deviceId === deviceIdRef.current) await sendCommand(`PROFILE_DELETE|${profile.deviceProfileIndex}`);
    await removePersisted("/api/health/profiles", { id: profile.id });
    setProfiles((current) => current.filter((item) => item.id !== profile.id));
    setNotice("Profil silindi.");
  }

  async function saveHospitalPatient(patient: HospitalPatient) {
    const updated = { ...patient, name: patient.name.trim(), barcode: patient.barcode.trim(), updatedAt: new Date().toISOString() };
    if (!updated.name || !updated.barcode) { setNotice("Hasta adı ve barkod numarası gerekli."); return; }
    setHospitalPatients((current) => [updated, ...current.filter((item) => item.id !== updated.id)]);
    setHospitalForm(null); setSelectedHospitalId(updated.id); setHospitalCode(updated.barcode);
    await persist("/api/health/hospital", updated);
    setNotice(`${updated.name} hastane kaydına eklendi.`);
  }

  async function deleteHospitalPatient(patient: HospitalPatient) {
    if (!window.confirm(`${patient.name} hastane kaydından silinsin mi?`)) return;
    await removePersisted("/api/health/hospital", { id: patient.id });
    setHospitalPatients((current) => current.filter((item) => item.id !== patient.id));
    if (selectedHospitalId === patient.id) setSelectedHospitalId(null);
  }

  function findHospitalByCode(code: string) {
    setHospitalCode(code);
    const patient = hospitalPatients.find((item) => item.barcode === code.trim());
    setSelectedHospitalId(patient?.id ?? null);
  }

  async function startHospitalMeasurement(patient: HospitalPatient) {
    if (connection !== "connected") { setNotice("Ölçüm için önce nRF52840_fy cihazına bağlan."); return; }
    const existing = profilesRef.current.find((item) => item.deviceId === deviceIdRef.current && item.hospitalPatientId === patient.id);
    const used = new Set(profilesRef.current.filter((item) => item.deviceId === deviceIdRef.current).map((item) => item.deviceProfileIndex));
    const slot = existing?.deviceProfileIndex ?? [0, 1, 2, 3, 4].find((index) => !used.has(index));
    if (slot === undefined) { setNotice("Kartta boş profil yeri yok. Önce cihazdaki profillerden birini sil."); return; }
    const profile: ProfileRecord = { id: `${deviceIdRef.current}-${slot}`, deviceId: deviceIdRef.current, deviceProfileIndex: slot, name: deviceSafeName(patient.name), age: patient.age, gender: patient.gender, heightCm: patient.heightCm, weightKg: patient.weightKg, hospitalPatientId: patient.id };
    setProfiles((current) => [profile, ...current.filter((item) => item.id !== profile.id)]); profilesRef.current = [profile, ...profilesRef.current.filter((item) => item.id !== profile.id)];
    await persist("/api/health/profiles", profile);
    await sendCommand(`PROFILE_SET|${slot}|${profile.name}|${profile.age}|${profile.gender}|${profile.heightCm}|${profile.weightKg}`);
    await sendCommand(`PROFILE_SELECT|${slot}`);
    setHospitalSession({ patientId: patient.id, profileIndex: slot }); setNotice(`${patient.name} cihazda aktif. Ölçüm türünü encoder ile seçebilirsin.`);
  }

  async function endHospitalMeasurement() {
    await sendCommand("SESSION_END"); setHospitalSession(null); setNotice("Hastane ölçüm oturumu kapandı; cihaz kişi seçimine döndü.");
  }

  const visibleMeasurements = useMemo(() => measurements.filter((item) => selectedProfile === "all" || item.profileIndex === selectedProfile), [measurements, selectedProfile]);
  const latestHeart = visibleMeasurements.find((item) => item.type === "heart_rate");
  const latestMep = visibleMeasurements.find((item) => item.type === "mep");
  const latestMip = visibleMeasurements.find((item) => item.type === "mip");
  const selectedProfileRecord = profiles.find((item) => item.deviceProfileIndex === selectedProfile);
  const localProfiles = profiles.filter((item) => !item.hospitalPatientId);
  const selectedHospital = hospitalPatients.find((item) => item.id === selectedHospitalId) ?? null;
  const hospitalMeasurements = measurements.filter((item) => item.hospitalPatientId === selectedHospitalId);
  const heartSeries = visibleMeasurements.filter((item) => item.type === "heart_rate").slice(0, 12).reverse();
  const chartPoints = useMemo(() => {
    if (heartSeries.length < 2) return "";
    const values = heartSeries.map((item) => item.value); const min = Math.min(...values) - 4; const max = Math.max(...values) + 4;
    return values.map((value, index) => `${((index / (values.length - 1)) * 280).toFixed(1)},${(88 - ((value - min) / Math.max(max - min, 1)) * 70).toFixed(1)}`).join(" ");
  }, [heartSeries]);

  const speak = useCallback((message: string) => { if (!voiceEnabled || !("speechSynthesis" in window)) return; window.speechSynthesis.cancel(); const utterance = new SpeechSynthesisUtterance(message); utterance.lang = "tr-TR"; utterance.rate = 0.9; window.speechSynthesis.speak(utterance); }, [voiceEnabled]);
  function startExercise() { setExerciseBreath(1); setExercisePhase("inhale"); setExerciseSecond(2); speak("Nefes al"); }
  useEffect(() => {
    if (exercisePhase !== "inhale" && exercisePhase !== "exhale") return;
    const timer = window.setTimeout(() => {
      if (exerciseSecond > 1) setExerciseSecond((value) => value - 1);
      else if (exercisePhase === "inhale") { setExercisePhase("exhale"); setExerciseSecond(2); speak("Nefes ver"); }
      else if (exerciseBreath >= 10) {
        setExercisePhase("rest"); setExerciseSecond(30); speak("Set tamamlandı. Normal nefes al ve dinlen.");
        void persist("/api/health/exercises", { id: makeId("exercise"), profileIndex: selectedProfile === "all" ? null : selectedProfile, preset: "SMA2_GENTLE_2_2", breaths: 10, completedAt: new Date().toISOString() });
      } else { setExerciseBreath((value) => value + 1); setExercisePhase("inhale"); setExerciseSecond(2); speak("Nefes al"); }
    }, 1000);
    return () => window.clearTimeout(timer);
  }, [exerciseBreath, exercisePhase, exerciseSecond, persist, selectedProfile, speak]);
  useEffect(() => {
    if (exercisePhase !== "rest") return;
    const timer = window.setTimeout(() => { if (exerciseSecond > 1) setExerciseSecond((value) => value - 1); else setExercisePhase("done"); }, 1000);
    return () => window.clearTimeout(timer);
  }, [exercisePhase, exerciseSecond]);

  const connectionText = connection === "connected" ? "Bağlı" : connection === "connecting" ? "Bağlanıyor" : connection === "unsupported" ? "Desteklenmiyor" : "Bağlı değil";

  return <main className="health-app">
    <header className="app-header"><a className="health-brand" href="#dashboard"><span className="health-mark"><span /></span><span><strong>SFT Health</strong><small>Solunum ve nabız takibi</small></span></a><div className="header-actions"><span className="viewer"><span>{viewerName.slice(0, 1).toUpperCase()}</span>{signedIn ? viewerName : "Önizleme"}</span><span className={`connection-state ${connection}`}><i />{connectionText}</span>{connection === "connected" ? <button className="button ghost" onClick={disconnect}>Bağlantıyı kes</button> : <button className="button primary" onClick={() => void connect()} disabled={connection === "connecting"}>+ Cihaz ekle</button>}</div></header>
    <nav className="section-nav"><a href="#dashboard">Genel bakış</a><a href="#people">Kişiler</a><a href="#records">Kayıtlar</a><a href="#exercise">Nefes egzersizi</a><a href="#devices">Cihazlar</a><a className="hospital-nav" href="#hospital">Hastane kayıt</a></nav>
    <section className="welcome-row" id="dashboard"><div><span className="kicker">SAĞLIK PANELİ</span><h1>Ölçümlerin, tek bir yerde.</h1><p>{notice}</p></div><label className="profile-filter">Aktif kişi<select value={selectedProfile} onChange={(event) => setSelectedProfile(event.target.value === "all" ? "all" : Number(event.target.value))}><option value="all">Tüm kişiler</option>{profiles.map((profile) => <option value={profile.deviceProfileIndex} key={profile.id}>{profile.name}</option>)}</select></label></section>
    <section className="metric-grid">
      <article className="metric-card coral"><div className="metric-icon"><Icon name="heart" /></div><div><span>Son nabız</span><strong>{latestHeart ? Math.round(latestHeart.value) : "--"}<small>BPM</small></strong><p>{formatDate(latestHeart?.measuredAt)}</p></div></article>
      <article className="metric-card blue"><div className="metric-icon"><Icon name="lungs" /></div><div><span>Ölçülen MEP</span><strong>{latestMep ? latestMep.value.toFixed(2) : "--"}<small>kPa</small></strong><p>{formatDate(latestMep?.measuredAt)}</p></div></article>
      <article className="metric-card mint"><div className="metric-icon"><Icon name="activity" /></div><div><span>Tahmini MEP</span><strong>{latestMep?.predicted ? latestMep.predicted.toFixed(2) : "--"}<small>kPa</small></strong><p>{latestMep?.status ?? "Referans bekleniyor"}</p></div></article>
      <article className="metric-card violet"><div className="metric-icon"><Icon name="lungs" /></div><div><span>Ölçülen MIP</span><strong>{latestMip ? latestMip.value.toFixed(2) : "--"}<small>kPa</small></strong><p>{formatDate(latestMip?.measuredAt)}</p></div></article>
      <article className="metric-card lilac"><div className="metric-icon"><Icon name="activity" /></div><div><span>Tahmini MIP</span><strong>{latestMip?.predicted ? latestMip.predicted.toFixed(2) : "--"}<small>kPa</small></strong><p>{latestMip?.status ?? "Referans bekleniyor"}</p></div></article>
      <article className="metric-card sand"><div className="metric-icon"><Icon name="user" /></div><div><span>Aktif profil</span><strong className="profile-value">{selectedProfileRecord?.name ?? (profiles.length ? `${profiles.length} kişi` : "--")}</strong><p>{selectedProfileRecord ? `${selectedProfileRecord.age} yaş · ${selectedProfileRecord.heightCm} cm · ${selectedProfileRecord.weightKg} kg` : "Cihazdan senkronlanır"}</p></div></article>
    </section>
    <section className="environment-strip" aria-label="Ortam sıcaklığı ve nemi">
      <div className="environment-title"><span className="environment-dot" /><div><span>ODA ORTAMI</span><strong>{environmentLabel(environment?.quality)}</strong></div></div>
      <div className="environment-values"><div><span>Sıcaklık</span><strong>{environment ? environment.temperature.toFixed(1) : "--"}<small>°C</small></strong></div><div><span>Nem</span><strong>{environment ? environment.humidity.toFixed(1) : "--"}<small>%</small></strong></div></div>
      <p>DHT11 sıcaklık ve bağıl nemi gösterir; havasızlık veya CO₂ ölçümü yapmaz. <span>{formatDate(environment?.measuredAt)}</span></p>
    </section>
    <section className="insight-grid">
      <article className="panel chart-panel"><div className="panel-heading"><div><span>SON ÖLÇÜMLER</span><h2>Nabız eğilimi</h2></div><span className="soft-badge">Son {heartSeries.length} kayıt</span></div><div className="chart-area">{chartPoints ? <svg viewBox="0 0 280 96" preserveAspectRatio="none"><defs><linearGradient id="area" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stopColor="#f07167" stopOpacity=".28" /><stop offset="1" stopColor="#f07167" stopOpacity="0" /></linearGradient></defs><polyline points={`0,96 ${chartPoints} 280,96`} fill="url(#area)" stroke="none" /><polyline points={chartPoints} fill="none" stroke="#e95d55" strokeWidth="3" vectorEffect="non-scaling-stroke" /></svg> : <div className="empty-chart"><Icon name="activity" /><p>Nabız ölçümleri geldikçe eğilim burada oluşacak.</p></div>}</div></article>
      <article className="panel mep-panel"><div className="panel-heading"><div><span>MEP DURUMU</span><h2>{latestMep?.status ?? "Ölçüm bekleniyor"}</h2></div><div className="lungs-orbit"><Icon name="lungs" /></div></div><div className="mep-gauge"><div className="gauge-track"><span style={{ width: `${Math.min(latestMep?.percent ?? 0, 150) / 1.5}%` }} /></div><div><span>0%</span><strong>{latestMep?.percent ? `%${latestMep.percent}` : "--"}</strong><span>150%+</span></div></div><p className="clinical-note">Bu sonuç prototip ölçümüdür; tanı koymaz ve klinik solunum testi yerine geçmez.</p></article>
      <article className="panel mip-panel"><div className="panel-heading"><div><span>MIP DURUMU</span><h2>{latestMip?.status ?? "Ölçüm bekleniyor"}</h2></div><div className="lungs-orbit"><Icon name="breath" /></div></div><div className="mep-gauge"><div className="gauge-track"><span style={{ width: `${Math.min(latestMip?.percent ?? 0, 150) / 1.5}%` }} /></div><div><span>0%</span><strong>{latestMip?.percent ? `%${latestMip.percent}` : "--"}</strong><span>150%+</span></div></div><p className="clinical-note">MIP için kapalı ağızlığa karşı nefes çekilir. Bu prototip sonuç klinik test değildir.</p></article>
    </section>
    <section className="section-block" id="people">
      <div className="section-title"><div><span>KİŞİLER</span><h2>Kayıtlı profiller</h2></div><p>Yaş, cinsiyet, boy ve kilo bilgilerini düzenleyebilir veya profili silebilirsin.</p></div>
      {editingProfile ? <div className="edit-form"><label>Ad<input value={editingProfile.name} maxLength={8} onChange={(event) => setEditingProfile({ ...editingProfile, name: event.target.value })} /></label><label>Yaş<input type="number" min="1" max="120" value={editingProfile.age} onChange={(event) => setEditingProfile({ ...editingProfile, age: Number(event.target.value) })} /></label><label>Cinsiyet<select value={editingProfile.gender} onChange={(event) => setEditingProfile({ ...editingProfile, gender: event.target.value as "M" | "W" })}><option value="M">Erkek</option><option value="W">Kadın</option></select></label><label>Boy (cm)<input type="number" min="100" max="230" value={editingProfile.heightCm} onChange={(event) => setEditingProfile({ ...editingProfile, heightCm: Number(event.target.value) })} /></label><label>Kilo (kg)<input type="number" min="25" max="250" value={editingProfile.weightKg} onChange={(event) => setEditingProfile({ ...editingProfile, weightKg: Number(event.target.value) })} /></label><div className="form-actions"><button className="button ghost" onClick={() => setEditingProfile(null)}>Vazgeç</button><button className="button primary" onClick={() => void saveProfile(editingProfile)}>Kaydet</button></div></div> : null}
      <div className="profile-grid">{localProfiles.length ? localProfiles.map((profile) => <article className="profile-card" key={profile.id}><div className="avatar">{profile.name.slice(0, 2)}</div><div className="profile-body"><strong>{profile.name}</strong><p>{profile.gender === "M" ? "Erkek" : "Kadın"} · {profile.age} yaş</p><span>{profile.heightCm} cm</span><span>{profile.weightKg} kg</span><div className="card-actions"><button onClick={() => setEditingProfile({ ...profile })}>Düzenle</button><button className="danger-link" onClick={() => void deleteProfile(profile)}>Sil</button></div></div></article>) : <div className="empty-state"><Icon name="user" /><strong>Henüz profil yok</strong><p>Karta bağlanıp senkronladığında profiller burada görünür.</p></div>}</div>
    </section>
    <section className="section-block" id="records"><div className="section-title"><div><span>KAYITLAR</span><h2>Ölçüm geçmişi</h2></div><p>Nabız, MEP ve MIP ölçümleri cihazdan geldiği anda tarih ve saatle kaydedilir.</p></div><div className="records-table"><div className="record-row record-head"><span>Kişi</span><span>Ölçüm</span><span>Sonuç</span><span>Durum</span><span>Zaman</span></div>{visibleMeasurements.length ? visibleMeasurements.slice(0, 40).map((item) => { const profile = profiles.find((entry) => entry.deviceProfileIndex === item.profileIndex); const pressure = item.type === "mep" || item.type === "mip"; return <div className="record-row" key={item.id}><span><b>{profile?.name ?? `Profil ${item.profileIndex + 1}`}</b></span><span>{item.type === "mep" ? "MEP" : item.type === "mip" ? "MIP" : "Nabız"}</span><span><b>{pressure ? item.value.toFixed(2) : Math.round(item.value)}</b> {item.unit}</span><span><i className={`record-dot ${item.type}`} />{item.status ?? "Kaydedildi"}</span><time>{formatDate(item.measuredAt)}</time></div>; }) : <div className="table-empty">Henüz kaydedilmiş ölçüm yok.</div>}</div></section>
    <section className="section-block exercise-section" id="exercise"><div className="section-title"><div><span>NEFES EGZERSİZİ</span><h2>Yönlendirmeli nefes</h2></div><p>SMA Tip 2 için yalnızca nazik tempo rehberi; dirençli eğitim değildir.</p></div><div className="exercise-grid"><article className={`breath-coach ${exercisePhase}`}><div className="breath-circle"><div><Icon name="breath" /><strong>{exercisePhase === "inhale" ? "Nefes al" : exercisePhase === "exhale" ? "Nefes ver" : exercisePhase === "rest" ? "Dinlen" : exercisePhase === "done" ? "Tamamlandı" : "Hazır"}</strong><span>{exercisePhase === "ready" || exercisePhase === "done" ? "2 sn al · 2 sn ver" : `${exerciseSecond} sn`}</span></div></div><div className="exercise-progress"><span>Nefes {exerciseBreath}/10</span><div><i style={{ width: `${exerciseBreath * 10}%` }} /></div></div><div className="coach-actions">{exercisePhase === "ready" || exercisePhase === "done" ? <button className="button primary" onClick={startExercise}>Egzersizi başlat</button> : <button className="button ghost" onClick={() => { setExercisePhase("ready"); setExerciseBreath(0); window.speechSynthesis?.cancel(); }}>Durdur</button>}<label><input type="checkbox" checked={voiceEnabled} onChange={(event) => setVoiceEnabled(event.target.checked)} /> Sesli komut</label></div></article><article className="exercise-info"><span className="info-number">10</span><h3>Yavaş ve kontrollü nefes</h3><p>Bir set 10 nefestir. Her nefes 2 saniye alma ve 2 saniye verme ile dakikada 15 nefes temposuna karşılık gelir.</p><ul><li>Dik ve destekli otur.</li><li>Baş dönmesi, ağrı veya nefes darlığında dur.</li><li>Direnç seviyesi yalnızca fizyoterapist/hekimin planıyla belirlenmelidir.</li></ul></article></div></section>
    <section className="section-block hospital-section" id="hospital">
      <div className="section-title"><div><span>HASTANE KAYIT</span><h2>Demo hasta akışı</h2></div><p>Bu bölüm ana kişi listesinden ayrıdır. Barkodla hastayı bulup bilgilerini cihaza göndererek ölçümü başlatır.</p></div>
      <div className="hospital-toolbar"><label>Hasta barkodu<input value={hospitalCode} placeholder="Barkodu okut veya yaz" onChange={(event) => findHospitalByCode(event.target.value)} /></label><button className="button ghost" onClick={() => setScannerActive(true)}>Kamerayla okut</button><button className="button primary" onClick={() => setHospitalForm(emptyHospitalPatient())}>+ Hasta ekle</button></div>
      {scannerActive ? <div className="scanner"><video ref={scannerVideoRef} playsInline muted /><button className="button ghost" onClick={() => setScannerActive(false)}>Kapat</button></div> : null}
      {hospitalForm ? <div className="edit-form hospital-form"><label>Barkod<input value={hospitalForm.barcode} onChange={(event) => setHospitalForm({ ...hospitalForm, barcode: event.target.value })} /></label><label>Ad Soyad<input value={hospitalForm.name} onChange={(event) => setHospitalForm({ ...hospitalForm, name: event.target.value })} /></label><label>Yaş<input type="number" min="1" max="120" value={hospitalForm.age} onChange={(event) => setHospitalForm({ ...hospitalForm, age: Number(event.target.value) })} /></label><label>Cinsiyet<select value={hospitalForm.gender} onChange={(event) => setHospitalForm({ ...hospitalForm, gender: event.target.value as "M" | "W" })}><option value="M">Erkek</option><option value="W">Kadın</option></select></label><label>Boy (cm)<input type="number" min="100" max="230" value={hospitalForm.heightCm} onChange={(event) => setHospitalForm({ ...hospitalForm, heightCm: Number(event.target.value) })} /></label><label>Kilo (kg)<input type="number" min="25" max="250" value={hospitalForm.weightKg} onChange={(event) => setHospitalForm({ ...hospitalForm, weightKg: Number(event.target.value) })} /></label><div className="form-actions"><button className="button ghost" onClick={() => setHospitalForm(null)}>Vazgeç</button><button className="button primary" onClick={() => void saveHospitalPatient(hospitalForm)}>Kaydet</button></div></div> : null}
      <div className="hospital-layout"><div className="hospital-list">{hospitalPatients.length ? hospitalPatients.map((patient) => <button className={`patient-row ${selectedHospitalId === patient.id ? "selected" : ""}`} key={patient.id} onClick={() => { setSelectedHospitalId(patient.id); setHospitalCode(patient.barcode); }}><span>{patient.name.slice(0, 2)}</span><div><strong>{patient.name}</strong><small>{patient.barcode} · {patient.age} yaş</small></div></button>) : <div className="empty-state"><strong>Hasta kaydı yok</strong><p>Demo için ilk kişiyi ekleyebilirsin.</p></div>}</div><div className="hospital-detail">{selectedHospital ? <><div className="patient-heading"><div><span>SEÇİLİ HASTA</span><h3>{selectedHospital.name}</h3><p>{selectedHospital.gender === "M" ? "Erkek" : "Kadın"} · {selectedHospital.age} yaş · {selectedHospital.heightCm} cm · {selectedHospital.weightKg} kg</p></div><div className="card-actions"><button onClick={() => setHospitalForm({ ...selectedHospital })}>Düzenle</button><button className="danger-link" onClick={() => void deleteHospitalPatient(selectedHospital)}>Sil</button></div></div><div className="hospital-actions">{hospitalSession?.patientId === selectedHospital.id ? <button className="button ghost" onClick={() => void endHospitalMeasurement()}>Oturumu bitir</button> : <button className="button primary" onClick={() => void startHospitalMeasurement(selectedHospital)}>Ölçüm al</button>}</div><div className="mini-records">{hospitalMeasurements.slice(0, 8).map((item) => <div key={item.id}><span>{item.type === "heart_rate" ? "Nabız" : item.type.toUpperCase()}</span><strong>{item.type === "heart_rate" ? Math.round(item.value) : item.value.toFixed(2)} {item.unit}</strong><time>{formatDate(item.measuredAt)}</time></div>)}{!hospitalMeasurements.length ? <p>Bu hasta için henüz ölçüm yok.</p> : null}</div></> : <div className="empty-state"><Icon name="user" /><strong>Hasta seç</strong><p>Listeden veya barkod alanından bir hasta seç.</p></div>}</div></div>
    </section>
    <section className="section-block" id="devices"><div className="section-title"><div><span>CİHAZLAR</span><h2>Bağlı sistemler</h2></div><button className="button primary compact" onClick={() => void connect()} disabled={connection === "connecting" || connection === "connected"}>+ Yeni cihaz</button></div><div className="device-list">{devices.length ? devices.map((device) => <article className="device-row" key={device.id}><div className="device-symbol"><Icon name="device" /></div><div><strong>{device.name}</strong><p>{device.model}</p></div><span>{device.id === deviceId && connection === "connected" ? "Şimdi bağlı" : `Son görülme ${formatDate(device.lastSeenAt)}`}</span></article>) : <div className="empty-state"><Icon name="device" /><strong>Cihaz eklenmedi</strong><p>nRF52840 kartını eklemek için üstteki düğmeyi kullan.</p></div>}</div></section>
    <footer><div className="health-brand small"><span className="health-mark"><span /></span><span><strong>SFT Health</strong><small>Prototip sağlık izleme sistemi</small></span></div><p>{deviceName} · Ölçümler bilgilendirme amaçlıdır; tıbbi tanı veya tedavi kararı için kullanılmaz.</p></footer>
  </main>;
}
