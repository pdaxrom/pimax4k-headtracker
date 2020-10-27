#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <hidapi.h>
#include <inttypes.h>

#include "hid.h"

#define MAX_STR 1024

#define FEATURE_BUFFER_SIZE 256

#define TICK_LEN (1.0f / 1000.0f)	// 1000 Hz ticks
#define KEEP_ALIVE_VALUE (10 * 1000)
#define SETFLAG(_s, _flag, _val) (_s) = ((_s) & ~(_flag)) | ((_val) ? (_flag) : 0)

#define SKIP_CMD (buffer++)
#define READ8 *(buffer++);
#define READ16 *buffer | (*(buffer + 1) << 8); buffer += 2;
#define READ32 *buffer | (*(buffer + 1) << 8) | (*(buffer + 2) << 16) | (*(buffer + 3) << 24); buffer += 4;
#define READFLOAT ((float)(*buffer)); buffer += 4;
#define READFIXED (float)(*buffer | (*(buffer + 1) << 8) | (*(buffer + 2) << 16) | (*(buffer + 3) << 24)) / 1000000.0f; buffer += 4;

#define WRITE8(_val) *(buffer++) = (_val);
#define WRITE16(_val) WRITE8((_val) & 0xff); WRITE8(((_val) >> 8) & 0xff);
#define WRITE32(_val) WRITE16((_val) & 0xffff) *buffer; WRITE16(((_val) >> 16) & 0xffff);

#define OHMD_MAX(_a, _b) ((_a) > (_b) ? (_a) : (_b))
#define OHMD_MIN(_a, _b) ((_a) < (_b) ? (_a) : (_b))

#define LOGD LOGI
#define LOGW LOGI
#define LOGE LOGI

static void LOGI(const char *fmt, ...)
{
	char message[2048];
	va_list ap;

	va_start(ap, fmt);
	//vsyslog( pri, fmt, ap );

	vsnprintf(message, sizeof(message), fmt, ap);

	va_end(ap);

	fprintf(stderr, "syslog: %s\n", message);
}

static void DUMP(unsigned char *buffer, int size)
{
	fprintf(stderr, "DUMP %d bytes:\n", size);

	for (int i = 0; i < size; i++) {
		fprintf(stderr, "%02X ", buffer[i]);
		if (i % 16 == 15) {
			fprintf(stderr, "\n");
		}
	}
	fprintf(stderr, "\n");
}

static int get_feature_report(HMDHidInfo * info, char cmd, unsigned char *buf);
static int send_feature_report(HMDHidInfo * info, const unsigned char *data,
			       size_t length);

static int decode_sensor_range(pkt_sensor_range * range,
			       const unsigned char *buffer, int size)
{
	if (!(size == 8 || size == 9)) {
		LOGE("invalid packet size (expected 8 or 9 but got %d)", size);
		return 0;
	}

	SKIP_CMD;
	range->command_id = READ16;
	range->accel_scale = READ8;
	range->gyro_scale = READ16;
	range->mag_scale = READ16;

	return 1;
}

static int decode_sensor_display_info(pkt_sensor_display_info * info,
				      const unsigned char *buffer, int size)
{
	if (!(size == 56 || size == 57)) {
		LOGE("invalid packet size (expected 56 or 57 but got %d)",
		     size);
		return 0;
	}

	SKIP_CMD;
	info->command_id = READ16;
	info->distortion_type = READ8;
	info->h_resolution = READ16;
	info->v_resolution = READ16;
	info->h_screen_size = READFIXED;
	info->v_screen_size = READFIXED;
	info->v_center = READFIXED;
	info->lens_separation = READFIXED;
	info->eye_to_screen_distance[0] = READFIXED;
	info->eye_to_screen_distance[1] = READFIXED;

	info->distortion_type_opts = 0;

	for (int i = 0; i < 6; i++) {
		info->distortion_k[i] = READFLOAT;
	}

	return 1;
}

static int decode_sensor_config(pkt_sensor_config * config,
				const unsigned char *buffer, int size)
{
	if (!(size == 7 || size == 8)) {
		LOGE("invalid packet size (expected 7 or 8 but got %d)", size);
		return 0;
	}

	SKIP_CMD;
	config->command_id = READ16;
	config->flags = READ8;
	config->packet_interval = READ8;
	config->keep_alive_interval = READ16;

	return 1;
}

