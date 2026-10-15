CREATE TABLE `health_hospital_patients` (
	`user_id` text NOT NULL,
	`id` text NOT NULL,
	`barcode` text NOT NULL,
	`name` text NOT NULL,
	`age` integer NOT NULL,
	`gender` text NOT NULL,
	`height_cm` integer NOT NULL,
	`weight_kg` integer NOT NULL,
	`created_at` text NOT NULL,
	`updated_at` text NOT NULL,
	PRIMARY KEY(`user_id`, `id`)
);
--> statement-breakpoint
CREATE INDEX `health_hospital_user_barcode_idx` ON `health_hospital_patients` (`user_id`,`barcode`);--> statement-breakpoint
ALTER TABLE `health_measurements` ADD `hospital_patient_id` text;--> statement-breakpoint
ALTER TABLE `health_profiles` ADD `hospital_patient_id` text;