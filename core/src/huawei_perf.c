// SPDX-License-Identifier: GPL-2.0
/*
 * huawei-perf - talk to the Huawei HWMI ACPI-WMI interface through the
 * debugfs escape hatch exposed by the in-tree huawei-wmi driver.
 *
 * The in-tree driver only implements battery thresholds and fn-lock.  The
 * firmware method behind GUID ABBC0F5B-8EA1-11D1-A000-C90629100000 also
 * implements the vendor "power unlock" / performance-mode switch used by
 * Huawei PC Manager on Windows:
 *
 *   GET 0x00000E04  -> status=0, payload[1] == 1 when unlocked
 *   SET 0x00000F04  -> argument in byte 2 (value << 16)
 *
 * The x64 `arg` debugfs node takes the raw 64-bit argument and reading `call`
 * evaluates the HWMI method once with it.  The driver retries the call twice
 * internally for normal sysfs users; debugfs does not, so we retry here.
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEBUGFS_DIR "/sys/kernel/debug/huawei-wmi"
#define ARG_PATH DEBUGFS_DIR "/arg"
#define CALL_PATH DEBUGFS_DIR "/call"

#define CMD_POWER_GET 0x00000E04ULL
#define CMD_POWER_SET 0x00000F04ULL
#define CMD_FAN_GET   0x00000802ULL
#define CMD_TEMP_GET  0x00000202ULL

#define MAX_GROUPS 4
#define MAX_GROUP_BYTES 260
#define WMI_PAYLOAD 256

#define RAPL_MSR  "/sys/class/powercap/intel-rapl:0"
#define RAPL_MMIO "/sys/class/powercap/intel-rapl-mmio:0"

struct wmi_reply {
	uint8_t payload[WMI_PAYLOAD];
	uint8_t status;
};

static int read_text(const char *path, char *buf, size_t size)
{
	int fd = open(path, O_RDONLY);
	ssize_t n, off = 0;

	if (fd < 0)
		return -1;
	while ((size_t)off < size - 1) {
		n = read(fd, buf + off, size - 1 - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return -1;
		}
		if (n == 0)
			break;
		off += n;
	}
	buf[off] = '\0';
	close(fd);
	return (int)off;
}

static int write_text(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	size_t len = strlen(text);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, text, len);
	close(fd);
	return n == (ssize_t)len ? 0 : -1;
}

static int parse_groups(const char *text, uint8_t groups[][MAX_GROUP_BYTES],
			int *group_len)
{
	const char *p = text;
	int ng = 0;

	while ((p = strchr(p, '{')) != NULL) {
		int n = 0;

		if (ng >= MAX_GROUPS)
			return -1;
		p++;
		while (*p && *p != '}') {
			char *end;
			unsigned long v;

			while (*p == ',' || isspace((unsigned char)*p))
				p++;
			if (*p == '}')
				break;
			if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X'))
				return -1;
			errno = 0;
			v = strtoul(p, &end, 16);
			if (errno || end == p || v > 0xff)
				return -1;
			if (n >= MAX_GROUP_BYTES)
				return -1;
			groups[ng][n++] = (uint8_t)v;
			p = end;
		}
		group_len[ng++] = n;
		if (*p == '}')
			p++;
	}
	return ng;
}

static int select_payload(uint8_t groups[][MAX_GROUP_BYTES], int *group_len,
			  int ng, uint8_t **payload, size_t *payload_len)
{
	/* Package of {4-byte prefix, 256-byte buffer}. */
	if (ng == 2 && group_len[1] == WMI_PAYLOAD) {
		*payload = groups[1];
		*payload_len = WMI_PAYLOAD;
		return 0;
	}
	/* Single 0x104 buffer: first four bytes are the ignored prefix. */
	if (ng == 1 && group_len[0] == WMI_PAYLOAD + 4) {
		*payload = groups[0] + 4;
		*payload_len = WMI_PAYLOAD;
		return 0;
	}
	if (ng == 1 && group_len[0] == WMI_PAYLOAD) {
		*payload = groups[0];
		*payload_len = WMI_PAYLOAD;
		return 0;
	}
	/* Be tolerant of a package whose first element is the payload. */
	if (ng >= 1 && group_len[ng - 1] == WMI_PAYLOAD) {
		*payload = groups[ng - 1];
		*payload_len = WMI_PAYLOAD;
		return 0;
	}
	return -1;
}

