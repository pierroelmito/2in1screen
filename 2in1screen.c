
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include <string>
#include <vector>
#include <functional>
#include <algorithm>

// ----------------------------------------------------------------------------------------------------

FILE* bdopen(const std::string& base, char const *fname, char leave_open){
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", base.c_str(), fname);
	FILE *fin = fopen(path, "r");
	if (fin != NULL)
		setvbuf(fin, NULL, _IONBF, 0);
	return fin;
}

double read_float(FILE* f)
{
	char content[256];
	fseek(f, 0, SEEK_SET);
	fgets(content, sizeof(content), f);
	return atof(content);
}

std::string str_tolower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return std::tolower(c); }
	);
	return s;
}

std::vector<std::string> read_cmd_output(const std::string& cmd)
{
	std::vector<std::string> r;
	FILE *pf = popen(cmd.c_str(), "r");
	if (pf) {
		char line[256];
		while (fgets(line, sizeof(line), pf) != NULL) {
			r.push_back(line);
			r.back().resize(r.back().size() - 1);
		}
		pclose(pf);
	}
	return r;
}

// ----------------------------------------------------------------------------------------------------

struct optparam {
	int& idx;
	int argsLeft;
	const char** args;
};

struct opthandler {
	const std::string name;
	const int vcount {};
	std::function<void(const optparam&)> handler {};
};

bool parseOptions(int ac, char const *av[], const std::vector<opthandler>& options)
{
	for (int i = 1; i < ac; ++i) {
		const std::string current = av[i];
		auto itf = std::find_if(options.begin(), options.end(), [&] (const opthandler& oh) { return oh.name == current; } );
		if (itf == options.end())
			return false;
		++i;
		if (!itf->handler) {
			return false;
		}
		itf->handler({ i, ac - i, av + i });
		i += itf->vcount;
		i--;
	}
	return true;
}

// ----------------------------------------------------------------------------------------------------

int compute_state(bool allowInverted, double x, double y, double z)
{
	double c = 3.0;
	double ax = c * abs(x);
	double az = abs(z);

	int state = 0;
	if (ax < az) {
		if (allowInverted)
			state = z > 0 ? 0 : 1;
		else
			state = 0;
	} else {
		state = x > 0 ? 3 : 2;
	}

	return state;
}

void rotate_screen(int state, const std::string& xdevName)
{
	const char* const ROT[]   = {
		"normal",
		"inverted",
		"left",
		"right",
	};
	const char* const COOR[]  = {
		" 1  0  0  0  1  0  0  0  1",
		"-1  0  1  0 -1  1  0  0  1",
		" 0 -1  1  1  0  0  0  0  1",
		" 0  1  0 -1  0  1  0  0  1",
	};
	char command[1024];
	snprintf(command, sizeof(command), "xrandr -o %s", ROT[state]);
	system(command);
	snprintf(command, sizeof(command), "xinput set-prop \"%s\" \"Coordinate Transformation Matrix\" %s", xdevName.c_str(), COOR[state]);
	system(command);
}

int main(int ac, char const *av[])
{
	const std::string defaultVal = "auto";

	std::string screenDevice = defaultVal;
	std::string accDir = defaultVal;
	std::string orientationCmd = "";
	bool allowInverted = true;
	bool verbose = true;

	using opr = const optparam&;
	const bool run = parseOptions(ac, av, {
		{ "--help" },
		{ "--touch-device", 1, [&] (opr p) { screenDevice = p.args[0]; } },
		{ "--accelerometer-dir", 1, [&] (opr p) { accDir = p.args[0]; } },
		{ "--orientation-cmd", 1, [&] (opr p) { orientationCmd = p.args[0]; } },
		{ "--no-inverted", 0, [&] (opr p) { allowInverted = false; } },
		{ "--quiet", 0, [&] (opr p) { verbose = false; } },
	});

	if (!run)
		return 3;

	// auto detect directory
	if (accDir == defaultVal)
	{
		FILE *pf = popen("ls /sys/bus/iio/devices/iio:device*/in_accel*", "r");
		if(!pf) {
			fprintf(stderr, "IO Error.\n");
			return 2;
		}
		char basedir[256];
		if (fgets(basedir, sizeof(basedir), pf) == NULL) {
			fprintf(stderr, "Unable to find any accelerometer.\n");
			return 1;
		}
		pclose(pf);
		char *basedir_end = strrchr(basedir, '/');
		if (basedir_end)
			*basedir_end = '\0';
		accDir = basedir;
	}

	// auto detect device
	if (screenDevice == defaultVal) {
		const std::vector<std::string> patterns = {
			"touchscreen",
			"wacom",
		};
		const std::vector<std::string> lines = read_cmd_output("xinput --list --name-only");
		for (const auto& l : lines) {
			const std::string lower = str_tolower(l);
			for (const auto& p : patterns) {
				if (lower.find(p) != std::string::npos) {
					screenDevice = l;
					break;
				}
			}
			if (screenDevice != defaultVal)
				break;
		}
	}

	if (verbose) {
		fprintf(stderr, "Accelerometer: '%s'\n", accDir.c_str());
		fprintf(stderr, "Touch device: '%s'\n", screenDevice.c_str());
		fprintf(stderr, "Orientation cmd: '%s'\n", orientationCmd.c_str());
		fprintf(stderr, "Allow inverted: '%s'\n", allowInverted ? "yes" : "no");
	}

	if (accDir == defaultVal || screenDevice == defaultVal) {
		fprintf(stderr, "Unable to detect accelerometer dir or touch device.\n");
		return 4;
	}

	FILE* dev_accel_y = bdopen(accDir, "in_accel_y_raw", 1);
	FILE* dev_accel_x = bdopen(accDir, "in_accel_x_raw", 1);
	FILE* dev_accel_z = bdopen(accDir, "in_accel_z_raw", 1);

	int displayState = 0;
	int lastState = displayState;
	int lastSetIndex = 0;
	int index = 0;

	while (1) {
		const double accel_x = read_float(dev_accel_x);
		const double accel_y = read_float(dev_accel_y);
		const double accel_z = read_float(dev_accel_z);

		const int state = compute_state(allowInverted, accel_x, accel_y, accel_z);
		if (state != lastState) {
			lastSetIndex = index;
			lastState = state;
		}

		if (displayState != lastState && (index - lastSetIndex) > 10) {
			// check orientation states...
			const bool v0 = displayState > 1;
			const bool v1 = lastState > 1;

			displayState = lastState;
			rotate_screen(displayState, screenDevice);

			// run orientation command if needed...
			if ((v0 ^ v1) && !orientationCmd.empty()) {
				system(orientationCmd.c_str());
			}
		}

		//fprintf(stderr, "r: %d / x: %.03f / y: %.03f / z: %.03f\n", lastState, accel_x, accel_y, accel_z);

		usleep(100 * 1000);
		++index;
	}

	return 0;
}