static void dump_packet_sensor_range(const pkt_sensor_range * range)
{
	(void)range;

	LOGD("sensor range");
	LOGD("  command id:  %d", range->command_id);
	LOGD("  accel scale: %d", range->accel_scale);
	LOGD("  gyro scale:  %d", range->gyro_scale);
	LOGD("  mag scale:   %d", range->mag_scale);
}

static void dump_packet_sensor_display_info(const pkt_sensor_display_info *
					    info)
{
	(void)info;

	LOGD("display info");
	LOGD("  command id:             %d", info->command_id);
	LOGD("  distortion_type:        %d", info->distortion_type);
	LOGD("  resolution:             %d x %d", info->h_resolution,
	     info->v_resolution);
	LOGD("  screen size:            %f x %f", info->h_screen_size,
	     info->v_screen_size);
	LOGD("  vertical center:        %f", info->v_center);
	LOGD("  lens_separation:        %f", info->lens_separation);
	LOGD("  eye_to_screen_distance: %f, %f",
	     info->eye_to_screen_distance[0], info->eye_to_screen_distance[1]);
	LOGD("  distortion_k:           %f, %f, %f, %f, %f, %f",
	     info->distortion_k[0], info->distortion_k[1],
	     info->distortion_k[2], info->distortion_k[3],
	     info->distortion_k[4], info->distortion_k[5]);
}

static void dump_packet_sensor_config(const pkt_sensor_config * config)
{
	(void)config;

	LOGD("sensor config");
	LOGD("  command id:          %u", config->command_id);
	LOGD("  flags:               %02x", config->flags);
	LOGD("    raw mode:                  %d",
	     !!(config->flags & RIFT_SCF_RAW_MODE));
	LOGD("    calibration test:          %d",
	     !!(config->flags & RIFT_SCF_CALIBRATION_TEST));
	LOGD("    use calibration:           %d",
	     !!(config->flags & RIFT_SCF_USE_CALIBRATION));
	LOGD("    auto calibration:          %d",
	     !!(config->flags & RIFT_SCF_AUTO_CALIBRATION));
	LOGD("    motion keep alive:         %d",
	     !!(config->flags & RIFT_SCF_MOTION_KEEP_ALIVE));
	LOGD("    motion command keep alive: %d",
	     !!(config->flags & RIFT_SCF_COMMAND_KEEP_ALIVE));
	LOGD("    sensor coordinates:        %d",
	     !!(config->flags & RIFT_SCF_SENSOR_COORDINATES));
	LOGD("  packet interval:     %u", config->packet_interval);
	LOGD("  keep alive interval: %u", config->keep_alive_interval);
}

void dump_packet_tracker_sensor(const pkt_tracker_sensor * sensor)
{
	(void)sensor;

	LOGD("tracker sensor:");
	LOGD("  last command id: %u", sensor->last_command_id);
	LOGD("  timestamp:       %u", sensor->timestamp);
	LOGD("  temperature:     %d", sensor->temperature);
	LOGD("  num samples:     %u", sensor->num_samples);
	LOGD("  magnetic field:  %i %i %i", sensor->mag[0], sensor->mag[1],
	     sensor->mag[2]);

	for (int i = 0; i < sensor->num_samples; i++) {
		LOGD("    accel: %d %d %d", sensor->samples[i].accel[0],
		     sensor->samples[i].accel[1], sensor->samples[i].accel[2]);
		LOGD("    gyro:  %d %d %d", sensor->samples[i].gyro[0],
		     sensor->samples[i].gyro[1], sensor->samples[i].gyro[2]);
	}
}

static int encode_sensor_config(unsigned char *buffer,
				const pkt_sensor_config * config)
{
	WRITE8(RIFT_CMD_SENSOR_CONFIG);
	WRITE16(config->command_id);
	WRITE8(config->flags);
	WRITE8(config->packet_interval);
	WRITE16(config->keep_alive_interval);
	return 7;		// sensor config packet size
}

static int encode_pimax_cmd_2(unsigned char *buffer)
{
	WRITE8(0x02);
	WRITE8(0x00);
	WRITE8(0x00);
	WRITE8(0x20);
	WRITE8(0x01);
	WRITE8(0xe8);
	WRITE8(0x03);
	return 7;
}

static int encode_pimax_cmd_17(unsigned char *buffer)
{
	WRITE8(0x11);
	WRITE8(0x00);
	WRITE8(0x00);
	WRITE8(0x0b);
	WRITE8(0x10);
	WRITE8(0x27);
	return 6;
}

