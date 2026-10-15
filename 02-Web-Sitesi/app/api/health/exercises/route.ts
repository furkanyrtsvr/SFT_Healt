import { exerciseSessions, healthContext, invalid, unauthorized } from "../_server";

export async function POST(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string; profileIndex?: number | null; preset?: string; breaths?: number; completedAt?: string };
  if (!body.id || !body.preset || !body.completedAt) return invalid("Eksik egzersiz bilgisi");
  await context.db.insert(exerciseSessions).values({
    userId: context.userId, id: body.id.slice(0, 160), profileIndex: Number.isInteger(body.profileIndex) ? body.profileIndex! : null,
    preset: body.preset.slice(0, 64), breaths: Math.max(1, Math.min(100, Number(body.breaths) || 10)), completedAt: body.completedAt,
  }).onConflictDoNothing();
  return Response.json({ ok: true }, { status: 201 });
}
