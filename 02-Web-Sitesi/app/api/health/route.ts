import { desc, eq } from "drizzle-orm";
import { devices, healthContext, hospitalPatients, measurements, profiles, unauthorized } from "./_server";

export async function GET() {
  const context = await healthContext();
  if (!context) {
    if (process.env.NODE_ENV !== "production") return Response.json({ devices: [], profiles: [], measurements: [], hospitalPatients: [] });
    return unauthorized();
  }
  const [deviceRows, profileRows, measurementRows, hospitalRows] = await Promise.all([
    context.db.select().from(devices).where(eq(devices.userId, context.userId)).orderBy(desc(devices.lastSeenAt)),
    context.db.select().from(profiles).where(eq(profiles.userId, context.userId)).orderBy(profiles.deviceProfileIndex),
    context.db.select().from(measurements).where(eq(measurements.userId, context.userId)).orderBy(desc(measurements.measuredAt)).limit(250),
    context.db.select().from(hospitalPatients).where(eq(hospitalPatients.userId, context.userId)).orderBy(desc(hospitalPatients.updatedAt)),
  ]);
  return Response.json({
    devices: deviceRows.map((row) => ({ id: row.id, name: row.name, model: row.model, lastSeenAt: row.lastSeenAt })),
    profiles: profileRows.map((row) => ({ id: row.id, deviceId: row.deviceId, deviceProfileIndex: row.deviceProfileIndex, name: row.name, age: row.age, gender: row.gender, heightCm: row.heightCm, weightKg: row.weightKg, hospitalPatientId: row.hospitalPatientId })),
    measurements: measurementRows.map((row) => ({ id: row.id, deviceId: row.deviceId, profileIndex: row.profileIndex, hospitalPatientId: row.hospitalPatientId, type: row.type, value: row.value, unit: row.unit, predicted: row.predicted, percent: row.percent, status: row.status, measuredAt: row.measuredAt })),
    hospitalPatients: hospitalRows.map((row) => ({ id: row.id, barcode: row.barcode, name: row.name, age: row.age, gender: row.gender, heightCm: row.heightCm, weightKg: row.weightKg, createdAt: row.createdAt, updatedAt: row.updatedAt })),
  });
}