static int wmi_call_once(uint64_t arg, struct wmi_reply *reply)
{
	uint8_t groups[MAX_GROUPS][MAX_GROUP_BYTES];
	int group_len[MAX_GROUPS];
	char text[8192];
	char cmd[32];
	int ng;
	uint8_t *payload = NULL;
	size_t payload_len = 0;

	snprintf(cmd, sizeof(cmd), "0x%llx", (unsigned long long)arg);
	if (write_text(ARG_PATH, cmd) < 0)
		return -errno;

	if (read_text(CALL_PATH, text, sizeof(text)) < 0)
		return -errno;

	ng = parse_groups(text, groups, group_len);
	if (ng <= 0)
		return -EPROTO;

	if (select_payload(groups, group_len, ng, &payload, &payload_len) < 0)
		return -EPROTO;
	if (payload_len < 2)
		return -EPROTO;

	memcpy(reply->payload, payload, WMI_PAYLOAD);
	reply->status = reply->payload[0];
	return 0;
}

static int wmi_call(uint64_t arg, struct wmi_reply *reply, int attempts)
{
	int err = -EIO;
	int i;

	memset(reply, 0, sizeof(*reply));
	for (i = 0; i < attempts; i++) {
		err = wmi_call_once(arg, reply);
		if (err == 0 && reply->status == 0)
			return 0;
		usleep(50000);
	}
	if (err)
		return err;
	return -EIO;
}

static int wmi_simple(uint64_t arg, uint8_t *value, struct wmi_reply *reply)
{
	int err = wmi_call(arg, reply, 3);

	if (err)
		return err;
	if (value)
		*value = reply->payload[1];
	return 0;
}

static int power_unlock_get(int *on, struct wmi_reply *reply)
{
	uint8_t value = 0;
	int err = wmi_simple(CMD_POWER_GET, &value, reply);

	if (err)
		return err;
	*on = value == 1;
	return 0;
}

static int power_unlock_set(int on, struct wmi_reply *reply)
{
	uint64_t arg = CMD_POWER_SET | ((uint64_t)(on ? 1 : 0) << 16);

	return wmi_call(arg, reply, 3);
}

static int read_u64(const char *path, unsigned long long *value)
{
	char buf[64];
	char *end;

	if (read_text(path, buf, sizeof(buf)) < 0)
		return -1;
	errno = 0;
	*value = strtoull(buf, &end, 10);
	if (errno || end == buf)
		return -1;
	return 0;
}

static int write_u64(const char *path, unsigned long long value)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%llu", value);
	return write_text(path, buf);
}

static void print_rapl(const char *label, const char *dir)
{
	char path[512];
	unsigned long long pl1, pl2;

	snprintf(path, sizeof(path), "%s/constraint_0_power_limit_uw", dir);
	if (read_u64(path, &pl1) < 0) {
		printf("%-10s unavailable\n", label);
		return;
	}
	snprintf(path, sizeof(path), "%s/constraint_1_power_limit_uw", dir);
	if (read_u64(path, &pl2) < 0) {
		printf("%-10s PL1=%8.3f W  PL2=unavailable\n", label,
		       pl1 / 1000000.0);
		return;
	}
	printf("%-10s PL1=%8.3f W  PL2=%8.3f W\n", label,
	       pl1 / 1000000.0, pl2 / 1000000.0);
}

