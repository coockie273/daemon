#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <libconfig.h>

#define DAEMON_NAME "watchdog_daemon"
#define WORKING_DIRECTORY  "/"
#define CFG_PATH "/etc/daemon.ini"

struct Cfg {
    int period;
    const char* directory;
};

struct Cfg global_cfg;


void daemonize() {

    pid_t pid;
    pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "Forking failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);

    pid_t sid = setsid();

    if (sid < 0) {
        syslog(LOG_ERR, "Failed to create SID");
        exit(EXIT_FAILURE);
    }

    if (chdir(WORKING_DIRECTORY) < 0) {
        syslog(LOG_ERR, "Failed to change the working directory");
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

}

bool read_config(const char* file, struct Cfg* tmp_cfg) {

    config_t cfg;

    config_init(&cfg);

    if (! config_read_file(&cfg, file)) {

        syslog(LOG_ERR, "Failed to open configuration file");
        config_destroy(&cfg);
        return false;

    }

    tmp_cfg->period = -1;

    if (config_lookup_int(&cfg, "period", &tmp_cfg->period)) {

        if (tmp_cfg->period <= 0) {

            syslog(LOG_ERR, "Configuration error, invalid period");
            return false;
        }
    } else {

        syslog(LOG_ERR, "Configuraion error, period wasn't specified");
        return false;
    }

    const char* temp_dir;

    if (config_lookup_string(&cfg, "directory", &temp_dir)) {

        tmp_cfg->directory = strdup(temp_dir);

        DIR* d;

        if ((d = opendir(tmp_cfg->directory)) == NULL) {

            syslog(LOG_ERR, "Configuration error, invalid directory");
            return false;
        }

        closedir(d);
    } else {

        syslog(LOG_ERR, "Configuraion error, directory wasn't specified");
        return false;
    }

    config_destroy(&cfg);

    return true;
}

void signal_handler(int sig) {

    switch(sig) {

        case SIGHUP:
            syslog(LOG_INFO, "Changing period and directory");
            struct Cfg cfg;
            if (read_config(CFG_PATH, &cfg)) {

                global_cfg = cfg;

            }
            break;

        case SIGTERM:
            syslog(LOG_INFO, "daemon was terminated");
            closelog();
            exit(0);
            break;

    }

}

bool file_was_modified(time_t mod_time) {

   return (int)time(NULL) - global_cfg.period < (int)mod_time;

}

void task(const char* directory) {

    struct stat dir_info;

    if (stat(directory, &dir_info) != 0) {
        return;
    }

    if (S_ISREG(dir_info.st_mode)) {

        if (file_was_modified(dir_info.st_mtime)) {

            time_t t;
            struct tm *tm_info;

            time(&t);
            tm_info = localtime(&dir_info.st_mtime);

            char time_buffer[20];
            strftime(time_buffer, 20, "%Y-%m-%d %H:%M:%S", tm_info);

            syslog(LOG_INFO,"%s %s %s %s", "file:", directory, "was modified at", time_buffer);
        }

    } else if (S_ISDIR(dir_info.st_mode)) {

        DIR* dir;
        struct dirent *entry;

        if ((dir = opendir(directory)) == NULL) {
            return;
        }

        while ( (entry = readdir(dir)) != NULL) {

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
               continue;
            }

            int next_dir_len = strlen(directory) + strlen(entry->d_name) + 2;

            char* next_dir = (char*)malloc((next_dir_len)*sizeof(char));

            snprintf(next_dir, next_dir_len, "%s/%s", directory, entry->d_name);

            task(next_dir);

            free(next_dir);
        }
        closedir(dir);
    }
}


int main() {

    openlog(DAEMON_NAME, LOG_PID, LOG_USER);

    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);

    struct Cfg cfg;
    if (read_config(CFG_PATH, &cfg)) {

        global_cfg = cfg;

    } else {

        exit(EXIT_FAILURE);

    }

    daemonize();

    syslog(LOG_INFO, "%s %d %s %s", "Daemon started with period:", global_cfg.period, "and directory:", global_cfg.directory);

    while(1) {

        sleep(global_cfg.period);
        task(global_cfg.directory);

    }
}
