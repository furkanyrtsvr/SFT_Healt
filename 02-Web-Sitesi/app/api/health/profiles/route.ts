import { and, eq } from "drizzle-orm";
import { healthContext, invalid, profiles, unauthorized } from "../_server";

export async function POST(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string; deviceId?: string; deviceProfileIndex?: number; name?: string; age?: number; gender?: string; heightCm?: number; weightKg?: number; hospitalPatientId?: string | null };
  if (!body.id || !body.deviceId || !body.name || !Number.isInteger(body.deviceProfileIndex)) return invalid("Eksik profil bilgisi");
  const values = {
    userId: context.userId, id: body.id.slice(0, 160), deviceId: body.deviceId.slice(0, 128), deviceProfileIndex: body.deviceProfileIndex!,
    name: body.name.slice(0, 32), age: Math.max(1, Math.min(120, Number(body.age) || 18)), gender: body.gender === "W" ? "W" : "M",
    heightCm: Math.max(100, Math.min(230, Number(body.heightCm) || 170)), weightKg: Math.max(25, Math.min(250, Number(body.weightKg) || 70)), hospitalPatientId: body.hospitalPatientId?.slice(0, 160) ?? null, updatedAt: new Date().toISOString(),
  };
  await context.db.insert(profiles).values(values).onConflictDoUpdate({
    target: [profiles.userId, profiles.id],
    set: { deviceId: values.deviceId, deviceProfileIndex: values.deviceProfileIndex, name: values.name, age: values.age, gender: values.gender, heightCm: values.heightCm, weightKg: values.weightKg, hospitalPatientId: values.hospitalPatientId, updatedAt: values.updatedAt },
  });
  return Response.json({ ok: true }, { status: 201 });
}

export async function DELETE(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string };
  if (!body.id) return invalid("Profil kimliği gerekli");
  await context.db.delete(profiles).where(and(eq(profiles.userId, context.userId), eq(profiles.id, body.id.slice(0, 160))));
  return Response.json({ ok: true });
}