static void print_cpu_state(void)
{
	char buf[128];
	const char *paths[] = {
		"/sys/devices/system/cpu/cpufreq/policy0/scaling_governor",
		"/sys/devices/system/cpu/cpufreq/policy0/energy_performance_preference",
	};
	const char *labels[] = { "Governor", "EPP     " };
	size_t i;

	for (i = 0; i < 2; i++) {
		if (read_text(paths[i], buf, sizeof(buf)) >= 0) {
			buf[strcspn(buf, "\n")] = '\0';
			printf("%s: %s\n", labels[i], buf);
		}
	}
}

static int cmd_status(void)
{
	struct wmi_reply reply;
	int on = -1;
	int err;

	printf("== Huawei performance status ==\n");
	err = power_unlock_get(&on, &reply);
	if (err) {
		printf("Power unlock : ERROR (%s)\n", strerror(-err));
	} else {
		printf("Power unlock : %s (WMI status=%u value=%u)\n",
		       on ? "ON / performance" : "OFF / balanced",
		       reply.status, reply.payload[1]);
	}
	print_cpu_state();
	print_rapl("RAPL MSR ", RAPL_MSR);
	print_rapl("RAPL MMIO", RAPL_MMIO);

	{
		uint8_t v = 0;
		if (wmi_simple(CMD_FAN_GET | (0ULL << 16), &v, &reply) == 0)
			printf("Fan 0    : %u RPM\n",
			       reply.payload[1] | (reply.payload[2] << 8));
		else
			printf("Fan 0    : unsupported\n");
	}
	{
		uint8_t v = 0;
		if (wmi_simple(CMD_TEMP_GET | (0ULL << 16), &v, &reply) == 0)
			printf("CPU temp : %u C\n", reply.payload[2]);
		else
			printf("CPU temp : unsupported\n");
	}
	return 0;
}

static int cmd_unlock(int argc, char **argv)
{
	struct wmi_reply reply;
	int on = 0, want;
	int err;

	if (argc != 1) {
		fprintf(stderr, "usage: huawei-perf unlock on|off|toggle|get\n");
		return 2;
	}

	if (!strcmp(argv[0], "get")) {
		err = power_unlock_get(&on, &reply);
		if (err) {
			fprintf(stderr, "读取失败：%s\n", strerror(-err));
			return 1;
		}
		printf("%d\n", on);
		return 0;
	}

	err = power_unlock_get(&on, &reply);
	if (err) {
		fprintf(stderr, "读取当前状态失败：%s\n", strerror(-err));
		return 1;
	}

	if (!strcmp(argv[0], "on"))
		want = 1;
	else if (!strcmp(argv[0], "off"))
		want = 0;
	else if (!strcmp(argv[0], "toggle"))
		want = !on;
	else {
		fprintf(stderr, "usage: huawei-perf unlock on|off|toggle|get\n");
		return 2;
	}

	if (!strcmp(argv[0], "on") && on) {
		printf("已经是性能模式（无需重复写入）\n");
		return 0;
	}
	if (!strcmp(argv[0], "off") && !on) {
		printf("已经是均衡模式（无需重复写入）\n");
		return 0;
	}

	printf("发送 HWMI 0x%llx（power_unlock=%d）...\n",
	       (unsigned long long)(CMD_POWER_SET | ((uint64_t)want << 16)), want);
	err = power_unlock_set(want, &reply);
	if (err) {
		fprintf(stderr, "写入失败：%s（WMI status=%u）\n",
			strerror(-err), reply.status);
		return 1;
	}
	usleep(150000);

	err = power_unlock_get(&on, &reply);
	if (err) {
		fprintf(stderr, "写入后回读失败：%s\n", strerror(-err));
		return 1;
	}
	printf("回读：power_unlock=%d（%s）\n", on,
	       on ? "性能模式" : "均衡模式");
	return on == want ? 0 : 1;
}

