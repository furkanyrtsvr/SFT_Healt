import { and, eq } from "drizzle-orm";
import { healthContext, hospitalPatients, invalid, unauthorized } from "../_server";

export async function POST(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string; barcode?: string; name?: string; age?: number; gender?: string; heightCm?: number; weightKg?: number; createdAt?: string };
  if (!body.id || !body.barcode || !body.name) return invalid("Hasta bilgileri eksik");
  const now = new Date().toISOString();
  const values = {
    userId: context.userId,
    id: body.id.slice(0, 160),
    barcode: body.barcode.slice(0, 64),
    name: body.name.slice(0, 32),
    age: Math.max(1, Math.min(120, Number(body.age) || 18)),
    gender: body.gender === "W" ? "W" : "M",
    heightCm: Math.max(100, Math.min(230, Number(body.heightCm) || 170)),
    weightKg: Math.max(25, Math.min(250, Number(body.weightKg) || 70)),
    createdAt: body.createdAt ?? now,
    updatedAt: now,
  };
  await context.db.insert(hospitalPatients).values(values).onConflictDoUpdate({
    target: [hospitalPatients.userId, hospitalPatients.id],
    set: { barcode: values.barcode, name: values.name, age: values.age, gender: values.gender, heightCm: values.heightCm, weightKg: values.weightKg, updatedAt: values.updatedAt },
  });
  return Response.json({ ok: true }, { status: 201 });
}

export async function DELETE(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string };
  if (!body.id) return invalid("Hasta kimliği gerekli");
  await context.db.delete(hospitalPatients).where(and(eq(hospitalPatients.userId, context.userId), eq(hospitalPatients.id, body.id.slice(0, 160))));
  return Response.json({ ok: true });
}
