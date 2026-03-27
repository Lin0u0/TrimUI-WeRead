#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "api.h"
#include "auth.h"
#include "reader.h"
#include "shelf.h"
#include "ui.h"

typedef struct {
    const char *data_dir;
    const char *font_path;
    const char *platform;
} AppOptions;

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s [--data DIR] [--font FILE] [--platform NAME] [command]\n"
            "Commands:\n"
            "  login [qr.png]\n"
            "  shelf\n"
            "  shelf-cache\n"
            "  reader <reader-url-or-bc> [font-size]\n"
            "  resume\n"
            "  ui\n",
            argv0);
}

int main(int argc, char **argv) {
    ApiContext ctx;
    AppOptions options = {
        .data_dir = ".",
        .font_path = NULL,
        .platform = NULL,
    };
    const char *command = NULL;
    int used_default_command = 0;
    int argi = 1;
    int rc = 1;

    while (argi < argc) {
        if (strcmp(argv[argi], "--data") == 0 && argi + 1 < argc) {
            options.data_dir = argv[++argi];
        } else if (strcmp(argv[argi], "--font") == 0 && argi + 1 < argc) {
            options.font_path = argv[++argi];
        } else if (strcmp(argv[argi], "--platform") == 0 && argi + 1 < argc) {
            options.platform = argv[++argi];
        } else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[argi][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[argi]);
            usage(argv[0]);
            return 1;
        } else {
            command = argv[argi++];
            break;
        }
        argi++;
    }

    if (!command) {
        command = options.font_path ? "ui" : "shelf";
        used_default_command = 1;
    }

    if (api_init(&ctx, options.data_dir) != 0) {
        fprintf(stderr, "Failed to initialize API context\n");
        return 1;
    }

    if (strcmp(command, "login") == 0) {
        AuthSession session;
        char qr_path[1024];
        if (argi < argc) {
            snprintf(qr_path, sizeof(qr_path), "%s", argv[argi]);
        } else {
            snprintf(qr_path, sizeof(qr_path), "%s/weread-login-qr.png", options.data_dir);
        }
        if (auth_start(&ctx, &session, qr_path) == 0 &&
            auth_poll_until_done(&ctx, &session, 120) == 0) {
            rc = 0;
        }
    } else if (strcmp(command, "shelf") == 0) {
        int session_ok = auth_check_session(&ctx, NULL);
        if (session_ok != 1) {
            fprintf(stderr, "%s\n",
                    session_ok == 0 ?
                    "Session expired or not logged in. Run `weread login` first." :
                    "Unable to verify login status. Check your network and try again.");
            api_cleanup(&ctx);
            return 1;
        }
        rc = shelf_print(&ctx) == 0 ? 0 : 1;
    } else if (strcmp(command, "shelf-cache") == 0) {
        rc = shelf_print_cached(&ctx) == 0 ? 0 : 1;
    } else if (strcmp(command, "reader") == 0) {
        int font_size = argi + 1 < argc ? atoi(argv[argi + 1]) : 3;
        if (argi >= argc) {
            usage(argv[0]);
        } else {
            int session_ok = auth_check_session(&ctx, NULL);
            if (session_ok != 1) {
                fprintf(stderr, "%s\n",
                        session_ok == 0 ?
                        "Session expired or not logged in. Run `weread login` first." :
                        "Unable to verify login status. Check your network and try again.");
                api_cleanup(&ctx);
                return 1;
            }
            rc = reader_print(&ctx, argv[argi], font_size) == 0 ? 0 : 1;
        }
    } else if (strcmp(command, "resume") == 0) {
        int session_ok = auth_check_session(&ctx, NULL);
        if (session_ok != 1) {
            fprintf(stderr, "%s\n",
                    session_ok == 0 ?
                    "Session expired or not logged in. Run `weread login` first." :
                    "Unable to verify login status. Check your network and try again.");
            api_cleanup(&ctx);
            return 1;
        }
        rc = reader_resume(&ctx) == 0 ? 0 : 1;
    } else if (strcmp(command, "ui") == 0) {
        if (!ui_is_available() && used_default_command) {
            fprintf(stderr, "UI support unavailable in this build, falling back to shelf.\n");
            rc = shelf_print(&ctx) == 0 ? 0 : 1;
        } else {
            rc = ui_run(&ctx, options.font_path) == 0 ? 0 : 1;
        }
    } else {
        usage(argv[0]);
    }

    api_cleanup(&ctx);
    return rc;
}