static int cmd_raw(int argc, char **argv)
{
	struct wmi_reply reply;
	unsigned long long arg;
	char *end;
	int err;
	int i;

	if (argc != 1) {
		fprintf(stderr, "usage: huawei-perf raw <hex-u64>\n");
		return 2;
	}
	errno = 0;
	arg = strtoull(argv[0], &end, 16);
	if (errno || end == argv[0]) {
		fprintf(stderr, "无效的 64 位十六进制参数：%s\n", argv[0]);
		return 2;
	}
	err = wmi_call(arg, &reply, 3);
	if (err) {
		fprintf(stderr, "调用失败：%s\n", strerror(-err));
		return 1;
	}
	printf("status=%u\n", reply.status);
	for (i = 0; i < 16; i++)
		printf("%02x%c", reply.payload[i], i == 15 ? '\n' : ' ');
	return 0;
}

static int cmd_fan(int argc, char **argv)
{
	struct wmi_reply reply;
	unsigned long idx = 0;
	uint8_t v = 0;
	int err;

	if (argc > 1) {
		fprintf(stderr, "usage: huawei-perf fan [index]\n");
		return 2;
	}
	if (argc == 1)
		idx = strtoul(argv[0], NULL, 0);
	err = wmi_simple(CMD_FAN_GET | ((idx & 0xff) << 16), &v, &reply);
	if (err) {
		fprintf(stderr, "风扇读取失败：%s\n", strerror(-err));
		return 1;
	}
	printf("%u\n", reply.payload[1] | (reply.payload[2] << 8));
	return 0;
}

static int cmd_temp(int argc, char **argv)
{
	struct wmi_reply reply;
	unsigned long idx = 0;
	uint8_t v = 0;
	int err;

	if (argc > 1) {
		fprintf(stderr, "usage: huawei-perf temp [zone]\n");
		return 2;
	}
	if (argc == 1)
		idx = strtoul(argv[0], NULL, 0);
	err = wmi_simple(CMD_TEMP_GET | ((idx & 0xff) << 16), &v, &reply);
	if (err) {
		fprintf(stderr, "温度读取失败：%s\n", strerror(-err));
		return 1;
	}
	printf("%u\n", reply.payload[2]);
	return 0;
}

static int read_pkg_temp(void)
{
	char path[128];
	char type[64];
	char buf[64];
	char *end;
	int i;

	for (i = 0; i < 32; i++) {
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", i);
		if (read_text(path, type, sizeof(type)) < 0)
			continue;
		type[strcspn(type, "\n")] = '\0';
		if (strcmp(type, "x86_pkg_temp") && strcmp(type, "TCPU"))
			continue;
		snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/temp", i);
		if (read_text(path, buf, sizeof(buf)) < 0)
			return -1;
		errno = 0;
		long value = strtol(buf, &end, 10);
		if (errno || end == buf)
			return -1;
		return (int)(value / 1000);
	}
	return -1;
}

