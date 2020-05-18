// gcc -O2 -o 2in1screen 2in1screen.c

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>

int compute_state(double x, double y, double z)
{
	double ax = abs(x);
	double az = abs(z);

	int state = 0;
	if (ax < az) {
		state = z > 0 ? 0 : 1;
	} else {
		state = x > 0 ? 3 : 2;
	}

	return state;
}

FILE* bdopen(const char* base, char const *fname, char leave_open){
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", base, fname);
	FILE *fin = fopen(path, "r");
	if (fin != NULL)
		setvbuf(fin, NULL, _IONBF, 0);
	return fin;
}

void rotate_screen(int state, const char* xdevName)
{
	char *ROT[]   = {
		"normal",
		"inverted",
		"left",
		"right"
	};
	char *COOR[]  = {
		" 1  0  0  0  1  0  0  0  1",
		"-1  0  1  0 -1  1  0  0  1",
		" 0 -1  1  1  0  0  0  0  1",
		" 0  1  0 -1  0  1  0  0  1"
	};
#if 0
	char *TOUCH[] = {
		"enable",
		"disable",
		"disable",
		"disable"
	};
#endif
	char command[1024];
	snprintf(command, sizeof(command), "xrandr -o %s", ROT[state]);
	system(command);
	snprintf(command, sizeof(command), "xinput set-prop \"%s\" \"Coordinate Transformation Matrix\" %s", xdevName, COOR[state]);
	system(command);
}

double read_float(FILE* f)
{
	char content[256];
	fseek(f, 0, SEEK_SET);
	fgets(content, sizeof(content), f);
	return atof(content);
}

int main(int argc, char const *argv[])
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

	//const char* screenDevice = "Wacom HID 4846 Finger";
	const char* screenDevice = "Elan Touchscreen";

	pclose(pf);

	char *basedir_end = strrchr(basedir, '/');
	if (basedir_end)
		*basedir_end = '\0';

	fprintf(stderr, "Accelerometer: '%s'\n", basedir);

	FILE* dev_accel_y = bdopen(basedir, "in_accel_y_raw", 1);
	FILE* dev_accel_x = bdopen(basedir, "in_accel_x_raw", 1);
	FILE* dev_accel_z = bdopen(basedir, "in_accel_z_raw", 1);

	int displayState = 0;
	int lastState = displayState;
	time_t lastSet = time(0);

	while (1) {
		const double accel_x = read_float(dev_accel_x);
		const double accel_y = read_float(dev_accel_y);
		const double accel_z = read_float(dev_accel_z);

		const int state = compute_state(accel_x, accel_y, accel_z);
		if (state != lastState) {
			lastSet = time(0);
			lastState = state;
		}

		if (displayState != lastState && (time(0) - lastSet) > 1) {
			displayState = lastState;
			rotate_screen(displayState, screenDevice);
		}

		//fprintf(stderr, "r: %d / x: %.03f / y: %.03f / z: %.03f\n", lastState, accel_x, accel_y, accel_z);

		usleep(250 * 1000);
	}

	return 0;
}

