#include "storage_reset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/settings/settings.h>

#define RESET_MARKER_KEY "sft/reset_v1"
#define RESET_MARKER_VALUE 0x53465431UL

struct reset_loader_value {
	uint32_t value;
	bool found;
};

static int reset_loader(
	const char *name,
	size_t len,
	settings_read_cb read_cb,
	void *cb_arg,
	void *param
)
{
	struct reset_loader_value *loaded = param;

	if (name[0] != '\0' || len != sizeof(loaded->value)) {
		return 0;
	}

	int result = read_cb(cb_arg, &loaded->value, sizeof(loaded->value));
	if (result >= 0) {
		loaded->found = true;
		return 0;
	}

	return result;
}

int storage_reset_once(void)
{
	int result = settings_subsys_init();

	if (result != 0) {
		return result;
	}

	struct reset_loader_value loaded = {0};
	result = settings_load_subtree_direct(
		RESET_MARKER_KEY,
		reset_loader,
		&loaded
	);
	if (result != 0) {
		return result;
	}

	if (loaded.found && loaded.value == RESET_MARKER_VALUE) {
		return 0;
	}

	/* Yeni SFT surumunde eski profil, kalibrasyon ve olcumleri bir kez sil. */
	(void)settings_delete("pulse/profiles");
	(void)settings_delete("pulse/pressure");
	(void)settings_delete("sft/records");

	uint32_t marker = RESET_MARKER_VALUE;
	return settings_save_one(
		RESET_MARKER_KEY,
		&marker,
		sizeof(marker)
	);
}