static int cmd_fan_health(int argc, char **argv)
{
	struct wmi_reply reply;
	struct timespec start, now;
	int seconds = 10;
	int samples = 0, fan_samples = 0;
	int fan_min = INT_MAX, fan_max = 0;
	long long fan_sum = 0;
	int ct_min = INT_MAX, ct_max = 0;
	int syst_min = INT_MAX, syst_max = 0;
	int pkg_min = INT_MAX, pkg_max = 0;
	unsigned long long pl1 = 0;
	double elapsed;
	const char *verdict;

	if (argc > 1) {
		fprintf(stderr, "usage: huawei-perf fan-health [1..120]\n");
		return 2;
	}
	if (argc == 1)
		seconds = atoi(argv[0]);
	if (seconds < 1 || seconds > 120) {
		fprintf(stderr, "usage: huawei-perf fan-health [1..120]\n");
		return 2;
	}
	if (access(ARG_PATH, W_OK) < 0) {
		fprintf(stderr, "fan-health 需要 root：无法访问 %s（%s）\n",
			ARG_PATH, strerror(errno));
		return 1;
	}

	clock_gettime(CLOCK_MONOTONIC, &start);
	do {
		uint8_t v = 0;
		int fan = -1, ct = -1, syst = -1, pkg;

		if (wmi_simple(CMD_FAN_GET, &v, &reply) == 0)
			fan = reply.payload[1] | (reply.payload[2] << 8);
		if (wmi_simple(CMD_TEMP_GET, &v, &reply) == 0)
			ct = reply.payload[2];
		if (wmi_simple(CMD_TEMP_GET | (5ULL << 16), &v, &reply) == 0)
			syst = reply.payload[2];
		pkg = read_pkg_temp();

		if (fan >= 0) {
			if (fan < fan_min)
				fan_min = fan;
			if (fan > fan_max)
				fan_max = fan;
			fan_sum += fan;
			fan_samples++;
		}
		if (ct >= 0) {
			if (ct < ct_min)
				ct_min = ct;
			if (ct > ct_max)
				ct_max = ct;
		}
		if (syst >= 0) {
			if (syst < syst_min)
				syst_min = syst;
			if (syst > syst_max)
				syst_max = syst;
		}
		if (pkg >= 0) {
			if (pkg < pkg_min)
				pkg_min = pkg;
			if (pkg > pkg_max)
				pkg_max = pkg;
		}
		samples++;

		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) +
			  (now.tv_nsec - start.tv_nsec) / 1e9;
		if (elapsed >= seconds)
			break;
		usleep(500000);
	} while (1);

	if (read_u64(RAPL_MMIO "/constraint_0_power_limit_uw", &pl1) == 0)
		printf("MMIO PL1 : %.3f W\n", pl1 / 1000000.0);
	printf("samples  : %d over %.1f s\n", samples, elapsed);
	if (fan_samples)
		printf("fan RPM  : min %d  max %d  avg %.0f\n", fan_min, fan_max,
		       (double)fan_sum / fan_samples);
	else
		printf("fan RPM  : no data (WMI fan command failed)\n");
	if (ct_min != INT_MAX)
		printf("CTMP     : min %d  max %d C\n", ct_min, ct_max);
	if (syst_min != INT_MAX)
		printf("SYST     : min %d  max %d C\n", syst_min, syst_max);
	if (pkg_min != INT_MAX)
		printf("package  : min %d  max %d C\n", pkg_min, pkg_max);

	if (fan_samples == 0)
		verdict = "NO_TACH";
	else if (fan_max == 0 && ct_max >= 55)
		verdict = "STALLED";
	else if (ct_max - ct_min >= 10 && fan_max - fan_min < 100)
		verdict = "NO_RESPONSE";
	else
		verdict = "OK";
	printf("verdict  : %s\n", verdict);

	if (pl1 && pl1 <= 12000000 && (ct_max - ct_min) < 5)
		printf("hint     : EC is limiting package power (PL1 %.1f W); "
		       "the fan has little heat to react to.\n",
		       pl1 / 1000000.0);

	return (strcmp(verdict, "STALLED") == 0) ? 1 : 0;
}

static int read_energy(const char *dir, unsigned long long *value)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/energy_uj", dir);
	return read_u64(path, value);
}

static int cmd_power(int argc, char **argv)
{
	const char *labels[] = { "MSR ", "MMIO" };
	const char *dirs[] = { RAPL_MSR, RAPL_MMIO };
	unsigned long long a[2], b[2];
	int seconds = 5;
	int i;

	if (argc == 1)
		seconds = atoi(argv[0]);
	if (seconds <= 0 || seconds > 60) {
		fprintf(stderr, "usage: huawei-perf power [1..60]\n");
		return 2;
	}

	for (i = 0; i < 2; i++) {
		if (read_energy(dirs[i], &a[i]) < 0) {
			fprintf(stderr, "读取 %s/energy_uj 失败：%s\n",
				dirs[i], strerror(errno));
			return 1;
		}
	}
	sleep(seconds);
	for (i = 0; i < 2; i++) {
		if (read_energy(dirs[i], &b[i]) < 0) {
			fprintf(stderr, "读取 %s/energy_uj 失败：%s\n",
				dirs[i], strerror(errno));
			return 1;
		}
		printf("%s package power: %.2f W\n", labels[i],
		       (b[i] - a[i]) / 1000000.0 / seconds);
	}
	return 0;
}