static int encode_keep_alive(unsigned char *buffer,
			     const pkt_keep_alive * keep_alive)
{
	WRITE8(RIFT_CMD_KEEP_ALIVE);
	WRITE16(keep_alive->command_id);
	WRITE16(keep_alive->keep_alive_interval);
	return 5;		// keep alive packet size
}

static void set_coordinate_frame(HMDHidInfo * info,
				 rift_coordinate_frame coordframe)
{
	info->coordinate_frame = coordframe;

	// set the RIFT_SCF_SENSOR_COORDINATES in the sensor config to match whether coordframe is hmd or sensor
	SETFLAG(info->sensor_config.flags, RIFT_SCF_SENSOR_COORDINATES,
		coordframe == RIFT_CF_SENSOR);

	// encode send the new config to the Rift
	unsigned char buf[FEATURE_BUFFER_SIZE];
	int size = encode_sensor_config(buf, &info->sensor_config);
	if (send_feature_report(info, buf, size) == -1) {
		LOGE("send_feature_report failed in set_coordinate frame");
		return;
	}

	// read the state again, set the hw_coordinate_frame to match what
	// the hardware actually is set to just incase it doesn't stick.
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buf);
	if (size <= 0) {
		LOGW("could not set coordinate frame");
		info->hw_coordinate_frame = RIFT_CF_HMD;
		return;
	}

	decode_sensor_config(&info->sensor_config, buf, size);
	info->hw_coordinate_frame =
	    (info->sensor_config.
	     flags & RIFT_SCF_SENSOR_COORDINATES) ? RIFT_CF_SENSOR :
	    RIFT_CF_HMD;

	if (info->hw_coordinate_frame != coordframe) {
		LOGW("coordinate frame didn't stick");
	}
}

static void decode_sample(const unsigned char *buffer, int32_t * smp)
{
	/*
	 * Decode 3 tightly packed 21 bit values from 4 bytes.
	 * We unpack them in the higher 21 bit values first and then shift
	 * them down to the lower in order to get the sign bits correct.
	 */

	int x =
	    (buffer[0] << 24) | (buffer[1] << 16) | ((buffer[2] & 0xF8) << 8);
	int y =
	    ((buffer[2] & 0x07) << 29) | (buffer[3] << 21) | (buffer[4] << 13) |
	    ((buffer[5] & 0xC0) << 5);
	int z =
	    ((buffer[5] & 0x3F) << 26) | (buffer[6] << 18) | (buffer[7] << 10);

	smp[0] = x >> 11;
	smp[1] = y >> 11;
	smp[2] = z >> 11;
}

static int decode_tracker_sensor_msg(pkt_tracker_sensor * msg,
				     const unsigned char *buffer, int size)
{
	if (!(size == 62 || size == 64)) {
		LOGE("invalid packet size (expected 62 or 64 but got %d)",
		     size);
		return 0;
	}

	SKIP_CMD;
	msg->num_samples = READ8;
	msg->timestamp = READ16;
	msg->timestamp *= 1000;	// DK1 timestamps are in milliseconds
	msg->last_command_id = READ16;
	msg->temperature = READ16;

	msg->num_samples = OHMD_MIN(msg->num_samples, 3);
	for (int i = 0; i < msg->num_samples; i++) {
		decode_sample(buffer, msg->samples[i].accel);
		buffer += 8;

		decode_sample(buffer, msg->samples[i].gyro);
		buffer += 8;
	}

	// Skip empty samples
	buffer += (3 - msg->num_samples) * 16;
	for (int i = 0; i < 3; i++) {
		msg->mag[i] = READ16;
	}

	return 1;
}

static int decode_tracker_sensor_msg_dk2(pkt_tracker_sensor * msg,
					 const unsigned char *buffer, int size)
{
	if (!(size == 64)) {
		LOGE("invalid packet size (expected 62 or 64 but got %d)",
		     size);
		return 0;
	}

	SKIP_CMD;
	msg->last_command_id = READ16;
	msg->num_samples = READ8;
	/* Next is the number of samples since start, excluding the samples
	   contained in this packet */
	buffer += 2;		// unused: nb_samples_since_start
	msg->temperature = READ16;
	msg->timestamp = READ32;
	LOGI("timestamp %08X", msg->timestamp);
	/* Second sample value is junk (outdated/uninitialized) value if
	   num_samples < 2. */
	LOGI("samples %02X", msg->num_samples);
	msg->num_samples = OHMD_MIN(msg->num_samples, 2);
	for (int i = 0; i < msg->num_samples; i++) {
		decode_sample(buffer, msg->samples[i].accel);
		buffer += 8;

		decode_sample(buffer, msg->samples[i].gyro);
		buffer += 8;
	}

	// Skip empty samples
	buffer += (2 - msg->num_samples) * 16;

	for (int i = 0; i < 3; i++) {
		msg->mag[i] = READ16;
	}

	// TODO: positional tracking data and frame data

	return 1;
}

