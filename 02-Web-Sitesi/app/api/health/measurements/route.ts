import { healthContext, invalid, measurements, unauthorized } from "../_server";

export async function POST(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string; deviceId?: string; profileIndex?: number; hospitalPatientId?: string | null; type?: string; value?: number; unit?: string; predicted?: number | null; percent?: number | null; status?: string | null; measuredAt?: string };
  if (!body.id || !body.deviceId || !Number.isInteger(body.profileIndex) || !["heart_rate", "mep", "mip"].includes(body.type ?? "") || !Number.isFinite(body.value)) return invalid("Eksik ölçüm bilgisi");
  await context.db.insert(measurements).values({
    userId: context.userId, id: body.id.slice(0, 160), deviceId: body.deviceId.slice(0, 128), profileIndex: body.profileIndex!, hospitalPatientId: body.hospitalPatientId?.slice(0, 160) ?? null, type: body.type!,
    value: body.value!, unit: body.unit === "kPa" ? "kPa" : "BPM", predicted: Number.isFinite(body.predicted) ? body.predicted : null,
    percent: Number.isFinite(body.percent) ? Math.round(body.percent!) : null, status: body.status?.slice(0, 48) ?? null, measuredAt: body.measuredAt ?? new Date().toISOString(),
  }).onConflictDoNothing();
  return Response.json({ ok: true }, { status: 201 });
}