static int cmd_rapl(int argc, char **argv)
{
	unsigned long long pl1 = 0, pl2 = 0;
	int have_pl1 = 0, have_pl2 = 0, apply = 0;
	int i;

	for (i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "--apply")) {
			apply = 1;
		} else if (!strcmp(argv[i], "--pl1") && i + 1 < argc) {
			pl1 = strtoull(argv[++i], NULL, 10) * 1000000ULL;
			have_pl1 = 1;
		} else if (!strcmp(argv[i], "--pl2") && i + 1 < argc) {
			pl2 = strtoull(argv[++i], NULL, 10) * 1000000ULL;
			have_pl2 = 1;
		} else {
			fprintf(stderr, "usage: huawei-perf rapl [--pl1 W] [--pl2 W] [--apply]\n");
			return 2;
		}
	}

	printf("before:\n");
	print_rapl("MSR ", RAPL_MSR);
	print_rapl("MMIO", RAPL_MMIO);

	if (!apply)
		return 0;
	if (!have_pl1 && !have_pl2) {
		fprintf(stderr, "--apply 需要至少一个 --pl1/--pl2\n");
		return 2;
	}

	if (have_pl2) {
		if (write_u64(RAPL_MSR "/constraint_1_power_limit_uw", pl2) < 0)
			fprintf(stderr, "MSR PL2 写入失败：%s\n", strerror(errno));
		if (write_u64(RAPL_MMIO "/constraint_1_power_limit_uw", pl2) < 0)
			fprintf(stderr, "MMIO PL2 写入失败：%s\n", strerror(errno));
	}
	if (have_pl1) {
		if (write_u64(RAPL_MSR "/constraint_0_power_limit_uw", pl1) < 0)
			fprintf(stderr, "MSR PL1 写入失败：%s\n", strerror(errno));
		if (write_u64(RAPL_MMIO "/constraint_0_power_limit_uw", pl1) < 0)
			fprintf(stderr, "MMIO PL1 写入失败：%s\n", strerror(errno));
	}

	printf("after:\n");
	print_rapl("MSR ", RAPL_MSR);
	print_rapl("MMIO", RAPL_MMIO);
	return 0;
}

static int append_hex_list(char *out, size_t outsz, size_t *pos,
			   const uint8_t *bytes, size_t len, int *first)
{
	size_t i;

	for (i = 0; i < len; i++) {
		int n = snprintf(out + *pos, outsz - *pos, "%s0x%02x",
				 *first ? "" : ",", bytes[i]);

		if (n < 0 || (size_t)n >= outsz - *pos)
			return -1;
		*pos += n;
		*first = 0;
	}
	return 0;
}

static int build_reply(char *out, size_t outsz, const uint8_t *payload,
		       int as_package, int with_prefix)
{
	static const uint8_t prefix[4] = { 0xde, 0xad, 0xbe, 0xef };
	size_t pos = 0;
	int first = 1;
	int n;

	if (as_package) {
		n = snprintf(out, outsz, "[{0x00,0x00,0x00,0x00},{");
		if (n < 0 || (size_t)n >= outsz)
			return -1;
		pos = n;
		if (append_hex_list(out, outsz, &pos, payload, WMI_PAYLOAD, &first))
			return -1;
		n = snprintf(out + pos, outsz - pos, "}]");
	} else {
		n = snprintf(out, outsz, "{");
		if (n < 0 || (size_t)n >= outsz)
			return -1;
		pos = n;
		if (with_prefix &&
		    append_hex_list(out, outsz, &pos, prefix, sizeof(prefix), &first))
			return -1;
		if (append_hex_list(out, outsz, &pos, payload, WMI_PAYLOAD, &first))
			return -1;
		n = snprintf(out + pos, outsz - pos, "}");
	}
	if (n < 0 || (size_t)n >= outsz - pos)
		return -1;
	return 0;
}