// TODO do we need to consider HMD vs sensor "centric" values
static void vec3f_from_rift_vec(const int32_t * smp, vec3f * out_vec)
{
	out_vec->x = (float)smp[0] * 0.0001f;
	out_vec->y = (float)smp[1] * 0.0001f;
	out_vec->z = (float)smp[2] * 0.0001f;
}

static void handle_tracker_sensor_msg(HMDHidInfo * info, unsigned char *buffer,
				      int size)
{
	if (buffer[0] == RIFT_IRQ_SENSORS
	    && !decode_tracker_sensor_msg(&info->sensor, buffer, size)) {
		LOGE("couldn't decode tracker sensor message");
	}

	if (buffer[0] == RIFT_IRQ_SENSORS_DK2
	    && !decode_tracker_sensor_msg_dk2(&info->sensor, buffer, size)) {
		LOGE("couldn't decode tracker sensor message");
	}

	pkt_tracker_sensor *s = &info->sensor;

	dump_packet_tracker_sensor(s);

	int32_t mag32[] = { s->mag[0], s->mag[1], s->mag[2] };
	vec3f_from_rift_vec(mag32, &info->raw_mag);

	// TODO: handle overflows in a nicer way
	float dt = TICK_LEN;	// TODO: query the Rift for the sample rate
	if (s->timestamp > info->last_imu_timestamp) {
		dt = (s->timestamp - info->last_imu_timestamp) / 1000000.0f;
		dt -= (s->num_samples - 1) * TICK_LEN;	// TODO: query the Rift for the sample rate
	}

	for (int i = 0; i < s->num_samples; i++) {
		vec3f_from_rift_vec(s->samples[i].accel, &info->raw_accel);
		vec3f_from_rift_vec(s->samples[i].gyro, &info->raw_gyro);

//              ofusion_update(&info->sensor_fusion, dt, &info->raw_gyro, &info->raw_accel, &info->raw_mag);
//              LOGI("raw_gyro = %f, %f, %f\nraw_accel = %f, %f, %f\nraw_mag = %f, %f, %f\n\n",
//                      info->raw_gyro.x,  info->raw_gyro.y,  info->raw_gyro.z,
//                      info->raw_accel.x, info->raw_accel.y, info->raw_accel.z,
//                      info->raw_mag.x,   info->raw_mag.y,   info->raw_mag.z);
		dt = TICK_LEN;	// TODO: query the Rift for the sample rate
	}

	info->last_imu_timestamp = s->timestamp;
}

static int get_feature_report(HMDHidInfo * info, char cmd, unsigned char *buf)
{
	memset(buf, 0, FEATURE_BUFFER_SIZE);
	buf[0] = (unsigned char)cmd;
	return hid_get_feature_report(info->handle, buf, FEATURE_BUFFER_SIZE);
}

static int send_feature_report(HMDHidInfo * info, const unsigned char *data,
			       size_t length)
{
	return hid_send_feature_report(info->handle, data, length);
}

static double HID_get_tick()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1.0 + (double)now.tv_nsec / 1000000000.0;
}

