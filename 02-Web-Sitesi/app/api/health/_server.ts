import { getChatGPTUser } from "../../chatgpt-auth";
import { getDb } from "../../../db";
export * from "../../../db/schema";

export async function healthContext() {
  const user = await getChatGPTUser();
  if (user) return { userId: user.userId, db: getDb() };
  return null;
}

export function unauthorized() {
  return Response.json({ error: "Oturum gerekli" }, { status: 401 });
}

export function invalid(message: string) {
  return Response.json({ error: message }, { status: 400 });
}
