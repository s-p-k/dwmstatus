#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <iwlib.h>
#include <mpd/client.h>
#include <X11/Xlib.h>

#define UPDATE_INTERVAL 2
#define CLOCK_FORMAT "\x01%a\x02%d\x01%b\x02%H:%M"
#define WIRED_DEVICE "enp4s0"
#define WIRELESS_DEVICE "wlan0"

char *get_time(char *buf, int bufsize, char *format)
{
	time_t tm;

	time(&tm);
	strftime(buf, bufsize, format, localtime(&tm));
	return buf;
}

float get_mem(void)
{
	FILE *f;
	float total, free, buffers, cached, mem;

	f = fopen("/proc/meminfo", "r");
	fscanf(f, "MemTotal: %f kB\nMemFree: %f kB\nBuffers: %f kB\nCached: %f kB\n",
		&total, &free, &buffers, &cached);
	fclose(f);
	mem = (total - free - buffers - cached) / total;
	return mem;
}

long get_total_jiffies()
{
	FILE *f;
	long j[7], total;

	f = fopen("/proc/stat", "r");
	fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
		&j[0], &j[1], &j[2], &j[3], &j[4], &j[5], &j[6]);
	fclose(f);
	total = j[0] + j[1] + j[2] + j[3] + j[4] + j[5] + j[6];
	return total;
}

long get_work_jiffies()
{
	FILE *f;
	long j[3], work;

	f = fopen("/proc/stat", "r");
	fscanf(f, "cpu %ld %ld %ld", &j[0], &j[1], &j[2]);
	fclose(f);
	work = j[0] + j[1] + j[2];
	return work;
}

float get_cpu(long total_jiffies, long work_jiffies)
{
	long total_jiffies_now, work_jiffies_now;
	long work_over_period, total_over_period;
	float cpu;
	
	total_jiffies_now = get_total_jiffies();
	work_jiffies_now = get_work_jiffies();
	work_over_period = work_jiffies_now - work_jiffies;
	total_over_period = total_jiffies_now - total_jiffies;
	if(total_over_period > 0)
		cpu = ((float)work_over_period / (float)total_over_period);
	else
		cpu = 0.0;
	return cpu;
}

int is_up(char *device)
{
	FILE *f;
	char fn[50], state[5];

	sprintf(fn, "/sys/class/net/%s/operstate", device);
	f = fopen(fn, "r");
	if(f != NULL) {
		fscanf(f, "%s", state);
		fclose(f);
		if(strcmp(state, "up") == 0)
			return 1;
	}
	return 0;
}

char *get_net(char *buf)
{
	int skfd;
	struct wireless_info *winfo;
	
	if(is_up(WIRED_DEVICE)) {
		strcpy(buf, "\x01 Eth\x02On");
		return buf;
	}
	else if(is_up(WIRELESS_DEVICE)) {
		winfo = malloc(sizeof(struct wireless_info));
		memset(winfo, 0, sizeof(struct wireless_info));

		skfd = iw_sockets_open();
		if (iw_get_basic_config(skfd, WIRELESS_DEVICE, &(winfo->b)) > -1) {
			if (winfo->b.has_essid && winfo->b.essid_on) {
				sprintf(buf, "\x01%s\x02%d", winfo->b.essid,
					(winfo->stats.qual.qual * 100) / winfo->range.max_qual.qual);
				return buf;
			}
		}
	}
	strcpy(buf, "\x01 Eth\x02No");
	return buf;
}

char *get_mpd(char *buf)
{
	const char *artist = NULL;
	const char *title = NULL;
	struct mpd_connection *conn;
	struct mpd_status *status;
	struct mpd_song *song;

	conn = mpd_connection_new(NULL, 0, 30000);
	if(mpd_connection_get_error(conn)) {
		strcpy(buf, "");
	}
	else {
	 	mpd_command_list_begin(conn, true);
		mpd_send_status(conn);
		mpd_send_current_song(conn);
		mpd_command_list_end(conn);
		status = mpd_recv_status(conn);
		if (status && mpd_status_get_state(status) != MPD_STATE_STOP) {
			mpd_response_next(conn);
			song = mpd_recv_song(conn);
			artist = mpd_song_get_tag(song, MPD_TAG_ARTIST, 0);
			title = mpd_song_get_tag(song, MPD_TAG_TITLE, 0);
			sprintf(buf, "%s\x02%s", artist, title);
			mpd_song_free(song);
		}
		else {
			strcpy(buf, "");
		}
		mpd_response_finish(conn);
	}
	mpd_connection_free(conn);
	return buf;
}

char *get_vol(char *buf)
{
	FILE *f;
	char fn[50];
	
	sprintf(fn, "%s/.volume", getenv("HOME"));
	f = fopen(fn, "r");
	if(f == NULL) {
		sprintf(buf, "N/A");
		return buf;
	}
	fscanf(f, "%s", buf);
	fclose(f);
	return buf;
}

int main(void)
{
	Display *dpy;
	Window root;
	char status[256], time[32], net[32], mpd[128], vol[4];
	long total_jiffies, work_jiffies;
	float cpu, mem;

	dpy = XOpenDisplay(NULL);
	if(dpy == NULL) {
		fprintf(stderr, "error: could not open display\n");
		return 1;
	}
	root = XRootWindow(dpy, DefaultScreen(dpy));

	total_jiffies = get_total_jiffies();
	work_jiffies = get_work_jiffies();
	
	while(1) {
		cpu = get_cpu(total_jiffies, work_jiffies);
		mem = get_mem();
		get_net(net);
		get_time(time, sizeof(time), CLOCK_FORMAT);
		get_mpd(mpd);
		get_vol(vol);

		sprintf(status, "\x01%s\x01 Cpu\x02%.2f \x01Mem\x02%.2f%s \x01Vol\x02%s %s",
			mpd, cpu, mem, net, vol, time);

		total_jiffies = get_total_jiffies();
		work_jiffies = get_work_jiffies();

		XStoreName(dpy, root, status);
		XFlush(dpy);
		sleep(UPDATE_INTERVAL);
	}

	XCloseDisplay(dpy);
	return 0;
}
