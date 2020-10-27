/* From OpenHMD Oculus Rift driver */

#ifndef __HMD_HID_INFO__
#define __HMD_HID_INFO__

typedef union {
	struct {
		float x, y, z;
	};
	float arr[3];
} vec3f;

typedef enum {
	RIFT_CMD_SENSOR_CONFIG = 2,
	RIFT_CMD_RANGE = 4,
	RIFT_CMD_KEEP_ALIVE = 8,
	RIFT_CMD_DISPLAY_INFO = 9,
	RIFT_CMD_ENABLE_COMPONENTS = 0x1d
} rift_sensor_feature_cmd;

typedef enum {
	RIFT_CF_SENSOR,
	RIFT_CF_HMD
} rift_coordinate_frame;

typedef enum {
	RIFT_IRQ_SENSORS = 1,
	RIFT_IRQ_SENSORS_DK2 = 11
} rift_irq_cmd;

typedef enum {
	RIFT_DT_NONE,
	RIFT_DT_SCREEN_ONLY,
	RIFT_DT_DISTORTION
} rift_distortion_type;

// Sensor config flags
#define RIFT_SCF_RAW_MODE           0x01
#define RIFT_SCF_CALIBRATION_TEST   0x02
#define RIFT_SCF_USE_CALIBRATION    0x04
#define RIFT_SCF_AUTO_CALIBRATION   0x08
#define RIFT_SCF_MOTION_KEEP_ALIVE  0x10
#define RIFT_SCF_COMMAND_KEEP_ALIVE 0x20
#define RIFT_SCF_SENSOR_COORDINATES 0x40

typedef struct {
	uint16_t command_id;
	uint16_t accel_scale;
	uint16_t gyro_scale;
	uint16_t mag_scale;
} pkt_sensor_range;

typedef struct {
	int32_t accel[3];
	int32_t gyro[3];
} pkt_tracker_sample;

typedef struct {
	uint8_t num_samples;
	uint32_t timestamp;
	uint16_t last_command_id;
	int16_t temperature;
	pkt_tracker_sample samples[3];
	int16_t mag[3];
} pkt_tracker_sensor;

typedef struct {
	uint16_t command_id;
	uint8_t flags;
	uint16_t packet_interval;
	uint16_t keep_alive_interval;	// in ms
} pkt_sensor_config;

typedef struct {
	uint16_t command_id;
	rift_distortion_type distortion_type;
	uint8_t distortion_type_opts;
	uint16_t h_resolution, v_resolution;
	float h_screen_size, v_screen_size;
	float v_center;
	float lens_separation;
	float eye_to_screen_distance[2];
	float distortion_k[6];
} pkt_sensor_display_info;

typedef struct {
	uint16_t command_id;
	uint16_t keep_alive_interval;
} pkt_keep_alive;

typedef struct {
	hid_device *handle;
	pkt_sensor_range sensor_range;
	pkt_sensor_display_info display_info;
	pkt_sensor_config sensor_config;
	pkt_tracker_sensor sensor;
	rift_coordinate_frame coordinate_frame, hw_coordinate_frame;
	double last_keep_alive;
	uint32_t last_imu_timestamp;
	vec3f raw_mag, raw_accel, raw_gyro;
} HMDHidInfo;

#endif
