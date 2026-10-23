/*
 * Init process: runs /etc/rc, then the desktop. If the desktop cannot run, a shell is started on
 * the console instead. Installed as /sbin/init, and as /sbin/textinit for a console-only boot.
 *
 * Like BusyBox's init, SIGUSR2 powers the system off, SIGTERM reboots it and SIGUSR1 halts it.
 * acpid runs poweroff when the power button is pressed, and the kernel sends SIGINT on
 * Ctrl+Alt+Del, which reboots. Installed as /sbin/poweroff, /sbin/reboot and /sbin/halt, the
 * program sends these signals to init.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The requested power action, as a signal number, or zero */
static volatile sig_atomic_t power_action;

static void on_power_signal(int sig) {
	power_action = sig;
}

/* Sends `sig` to every process but init */
static void kill_all(int sig) {
	DIR *d = opendir("/proc");
	struct dirent *de;
	while (d && (de = readdir(d))) {
		int pid = atoi(de->d_name);
		if (pid > 1)
			kill(pid, sig);
	}
	if (d)
		closedir(d);
}

/* Stops every process, writes data to disk, then powers off, reboots or halts */
static void power_down(int sig) {
	if (sig == SIGINT)
		sig = SIGTERM;
	const char *what = sig == SIGTERM ? "Restarting" : sig == SIGUSR1 ? "Halting" : "Shutting down";
	printf("%s...\n", what);
	kill_all(SIGTERM);
	for (int i = 0; i < 20; i++) {
		while (waitpid(-1, NULL, WNOHANG) > 0)
			;
		usleep(100000);
	}
	kill_all(SIGKILL);
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
	sync();
	reboot(sig == SIGTERM ? RB_AUTOBOOT : sig == SIGUSR1 ? RB_HALT_SYSTEM : RB_POWER_OFF);
	for (;;)
		pause();
}

/* Runs a program and waits for it, reaping orphans meanwhile. Returns its exit status */
static int run(char *const argv[], int console) {
	pid_t pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		if (console) {
			/* Its own session, with the console as controlling terminal */
			setsid();
			ioctl(STDIN_FILENO, TIOCSCTTY, 0);
			tcsetpgrp(STDIN_FILENO, getpid());
		}
		if (!console) {
			/* Messages of services go to the kernel log */
			int fd = open("/dev/kmsg", O_WRONLY);
			if (fd >= 0) {
				dup2(fd, 1);
				dup2(fd, 2);
				close(fd);
			}
		}
		execv(argv[0], argv);
		perror(argv[0]);
		_exit(127);
	}
	int status;
	for (;;) {
		pid_t w = wait(&status);
		if (power_action)
			power_down(power_action);
		if (w == pid)
			return status;
		if (w < 0 && errno != EINTR)
			return -1;
	}
}

static void shell_forever(void) {
	char *sh[] = {"/bin/sh", "-l", NULL};
	for (;;)
		run(sh, 1);
}

int main(int argc, char **argv) {
	(void)argc;
	const char *name = strrchr(argv[0], '/');
	name = name ? name + 1 : argv[0];
	/* Commands asking init to change the power state */
	int cmd = !strcmp(name, "poweroff") ? SIGUSR2 : !strcmp(name, "reboot") ? SIGTERM
		: !strcmp(name, "halt") ? SIGUSR1 : 0;
	if (cmd) {
		if (kill(1, cmd) < 0) {
			perror("kill");
			return 1;
		}
		return 0;
	}
	struct sigaction sa = {0};
	sa.sa_handler = on_power_signal;
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	/* Ctrl+Alt+Del sends SIGINT to init instead of rebooting at once */
	reboot(RB_DISABLE_CAD);
	sigaction(SIGINT, &sa, NULL);
	char *rc[] = {"/etc/rc", NULL};
	run(rc, 0);
	if (!strcmp(name, "textinit")) {
		puts("Type 'desk' to start the desktop.");
		shell_forever();
	}
	char *desk[] = {"/bin/desk", NULL};
	int failures = 0;
	while (failures < 3) {
		time_t start = time(NULL);
		run(desk, 0);
		if (power_action)
			power_down(power_action);
		/* Quick exits count as failures */
		failures = time(NULL) - start < 5 ? failures + 1 : 0;
		sleep(1);
	}
	puts("The desktop failed to start. Starting a shell.");
	shell_forever();
}
