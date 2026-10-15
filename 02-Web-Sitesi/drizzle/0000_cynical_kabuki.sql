CREATE TABLE `health_devices` (
	`user_id` text NOT NULL,
	`id` text NOT NULL,
	`name` text NOT NULL,
	`model` text NOT NULL,
	`last_seen_at` text NOT NULL,
	PRIMARY KEY(`user_id`, `id`)
);
--> statement-breakpoint
CREATE INDEX `health_devices_user_seen_idx` ON `health_devices` (`user_id`,`last_seen_at`);--> statement-breakpoint
CREATE TABLE `health_exercise_sessions` (
	`user_id` text NOT NULL,
	`id` text NOT NULL,
	`profile_index` integer,
	`preset` text NOT NULL,
	`breaths` integer NOT NULL,
	`completed_at` text NOT NULL,
	PRIMARY KEY(`user_id`, `id`)
);
--> statement-breakpoint
CREATE INDEX `health_exercises_user_time_idx` ON `health_exercise_sessions` (`user_id`,`completed_at`);--> statement-breakpoint
CREATE TABLE `health_measurements` (
	`user_id` text NOT NULL,
	`id` text NOT NULL,
	`device_id` text NOT NULL,
	`profile_index` integer NOT NULL,
	`type` text NOT NULL,
	`value` real NOT NULL,
	`unit` text NOT NULL,
	`predicted` real,
	`percent` integer,
	`status` text,
	`measured_at` text NOT NULL,
	PRIMARY KEY(`user_id`, `id`)
);
--> statement-breakpoint
CREATE INDEX `health_measurements_user_time_idx` ON `health_measurements` (`user_id`,`measured_at`);--> statement-breakpoint
CREATE INDEX `health_measurements_user_profile_idx` ON `health_measurements` (`user_id`,`profile_index`,`measured_at`);--> statement-breakpoint
CREATE TABLE `health_profiles` (
	`user_id` text NOT NULL,
	`id` text NOT NULL,
	`device_id` text NOT NULL,
	`device_profile_index` integer NOT NULL,
	`name` text NOT NULL,
	`age` integer NOT NULL,
	`gender` text NOT NULL,
	`height_cm` integer NOT NULL,
	`weight_kg` integer NOT NULL,
	`updated_at` text NOT NULL,
	PRIMARY KEY(`user_id`, `id`)
);
--> statement-breakpoint
CREATE INDEX `health_profiles_user_device_idx` ON `health_profiles` (`user_id`,`device_id`);