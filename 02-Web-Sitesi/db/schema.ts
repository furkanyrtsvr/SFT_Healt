import { index, integer, primaryKey, real, sqliteTable, text } from "drizzle-orm/sqlite-core";

export const devices = sqliteTable("health_devices", {
  userId: text("user_id").notNull(),
  id: text("id").notNull(),
  name: text("name").notNull(),
  model: text("model").notNull(),
  lastSeenAt: text("last_seen_at").notNull(),
}, (table) => [
  primaryKey({ columns: [table.userId, table.id] }),
  index("health_devices_user_seen_idx").on(table.userId, table.lastSeenAt),
]);

export const profiles = sqliteTable("health_profiles", {
  userId: text("user_id").notNull(),
  id: text("id").notNull(),
  deviceId: text("device_id").notNull(),
  deviceProfileIndex: integer("device_profile_index").notNull(),
  name: text("name").notNull(),
  age: integer("age").notNull(),
  gender: text("gender").notNull(),
  heightCm: integer("height_cm").notNull(),
  weightKg: integer("weight_kg").notNull(),
  hospitalPatientId: text("hospital_patient_id"),
  updatedAt: text("updated_at").notNull(),
}, (table) => [
  primaryKey({ columns: [table.userId, table.id] }),
  index("health_profiles_user_device_idx").on(table.userId, table.deviceId),
]);

export const measurements = sqliteTable("health_measurements", {
  userId: text("user_id").notNull(),
  id: text("id").notNull(),
  deviceId: text("device_id").notNull(),
  profileIndex: integer("profile_index").notNull(),
  hospitalPatientId: text("hospital_patient_id"),
  type: text("type").notNull(),
  value: real("value").notNull(),
  unit: text("unit").notNull(),
  predicted: real("predicted"),
  percent: integer("percent"),
  status: text("status"),
  measuredAt: text("measured_at").notNull(),
}, (table) => [
  primaryKey({ columns: [table.userId, table.id] }),
  index("health_measurements_user_time_idx").on(table.userId, table.measuredAt),
  index("health_measurements_user_profile_idx").on(table.userId, table.profileIndex, table.measuredAt),
]);

export const hospitalPatients = sqliteTable("health_hospital_patients", {
  userId: text("user_id").notNull(),
  id: text("id").notNull(),
  barcode: text("barcode").notNull(),
  name: text("name").notNull(),
  age: integer("age").notNull(),
  gender: text("gender").notNull(),
  heightCm: integer("height_cm").notNull(),
  weightKg: integer("weight_kg").notNull(),
  createdAt: text("created_at").notNull(),
  updatedAt: text("updated_at").notNull(),
}, (table) => [
  primaryKey({ columns: [table.userId, table.id] }),
  index("health_hospital_user_barcode_idx").on(table.userId, table.barcode),
]);

export const exerciseSessions = sqliteTable("health_exercise_sessions", {
  userId: text("user_id").notNull(),
  id: text("id").notNull(),
  profileIndex: integer("profile_index"),
  preset: text("preset").notNull(),
  breaths: integer("breaths").notNull(),
  completedAt: text("completed_at").notNull(),
}, (table) => [
  primaryKey({ columns: [table.userId, table.id] }),
  index("health_exercises_user_time_idx").on(table.userId, table.completedAt),
]);