static int selftest_one(const uint8_t *payload, int as_package,
			int with_prefix, uint8_t expect)
{
	char text[4096];
	uint8_t groups[MAX_GROUPS][MAX_GROUP_BYTES];
	int group_len[MAX_GROUPS];
	uint8_t *selected = NULL;
	size_t selected_len = 0;
	int ng;

	if (build_reply(text, sizeof(text), payload, as_package, with_prefix))
		return -1;
	ng = parse_groups(text, groups, group_len);
	if (ng <= 0)
		return -1;
	if (select_payload(groups, group_len, ng, &selected, &selected_len))
		return -1;
	if (selected_len != WMI_PAYLOAD || selected[1] != expect)
		return -1;
	return 0;
}

static int cmd_selftest(void)
{
	uint8_t payload[WMI_PAYLOAD] = { 0 };
	uint8_t groups[MAX_GROUPS][MAX_GROUP_BYTES];
	int group_len[MAX_GROUPS];
	int fails = 0;

	payload[1] = 1;
	if (selftest_one(payload, 1, 0, 1)) {
		fprintf(stderr, "selftest: package reply failed\n");
		fails++;
	}

	payload[1] = 0;
	if (selftest_one(payload, 0, 1, 0)) {
		fprintf(stderr, "selftest: 0x104 buffer reply failed\n");
		fails++;
	}
	if (selftest_one(payload, 0, 0, 0)) {
		fprintf(stderr, "selftest: 0x100 buffer reply failed\n");
		fails++;
	}
	if (parse_groups("not a reply", groups, group_len) > 0) {
		fprintf(stderr, "selftest: malformed reply accepted\n");
		fails++;
	}
	if ((CMD_POWER_SET | (1ULL << 16)) != 0x00010f04ULL) {
		fprintf(stderr, "selftest: power unlock encoding failed\n");
		fails++;
	}
	if ((CMD_POWER_GET & 0xffff) != 0x0e04 ||
	    (CMD_POWER_SET & 0xffff) != 0x0f04) {
		fprintf(stderr, "selftest: command constants failed\n");
		fails++;
	}

	if (fails)
		return 1;
	printf("selftest: PASS\n");
	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"用法：\n"
		"  %s status                 查看 power_unlock、RAPL、风扇/温度\n"
		"  %s unlock on|off|toggle   切换华为性能模式\n"
		"  %s unlock get             只输出 0/1（脚本用）\n"
		"  %s raw <hex-u64>          直接调用 HWMI 方法\n"
		"  %s fan [index]            读风扇转速\n"
		"  %s temp [zone]            读温度\n"
		"  %s power [seconds]        读 RAPL 平均功耗\n"
		"  %s rapl [--pl1 W] [--pl2 W] [--apply]\n"
		"                           查看/写入 RAPL 功耗墙\n"
		"  %s fan-health [seconds]  采样风扇/温度并判断风扇是否响应\n"
		"  %s selftest               运行解析器自检（CI 用）\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		usage(argv[0]);
		return 2;
	}
	if (!strcmp(argv[1], "status"))
		return cmd_status();
	if (!strcmp(argv[1], "unlock"))
		return cmd_unlock(argc - 2, argv + 2);
	if (!strcmp(argv[1], "raw"))
		return cmd_raw(argc - 2, argv + 2);
	if (!strcmp(argv[1], "fan"))
		return cmd_fan(argc - 2, argv + 2);
	if (!strcmp(argv[1], "temp"))
		return cmd_temp(argc - 2, argv + 2);
	if (!strcmp(argv[1], "power"))
		return cmd_power(argc - 2, argv + 2);
	if (!strcmp(argv[1], "rapl"))
		return cmd_rapl(argc - 2, argv + 2);
	if (!strcmp(argv[1], "fan-health"))
		return cmd_fan_health(argc - 2, argv + 2);
	if (!strcmp(argv[1], "selftest"))
		return cmd_selftest();
	if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage(argv[0]);
		return 0;
	}
	usage(argv[0]);
	return 2;
}
