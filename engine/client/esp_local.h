#pragma once
/* Shared snapshot for the local-only avatar overlay. Packed so a 64-bit
   reader matches the 32-bit engine. */
#include <stdint.h>

#define ESP_MAGIC 0x36315345u /* 'ES16' little-endian */
#define ESP_MAX   32
#define ESP_PATH  "/tmp/cs16_local_esp.bin"

#pragma pack(push, 1)
typedef struct
{
	float origin[3];
	float mins[3];
	float maxs[3];
	int32_t health;
	int32_t dead;
	int32_t team; /* 1 = T, 2 = CT, 0 = unknown */
	char name[32];
	char model[32];
} esp_ent_t;

typedef struct
{
	uint32_t magic;
	uint32_t seq;     /* odd while the engine is writing */
	uint32_t time_ms;
	int32_t win_x, win_y, win_w, win_h;
	float vieworg[3];
	float viewangles[3];
	float fov_x, fov_y;
	int32_t count;
	esp_ent_t ent[ESP_MAX];
	float camera_mvp[16];
	int32_t camera_vp[4];
	int32_t camera_ready;
} esp_frame_t;
#pragma pack(pop)
