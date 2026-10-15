import { devices, healthContext, invalid, unauthorized } from "../_server";

export async function POST(request: Request) {
  const context = await healthContext();
  if (!context) return unauthorized();
  const body = await request.json() as { id?: string; name?: string; model?: string; lastSeenAt?: string };
  if (!body.id || !body.name || !body.lastSeenAt) return invalid("Eksik cihaz bilgisi");
  const id = body.id.slice(0, 128);
  const name = body.name.slice(0, 80);
  const model = (body.model ?? "SFT Health").slice(0, 120);
  await context.db.insert(devices).values({ userId: context.userId, id, name, model, lastSeenAt: body.lastSeenAt }).onConflictDoUpdate({
    target: [devices.userId, devices.id],
    set: { name, model, lastSeenAt: body.lastSeenAt },
  });
  return Response.json({ ok: true }, { status: 201 });
}
