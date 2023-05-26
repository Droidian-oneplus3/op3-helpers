#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <linux/limits.h>
#include <pulse/pulseaudio.h>
#include <sys/stat.h>

#define STOP ((void *)(1))

#define SINK_NAME       "@DEFAULT_SINK@"
#define SOURCE_NAME     "@DEFAULT_SOURCE@"

#define DEVICE_FILE "/dev/input/event1"

static pa_mainloop_api *api = NULL;

static void error(const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  fputs("mute: ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
}

static void set_status(int status) {
  api->quit(api, status);
}

static void complete_callback(pa_context *context, int success, void *disconnect) {
  if (!success) {
    error("failure: %s", pa_strerror(pa_context_errno(context)));
    set_status(EXIT_FAILURE);
  }
  if (disconnect) {
    pa_context_disconnect(context);
  }
}

static void audio_callback(pa_context *context, const pa_sink_info *info, int eol, void *userdata) {
  if (eol < 0) {
    error("failed to get sink information: %s", pa_strerror(pa_context_errno(context)));
    set_status(EXIT_FAILURE);
    return;
  }
  if (eol == 0) {
    pa_operation_unref(pa_context_set_sink_mute_by_name(context, SINK_NAME, !info->mute, complete_callback, STOP));
  }
}

static void context_state_callback(pa_context *context, void *userdata) {
  pa_operation *op = NULL;

  switch (pa_context_get_state(context)) {
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;

    case PA_CONTEXT_READY:
      op = pa_context_get_sink_info_by_name(context, SINK_NAME, audio_callback, NULL);
      if (op != NULL) {
        pa_operation_unref(op);
      }
      break;

    case PA_CONTEXT_TERMINATED:
      set_status(EXIT_SUCCESS);
      break;
    case PA_CONTEXT_FAILED:
      error("connection failure: %s", pa_strerror(pa_context_errno(context)));
      set_status(EXIT_FAILURE);
      break;
  }
}

static int toggle(void) {
  int status = EXIT_FAILURE;

  pa_mainloop *m = NULL;
  pa_context *context = NULL;

  if ((m = pa_mainloop_new()) == NULL) {
    error("pa_mainloop_new failed");
    goto quit;
  }
  if ((api = pa_mainloop_get_api(m)) == NULL) {
    error("pa_mainloop_get_api failed");
    goto quit;
  }
  if ((context = pa_context_new(api, NULL)) == NULL) {
    error("pa_context_new failed");
    goto quit;
  }
  pa_context_set_state_callback(context, context_state_callback, NULL);
  if (pa_context_connect(context, NULL, 0, NULL) < 0) {
    error("pa_context_connect failed: %s", pa_strerror(pa_context_errno(context)));
    goto quit;
  }
  if (pa_mainloop_run(m, &status) < 0) {
    error("pa_mainloop_run failed");
  }
quit:
  if (context != NULL) {
    pa_context_unref(context);
  }
  if (m != NULL) {
    pa_mainloop_free(m);
  }
  return status;
}

int main(int argc, char *argv[]) {
    int fd = inotify_init();

    if (fd < 0) {
        printf("Error: inotify_init failed\n");
        return 1;
    }

    int wd = inotify_add_watch(fd, DEVICE_FILE, IN_MODIFY);

    if (wd < 0) {
        printf("Error: inotify_add_watch failed\n");
        return 1;
    }

    char buffer[sizeof(struct inotify_event) + 255];
    ssize_t len;

    while (1) {
        len = read(fd, buffer, sizeof(buffer));

        if (len > 0) {
            toggle();
        }
    }

    inotify_rm_watch(fd, wd);
    close(fd);

    return 0;
}