int HID_Init(HMDHidInfo * info)
{
	unsigned char buffer[FEATURE_BUFFER_SIZE];
	wchar_t wstr[MAX_STR];

	int res = hid_init();

	info->handle = hid_open(0x0483, 0x0021, NULL);

	res = hid_get_manufacturer_string(info->handle, wstr, MAX_STR);
	printf("Manufacturer String: %ls\n", wstr);

	res = hid_get_product_string(info->handle, wstr, MAX_STR);
	printf("Product String: %ls\n", wstr);

	res = hid_get_serial_number_string(info->handle, wstr, MAX_STR);
	printf("Serial Number String: (%d) %ls\n", wstr[0], wstr);

	hid_set_nonblocking(info->handle, 1);

	int size;

#if 0
	// Read and decode the sensor range
	size = get_feature_report(info, RIFT_CMD_RANGE, buffer);
	if (size) {
		decode_sensor_range(&info->sensor_range, buffer, size);
		dump_packet_sensor_range(&info->sensor_range);
	}

	// Read and decode display information
	size = get_feature_report(info, RIFT_CMD_DISPLAY_INFO, buffer);
	if (size) {
		decode_sensor_display_info(&info->display_info, buffer, size);
		dump_packet_sensor_display_info(&info->display_info);
	}
#endif

	// Read and decode the sensor config
	// 33
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	// 35
	size = encode_sensor_config(buffer, &info->sensor_config);
	if (send_feature_report(info, buffer, size) == -1) {
		LOGE("error setting up cmd17");
	}

	// 37
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	// 39
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	// 41
	size = encode_sensor_config(buffer, &info->sensor_config);
	if (send_feature_report(info, buffer, size) == -1) {
		LOGE("error setting up cmd17");
	}

	// 43
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	// 45
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	info->sensor_config.packet_interval = 1;
	// 47
	size = encode_sensor_config(buffer, &info->sensor_config);
	if (send_feature_report(info, buffer, size) == -1) {
		LOGE("error setting up cmd17");
	}

	// 49
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	// 51
	size = encode_sensor_config(buffer, &info->sensor_config);
	if (send_feature_report(info, buffer, size) == -1) {
		LOGE("error setting up cmd17");
	}

	// 53
	size = get_feature_report(info, RIFT_CMD_SENSOR_CONFIG, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_config(&info->sensor_config, buffer, size);
		dump_packet_sensor_config(&info->sensor_config);
	}

	size = get_feature_report(info, RIFT_CMD_RANGE, buffer);
	if (size) {
		DUMP(buffer, size);
		decode_sensor_range(&info->sensor_range, buffer, size);
		dump_packet_sensor_range(&info->sensor_range);
	}

	// 55
	size = get_feature_report(info, 240, buffer);
	if (size) {
		DUMP(buffer, size);
//      decode_sensor_config(&info->sensor_config, buffer, size);
//      dump_packet_sensor_config(&info->sensor_config);
	}

	// 57
	size = encode_pimax_cmd_17(buffer);
	if (send_feature_report(info, buffer, size) == -1) {
		LOGE("error setting up cmd17");
	}

	info->last_keep_alive = HID_get_tick();

	return 0;
}

int HID_Close(HMDHidInfo * info)
{
	hid_close(info->handle);
	return hid_exit();
}

void HID_Read(HMDHidInfo * info)
{
	unsigned char buffer[FEATURE_BUFFER_SIZE];

//#if 1
	// Handle keep alive messages
	double t = HID_get_tick();
//LOGI("TIME: %f", t);

	if (t - info->last_keep_alive >=
	    (double)info->sensor_config.keep_alive_interval / 1000.0 - .2) {
		int size = encode_pimax_cmd_17(buffer);
		if (send_feature_report(info, buffer, size) == -1) {
			LOGE("error setting up cmd17");
		}

		LOGI("OK1");

		// Update the time of the last keep alive we have sent.
		info->last_keep_alive = t;
	}
//#endif

	// Read all the messages from the device.
//      while(1) {
//LOGI("READSTART");
	int size =
	    hid_read_timeout(info->handle, buffer, FEATURE_BUFFER_SIZE, 100000);
//LOGI("READEND");
	if (size < 0) {
		LOGE("error reading from device");
		return;
	} else if (size == 0) {

		LOGI("No data!");
//    size = encode_pimax_cmd_17(buffer);
//    if (send_feature_report(info, buffer, size) == -1) {
//      LOGE("error setting up cmd17");
//    }

		return;		// No more messages, return.
	}

	//LOGI("OK2 %d", size);

	DUMP(buffer, size);

#if 1
	// currently the only message type the hardware supports (I think)
	if (buffer[0] == RIFT_IRQ_SENSORS || buffer[0] == RIFT_IRQ_SENSORS_DK2) {
		handle_tracker_sensor_msg(info, buffer, size);
	} else {
		LOGE("unknown message type: %u", buffer[0]);
	}
#endif
//      }
}

int main(int argc, char *argv[])
{
	HMDHidInfo info;

	HID_Init(&info);

	while (1) {
		HID_Read(&info);
		usleep(1000);
	}

	HID_Close(&info);

	return 0;
}
