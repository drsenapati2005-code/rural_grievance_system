/*
 * ============================================================================
 *  RURAL PUBLIC SERVICE REQUEST & GRIEVANCE MANAGEMENT SYSTEM
 *  Government of India | Panchayat Service Portal
 * ============================================================================
 *
 *  Core Modules:
 *    1. Citizen Portal      — Grievance lodging, status tracking, reopen & feedback
 *    2. Field Official      — Priority triage, task lifecycle, route optimization
 *    3. Administration     — Account governance, audit ledger & operational analytics
 *    4. Storage Engine      — Plain text persistence (*.txt)
 *
 *  Platform: Standard C11 CLI Application
 *  Compile : gcc rural_grievance.c -o rural_app -Wall -Wextra -std=c11
 *  Run     : ./rural_app (or rural_app.exe on Windows)
 * ============================================================================
 */

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Platform-specific headers for raw keystroke capture, mouse & hardware ID */
#ifdef _WIN32
  #include <conio.h>
  #include <windows.h>
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
  #endif
  #ifndef ENABLE_EXTENDED_FLAGS
    #define ENABLE_EXTENDED_FLAGS 0x0080
  #endif
  #ifndef ENABLE_QUICK_EDIT_MODE
    #define ENABLE_QUICK_EDIT_MODE 0x0040
  #endif
  #ifndef ENABLE_MOUSE_INPUT
    #define ENABLE_MOUSE_INPUT 0x0010
  #endif
#else
  #include <termios.h>
  #include <unistd.h>
  #include <sys/select.h>
#endif

/* ═══════════════════════════════════════════════════════════════
 *  ANSI COLOR PALETTE & TERMINAL UI CONSTANTS
 * ═══════════════════════════════════════════════════════════════ */
#define ANSI_RESET    "\033[0m"
#define ANSI_BOLD     "\033[1m"
#define ANSI_DIM      "\033[2m"
#define ANSI_CYAN     "\033[36m"
#define ANSI_BCYAN    "\033[96m"
#define ANSI_GREEN    "\033[92m"
#define ANSI_AMBER    "\033[93m"
#define ANSI_RED      "\033[91m"
#define ANSI_GRAY     "\033[90m"
#define ANSI_WHITE    "\033[97m"
#define ANSI_BG_TEAL  "\033[48;5;23m"

#define UI_TOTAL_WIDTH       78
#define UI_SIDEBAR_COLLAPSED 3
#define UI_SIDEBAR_EXPANDED  26
#define UI_BODY_ROWS         17
#define UI_BODY_START_ROW    4
#define UI_ITEMS_START_ROW   7
#define UI_MAX_WS_LINES      10

/* Device key file for hardware-locked admin binding */
#define FILE_DEVICE_KEY  "admin_device.key"

/* ═══════════════════════════════════════════════════════════════
 *  COMPILE-TIME CONSTANTS
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_OFFICIALS        50
#define MAX_CITIZENS         200
#define MAX_DESC             300
#define MAX_NAME             80
#define MAX_PHONE            16
#define MAX_AADHAAR          16
#define MAX_DEPT             40
#define MAX_VILLAGE          40
#define MAX_STATUS           80
#define MAX_REMARKS          200
#define MAX_COMMENT          200
#define MAX_TS               26

#define NUM_VILLAGES         6
#define NUM_DEPARTMENTS      5
#define TICKET_ID_START      1001

#define PRIORITY_EMERGENCY   10
#define PRIORITY_NORMAL      5
#define ESCALATION_THRESHOLD 7      /* days_pending >= this → auto-escalate */

/* File names (Plain text persistence) */
#define FILE_CITIZENS        "citizens.txt"
#define FILE_GRIEVANCES      "grievances.txt"
#define FILE_OFFICIALS       "officials.txt"
#define FILE_HISTORY         "history.txt"
#define FILE_MASTER_LOG      "panchayat_records.txt"

/* Admin master credentials (32-bit DJB2 password hash) */
#define ADMIN_USERNAME       "admin"
#define ADMIN_PASS_HASH      1848599382UL

/* ═══════════════════════════════════════════════════════════════
 *  STATIC LOOKUP ARRAYS  (Array DS)
 * ═══════════════════════════════════════════════════════════════ */
static const char villages[NUM_VILLAGES][MAX_VILLAGE] = {
    "Block HQ",    /* 0 — administrative centre, rarely a complaint village */
    "Rampur",      /* 1 */
    "Sitapur",     /* 2 */
    "Gopalpur",    /* 3 */
    "Krishnapur",  /* 4 */
    "Balipur"      /* 5 */
};

static const char departments[NUM_DEPARTMENTS][MAX_DEPT] = {
    "Water Supply",    /* 0 */
    "Electricity",     /* 1 */
    "Roads",           /* 2 */
    "Sanitation",      /* 3 */
    "Welfare Schemes"  /* 4 */
};

/* ═══════════════════════════════════════════════════════════════
 *  STRUCT DEFINITIONS
 * ═══════════════════════════════════════════════════════════════ */

/* ── 1. Citizen (Array DS element) ── */
typedef struct {
    int  citizen_id;              /* auto-increment */
    char name[MAX_NAME];
    char aadhaar[MAX_AADHAAR];   /* used as login ID */
    char phone[MAX_PHONE];
    int  active;                  /* 1 = registered */
} Citizen;

/* ── 2. Official Staff Account ── */
typedef struct {
    int  official_id;
    char name[MAX_NAME];
    char username[MAX_NAME];
    char password[MAX_NAME];
    int  dept_assigned;           /* -1 = unassigned, else dept index */
    int  active;
} Official;

/* ── 3. Grievance Service Ticket Record ── */
typedef struct GrievanceNode {
    /* Identity */
    int  ticket_id;
    int  citizen_id;              /* links back to citizen */

    /* Location & classification */
    int  village_id;              /* index into villages[] */
    int  dept_id;                 /* index into departments[] */
    int  is_hazard;               /* 1 = life-safety hazard */

    /* Content */
    char description[MAX_DESC];

    /* Workflow state */
    char status[MAX_STATUS];      /* "Pending" | "In Progress" |
                                     "Escalated" | "Resolved" */
    int  priority;                /* 5 or 10 */
    int  days_pending;            /* increments each simulated day */
    int  assigned_official_id;    /* -1 = unassigned */
    char remarks[MAX_REMARKS];    /* official field notes */

    /* Timestamps */
    char submitted_at[MAX_TS];
    char resolved_at[MAX_TS];     /* empty until resolved */

    /* Citizen feedback (populated after resolution) */
    int  rating;                  /* 0 = not rated, 1–5 stars */
    char feedback[MAX_COMMENT];

    struct GrievanceNode *next;   /* next record in ledger */
} GrievanceNode;

/* ── 4. Fast Ticket Search Index Node ── */
typedef struct BSTNode {
    int            ticket_id;
    GrievanceNode *ref;           /* direct pointer into active ledger */
    struct BSTNode *left, *right;
} BSTNode;

/* ── 5. Priority Task Dispatch Queue Node ── */
typedef struct PQNode {
    int            ticket_id;
    int            priority;
    int            days_pending;
    GrievanceNode *ref;
    struct PQNode *next;
} PQNode;

/* ── 6. History / Audit log entry ── */
typedef struct HistEntry {
    int  ticket_id;
    char action[MAX_STATUS];
    char actor[MAX_NAME];
    char timestamp[MAX_TS];
    struct HistEntry *next;
} HistEntry;

/* ── Flat structs for binary file I/O ── */
typedef struct {
    int  ticket_id;
    int  citizen_id;
    int  village_id;
    int  dept_id;
    int  is_hazard;
    char description[MAX_DESC];
    char status[MAX_STATUS];
    int  priority;
    int  days_pending;
    int  assigned_official_id;
    char remarks[MAX_REMARKS];
    char submitted_at[MAX_TS];
    char resolved_at[MAX_TS];
    int  rating;
    char feedback[MAX_COMMENT];
} GrievanceRecord;

typedef struct {
    int  ticket_id;
    char action[MAX_STATUS];
    char actor[MAX_NAME];
    char timestamp[MAX_TS];
} HistRecord;

/* ═══════════════════════════════════════════════════════════════
 *  GLOBAL STATE
 * ═══════════════════════════════════════════════════════════════ */
static Citizen   citizens[MAX_CITIZENS];
static int       citizen_count  = 0;
static int       next_citizen_id = 1;

static Official  officials[MAX_OFFICIALS];
static int       official_count = 0;
static int       next_official_id = 101;

static int       next_ticket_id = TICKET_ID_START;

/* Core Service Registry & Fast-Lookup Indexes */
static GrievanceNode *ledger_head = NULL;   /* Active Grievance Ledger */
static BSTNode       *bst_root    = NULL;   /* Fast Ticket Search Index */
static PQNode        *pq_front    = NULL;   /* Priority Task Dispatch Queue */
static HistEntry     *hist_head   = NULL;   /* Compliance & Audit Trail Log */

/* ═══════════════════════════════════════════════════════════════
 *  CROSS-PLATFORM CONSOLE, ANSI STYLING & HOVER SIDEBAR ENGINE
 * ═══════════════════════════════════════════════════════════════ */
static int g_is_tty = 0;
#ifdef _WIN32
static HANDLE g_hIn  = NULL;
static HANDLE g_hOut = NULL;
static DWORD  g_orig_in_mode  = 0;
static DWORD  g_orig_out_mode = 0;
static int    g_console_inited = 0;
#else
static struct termios g_orig_termios;
static int            g_termios_saved = 0;
#endif
static int g_in_nav_mode = 0;

static void sleep_ms(int ms) {
    if (ms <= 0 || !g_is_tty) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}

static void console_leave_nav_mode(void) {
    if (!g_is_tty) return;
    printf("\033[?25h");
    if (!g_in_nav_mode) { fflush(stdout); return; }
#ifdef _WIN32
    if (g_hIn && g_hIn != INVALID_HANDLE_VALUE) {
        DWORD restore = g_orig_in_mode | ENABLE_LINE_INPUT |
                        ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT;
        SetConsoleMode(g_hIn, restore);
        FlushConsoleInputBuffer(g_hIn);
    }
#else
    printf("\033[?1003l\033[?1006l");
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
    }
#endif
    g_in_nav_mode = 0;
    fflush(stdout);
}

static void console_enter_nav_mode(void) {
    if (!g_is_tty) return;
#ifdef _WIN32
    if (g_hIn && g_hIn != INVALID_HANDLE_VALUE) {
        FlushConsoleInputBuffer(g_hIn);
        DWORD nav = g_orig_in_mode;
        nav |= (ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
        nav &= ~(DWORD)(ENABLE_QUICK_EDIT_MODE | ENABLE_LINE_INPUT |
                        ENABLE_ECHO_INPUT);
        SetConsoleMode(g_hIn, nav);
    }
#else
    if (g_termios_saved) {
        struct termios raw = g_orig_termios;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    printf("\033[?1003h\033[?1006h");
#endif
    g_in_nav_mode = 1;
    fflush(stdout);
}

static void console_restore_on_exit(void) {
    console_leave_nav_mode();
#ifdef _WIN32
    if (g_console_inited && g_hIn && g_hIn != INVALID_HANDLE_VALUE)
        SetConsoleMode(g_hIn, g_orig_in_mode);
    if (g_console_inited && g_hOut && g_hOut != INVALID_HANDLE_VALUE)
        SetConsoleMode(g_hOut, g_orig_out_mode);
#endif
    printf(ANSI_RESET "\033[?25h");
    fflush(stdout);
}

static void console_init(void) {
#ifdef _WIN32
    SetConsoleOutputCP(65001U);
    SetConsoleCP(65001U);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mi = 0, mo = 0;
    if (g_hIn && g_hOut &&
        GetConsoleMode(g_hIn, &mi) && GetConsoleMode(g_hOut, &mo)) {
        g_is_tty        = 1;
        g_orig_in_mode  = mi;
        g_orig_out_mode = mo;
        g_console_inited = 1;
        SetConsoleMode(g_hOut, mo | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#else
    if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)) {
        g_is_tty = 1;
        if (tcgetattr(STDIN_FILENO, &g_orig_termios) == 0)
            g_termios_saved = 1;
    }
#endif
    atexit(console_restore_on_exit);
}

/* ═══════════════════════════════════════════════════════════════
 *  UTILITY & SANITIZATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════ */
static void get_timestamp(char *buf) {
    time_t t = time(NULL);
    struct tm *ti = localtime(&t);
    strftime(buf, MAX_TS, "%Y-%m-%d %H:%M:%S", ti);
}

static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/* Count display columns for a UTF-8 string (box-drawing/arrows = width 1) */
static int utf8_cols(const char *s) {
    int cols = 0;
    if (!s) return 0;
    for (size_t i = 0; s[i]; i++) {
        unsigned char b = (unsigned char)s[i];
        if ((b & 0xC0) != 0x80) cols++;
    }
    return cols;
}

/* Print up to max_cols display columns and pad trailing spaces cleanly */
static void print_clipped_padded(const char *s, int max_cols) {
    int printed = 0;
    if (s) {
        size_t i = 0;
        while (s[i] && printed < max_cols) {
            unsigned char b = (unsigned char)s[i];
            size_t seq = 1;
            if      ((b & 0xE0) == 0xC0) seq = 2;
            else if ((b & 0xF0) == 0xE0) seq = 3;
            else if ((b & 0xF8) == 0xF0) seq = 4;
            if (printed + 3 == max_cols && s[i + seq] != '\0') {
                /* Clip cleanly with "..." if text exceeds box width */
                size_t rem_bytes = strlen(s + i);
                if (rem_bytes > 0 && utf8_cols(s + i) > 3) {
                    fputs("...", stdout);
                    printed += 3;
                    break;
                }
            }
            for (size_t k = 0; k < seq && s[i + k]; k++) {
                putchar((unsigned char)s[i + k]);
            }
            i += seq;
            printed++;
        }
    }
    while (printed < max_cols) {
        putchar(' ');
        printed++;
    }
}

static void print_rep_utf8(const char *glyph, int count) {
    for (int i = 0; i < count; i++) fputs(glyph, stdout);
}

static void print_line(char ch, int n) {
    printf("%s", g_is_tty ? ANSI_GRAY : "");
    for (int i = 0; i < n; i++) putchar(ch);
    printf("%s\n", g_is_tty ? ANSI_RESET : "");
}

static void print_header(const char *title) {
    if (g_is_tty) {
        printf(ANSI_CYAN "  ╭");
        print_rep_utf8("─", 66);
        printf("╮\n  │ " ANSI_BOLD ANSI_BCYAN);
        print_clipped_padded(title, 64);
        printf(ANSI_RESET ANSI_CYAN " │\n  ╰");
        print_rep_utf8("─", 66);
        printf("╯" ANSI_RESET "\n");
    } else {
        print_line('=', 68);
        printf("   %s\n", title);
        print_line('=', 68);
    }
}

static void print_subheader(const char *title) {
    if (g_is_tty) {
        printf(ANSI_CYAN "  ─── " ANSI_BOLD ANSI_BCYAN "%s" ANSI_RESET ANSI_GRAY
               " ────────────────────────────────────────" ANSI_RESET "\n", title);
    } else {
        print_line('-', 68);
        printf("  %s\n", title);
        print_line('-', 68);
    }
}

/* Helper to sanitize pipe characters from strings stored in text files */
static void sanitize_pipes(char *str) {
    if (!str) return;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '|') str[i] = '-';
        if (str[i] == '\n' || str[i] == '\r') str[i] = ' ';
    }
}

/* Safe string input — strips newlines AND pipe delimiters immediately */
static void read_line(const char *prompt, char *buf, int size) {
    printf("  %s", prompt);
    fflush(stdout);
    buf[0] = '\0';
    if (fgets(buf, size, stdin)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        sanitize_pipes(buf);
    }
}

/* Masked password input (* echo + Backspace support + pipe sanitization) */
static void read_password(const char *prompt, char *buf, int size) {
    printf("  %s", prompt);
    fflush(stdout);
    if (!g_is_tty) {
        buf[0] = '\0';
        if (fgets(buf, size, stdin)) {
            buf[strcspn(buf, "\r\n")] = '\0';
            sanitize_pipes(buf);
        }
        return;
    }

    int len = 0;
    buf[0] = '\0';
    while (1) {
        int c;
#ifdef _WIN32
        c = _getch();
        if (c == 0 || c == 0xE0) {
            (void)_getch(); /* discard extended arrow/function scan code */
            continue;
        }
#else
        struct termios oldt, rawt;
        if (g_termios_saved) {
            tcgetattr(STDIN_FILENO, &oldt);
            rawt = oldt;
            rawt.c_lflag &= (tcflag_t)~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &rawt);
        }
        c = getchar();
        if (g_termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
        if (c == '\r' || c == '\n' || c == EOF) {
            putchar('\n');
            break;
        } else if (c == 0x03) {
            putchar('\n');
            memset(buf, 0, (size_t)size);
            exit(1);
        } else if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                buf[len] = '\0';
                fputs("\b \b", stdout);
                fflush(stdout);
            }
        } else if (c >= 0x20 && c < 0x7F && len < size - 1) {
            if (c == '|') c = '-';
            buf[len++] = (char)c;
            buf[len]   = '\0';
            putchar('*');
            fflush(stdout);
        }
    }
    sanitize_pipes(buf);
}

/* ═══════════════════════════════════════════════════════════════
 *  MINIMAL FLUID ANIMATIONS
 * ═══════════════════════════════════════════════════════════════ */
static void anim_boot_progress_bar(const char *label) {
    if (!g_is_tty) {
        printf("  %s\n", label);
        return;
    }
    const int steps = 20;
    printf("\n");
    for (int i = 0; i <= steps; i++) {
        printf("\r  " ANSI_CYAN "%s " ANSI_GRAY "[" ANSI_GREEN, label);
        for (int j = 0; j < steps; j++) {
            fputs(j < i ? "█" : "░", stdout);
        }
        printf(ANSI_GRAY "] " ANSI_BOLD ANSI_WHITE "%3d%%" ANSI_RESET, i * 5);
        fflush(stdout);
        if (i < steps) sleep_ms(12);
    }
    putchar('\n');
}

static void anim_typewriter(const char *color, const char *text) {
    if (!text) return;
    if (!g_is_tty) {
        printf("%s\n", text);
        return;
    }
    printf("  %s" ANSI_BOLD, color ? color : ANSI_BCYAN);
    for (size_t i = 0; text[i]; i++) {
        putchar((unsigned char)text[i]);
        fflush(stdout);
        sleep_ms(10);
    }
    printf(ANSI_RESET "\n");
}

static void anim_spinner(const char *label, const char *done_msg) {
    if (!g_is_tty) {
        if (done_msg && done_msg[0]) printf("  [OK] %s\n", done_msg);
        return;
    }
    static const char *frames[3] = {"⠋", "⠙", "⠹"};
    for (int i = 0; i < 6; i++) {
        printf("\r  " ANSI_BCYAN "%s " ANSI_CYAN "%s..." ANSI_RESET,
               frames[i % 3], label ? label : "Processing");
        fflush(stdout);
        sleep_ms(32);
    }
    if (done_msg && done_msg[0]) {
        printf("\r  " ANSI_GREEN "[OK] %-58s" ANSI_RESET "\n", done_msg);
    } else {
        printf("\r  %-64s\r", "");
    }
    fflush(stdout);
}

/* DJB2 string hash (32-bit masked for cross-platform consistency) */
static unsigned long hash_password(const char *str) {
    uint32_t hash = 5381U;
    int c;
    if (!str) return 0UL;
    while ((c = (unsigned char)*str++) != 0) {
        hash = ((hash << 5) + hash) + (uint32_t)c;
    }
    return (unsigned long)hash;
}

/* ═══════════════════════════════════════════════════════════════
 *  HARDWARE FINGERPRINT — DJB2 hash of a unique machine identifier
 * ═══════════════════════════════════════════════════════════════ */
static unsigned long get_machine_fingerprint(void) {
    char id_str[256];
    memset(id_str, 0, sizeof(id_str));

#ifdef _WIN32
    /* Try Windows Machine GUID from registry */
    FILE *fp = _popen(
        "reg query \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\""
        " /v MachineGuid 2>nul", "r");
    if (fp) {
        char line[512];
        int found = 0;
        while (fgets(line, sizeof(line), fp)) {
            char *ptr = strstr(line, "REG_SZ");
            if (ptr) {
                ptr += 6;                         /* skip "REG_SZ" */
                while (*ptr == ' ' || *ptr == '\t') ptr++;
                size_t len = strlen(ptr);
                while (len > 0 &&
                       (ptr[len - 1] == '\n' || ptr[len - 1] == '\r' ||
                        ptr[len - 1] == ' '))
                    len--;
                if (len > 0 && len < sizeof(id_str)) {
                    memcpy(id_str, ptr, len);
                    id_str[len] = '\0';
                    found = 1;
                }
                break;
            }
        }
        _pclose(fp);
        if (!found) {
            const char *cn = getenv("COMPUTERNAME");
            if (cn) strncpy(id_str, cn, sizeof(id_str) - 1);
        }
    } else {
        const char *cn = getenv("COMPUTERNAME");
        if (cn) strncpy(id_str, cn, sizeof(id_str) - 1);
    }
#else
    /* Try /etc/machine-id or /var/lib/dbus/machine-id on Linux/macOS */
    FILE *fp = fopen("/etc/machine-id", "r");
    if (!fp) fp = fopen("/var/lib/dbus/machine-id", "r");
    if (fp) {
        if (fgets(id_str, sizeof(id_str), fp)) {
            size_t len = strlen(id_str);
            while (len > 0 &&
                   (id_str[len - 1] == '\n' || id_str[len - 1] == '\r' ||
                    id_str[len - 1] == ' '))
                id_str[--len] = '\0';
        }
        fclose(fp);
    }
    if (id_str[0] == '\0') {
        const char *hn = getenv("HOSTNAME");
        if (hn) strncpy(id_str, hn, sizeof(id_str) - 1);
    }
#endif

    if (id_str[0] == '\0') return 0UL;      /* should never happen */

    /* DJB2 hash of the machine identifier string */
    uint32_t hash = 5381U;
    for (int i = 0; id_str[i]; i++)
        hash = ((hash << 5) + hash) + (uint32_t)(unsigned char)id_str[i];
    return (unsigned long)hash;
}

/* ═══════════════════════════════════════════════════════════════
 *  UNIFIED MOUSE + KEYBOARD NAVIGATION & SLIDE-OUT SIDEBAR ENGINE
 * ═══════════════════════════════════════════════════════════════ */
typedef enum {
    EV_NONE = 0,
    EV_KEY,
    EV_MOUSE_MOVE,
    EV_MOUSE_CLICK
} NavEventType;

typedef struct {
    NavEventType type;
    int key;
    int col; /* 1-based column (X) */
    int row; /* 1-based row    (Y) */
} NavEvent;

typedef struct {
    char        key;
    const char *label;
    const char *badge;
} SidebarItem;

typedef struct {
    const char        *portal_title;
    const char        *subtitle;
    const char        *workspace_title;
    const char        *workspace_lines[UI_MAX_WS_LINES];
    int                workspace_line_count;
    const SidebarItem *items;
    int                item_count;
    const char        *stats_line1;
    const char        *stats_line2;
    int                allow_secret_admin;
} SidebarLayoutConfig;

static NavEvent poll_nav_event(void) {
    NavEvent ev = { EV_NONE, 0, 0, 0 };
    if (!g_is_tty) {
        int c = getchar();
        if (c == EOF) { ev.type = EV_KEY; ev.key = '0'; return ev; }
        if (c == '\r' || c == '\n') return ev;
        ev.type = EV_KEY;
        ev.key  = c;
        return ev;
    }

#ifdef _WIN32
    if (!g_hIn || g_hIn == INVALID_HANDLE_VALUE) return ev;
    INPUT_RECORD rec;
    DWORD nread = 0;
    while (ReadConsoleInputA(g_hIn, &rec, 1, &nread) && nread > 0) {
        if (rec.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD me = rec.Event.MouseEvent;
            /* Coalesce queued mouse-move events for zero-lag sliding */
            if (me.dwEventFlags & MOUSE_MOVED) {
                INPUT_RECORD peek;
                DWORD navail = 0;
                while (PeekConsoleInputA(g_hIn, &peek, 1, &navail) &&
                       navail > 0 && peek.EventType == MOUSE_EVENT &&
                       (peek.Event.MouseEvent.dwEventFlags & MOUSE_MOVED)) {
                    ReadConsoleInputA(g_hIn, &rec, 1, &nread);
                    me = rec.Event.MouseEvent;
                }
                ev.type = EV_MOUSE_MOVE;
                ev.col  = (int)me.dwMousePosition.X + 1;
                ev.row  = (int)me.dwMousePosition.Y + 1;
                return ev;
            }
            if (me.dwEventFlags == 0 &&
                (me.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
                ev.type = EV_MOUSE_CLICK;
                ev.col  = (int)me.dwMousePosition.X + 1;
                ev.row  = (int)me.dwMousePosition.Y + 1;
                return ev;
            }
        } else if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            KEY_EVENT_RECORD ke = rec.Event.KeyEvent;
            int ctrl_held = (ke.dwControlKeyState &
                             (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
            if ((unsigned char)ke.uChar.AsciiChar == 1 ||
                (ke.wVirtualKeyCode == 'A' && ctrl_held)) {
                ev.type = EV_KEY;
                ev.key  = 0x01;
                return ev;
            }
            if (ke.wVirtualKeyCode == VK_TAB || ke.uChar.AsciiChar == '\t') {
                ev.type = EV_KEY;
                ev.key  = '\t';
                return ev;
            }
            if ((unsigned char)ke.uChar.AsciiChar != 0) {
                ev.type = EV_KEY;
                ev.key  = (unsigned char)ke.uChar.AsciiChar;
                return ev;
            }
        }
    }
#else
    int c = getchar();
    if (c == EOF) { ev.type = EV_KEY; ev.key = '0'; return ev; }
    if (c == 0x1B) {
        fd_set fds;
        struct timeval tv = {0, 40000};
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) <= 0) {
            ev.type = EV_KEY;
            ev.key  = 0x1B;
            return ev;
        }
        int c2 = getchar();
        if (c2 == '[') {
            int c3 = getchar();
            if (c3 == '<') {
                int pb = 0, px = 0, py = 0, stage = 0;
                int term_ch = 0;
                int ch;
                while ((ch = getchar()) != EOF) {
                    if (ch >= '0' && ch <= '9') {
                        if      (stage == 0) pb = pb * 10 + (ch - '0');
                        else if (stage == 1) px = px * 10 + (ch - '0');
                        else                 py = py * 10 + (ch - '0');
                    } else if (ch == ';') {
                        stage++;
                    } else if (ch == 'M' || ch == 'm') {
                        term_ch = ch;
                        break;
                    } else {
                        break;
                    }
                }
                if (pb & 32) {
                    ev.type = EV_MOUSE_MOVE;
                    ev.col  = px;
                    ev.row  = py;
                    return ev;
                }
                if (term_ch == 'M' && (pb & 3) == 0) {
                    ev.type = EV_MOUSE_CLICK;
                    ev.col  = px;
                    ev.row  = py;
                    return ev;
                }
                return ev;
            }
        }
        return ev;
    }
    ev.type = EV_KEY;
    ev.key  = c;
#endif
    return ev;
}

static void draw_top_banner_box(const SidebarLayoutConfig *cfg) {
    printf("\033[1;1H");
    printf(ANSI_CYAN "╭");
    print_rep_utf8("─", UI_TOTAL_WIDTH - 2);
    printf("╮\n");

    printf("\033[2;1H" ANSI_CYAN "│ " ANSI_BOLD ANSI_BCYAN);
    print_clipped_padded(cfg->portal_title, UI_TOTAL_WIDTH - 4);
    printf(ANSI_RESET ANSI_CYAN " │\n");

    printf("\033[3;1H" ANSI_CYAN "╰" ANSI_RESET ANSI_GRAY);
    /* Put compact subtitle inside bottom divider feel cleanly */
    char sub[256];
    snprintf(sub, sizeof(sub), " %s ", cfg->subtitle ? cfg->subtitle : "");
    int sub_w = utf8_cols(sub);
    if (sub_w > UI_TOTAL_WIDTH - 6) sub_w = UI_TOTAL_WIDTH - 6;
    printf("─");
    print_clipped_padded(sub, sub_w);
    printf(ANSI_CYAN);
    print_rep_utf8("─", (UI_TOTAL_WIDTH - 3) - sub_w);
    printf("╯" ANSI_RESET);
}

/* Render collapsible sidebar (cols 1..sidebar_w) + right workspace card */
static void render_layout_with_sidebar(const SidebarLayoutConfig *cfg,
                                       int sidebar_w, int hover_idx) {
    if (!g_is_tty) {
        printf("\n");
        print_header(cfg->portal_title);
        for (int i = 0; i < cfg->item_count; i++) {
            printf("  %c. %s\n", cfg->items[i].key, cfg->items[i].label);
        }
        printf("  Choice: ");
        fflush(stdout);
        return;
    }

    int right_start = sidebar_w + 2;
    int right_w     = UI_TOTAL_WIDTH - right_start + 1;
    int right_inner = right_w - 4;
    if (right_inner < 10) right_inner = 10;

    printf("\033[?25l");
    for (int r = 0; r < UI_BODY_ROWS; r++) {
        int term_row = UI_BODY_START_ROW + r;
        printf("\033[%d;1H", term_row);

        /* ── Left Sidebar Column Strip / Drawer ── */
        if (sidebar_w <= 3) {
            static const char *strip_chars = "   ▸MENU▸  ";
            if (r == 0) {
                printf(ANSI_GRAY "╭─╮" ANSI_RESET);
            } else if (r == UI_BODY_ROWS - 1) {
                printf(ANSI_GRAY "╰─╯" ANSI_RESET);
            } else {
                char gl[8] = " ";
                if (r == 2)      strcpy(gl, "▸");
                else if (r == 5) strcpy(gl, "M");
                else if (r == 6) strcpy(gl, "E");
                else if (r == 7) strcpy(gl, "N");
                else if (r == 8) strcpy(gl, "U");
                else if (r == 10) strcpy(gl, "▸");
                else (void)strip_chars;
                printf(ANSI_GRAY "│" ANSI_BCYAN "%s" ANSI_GRAY "│" ANSI_RESET, gl);
            }
        } else {
            int sb_inner = sidebar_w - 2;
            if (r == 0) {
                printf(ANSI_BCYAN "╭");
                print_rep_utf8("─", sb_inner);
                printf("╮" ANSI_RESET);
            } else if (r == 1) {
                printf(ANSI_BCYAN "│" ANSI_BOLD ANSI_WHITE);
                print_clipped_padded(sb_inner >= 18 ? " NAVIGATION MENU  ▾" : " MENU", sb_inner);
                printf(ANSI_RESET ANSI_BCYAN "│" ANSI_RESET);
            } else if (r == 2) {
                printf(ANSI_BCYAN "├" ANSI_GRAY);
                print_rep_utf8("─", sb_inner);
                printf(ANSI_BCYAN "┤" ANSI_RESET);
            } else if (r == UI_BODY_ROWS - 4) {
                printf(ANSI_BCYAN "├" ANSI_GRAY);
                print_rep_utf8("─", sb_inner);
                printf(ANSI_BCYAN "┤" ANSI_RESET);
            } else if (r == UI_BODY_ROWS - 3) {
                printf(ANSI_BCYAN "│" ANSI_GRAY);
                print_clipped_padded(cfg->stats_line1 ? cfg->stats_line1 : "", sb_inner);
                printf(ANSI_BCYAN "│" ANSI_RESET);
            } else if (r == UI_BODY_ROWS - 2) {
                printf(ANSI_BCYAN "│" ANSI_CYAN);
                print_clipped_padded(cfg->stats_line2 ? cfg->stats_line2 : "", sb_inner);
                printf(ANSI_BCYAN "│" ANSI_RESET);
            } else if (r == UI_BODY_ROWS - 1) {
                printf(ANSI_BCYAN "╰");
                print_rep_utf8("─", sb_inner);
                printf("╯" ANSI_RESET);
            } else {
                int item_idx = r - 3;
                if (item_idx >= 0 && item_idx < cfg->item_count) {
                    char rowbuf[64];
                    int is_exit = (cfg->items[item_idx].key == '0');
                    int is_hov  = (hover_idx == item_idx);
                    snprintf(rowbuf, sizeof(rowbuf), "%s[%c] %s",
                             is_hov ? "▸" : " ",
                             cfg->items[item_idx].key,
                             cfg->items[item_idx].label);
                    printf(ANSI_BCYAN "│");
                    if (is_hov)        printf(ANSI_BG_TEAL ANSI_BOLD ANSI_WHITE);
                    else if (is_exit)  printf(ANSI_AMBER);
                    else               printf(ANSI_WHITE);
                    print_clipped_padded(rowbuf, sb_inner);
                    printf(ANSI_RESET ANSI_BCYAN "│" ANSI_RESET);
                } else {
                    printf(ANSI_BCYAN "│");
                    print_clipped_padded("", sb_inner);
                    printf("│" ANSI_RESET);
                }
            }
        }

        /* ── Gap Col ── */
        putchar(' ');

        /* ── Right Main Workspace Card ── */
        if (r == 0) {
            printf(ANSI_CYAN "╭");
            print_rep_utf8("─", right_w - 2);
            printf("╮" ANSI_RESET);
        } else if (r == 1) {
            printf(ANSI_CYAN "│ " ANSI_BOLD ANSI_BCYAN);
            print_clipped_padded(cfg->workspace_title ? cfg->workspace_title : "",
                                 right_inner);
            printf(ANSI_RESET ANSI_CYAN " │" ANSI_RESET);
        } else if (r == 2 || r == UI_BODY_ROWS - 3) {
            printf(ANSI_CYAN "├" ANSI_GRAY);
            print_rep_utf8("─", right_w - 2);
            printf(ANSI_CYAN "┤" ANSI_RESET);
        } else if (r == UI_BODY_ROWS - 2) {
            /* Quick navigation summary strip in workspace footer */
            char quick[128] = "";
            for (int i = 0; i < cfg->item_count; i++) {
                char seg[36];
                snprintf(seg, sizeof(seg), "[%c] %s  ",
                         cfg->items[i].key, cfg->items[i].label);
                if ((int)strlen(quick) + (int)strlen(seg) < right_inner - 1)
                    strcat(quick, seg);
            }
            printf(ANSI_CYAN "│ " ANSI_GREEN);
            print_clipped_padded(quick, right_inner);
            printf(ANSI_RESET ANSI_CYAN " │" ANSI_RESET);
        } else if (r == UI_BODY_ROWS - 1) {
            printf(ANSI_CYAN "╰");
            print_rep_utf8("─", right_w - 2);
            printf("╯" ANSI_RESET);
        } else {
            int ws_idx = r - 3;
            const char *line_txt =
                (ws_idx >= 0 && ws_idx < cfg->workspace_line_count)
                ? cfg->workspace_lines[ws_idx]
                : "";
            const char *col_ansi = ANSI_WHITE;
            if (line_txt && (strstr(line_txt, "Escalated") || strstr(line_txt, "ALERT")))
                col_ansi = ANSI_AMBER;
            else if (line_txt && strstr(line_txt, "Resolved"))
                col_ansi = ANSI_GREEN;
            else if (line_txt && (strstr(line_txt, "Hover") || strstr(line_txt, "───")))
                col_ansi = ANSI_GRAY;

            printf(ANSI_CYAN "│ %s", col_ansi);
            print_clipped_padded(line_txt, right_inner);
            printf(ANSI_RESET ANSI_CYAN " │" ANSI_RESET);
        }
        printf("\033[K");
    }
    printf("\033[%d;1H" ANSI_GRAY "  Hint: Hover mouse at left edge or press "
           ANSI_BCYAN "[Tab]/[M]" ANSI_GRAY " to slide menu · Click item or press key"
           ANSI_RESET "\033[K",
           UI_BODY_START_ROW + UI_BODY_ROWS);
    fflush(stdout);
}

static void animate_sidebar_transition(const SidebarLayoutConfig *cfg,
                                       int opening) {
    if (!g_is_tty) return;
    if (opening) {
        const int frames[3] = {4, 14, UI_SIDEBAR_EXPANDED};
        for (int i = 0; i < 3; i++) {
            render_layout_with_sidebar(cfg, frames[i], -1);
            sleep_ms(18);
        }
    } else {
        const int frames[3] = {UI_SIDEBAR_EXPANDED, 14, 4};
        for (int i = 0; i < 3; i++) {
            render_layout_with_sidebar(cfg, frames[i], -1);
            sleep_ms(18);
        }
        render_layout_with_sidebar(cfg, UI_SIDEBAR_COLLAPSED, -1);
    }
}

/* Interactive sidebar dashboard event loop returning selected key */
static int run_sidebar_menu(const SidebarLayoutConfig *cfg) {
    int expanded     = 0;
    int pinned_by_kb = 0;
    int hover_idx    = -1;

    if (g_is_tty) {
        printf("\033[2J\033[H");
        draw_top_banner_box(cfg);
        render_layout_with_sidebar(cfg, UI_SIDEBAR_COLLAPSED, -1);
    } else {
        render_layout_with_sidebar(cfg, UI_SIDEBAR_COLLAPSED, -1);
    }

    console_enter_nav_mode();

    while (1) {
        NavEvent ev = poll_nav_event();

        if (ev.type == EV_MOUSE_MOVE && g_is_tty) {
            if (!expanded && ev.col <= 3) {
                expanded     = 1;
                pinned_by_kb = 0;
                animate_sidebar_transition(cfg, 1);
            } else if (expanded) {
                if (ev.col <= UI_SIDEBAR_EXPANDED) {
                    pinned_by_kb = 0;
                    int new_hov = -1;
                    int idx = ev.row - UI_ITEMS_START_ROW;
                    if (idx >= 0 && idx < cfg->item_count) new_hov = idx;
                    if (new_hov != hover_idx) {
                        hover_idx = new_hov;
                        render_layout_with_sidebar(cfg, UI_SIDEBAR_EXPANDED, hover_idx);
                    }
                } else if (!pinned_by_kb && ev.col > UI_SIDEBAR_EXPANDED) {
                    expanded  = 0;
                    hover_idx = -1;
                    animate_sidebar_transition(cfg, 0);
                }
            }
            continue;
        }

        if (ev.type == EV_MOUSE_CLICK && g_is_tty) {
            if (!expanded && ev.col <= 4) {
                expanded     = 1;
                pinned_by_kb = 1;
                animate_sidebar_transition(cfg, 1);
            } else if (expanded && ev.col <= UI_SIDEBAR_EXPANDED) {
                int idx = ev.row - UI_ITEMS_START_ROW;
                if (idx >= 0 && idx < cfg->item_count) {
                    int chosen = (unsigned char)cfg->items[idx].key;
                    console_leave_nav_mode();
                    printf("\033[%d;1H\033[K\n",
                           UI_BODY_START_ROW + UI_BODY_ROWS + 1);
                    return chosen;
                }
            } else if (expanded && ev.col > UI_SIDEBAR_EXPANDED) {
                expanded     = 0;
                pinned_by_kb = 0;
                hover_idx    = -1;
                animate_sidebar_transition(cfg, 0);
            }
            continue;
        }

        if (ev.type == EV_KEY) {
            int k = ev.key;
            if (k == 0x03) {
                console_leave_nav_mode();
                return '0';
            }
            if (k == 0x01 && cfg->allow_secret_admin) {
                console_leave_nav_mode();
                printf("\033[%d;1H\033[K", UI_BODY_START_ROW + UI_BODY_ROWS + 1);
                return 0x01;
            }
            if ((k == '\t' || k == 'm' || k == 'M') && g_is_tty) {
                if (!expanded) {
                    expanded     = 1;
                    pinned_by_kb = 1;
                    animate_sidebar_transition(cfg, 1);
                } else {
                    expanded     = 0;
                    pinned_by_kb = 0;
                    hover_idx    = -1;
                    animate_sidebar_transition(cfg, 0);
                }
                continue;
            }
            for (int i = 0; i < cfg->item_count; i++) {
                if (k == (unsigned char)cfg->items[i].key) {
                    console_leave_nav_mode();
                    if (g_is_tty) {
                        printf("\033[%d;1H\033[K\n",
                               UI_BODY_START_ROW + UI_BODY_ROWS + 1);
                    }
                    return k;
                }
            }
            if (g_is_tty) {
                printf("\033[%d;1H  " ANSI_RED "[!] Invalid choice." ANSI_RESET "\033[K",
                       UI_BODY_START_ROW + UI_BODY_ROWS + 1);
                fflush(stdout);
            } else {
                printf("  [!] Invalid choice.\n");
            }
        }
    }
}

/* Integer menu input with range validation */
static int read_int(const char *prompt, int lo, int hi) {
    int val;
    while (1) {
        printf("  %s", prompt);
        fflush(stdout);
        int ret = scanf("%d", &val);
        if (ret == EOF) return 0;
        if (ret == 1 && val >= lo && val <= hi) {
            clear_input_buffer();
            return val;
        }
        clear_input_buffer();
        printf("  " ANSI_RED "[!] Enter a number between %d and %d." ANSI_RESET "\n", lo, hi);
    }
}

static void press_enter(void) {
    printf("\n  " ANSI_GRAY "Press ENTER to continue..." ANSI_RESET);
    fflush(stdout);
    clear_input_buffer();
}

/* Fast delimiter splitter that handles empty fields */
static int split_line(char *line, char **fields, int max_fields) {
    int count = 0;
    line[strcspn(line, "\r\n")] = '\0';
    char *p = line;
    while (*p && count < max_fields) {
        fields[count++] = p;
        char *sep = strchr(p, '|');
        if (!sep) break;
        *sep = '\0';
        p = sep + 1;
    }
    return count;
}

/* ═══════════════════════════════════════════════════════════════
 *  AUDIT TRAIL & TRANSACTION LOG (Most Recent First)
 * ═══════════════════════════════════════════════════════════════ */
static void hist_push(int ticket_id, const char *action, const char *actor) {
    HistEntry *e = (HistEntry *)malloc(sizeof(HistEntry));
    if (!e) return;
    e->ticket_id = ticket_id;
    strncpy(e->action, action, MAX_STATUS - 1);
    e->action[MAX_STATUS - 1] = '\0';
    strncpy(e->actor, actor, MAX_NAME - 1);
    e->actor[MAX_NAME - 1] = '\0';
    get_timestamp(e->timestamp);
    e->next  = hist_head;
    hist_head = e;
}

static void hist_display(void) {
    HistEntry *cur = hist_head;
    if (!cur) { printf("  [History log is empty]\n"); return; }
    printf("  %-6s %-30s %-20s %s\n", "TktID", "Action", "Actor", "Timestamp");
    print_line('-', 68);
    int idx = 1;
    while (cur && idx <= 50) {          /* show up to 50 most recent */
        printf("  %-6d %-30s %-20s %s\n",
               cur->ticket_id, cur->action, cur->actor, cur->timestamp);
        cur = cur->next;
        idx++;
    }
}

static void hist_free(void) {
    while (hist_head) {
        HistEntry *tmp = hist_head->next;
        free(hist_head);
        hist_head = tmp;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  CENTRAL GRIEVANCE SERVICE RECORD LEDGER
 * ═══════════════════════════════════════════════════════════════ */

/*  Allocate + zero-fill a new node and append it to the TAIL */
static GrievanceNode *ll_append(int ticket_id, int citizen_id,
                                int village_id, int dept_id,
                                int is_hazard, const char *desc,
                                int priority) {
    GrievanceNode *node = (GrievanceNode *)calloc(1, sizeof(GrievanceNode));
    if (!node) { fprintf(stderr, "malloc fail\n"); exit(1); }

    node->ticket_id          = ticket_id;
    node->citizen_id         = citizen_id;
    node->village_id         = village_id;
    node->dept_id            = dept_id;
    node->is_hazard          = is_hazard;
    node->priority           = priority;
    node->days_pending       = 0;
    node->assigned_official_id = -1;
    node->rating             = 0;
    strncpy(node->description, desc, MAX_DESC - 1);
    strncpy(node->status, "Pending", MAX_STATUS - 1);
    get_timestamp(node->submitted_at);
    node->next = NULL;

    if (!ledger_head) {
        ledger_head = node;
    } else {
        GrievanceNode *cur = ledger_head;
        while (cur->next) cur = cur->next;
        cur->next = node;
    }
    return node;
}

/*  Linear search by ticket_id */
static GrievanceNode *ll_find(int ticket_id) {
    GrievanceNode *cur = ledger_head;
    while (cur) {
        if (cur->ticket_id == ticket_id) return cur;
        cur = cur->next;
    }
    return NULL;
}

/*  Duplicate detection: unresolved ticket for same village + dept */
static int ll_duplicate_check(int village_id, int dept_id, int *out_id) {
    GrievanceNode *cur = ledger_head;
    while (cur) {
        if (cur->village_id == village_id &&
            cur->dept_id    == dept_id    &&
            strcmp(cur->status, "Resolved") != 0) {
            *out_id = cur->ticket_id;
            return 1;
        }
        cur = cur->next;
    }
    return 0;
}

/*  Update status field */
static void ll_set_status(GrievanceNode *g, const char *status) {
    strncpy(g->status, status, MAX_STATUS - 1);
    g->status[MAX_STATUS - 1] = '\0';
}

/*  Display a single grievance record (compact) */
static void ll_print_one(const GrievanceNode *g) {
    printf("  Ticket ID     : #%d\n",  g->ticket_id);
    printf("  Village       : %s\n",   villages[g->village_id]);
    printf("  Department    : %s\n",   departments[g->dept_id]);
    printf("  Hazard        : %s\n",   g->is_hazard ? "YES (Life-Safety)" : "No");
    printf("  Priority      : %d\n",   g->priority);
    printf("  Days Pending  : %d\n",   g->days_pending);
    printf("  Status        : %s\n",   g->status);
    printf("  Description   : %s\n",   g->description);
    printf("  Submitted     : %s\n",   g->submitted_at);
    if (g->remarks[0])
        printf("  Remarks       : %s\n", g->remarks);
    if (g->rating > 0) {
        printf("  Citizen Rating: %d/5 stars\n", g->rating);
        printf("  Feedback      : %s\n", g->feedback);
    }
}

/*  Free the entire list */
static void ll_free_all(void) {
    GrievanceNode *cur = ledger_head;
    while (cur) {
        GrievanceNode *tmp = cur->next;
        free(cur);
        cur = tmp;
    }
    ledger_head = NULL;
}

/* ═══════════════════════════════════════════════════════════════
 *  HIGH-SPEED TICKET SEARCH & INDEXING ENGINE
 * ═══════════════════════════════════════════════════════════════ */
static BSTNode *bst_new(int ticket_id, GrievanceNode *ref) {
    BSTNode *n = (BSTNode *)malloc(sizeof(BSTNode));
    if (!n) { fprintf(stderr, "bst malloc\n"); exit(1); }
    n->ticket_id = ticket_id;
    n->ref       = ref;
    n->left = n->right = NULL;
    return n;
}

static BSTNode *bst_insert(BSTNode *root, int tid, GrievanceNode *ref) {
    if (!root) return bst_new(tid, ref);
    if (tid < root->ticket_id)
        root->left  = bst_insert(root->left,  tid, ref);
    else if (tid > root->ticket_id)
        root->right = bst_insert(root->right, tid, ref);
    return root;
}

/*  O(log n) lookup — returns linked-list node pointer or NULL */
static GrievanceNode *bst_search(BSTNode *root, int tid) {
    if (!root) return NULL;
    if (tid == root->ticket_id) return root->ref;
    if (tid  < root->ticket_id) return bst_search(root->left,  tid);
    return bst_search(root->right, tid);
}

static void bst_free(BSTNode *root) {
    if (!root) return;
    bst_free(root->left);
    bst_free(root->right);
    free(root);
}

/* ═══════════════════════════════════════════════════════════════
 *  TASK DISPATCH & EMERGENCY TRIAGE ENGINE (Urgent First)
 * ═══════════════════════════════════════════════════════════════ */

/*  Insert into PQ keeping descending order */
static void pq_insert(int tid, int pri, int days, GrievanceNode *ref) {
    /* Reject if already present */
    PQNode *chk = pq_front;
    while (chk) {
        if (chk->ticket_id == tid) return;
        chk = chk->next;
    }

    PQNode *node = (PQNode *)malloc(sizeof(PQNode));
    if (!node) { fprintf(stderr, "pq malloc\n"); exit(1); }
    node->ticket_id  = tid;
    node->priority   = pri;
    node->days_pending = days;
    node->ref        = ref;
    node->next       = NULL;

    /* Compare: higher priority first; ties broken by more days_pending */
    if (!pq_front ||
        pri > pq_front->priority ||
        (pri == pq_front->priority && days >= pq_front->days_pending)) {
        node->next = pq_front;
        pq_front   = node;
        return;
    }
    PQNode *cur = pq_front;
    while (cur->next &&
           !(pri > cur->next->priority ||
             (pri == cur->next->priority && days >= cur->next->days_pending))) {
        cur = cur->next;
    }
    node->next = cur->next;
    cur->next  = node;
}

/*  Remove a ticket_id from PQ (after resolution / status change) */
static void pq_remove(int tid) {
    if (!pq_front) return;
    if (pq_front->ticket_id == tid) {
        PQNode *tmp = pq_front;
        pq_front = pq_front->next;
        free(tmp);
        return;
    }
    PQNode *cur = pq_front;
    while (cur->next && cur->next->ticket_id != tid) cur = cur->next;
    if (cur->next) {
        PQNode *tmp = cur->next;
        cur->next   = tmp->next;
        free(tmp);
    }
}

/*  Re-insert with updated priority (escalation) */
static void pq_update_priority(int tid, int new_pri, int days) {
    pq_remove(tid);
    GrievanceNode *g = ll_find(tid);
    if (g) pq_insert(tid, new_pri, days, g);
}

/*  Display entire PQ */
static void pq_display(void) {
    PQNode *cur = pq_front;
    if (!cur) { printf("  [No pending tasks to display]\n"); return; }
    printf("  %-6s %-16s %-16s %-5s %-8s %s\n",
           "ID", "Village", "Department", "Pri", "Days", "Status");
    print_line('-', 68);
    while (cur) {
        GrievanceNode *g = cur->ref;
        printf("  %-6d %-16s %-16s %-5d %-8d %s\n",
               cur->ticket_id,
               villages[g->village_id],
               departments[g->dept_id],
               cur->priority,
               cur->days_pending,
               g->status);
        cur = cur->next;
    }
}

static void pq_free(void) {
    while (pq_front) {
        PQNode *tmp = pq_front->next;
        free(pq_front);
        pq_front = tmp;
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  FILE PERSISTENCE  (Human-Readable Plain Text Files)
 * ═══════════════════════════════════════════════════════════════ */

/* ── Citizens (citizens.txt) ── */
static void save_citizens(void) {
    FILE *f = fopen(FILE_CITIZENS, "w");
    if (!f) return;
    fprintf(f, "# CITIZEN REGISTRY\n");
    fprintf(f, "# NEXT_CITIZEN_ID: %d\n", next_citizen_id);
    fprintf(f, "# citizen_id|aadhaar|phone|active|name\n");
    for (int i = 0; i < citizen_count; i++) {
        char nm[MAX_NAME];
        strncpy(nm, citizens[i].name, MAX_NAME - 1);
        nm[MAX_NAME - 1] = '\0';
        sanitize_pipes(nm);
        fprintf(f, "%d|%s|%s|%d|%s\n",
                citizens[i].citizen_id,
                citizens[i].aadhaar,
                citizens[i].phone,
                citizens[i].active,
                nm);
    }
    fclose(f);
}

static void load_citizens(void) {
    FILE *f = fopen(FILE_CITIZENS, "r");
    if (!f) return;
    char line[512];
    citizen_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            if (strncmp(line, "# NEXT_CITIZEN_ID:", 18) == 0) {
                sscanf(line + 18, "%d", &next_citizen_id);
            }
            continue;
        }
        char *flds[6] = {0};
        int nf = split_line(line, flds, 6);
        if (nf < 5 || citizen_count >= MAX_CITIZENS) continue;
        Citizen c;
        memset(&c, 0, sizeof(c));
        c.citizen_id = atoi(flds[0]);
        strncpy(c.aadhaar, flds[1], MAX_AADHAAR - 1);
        strncpy(c.phone,   flds[2], MAX_PHONE - 1);
        c.active     = atoi(flds[3]);
        strncpy(c.name,    flds[4], MAX_NAME - 1);
        citizens[citizen_count++] = c;
    }
    fclose(f);
}

/* ── Officials (officials.txt) ── */
static void save_officials(void) {
    FILE *f = fopen(FILE_OFFICIALS, "w");
    if (!f) return;
    fprintf(f, "# OFFICIAL ACCOUNTS REGISTRY\n");
    fprintf(f, "# NEXT_OFFICIAL_ID: %d\n", next_official_id);
    fprintf(f, "# official_id|username|password|dept_assigned|active|name\n");
    for (int i = 0; i < official_count; i++) {
        char nm[MAX_NAME];
        strncpy(nm, officials[i].name, MAX_NAME - 1);
        nm[MAX_NAME - 1] = '\0';
        sanitize_pipes(nm);
        fprintf(f, "%d|%s|%s|%d|%d|%s\n",
                officials[i].official_id,
                officials[i].username,
                officials[i].password,
                officials[i].dept_assigned,
                officials[i].active,
                nm);
    }
    fclose(f);
}

static void load_officials(void) {
    FILE *f = fopen(FILE_OFFICIALS, "r");
    if (!f) return;
    char line[512];
    official_count = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            if (strncmp(line, "# NEXT_OFFICIAL_ID:", 19) == 0) {
                sscanf(line + 19, "%d", &next_official_id);
            }
            continue;
        }
        char *flds[7] = {0};
        int nf = split_line(line, flds, 7);
        if (nf < 6 || official_count >= MAX_OFFICIALS) continue;
        Official o;
        memset(&o, 0, sizeof(o));
        o.official_id   = atoi(flds[0]);
        strncpy(o.username, flds[1], MAX_NAME - 1);
        strncpy(o.password, flds[2], MAX_NAME - 1);
        o.dept_assigned = atoi(flds[3]);
        o.active        = atoi(flds[4]);
        strncpy(o.name,     flds[5], MAX_NAME - 1);
        officials[official_count++] = o;
    }
    fclose(f);
}

/* ── Grievances (grievances.txt) ── */
static void save_grievances(void) {
    FILE *f = fopen(FILE_GRIEVANCES, "w");
    if (!f) return;
    fprintf(f, "# GRIEVANCES DATABASE\n");
    fprintf(f, "# NEXT_TICKET_ID: %d\n", next_ticket_id);
    fprintf(f, "# ticket_id|citizen_id|village_id|dept_id|is_hazard|priority|days_pending|assigned_official_id|status|submitted_at|resolved_at|rating|description|remarks|feedback\n");
    GrievanceNode *cur = ledger_head;
    while (cur) {
        char desc[MAX_DESC], rem[MAX_REMARKS], fb[MAX_COMMENT];
        strncpy(desc, cur->description, MAX_DESC - 1); desc[MAX_DESC - 1] = '\0';
        strncpy(rem,  cur->remarks,     MAX_REMARKS - 1); rem[MAX_REMARKS - 1] = '\0';
        strncpy(fb,   cur->feedback,    MAX_COMMENT - 1); fb[MAX_COMMENT - 1] = '\0';
        sanitize_pipes(desc);
        sanitize_pipes(rem);
        sanitize_pipes(fb);
        fprintf(f, "%d|%d|%d|%d|%d|%d|%d|%d|%s|%s|%s|%d|%s|%s|%s\n",
                cur->ticket_id,
                cur->citizen_id,
                cur->village_id,
                cur->dept_id,
                cur->is_hazard,
                cur->priority,
                cur->days_pending,
                cur->assigned_official_id,
                cur->status,
                cur->submitted_at,
                cur->resolved_at,
                cur->rating,
                desc,
                rem,
                fb);
        cur = cur->next;
    }
    fclose(f);
}

static void load_grievances(void) {
    FILE *f = fopen(FILE_GRIEVANCES, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') {
            if (strncmp(line, "# NEXT_TICKET_ID:", 17) == 0) {
                sscanf(line + 17, "%d", &next_ticket_id);
            }
            continue;
        }
        char *flds[16] = {0};
        int nf = split_line(line, flds, 16);
        if (nf < 13) continue;

        int tid  = atoi(flds[0]);
        int cid  = atoi(flds[1]);
        int vid  = atoi(flds[2]);
        int did  = atoi(flds[3]);
        int haz  = atoi(flds[4]);
        int pri  = atoi(flds[5]);
        int days = atoi(flds[6]);
        int aoid = atoi(flds[7]);
        const char *stat = flds[8];
        const char *sub  = flds[9];
        const char *res  = flds[10];
        int rat  = atoi(flds[11]);
        const char *desc = flds[12];
        const char *rem  = (nf > 13) ? flds[13] : "";
        const char *fb   = (nf > 14) ? flds[14] : "";

        GrievanceNode *node = ll_append(tid, cid, vid, did, haz, desc, pri);
        ll_set_status(node, stat);
        strncpy(node->submitted_at, sub, MAX_TS - 1);
        strncpy(node->resolved_at,  res, MAX_TS - 1);
        strncpy(node->remarks,      rem, MAX_REMARKS - 1);
        strncpy(node->feedback,     fb,  MAX_COMMENT - 1);
        node->days_pending         = days;
        node->assigned_official_id = aoid;
        node->rating               = rat;

        /* Rebuild BST for every record */
        bst_root = bst_insert(bst_root, tid, node);

        /* Rebuild PQ only for active (non-resolved) tickets */
        if (strcmp(stat, "Resolved") != 0) {
            pq_insert(tid, pri, days, node);
        }
    }
    fclose(f);
}

/* ── History (history.txt) ── */
static void save_history(void) {
    FILE *f = fopen(FILE_HISTORY, "w");
    if (!f) return;
    fprintf(f, "# ACTION / AUDIT HISTORY LOG (Most recent on top)\n");
    fprintf(f, "# ticket_id|action|actor|timestamp\n");
    HistEntry *cur = hist_head;
    while (cur) {
        char act[MAX_STATUS], actor[MAX_NAME];
        strncpy(act,   cur->action, MAX_STATUS - 1); act[MAX_STATUS - 1] = '\0';
        strncpy(actor, cur->actor,  MAX_NAME - 1);   actor[MAX_NAME - 1] = '\0';
        sanitize_pipes(act);
        sanitize_pipes(actor);
        fprintf(f, "%d|%s|%s|%s\n",
                cur->ticket_id,
                act,
                actor,
                cur->timestamp);
        cur = cur->next;
    }
    fclose(f);
}

static void load_history(void) {
    FILE *f = fopen(FILE_HISTORY, "r");
    if (!f) return;
    char line[512];
    HistRecord *temp = (HistRecord *)malloc(1000 * sizeof(HistRecord));
    if (!temp) { fclose(f); return; }
    int count = 0;
    while (fgets(line, sizeof(line), f) && count < 1000) {
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n') continue;
        char *flds[5] = {0};
        int nf = split_line(line, flds, 5);
        if (nf < 4) continue;
        temp[count].ticket_id = atoi(flds[0]);
        strncpy(temp[count].action,    flds[1], MAX_STATUS - 1);
        temp[count].action[MAX_STATUS - 1] = '\0';
        strncpy(temp[count].actor,     flds[2], MAX_NAME - 1);
        temp[count].actor[MAX_NAME - 1] = '\0';
        strncpy(temp[count].timestamp, flds[3], MAX_TS - 1);
        temp[count].timestamp[MAX_TS - 1] = '\0';
        count++;
    }
    fclose(f);

    /* Push in reverse so the first line in the file becomes hist_head */
    for (int i = count - 1; i >= 0; i--) {
        HistEntry *e = (HistEntry *)malloc(sizeof(HistEntry));
        if (!e) break;
        e->ticket_id = temp[i].ticket_id;
        strncpy(e->action,    temp[i].action,    MAX_STATUS - 1);
        strncpy(e->actor,     temp[i].actor,     MAX_NAME - 1);
        strncpy(e->timestamp, temp[i].timestamp, MAX_TS - 1);
        e->action[MAX_STATUS - 1] = e->actor[MAX_NAME - 1] = e->timestamp[MAX_TS - 1] = '\0';
        e->next = hist_head;
        hist_head = e;
    }
    free(temp);
}

static void save_all(void) {
    save_citizens();
    save_officials();
    save_grievances();
    save_history();
}

static void save_all_with_spinner(const char *msg) {
    save_all();
    anim_spinner("Writing Registry Files", msg);
}

static void collect_system_kpis(int *tot, int *pend, int *inp, int *esc, int *res) {
    int t = 0, p = 0, ip = 0, e = 0, r = 0;
    GrievanceNode *cur = ledger_head;
    while (cur) {
        t++;
        if      (strcmp(cur->status, "Pending")     == 0) p++;
        else if (strcmp(cur->status, "In Progress") == 0) ip++;
        else if (strcmp(cur->status, "Escalated")   == 0) e++;
        else if (strcmp(cur->status, "Resolved")    == 0) r++;
        cur = cur->next;
    }
    if (tot)  *tot  = t;
    if (pend) *pend = p;
    if (inp)  *inp  = ip;
    if (esc)  *esc  = e;
    if (res)  *res  = r;
}

/* ── Append-Only Master Record Log (panchayat_records.txt) ── */
static void append_master_record(const char *record_type,
                                 const char *actor,
                                 int ref_id,
                                 const char *details) {
    FILE *f = fopen(FILE_MASTER_LOG, "a");
    if (!f) return;

    /* Write header only when creating a brand-new empty ledger file */
    fseek(f, 0, SEEK_END);
    if (ftell(f) == 0L) {
        fprintf(f, "# ============================================================================\n");
        fprintf(f, "# RURAL PUBLIC SERVICE REQUEST & GRIEVANCE MANAGEMENT SYSTEM\n");
        fprintf(f, "# Master Append-Only Record Ledger (panchayat_records.txt)\n");
        fprintf(f, "# Format: TIMESTAMP | SOURCE | RECORD_TYPE | ID | ACTOR | DETAILS\n");
        fprintf(f, "# ============================================================================\n");
    }

    char ts[MAX_TS];
    get_timestamp(ts);

    char safe_actor[MAX_NAME];
    strncpy(safe_actor, actor ? actor : "System", MAX_NAME - 1);
    safe_actor[MAX_NAME - 1] = '\0';
    sanitize_pipes(safe_actor);

    char safe_details[768];
    strncpy(safe_details, details ? details : "", sizeof(safe_details) - 1);
    safe_details[sizeof(safe_details) - 1] = '\0';
    sanitize_pipes(safe_details);

    fprintf(f, "%s | %-8s | %-18s | #%-6d | %-18s | %s\n",
            ts,
            "CLI",
            record_type ? record_type : "RECORD",
            ref_id,
            safe_actor,
            safe_details);
    fclose(f);
}

/* ═══════════════════════════════════════════════════════════════
 *  DEFAULT DATASET INITIALIZER
 * ═══════════════════════════════════════════════════════════════ */
static void seed_initial_data(void) {
    /* ── Clear everything ── */
    ll_free_all();
    bst_free(bst_root); bst_root = NULL;
    pq_free();
    hist_free();
    citizen_count   = 0;  next_citizen_id  = 1;
    official_count  = 0;  next_official_id = 101;
    next_ticket_id  = TICKET_ID_START;

    char ts[MAX_TS];
    get_timestamp(ts);

    /* ── 50 Citizens (46 Active, 4 Deactivated) ── */
    Citizen c[50] = {
        { 1, "Ramesh Kumar", "1234-5678-9101", "9876543210", 1},
        { 2, "Sunita Devi", "2345-6789-1012", "9823456780", 1},
        { 3, "Mohan Singh", "3456-7891-0123", "9712345670", 1},
        { 4, "Priya Sharma", "4567-8910-1234", "9634567890", 1},
        { 5, "Lakshmi Bai", "5678-9101-2345", "9545678901", 1},
        { 6, "Vikram Singh", "9988-7766-5544", "9123456789", 1},
        { 7, "Arjun Yadav", "6789-1012-3456", "9811223344", 1},
        { 8, "Kamla Verma", "7890-1234-5678", "9845012345", 1},
        { 9, "Harish Chandra", "8901-2345-6789", "9755112233", 1},
        {10, "Geeta Kumari", "9012-3456-7890", "9665443322", 1},
        {11, "Mahesh Prasad", "1122-3344-5566", "9911887766", 1},
        {12, "Rekha Dubey", "2233-4455-6677", "9822998877", 1},
        {13, "Dinesh Chauhan", "3344-5566-7788", "9733009988", 1},
        {14, "Sarita Patel", "4455-6677-8899", "9644110099", 1},
        {15, "Bhagwan Das", "5566-7788-9900", "9555221100", 0},
        {16, "Santosh Tiwari", "2692-3948-5236", "9803099376", 1},
        {17, "Meera Rathore", "2729-4001-5307", "9803293087", 1},
        {18, "Kailash Maurya", "2766-4054-5378", "9803486798", 1},
        {19, "Savita Nishad", "2803-4107-5449", "9803680509", 1},
        {20, "Raghavendra Pal", "2840-4160-5520", "9803874220", 1},
        {21, "Nirmala Devi", "2877-4213-5591", "9804067931", 1},
        {22, "Prakash Vishwakarma", "2914-4266-5662", "9804261642", 1},
        {23, "Anita Rawat", "2951-4319-5733", "9804455353", 1},
        {24, "Umesh Kashyap", "2988-4372-5804", "9804649064", 1},
        {25, "Babita Gautam", "3025-4425-5875", "9804842775", 1},
        {26, "Shyam Sunder", "3062-4478-5946", "9805036486", 1},
        {27, "Kiran Lata", "3099-4531-6017", "9805230197", 1},
        {28, "Brijesh Lodhi", "3136-4584-6088", "9805423908", 1},
        {29, "Girdhari Lal", "3173-4637-6159", "9805617619", 0},
        {30, "Poonam Shukla", "3210-4690-6230", "9805811330", 1},
        {31, "Narendra Kushwaha", "3247-4743-6301", "9806005041", 1},
        {32, "Shashi Kala", "3284-4796-6372", "9806198752", 1},
        {33, "Omprakash Sahu", "3321-4849-6443", "9806392463", 1},
        {34, "Phoolwati Devi", "3358-4902-6514", "9806586174", 1},
        {35, "Devendra Rajput", "3395-4955-6585", "9806779885", 1},
        {36, "Radha Krishnan", "3432-5008-6656", "9806973596", 1},
        {37, "Munna Lal", "3469-5061-6727", "9807167307", 1},
        {38, "Vimla Srivastava", "3506-5114-6798", "9807361018", 0},
        {39, "Jitendra Pandey", "3543-5167-6869", "9807554729", 1},
        {40, "Kavita Solanki", "3580-5220-6940", "9807748440", 1},
        {41, "Gopal Ji Tiwari", "3617-5273-7011", "9807942151", 1},
        {42, "Usha Bharti", "3654-5326-7082", "9808135862", 1},
        {43, "Rakesh Ahirwar", "3691-5379-7153", "9808329573", 1},
        {44, "Pushpa Tomar", "3728-5432-7224", "9808523284", 1},
        {45, "Subhash Baghel", "3765-5485-7295", "9808716995", 1},
        {46, "Malti Sen", "3802-5538-7366", "9808910706", 1},
        {47, "Harprasad Korwa", "3839-5591-7437", "9809104417", 0},
        {48, "Suman Lata Joshi", "3876-5644-7508", "9809298128", 1},
        {49, "Trilok Chand", "3913-5697-7579", "9809491839", 1},
        {50, "Kunti Devi", "3950-5750-7650", "9809685550", 1}
    };
    for (int i = 0; i < 50; i++) citizens[i] = c[i];
    citizen_count = 50;
    next_citizen_id = 51;

    /* ── 25 Officials covering all 5 departments + Block Supervisors ── */
    Official o[25] = {
        {101, "Suresh Verma", "sverma", "pass101",  1, 1},
        {102, "Anita Pandey", "apandey", "pass102",  0, 1},
        {103, "Rajan Tiwari", "rtiwari", "pass103",  2, 1},
        {104, "Meena Kulkarni", "mkulkarni", "pass104",  3, 1},
        {105, "Vinod Yadav", "vyadav", "pass105",  4, 1},
        {106, "Deepak Patel", "dpatel", "pass106",  0, 1},
        {107, "Rajeshwar Singh", "rsingh", "pass107", -1, 1},
        {108, "Kavita Mishra", "kmishra", "pass108",  2, 0},
        {109, "Alok Tripathi", "atripathi", "pass109",  1, 1},
        {110, "Vandana Joshi", "vjoshi", "pass110",  3, 1},
        {111, "Rameshwar Gupta", "rgupta", "pass111",  4, 1},
        {112, "Neha Chaudhary", "nchaudhary", "pass112",  0, 1},
        {113, "Pradeep Thakur", "pthakur", "pass113",  2, 1},
        {114, "Seema Bhargava", "sbhargava", "pass114",  1, 1},
        {115, "Manoj Saxena", "msaxena", "pass115",  3, 1},
        {116, "Kalyani Deshmukh", "kdeshmukh", "pass116",  4, 1},
        {117, "Harinder Gill", "hgill", "pass117",  2, 1},
        {118, "Sunil Banjara", "sbanjara", "pass118",  0, 1},
        {119, "Pooja Rathore", "prathore", "pass119",  1, 1},
        {120, "Nitin Chaturvedi", "nchaturvedi", "pass120",  3, 1},
        {121, "Rekha Sengar", "rsengar", "pass121",  4, 1},
        {122, "Tarun Bhasin", "tbhasin", "pass122",  2, 1},
        {123, "Dr. Asha Nair", "anair", "pass123", -1, 1},
        {124, "Yogeshwar Koli", "ykoli", "pass124",  0, 0},
        {125, "Bharti Menon", "bmenon", "pass125",  4, 1}
    };
    for (int i = 0; i < 25; i++) officials[i] = o[i];
    official_count   = 25;
    next_official_id = 126;

    /* ── 60 Grievances across all statuses, departments & villages ── */
    struct {
        int cid, vid, did, haz, pri, days, aoid;
        const char *desc, *status, *remarks, *sub_at, *res_at;
        int rating; const char *feedback;
    } seeds[] = {
        { 1,1,1,1,10, 9,101,"High-tension live wire fallen across school road. Severe electrocution risk.","Escalated","Sent to divisional lineman team for pole isolation.","2026-11-19 09:15:12","",0,""},
        { 2,2,0,0, 5, 8,102,"Community handpump broken near Ward 3. 300 families without drinking water.","Pending","Spare cylinder indented from block store.","2026-11-20 10:30:00","",0,""},
        { 3,3,2,1,10, 4,103,"Large pothole filled with stagnant water blocking ambulance route to PHC.","In Progress","Road crew dispatched with bitumen mix. ETA 2 days.","2026-11-24 11:20:45","",0,""},
        { 4,4,4,0, 5, 2,105,"MNREGA October wages not credited for 45 registered labourers.","Pending","Muster roll audit queued at Block Welfare Desk.","2026-11-26 14:05:18","",0,""},
        { 5,5,3,0, 5, 5,104,"Open storm drain overflowing beside Government Primary School.","Pending","Sanitation squad scheduled for silt clearance.","2026-11-23 08:50:10","",0,""},
        { 1,1,1,0, 5, 0,101,"All street lights on main bazaar road non-functional for 5 nights.","Resolved","Relay switch and fuse replaced; full stretch restored.","2026-11-18 16:10:00","2026-11-20 17:15:12",4,"Fixed quickly, good work!"},
        { 7,4,0,1,10, 1,106,"Tap water appears brown and contaminated near Harijan Basti.","Pending","Water sample collected for orthotolidine lab check.","2026-11-27 09:40:22","",0,""},
        { 6,1,2,1,10,10,103,"Culvert bridge slab cracked near grain mandi; tractors cannot cross safely.","Escalated","Barricades placed; PWD structural engineer alerted.","2026-11-18 07:45:00","",0,""},
        { 8,2,3,1,10, 3,104,"Choked sewer chamber overflowing behind sub-health centre.","In Progress","Suction tanker and disinfectant spray crew active on site.","2026-11-25 12:15:30","",0,""},
        { 9,3,1,0, 5, 6,101,"Severe voltage fluctuation damaging agricultural irrigation tube-well motors.","In Progress","Load balancer tap inspection underway at 63kVA unit.","2026-11-22 15:40:00","",0,""},
        {10,5,2,0,10, 7,103,"Link road shoulder washed out near bus stand creating steep drop.","Escalated","Retaining stone pitching approved; urgent repair active.","2026-11-21 10:12:05","",0,""},
        {11,4,1,1,10, 2,101,"Sparking feeder transformer leaking hot oil outside Anganwadi gate.","In Progress","Feeder isolated; oil seal gasket replacement started.","2026-11-26 11:05:44","",0,""},
        {12,1,0,0, 5, 4,102,"Overhead tank sluice valve leaking continuously; West Tola supply pressure low.","Pending","Valve assembly requisitioned from PHE store.","2026-11-24 08:25:19","",0,""},
        {13,3,3,0,10, 9,104,"Solid-waste collection tractor inactive for 10 days; waste piling at market chowk.","Escalated","Alternate tractor requisitioned by Block Nodal Officer.","2026-11-19 13:50:11","",0,""},
        {14,2,4,0, 5, 3,105,"Old-age and widow pension verification list pending e-KYC clearance at Block HQ.","In Progress","CSC biometric camp organized for 28 pending applicants.","2026-11-25 16:30:52","",0,""},
        { 6,5,0,0, 5, 1,106,"Solar borewell inverter tripped at northern hamlet drinking point.","Pending","Solar technician inspection scheduled tomorrow morning.","2026-11-27 11:18:09","",0,""},
        { 2,2,2,0, 5, 0,103,"Gravel resurfacing needed along Panchayat Bhavan approach lane.","Resolved","200m gravel compaction and roller levelling completed.","2026-11-14 09:20:00","2026-11-17 16:40:00",5,"Smooth road now, even ambulance enters easily!"},
        { 3,3,0,1,10, 0,102,"Main drinking water pipeline burst near canal crossing.","Resolved","HDPE collar joint clamped and pressure tested cleanly.","2026-11-15 07:30:00","2026-11-16 14:15:00",5,"Water restored within 30 hours. Very responsive team."},
        { 4,4,3,0, 5, 0,104,"Desilting and bleaching of community soak-pit drains required.","Resolved","All 6 soak pits desilted and chlorinated.","2026-11-13 11:00:00","2026-11-16 18:05:00",4,"Clean surroundings now, appreciate the follow-up."},
        { 5,5,1,1,10, 0,101,"Leaning LT electricity pole over livestock shed after windstorm.","Resolved","Pole straightened with concrete collar and guy wire anchored.","2026-11-12 18:10:00","2026-11-14 13:20:00",5,"Saved our cattle shed before nightfall, thank you."},
        { 8,2,1,0, 5, 0,101,"Frequent fuse blowouts along dairy cooperative feeder line.","Resolved","Upgraded HRC fuse rating and tightened loose jumper clamps.","2026-11-16 10:00:00","2026-11-19 15:45:00",3,"Issue solved, though response took three days."},
        { 9,3,4,0, 5, 0,105,"Ration card e-KYC family member linkage stuck for 12 households.","Resolved","District Supply portal sync completed for all 12 families.","2026-11-14 12:40:00","2026-11-18 17:10:00",5,"Everyone received November quota smoothly."},
        {10,1,4,0, 5, 0,105,"PM-KISAN Aadhaar land record seeding camp requested for smallholders.","Resolved","Special Gram Sabha verification camp held; 34 farmers updated.","2026-11-17 10:15:00","2026-11-21 16:00:00",0,""},
        { 1,5,4,0, 5, 0,105,"MNREGA job-card renewal batch pending for Ward 2 agricultural workers.","Resolved","19 renewed job cards issued and verified at Panchayat Kendra.","2026-11-18 14:50:00","2026-11-22 15:25:00",0,""},
        {16,1,4,0, 5, 3,111,"Maternity benefit disbursement delayed for 9 registered mothers in Rampur.","In Progress","PFMS bank account validation cleared for 7 beneficiaries.","2026-11-25 10:15:00","",0,""},
        {17,2,2,0, 5, 2,113,"Broken speed-breaker slabs near Sitapur High School entrance.","Pending","Bitumen patch estimate prepared by junior engineer.","2026-11-26 09:05:00","",0,""},
        {18,3,0,1,10, 4,112,"Main chlorinator dosing pump failed at Gopalpur overhead reservoir.","In Progress","Emergency manual dosing running; standby pump dispatched.","2026-11-24 13:20:00","",0,""},
        {19,3,4,0,10, 8,116,"Crop insurance hailstorm loss survey pending for 18 marginal farmers.","Escalated","Revenue-agriculture joint verification team mobilized.","2026-11-20 11:45:00","",0,""},
        {20,4,3,1,10, 7,110,"Dead livestock carcass disposal pit flooded near Krishnapur eastern bund.","Escalated","Sanitation team with lime & earthmover deployed urgently.","2026-11-21 14:10:00","",0,""},
        {21,5,1,1,10, 1,109,"Snapped service drop cable sparking above Balipur ration shop roof.","Pending","Lineman alerted for morning shutoff and cable replacement.","2026-11-27 16:05:00","",0,""},
        { 8,1,0,0, 5, 0,102,"Check-dam sluice shutter jammed near irrigation tank.","Resolved","Gate lubricated and rubber gasket seated.","2026-11-01 08:00:00","2026-11-03 11:00:00",5,"Water flow stabilized immediately."},
        {11,2,1,1,10, 0,109,"Damaged capacitor bank at rural 11kV substation bay.","Resolved","Capacitor bank unit swapped; power factor restored.","2026-11-02 09:07:00","2026-11-05 12:11:00",4,"Voltage improved significantly after repair."},
        {14,3,2,0, 5, 0,117,"Waterlogged bus-stop layby causing passenger slip hazard.","Resolved","Stone aggregate compacted and side trench cleared.","2026-11-03 10:14:00","2026-11-07 13:22:00",5,"Clean dry platform now, thank you."},
        {17,4,3,0, 5, 0,120,"Blocked culvert grating collecting plastic debris near school.","Resolved","Grating cleared and wire-mesh trap fitted.","2026-11-04 11:21:00","2026-11-06 14:33:00",4,"No stagnation during last rain."},
        {20,5,4,0, 5, 0,125,"Widow pension bank IFSC migration pending post bank merger.","Resolved","Bulk IFSC code update pushed cleanly.","2026-11-05 12:28:00","2026-11-08 15:44:00",5,"Pension credited for this month."},
        {23,1,0,0, 5, 0,106,"Handpump platform apron cracked letting dirty runoff seep in.","Resolved","Concrete apron rebuilt with soak-pit channel.","2026-11-06 13:35:00","2026-11-10 16:55:00",4,"Platform is clean and hygienic now."},
        {26,2,1,0, 5, 0,114,"Loose neutral conductor causing flickering in Harijan Basti.","Resolved","Neutral lug crimped and load rebalanced.","2026-11-07 14:42:00","2026-11-09 17:06:00",5,"Bulbs and fans running steadily."},
        { 2,3,2,1,10, 0,122,"Overgrown thorny scrub narrowing blind turn toward canal road.","Resolved","Roadside jungle clearance completed for 600m.","2026-11-08 15:49:00","2026-11-11 11:17:00",5,"Clear visibility for morning school vans."},
        {32,4,3,0, 5, 0,104,"Mosquito fogging overdue around low-lying paddy fringes.","Resolved","Thermal fogging & larvicide spray conducted twice.","2026-11-09 16:56:00","2026-11-13 12:28:00",4,"Much better at dusk now."},
        {35,5,4,0, 5, 0,125,"PMAY-G second installment geo-tag photo verification stuck.","Resolved","AwaasSoft inspection uploaded on site.","2026-11-10 08:03:00","2026-11-12 13:39:00",5,"Installment approved within 48 hours."},
        {11,1,0,0, 5, 0,112,"Standpost brass tap stolen leaving community point running continuously.","Resolved","Heavy-duty pushcock tap installed and locked.","2026-11-11 09:10:00","2026-11-14 14:50:00",4,"Stops water wastage cleanly."},
        {41,2,1,0, 5, 0,119,"High-mast solar tower battery controller fault at bazaar chowk.","Resolved","MPPT controller replaced under warranty.","2026-11-12 10:17:00","2026-11-16 15:01:00",5,"Chowk brightly lit all night."},
        {44,3,2,0, 5, 0,103,"Missing kilometer milestones and curve warning boards on link road.","Resolved","Reflective signage and painted stones installed.","2026-11-13 11:24:00","2026-11-15 16:12:00",4,"Much safer for night riders."},
        { 1,4,3,1,10, 0,110,"Community toilet overhead tank float valve broken.","Resolved","PVC ballcock replaced and plumbing checked.","2026-11-14 12:31:00","2026-11-17 17:23:00",3,"Works fine now, took a few reminders."},
        { 4,5,4,0, 5, 0,125,"PM-SYM labour e-Shram card printouts pending at Kendra.","Resolved","Kendra batch printed and distributed 26 cards.","2026-11-15 13:38:00","2026-11-19 11:34:00",5,"Helpful staff and zero hassle."},
        { 7,1,0,0, 5, 0,118,"Fluoride filtration unit media backwash overdue at school plant.","Resolved","Activated alumina media regenerated and tested.","2026-11-01 14:45:00","2026-11-03 12:45:00",5,"Water tastes clean again."},
        {10,2,1,0, 5, 0,101,"Bird-fault tripping on Rampur-Sitapur 11kV spur feeder.","Resolved","Insulator hoods installed along 14 span poles.","2026-11-02 15:52:00","2026-11-05 13:56:00",4,"No trippings in five days."},
        {13,3,2,0, 5, 0,113,"Wooden footbridge planks rotted over seasonal nallah.","Resolved","Replaced with bolted precast concrete slabs.","2026-11-03 16:59:00","2026-11-07 14:07:00",5,"Very sturdy work by road team."},
        {16,4,3,0, 5, 0,115,"Compost pit shed tin roof blown off in squall.","Resolved","Corrugated sheets re-bolted with J-hooks.","2026-11-04 08:06:00","2026-11-06 15:18:00",4,"Protected before rains started."},
        {19,5,4,1,10, 0,125,"Student pre-matric scholarship Aadhaar seeding correction needed.","Resolved","Verified 17 student bank mappings with school.","2026-11-05 09:13:00","2026-11-08 16:29:00",5,"Payments cleared cleanly."},
        {22,1,0,0, 5, 0,102,"Public drinking trough tap leaking beside cattle fair ground.","Resolved","Installed heavy brass bibcock with masonry collar.","2026-11-06 10:20:00","2026-11-10 17:40:00",4,"Dry ground now, clean water for cattle."},
        {25,2,1,0, 5, 0,109,"Damaged meter terminal box after surge at Panchayat well.","Resolved","Weatherproof polycarbonate box & MCB fitted.","2026-11-07 11:27:00","2026-11-09 11:51:00",5,"Pump running safely again."},
        {28,3,2,0, 5, 0,117,"Collapsed retaining brick wall along steep pond embankment.","Resolved","Constructed 18m stepped stone retaining wall.","2026-11-08 12:34:00","2026-11-11 12:02:00",5,"Pathway feels completely secure now."},
        {31,4,3,0, 5, 0,120,"Public handwashing unit drainpipe clogged at Anganwadi.","Resolved","Replaced 40mm trap pipe and cleared gully.","2026-11-09 13:41:00","2026-11-13 13:13:00",4,"Children can wash hands easily."},
        {34,5,4,0, 5, 0,125,"KCC crop loan renewal no-dues certificate camp requested.","Resolved","Joint revenue-bank counter cleared 22 files.","2026-11-10 14:48:00","2026-11-12 14:24:00",5,"Saved multiple trips to block town."},
        {37,1,0,1,10, 0,106,"Turbid borewell supply after submersible motor vibration.","Resolved","Pulled riser pipe, replaced worn brass bush.","2026-11-11 15:55:00","2026-11-14 15:35:00",4,"Clear water pumped cleanly."},
        {40,2,1,0, 5, 0,114,"Uninsulated jumper sparking near threshing yard.","Resolved","Replaced with shrouded ABC connector clamp.","2026-11-12 16:02:00","2026-11-16 16:46:00",5,"Prevented fire risk during harvest."},
        { 2,3,2,0, 5, 0,122,"Loose cobbles on inner lane outside cooperative bank.","Resolved","Re-laid interlocking pavers over compacted sand.","2026-11-13 08:09:00","2026-11-15 17:57:00",0,""},
        {46,4,3,0, 5, 0,104,"Sanitation tricycle tyres punctured blocking ward waste pickup.","Resolved","Solid puncture-proof tyres fitted across 4 carts.","2026-11-14 09:16:00","2026-11-17 11:08:00",4,"Daily door-to-door pickup resumed."},
        { 6,5,4,0, 5, 0,125,"Ujjwala LPG subsidy bank linkage verification batch.","Resolved","Completed NPCI mapper check for 15 families.","2026-11-15 10:23:00","2026-11-19 12:19:00",0,""}
    };
    int n_seeds = (int)(sizeof(seeds) / sizeof(seeds[0]));
    for (int i = 0; i < n_seeds; i++) {
        GrievanceNode *g = ll_append(
            next_ticket_id, seeds[i].cid,
            seeds[i].vid,   seeds[i].did,
            seeds[i].haz,   seeds[i].desc,
            seeds[i].pri);

        ll_set_status(g, seeds[i].status);
        g->days_pending         = seeds[i].days;
        g->assigned_official_id = seeds[i].aoid;
        strncpy(g->remarks, seeds[i].remarks, MAX_REMARKS - 1);
        g->rating = seeds[i].rating;
        strncpy(g->feedback, seeds[i].feedback, MAX_COMMENT - 1);
        strncpy(g->submitted_at, seeds[i].sub_at[0] ? seeds[i].sub_at : ts, MAX_TS - 1);
        if (seeds[i].res_at[0])
            strncpy(g->resolved_at, seeds[i].res_at, MAX_TS - 1);

        bst_root = bst_insert(bst_root, next_ticket_id, g);
        if (strcmp(seeds[i].status, "Resolved") != 0)
            pq_insert(next_ticket_id, seeds[i].pri, seeds[i].days, g);

        hist_push(next_ticket_id, seeds[i].status, "System");
        next_ticket_id++;
    }

    save_all();
    append_master_record("SYSTEM_SEED", "System", n_seeds,
                         "Initialized baseline citizens, officials, and grievance records");
    printf("\n  [SYSTEM] Service records initialized successfully.\n");
    printf("  Citizens: %d | Officials: %d | Grievances: %d\n",
           citizen_count, official_count, n_seeds);
}

/* ═══════════════════════════════════════════════════════════════
 *  CITIZEN PORTAL
 * ═══════════════════════════════════════════════════════════════ */

/* Find citizen by Aadhaar + phone (login credentials) */
static Citizen *citizen_find_by_creds(const char *aadhaar, const char *phone) {
    for (int i = 0; i < citizen_count; i++) {
        if (citizens[i].active &&
            strcmp(citizens[i].aadhaar, aadhaar) == 0 &&
            strcmp(citizens[i].phone,   phone)   == 0)
            return &citizens[i];
    }
    return NULL;
}

static Citizen *citizen_find_by_id(int id) {
    for (int i = 0; i < citizen_count; i++)
        if (citizens[i].active && citizens[i].citizen_id == id)
            return &citizens[i];
    return NULL;
}

/* Register a new citizen */
static void citizen_register(void) {
    if (citizen_count >= MAX_CITIZENS) {
        printf("  [!] Citizen registry is full.\n"); return;
    }
    printf("\n");
    print_subheader("New Citizen Registration");

    Citizen nc;
    memset(&nc, 0, sizeof(nc));
    nc.citizen_id = next_citizen_id;
    nc.active     = 1;

    read_line("Full Name        : ", nc.name,    MAX_NAME);
    read_line("Aadhaar Number   : ", nc.aadhaar, MAX_AADHAAR);
    read_line("Phone Number     : ", nc.phone,   MAX_PHONE);

    /* Check for duplicate Aadhaar */
    for (int i = 0; i < citizen_count; i++) {
        if (strcmp(citizens[i].aadhaar, nc.aadhaar) == 0) {
            printf("  [!] Aadhaar already registered. Please login.\n");
            return;
        }
    }

    citizens[citizen_count++] = nc;
    next_citizen_id++;
    save_citizens();
    {
        char logbuf[512];
        snprintf(logbuf, sizeof(logbuf),
                 "Name: %s; Aadhaar: %s; Phone: %s; Status: Active",
                 nc.name, nc.aadhaar, nc.phone);
        append_master_record("CITIZEN_REGISTER", nc.name, nc.citizen_id, logbuf);
    }
    printf("\n  [OK] Registration successful! Citizen ID: %d\n", nc.citizen_id);
    printf("  Login using your Aadhaar + Phone number.\n");
}

/* ── 1. Submit Grievance (with strict duplicate rejection) ── */
static void citizen_submit(Citizen *cit) {
    printf("\n");
    print_subheader("Submit New Grievance");

    /* Choose village */
    printf("  Select Village:\n");
    for (int i = 1; i < NUM_VILLAGES; i++)   /* skip 0=Block HQ */
        printf("    %d. %s\n", i, villages[i]);
    int vid = read_int("Village (1-5): ", 1, NUM_VILLAGES - 1);

    /* Choose department */
    printf("  Select Category/Department:\n");
    for (int i = 0; i < NUM_DEPARTMENTS; i++)
        printf("    %d. %s\n", i, departments[i]);
    int did = read_int("Category (0-4): ", 0, NUM_DEPARTMENTS - 1);

    /* ── DUPLICATE REJECTION ──
       If an unresolved ticket exists for same Village + Category, REJECT. */
    int existing_id = 0;
    if (ll_duplicate_check(vid, did, &existing_id)) {
        printf("\n  ╔══════════════════════════════════════════════════════╗\n");
        printf("  ║  DUPLICATE SUBMISSION REJECTED                       ║\n");
        printf("  ╚══════════════════════════════════════════════════════╝\n");
        printf("  An unresolved complaint for  %-16s\n", departments[did]);
        printf("  in %-20s already exists.\n", villages[vid]);
        printf("  Existing Ticket ID : #%d\n", existing_id);
        printf("  Please track that ticket or wait for it to be resolved\n");
        printf("  before submitting a new one for the same issue.\n");
        return;
    }

    char desc[MAX_DESC];
    read_line("Description (what is the problem?): ", desc, MAX_DESC);

    printf("  Is this a life-safety hazard? (1=Yes / 0=No): ");
    int haz;
    scanf("%d", &haz);
    clear_input_buffer();
    if (haz != 0 && haz != 1) haz = 0;

    int pri = haz ? PRIORITY_EMERGENCY : PRIORITY_NORMAL;

    GrievanceNode *g = ll_append(next_ticket_id, cit->citizen_id,
                                  vid, did, haz, desc, pri);
    bst_root = bst_insert(bst_root, next_ticket_id, g);
    pq_insert(next_ticket_id, pri, 0, g);
    hist_push(next_ticket_id, "Submitted", cit->name);

    printf("\n  ╔══════════════════════════════════════════════════════╗\n");
    printf("  ║  GRIEVANCE SUBMITTED — RECEIPT                       ║\n");
    printf("  ╚══════════════════════════════════════════════════════╝\n");
    printf("  Ticket ID     : #%d\n",  next_ticket_id);
    printf("  Category      : %s\n",  departments[did]);
    printf("  Village       : %s\n",  villages[vid]);
    printf("  Priority      : %d  (%s)\n", pri,
           haz ? "EMERGENCY" : "Normal");
    printf("  Status        : Pending\n");
    printf("  Submitted At  : %s\n",  g->submitted_at);

    next_ticket_id++;
    save_all();
    {
        char logbuf[768];
        snprintf(logbuf, sizeof(logbuf),
                 "Village: %s; Dept: %s; Priority: %d (%s); Status: Pending; Desc: %s",
                 villages[vid], departments[did], pri,
                 haz ? "Hazard" : "Normal", desc);
        append_master_record("GRIEVANCE_SUBMIT", cit->name, g->ticket_id, logbuf);
    }
}

/* ── 2. Track Grievance Status by Ticket ID ── */
static void citizen_track(void) {
    printf("\n");
    print_subheader("Track Grievance");
    printf("  Enter your ticket number: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = bst_search(bst_root, tid);  /* O(log n) */
    if (!g) {
        printf("  [!] Ticket #%d not found in the system.\n", tid);
        return;
    }
    printf("\n  Search Result:\n");
    print_line('-', 50);
    ll_print_one(g);
}

/* ── 3. View My Grievances ── */
static void citizen_view_mine(Citizen *cit) {
    printf("\n");
    print_subheader("My Grievances");

    GrievanceNode *cur = ledger_head;
    int found = 0;
    printf("  %-6s %-16s %-16s %-5s %-8s %s\n",
           "ID", "Village", "Department", "Pri", "Days", "Status");
    print_line('-', 68);
    while (cur) {
        if (cur->citizen_id == cit->citizen_id) {
            printf("  %-6d %-16s %-16s %-5d %-8d %s\n",
                   cur->ticket_id,
                   villages[cur->village_id],
                   departments[cur->dept_id],
                   cur->priority,
                   cur->days_pending,
                   cur->status);
            found = 1;
        }
        cur = cur->next;
    }
    if (!found) printf("  [You have no grievances on record]\n");
}

/* ── 4. Reopen Grievance ──
   Rules: ticket must be "Resolved" AND belong to this citizen.
   Effect: status → "Pending", days_pending = 0,
           priority recalculated from hazard flag,
           re-inserted into Priority Queue.
*/
static void citizen_reopen(Citizen *cit) {
    printf("\n");
    print_subheader("Reopen a Resolved Grievance");
    printf("  Enter ticket number to reopen: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = bst_search(bst_root, tid);
    if (!g) { printf("  [!] Ticket #%d not found.\n", tid); return; }
    if (g->citizen_id != cit->citizen_id) {
        printf("  [!] You can only reopen your own tickets.\n"); return;
    }
    if (strcmp(g->status, "Resolved") != 0) {
        printf("  [!] Ticket #%d is not resolved (Status: %s).\n",
               tid, g->status);
        return;
    }

    /* Recalculate priority, reset days, change status */
    g->priority     = g->is_hazard ? PRIORITY_EMERGENCY : PRIORITY_NORMAL;
    g->days_pending = 0;
    g->rating       = 0;           /* clear old feedback so citizen can re-rate */
    g->feedback[0]  = '\0';
    g->resolved_at[0] = '\0';
    ll_set_status(g, "Pending");
    pq_insert(tid, g->priority, 0, g);    /* re-enter Priority Queue */

    hist_push(tid, "Reopened by Citizen", cit->name);
    save_all();
    {
        char logbuf[256];
        snprintf(logbuf, sizeof(logbuf),
                 "Status: Pending; Priority: %d; Reopened by owner", g->priority);
        append_master_record("GRIEVANCE_REOPEN", cit->name, tid, logbuf);
    }

    printf("  [OK] Ticket #%d has been reopened.\n", tid);
    printf("  Status reset to: Pending | Priority: %d\n", g->priority);
    printf("  It will re-enter the official's task queue.\n");
}

/* ── 5. Give Feedback ── */
static void citizen_feedback(Citizen *cit) {
    printf("\n");
    print_subheader("Give Feedback on Resolved Ticket");
    printf("  Enter ticket number: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = bst_search(bst_root, tid);
    if (!g) { printf("  [!] Ticket not found.\n"); return; }
    if (g->citizen_id != cit->citizen_id) {
        printf("  [!] You can only rate your own tickets.\n"); return;
    }
    if (strcmp(g->status, "Resolved") != 0) {
        printf("  [!] Feedback is only available for Resolved tickets.\n");
        printf("  Current status: %s\n", g->status);
        return;
    }
    if (g->rating > 0) {
        printf("  [!] You have already rated this ticket (%d/5 stars).\n",
               g->rating);
        return;
    }

    g->rating = read_int("Rating (1-5 stars): ", 1, 5);
    read_line("Comment (optional): ", g->feedback, MAX_COMMENT);
    hist_push(tid, "Feedback Submitted", cit->name);
    save_all();
    {
        char logbuf[512];
        snprintf(logbuf, sizeof(logbuf),
                 "Rating: %d/5; Comment: %s",
                 g->rating, g->feedback[0] ? g->feedback : "None");
        append_master_record("FEEDBACK_SUBMIT", cit->name, tid, logbuf);
    }
    printf("  [OK] Thank you! Feedback recorded.\n");
}

/* ── Citizen Portal main loop ── */
static void portal_citizen(void) {
    printf("\n");
    print_subheader("Citizen Portal — Login / Register");
    printf("  1. Register (New Citizen)\n");
    printf("  2. Login\n");
    printf("  0. Back\n");
    int ch = read_int("Choice: ", 0, 2);
    if (ch == 0) return;
    if (ch == 1) { citizen_register(); return; }

    /* Login */
    char aadhaar[MAX_AADHAAR], phone[MAX_PHONE];
    read_line("Aadhaar Number  : ", aadhaar, MAX_AADHAAR);
    read_line("Phone Number    : ", phone,   MAX_PHONE);

    anim_spinner("Verifying Citizen Profile", NULL);
    Citizen *cit = citizen_find_by_creds(aadhaar, phone);
    if (!cit) {
        printf("  " ANSI_RED "[!] Invalid credentials." ANSI_RESET "\n");
        press_enter();
        return;
    }
    printf("  " ANSI_GREEN "[OK] Welcome, %s!" ANSI_RESET "\n", cit->name);
    anim_typewriter(ANSI_BCYAN, "CITIZEN PORTAL -- PUBLIC SERVICE DESK");

    static const SidebarItem cit_items[] = {
        { '1', "Submit Grievance",   NULL },
        { '2', "Track Grievance",    NULL },
        { '3', "My Grievances",      NULL },
        { '4', "Reopen Grievance",   NULL },
        { '5', "Give Feedback",      NULL },
        { '0', "Logout",             NULL }
    };

    int key;
    do {
        int mine = 0, my_act = 0, my_res = 0, my_unrated = 0;
        GrievanceNode *cur = ledger_head;
        while (cur) {
            if (cur->citizen_id == cit->citizen_id) {
                mine++;
                if (strcmp(cur->status, "Resolved") == 0) {
                    my_res++;
                    if (cur->rating == 0) my_unrated++;
                } else {
                    my_act++;
                }
            }
            cur = cur->next;
        }

        char sub[192], ws1[80], ws2[80], ws3[80], ws4[80], ws5[80], st1[40], st2[40];
        snprintf(sub, sizeof(sub), "Logged in: %s | Aadhaar: %s", cit->name, cit->aadhaar);
        snprintf(ws1, sizeof(ws1), "Citizen ID   : #%d  |  Phone: %s", cit->citizen_id, cit->phone);
        snprintf(ws2, sizeof(ws2), "Total Filed  : %d Complaints", mine);
        snprintf(ws3, sizeof(ws3), "Active Cases : %d   |   Resolved: %d", my_act, my_res);
        snprintf(ws4, sizeof(ws4), "Feedback Due : %d Resolved Tickets Unrated", my_unrated);
        snprintf(ws5, sizeof(ws5), "Note: Duplicate unresolved Village+Dept issues are blocked.");
        snprintf(st1, sizeof(st1), " Filed: %-4d Act: %-3d", mine, my_act);
        snprintf(st2, sizeof(st2), " Hover Left / Tab");

        SidebarLayoutConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.portal_title         = "CITIZEN DASHBOARD  |  GRAM PANCHAYAT PORTAL";
        cfg.subtitle             = sub;
        cfg.workspace_title      = "Citizen Service Overview";
        cfg.workspace_lines[0]   = ws1;
        cfg.workspace_lines[1]   = "─────────────────────────────────────────────────────────";
        cfg.workspace_lines[2]   = ws2;
        cfg.workspace_lines[3]   = ws3;
        cfg.workspace_lines[4]   = ws4;
        cfg.workspace_lines[5]   = "";
        cfg.workspace_lines[6]   = ws5;
        cfg.workspace_line_count = 7;
        cfg.items                = cit_items;
        cfg.item_count           = (int)(sizeof(cit_items) / sizeof(cit_items[0]));
        cfg.stats_line1          = st1;
        cfg.stats_line2          = st2;
        cfg.allow_secret_admin   = 0;

        key = run_sidebar_menu(&cfg);
        switch (key) {
            case '1': citizen_submit(cit);    save_all_with_spinner("Citizen records synced."); break;
            case '2': citizen_track();        break;
            case '3': citizen_view_mine(cit); break;
            case '4': citizen_reopen(cit);    save_all_with_spinner("Ticket reopened & saved."); break;
            case '5': citizen_feedback(cit);  save_all_with_spinner("Feedback saved."); break;
            case '0': printf("  " ANSI_GREEN "Logged out safely." ANSI_RESET "\n"); break;
        }
        if (key != '0') press_enter();
    } while (key != '0');
}

/* ═══════════════════════════════════════════════════════════════
 *  OFFICIAL (FIELD TECHNICIAN) PORTAL
 * ═══════════════════════════════════════════════════════════════ */

static Official *official_find_by_creds(const char *uname, const char *pass) {
    for (int i = 0; i < official_count; i++) {
        if (officials[i].active &&
            strcmp(officials[i].username, uname) == 0 &&
            strcmp(officials[i].password, pass)  == 0)
            return &officials[i];
    }
    return NULL;
}

/* ── 1. View Assigned Tasks (all non-resolved from their dept) ── */
static void official_view_assigned(Official *off) {
    printf("\n");
    print_subheader("View Assigned Tasks");

    GrievanceNode *cur = ledger_head;
    int count = 0;
    printf("  %-6s %-16s %-16s %-5s %-8s %s\n",
           "ID", "Village", "Department", "Pri", "Days", "Status");
    print_line('-', 68);
    while (cur) {
        int dept_match = (off->dept_assigned == -1) ||
                         (cur->dept_id == off->dept_assigned);
        if (dept_match && strcmp(cur->status, "Resolved") != 0) {
            printf("  %-6d %-16s %-16s %-5d %-8d %s\n",
                   cur->ticket_id,
                   villages[cur->village_id],
                   departments[cur->dept_id],
                   cur->priority,
                   cur->days_pending,
                   cur->status);
            count++;
        }
        cur = cur->next;
    }
    if (!count) printf("  [No active tickets in your department]\n");
    else {
        print_line('-', 68);
        printf("  Total Active Assigned Tickets: %d\n", count);
    }
}

/* ── 2. View by Priority (Urgent Task Triage) ── */
static void official_view_priority(Official *off) {
    printf("\n");
    print_subheader("View by Priority (Urgent First)");

    PQNode *cur = pq_front;
    int found = 0, rank = 1;
    printf("  %-4s %-6s %-16s %-16s %-5s %-8s %s\n",
           "Rank","ID","Village","Department","Pri","Days","Status");
    print_line('-', 72);
    while (cur) {
        GrievanceNode *g = cur->ref;
        int dept_match = (off->dept_assigned == -1) ||
                         (g->dept_id == off->dept_assigned);
        if (dept_match) {
            printf("  %-4d %-6d %-16s %-16s %-5d %-8d %s\n",
                   rank++, cur->ticket_id,
                   villages[g->village_id],
                   departments[g->dept_id],
                   cur->priority,
                   cur->days_pending,
                   g->status);
            found = 1;
        }
        cur = cur->next;
    }
    if (!found) printf("  [No active tickets in your department]\n");
}

/* ── 3. View Full Details of a Ticket ── */
static void official_view_details(void) {
    printf("\n");
    print_subheader("View Ticket Details");
    printf("  Enter ticket number: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = bst_search(bst_root, tid);
    if (!g) { printf("  [!] Ticket #%d not found.\n", tid); return; }

    Citizen *cit = citizen_find_by_id(g->citizen_id);
    printf("\n");
    print_line('-', 50);
    ll_print_one(g);
    if (cit) {
        printf("  Citizen Name  : %s\n", cit->name);
        printf("  Citizen Phone : %s\n", cit->phone);
    }
}

/* ── 4. Update Status & Remarks ── */
static void official_update_status(Official *off) {
    printf("\n");
    print_subheader("Update Status & Remarks");
    printf("  Enter ticket number: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = ll_find(tid);
    if (!g) { printf("  [!] Ticket #%d not found.\n", tid); return; }
    if (strcmp(g->status, "Resolved") == 0) {
        printf("  [!] Ticket is already resolved.\n"); return;
    }
    int dept_ok = (off->dept_assigned == -1) ||
                  (g->dept_id == off->dept_assigned);
    if (!dept_ok) {
        printf("  [!] Ticket belongs to %s — not your department.\n",
               departments[g->dept_id]);
        return;
    }

    printf("  Current Status : %s\n", g->status);
    printf("  New Status:\n");
    printf("    1. In Progress\n");
    printf("    2. Pending\n");
    printf("    3. Escalated\n");
    int sc = read_int("  Choice: ", 1, 3);
    const char *new_status[] = {"In Progress", "Pending", "Escalated"};
    if (sc == 3) {
        g->priority = PRIORITY_EMERGENCY;
    }
    ll_set_status(g, new_status[sc - 1]);
    pq_update_priority(tid, g->priority, g->days_pending);

    char remarks[MAX_REMARKS];
    read_line("Add/Update Remarks: ", remarks, MAX_REMARKS);
    if (remarks[0])
        strncpy(g->remarks, remarks, MAX_REMARKS - 1);

    hist_push(tid, new_status[sc - 1], off->name);
    save_all();
    {
        char logbuf[512];
        snprintf(logbuf, sizeof(logbuf),
                 "Status: %s; Priority: %d; Remarks: %s",
                 g->status, g->priority, g->remarks[0] ? g->remarks : "None");
        append_master_record("STATUS_UPDATE", off->name, tid, logbuf);
    }
    printf("  [OK] Status updated to: %s\n", g->status);
}

/* ── 5. Escalate Critical / Overdue ── */
static void official_escalate(Official *off) {
    printf("\n");
    print_subheader("Escalate Critical / Overdue Ticket");
    printf("  Enter ticket number to escalate: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = ll_find(tid);
    if (!g) { printf("  [!] Ticket not found.\n"); return; }
    if (strcmp(g->status, "Resolved") == 0) {
        printf("  [!] Cannot escalate a resolved ticket.\n"); return;
    }
    int dept_ok = (off->dept_assigned == -1) ||
                  (g->dept_id == off->dept_assigned);
    if (!dept_ok) {
        printf("  [!] Ticket not in your department.\n"); return;
    }

    g->priority = PRIORITY_EMERGENCY;
    ll_set_status(g, "Escalated");
    pq_remove(tid);
    pq_insert(tid, PRIORITY_EMERGENCY, g->days_pending, g);
    hist_push(tid, "Escalated by Official", off->name);
    save_all();
    {
        char logbuf[256];
        snprintf(logbuf, sizeof(logbuf),
                 "Status: Escalated; Priority: %d; escalated by Field Officer",
                 PRIORITY_EMERGENCY);
        append_master_record("GRIEVANCE_ESCALATE", off->name, tid, logbuf);
    }
    printf("  [OK] Ticket #%d escalated to Priority 10.\n", tid);
}

/* ── 6. Mark as Resolved ── */
static void official_resolve(Official *off) {
    printf("\n");
    print_subheader("Mark Ticket as Resolved");
    printf("  Enter ticket number: ");
    int tid;
    scanf("%d", &tid);
    clear_input_buffer();

    GrievanceNode *g = ll_find(tid);
    if (!g) { printf("  [!] Ticket not found.\n"); return; }
    if (strcmp(g->status, "Resolved") == 0) {
        printf("  [!] Ticket is already resolved.\n"); return;
    }
    int dept_ok = (off->dept_assigned == -1) ||
                  (g->dept_id == off->dept_assigned);
    if (!dept_ok) {
        printf("  [!] Ticket not in your department.\n"); return;
    }

    char remarks[MAX_REMARKS];
    read_line("Resolution Remarks: ", remarks, MAX_REMARKS);
    if (remarks[0]) strncpy(g->remarks, remarks, MAX_REMARKS - 1);

    ll_set_status(g, "Resolved");
    get_timestamp(g->resolved_at);
    pq_remove(tid);     /* remove from active Priority Queue */
    hist_push(tid, "Resolved", off->name);
    save_all();
    {
        char logbuf[512];
        snprintf(logbuf, sizeof(logbuf),
                 "Status: Resolved; ResolvedAt: %s; Remarks: %s",
                 g->resolved_at, g->remarks[0] ? g->remarks : "None");
        append_master_record("GRIEVANCE_RESOLVE", off->name, tid, logbuf);
    }

    printf("  [OK] Ticket #%d marked as Resolved.\n", tid);
    printf("  Citizen can now submit feedback.\n");
}

/* ── 7. View Archive / History ── */
static void official_view_archive(Official *off) {
    printf("\n");
    print_subheader("Archive — Resolved Tickets (Your Department)");

    GrievanceNode *cur = ledger_head;
    int found = 0;
    printf("  %-6s %-16s %-16s %-5s %s\n",
           "ID", "Village", "Department", "★", "Resolved At");
    print_line('-', 68);
    while (cur) {
        int dept_match = (off->dept_assigned == -1) ||
                         (cur->dept_id == off->dept_assigned);
        if (dept_match && strcmp(cur->status, "Resolved") == 0) {
            printf("  %-6d %-16s %-16s %-5d %s\n",
                   cur->ticket_id,
                   villages[cur->village_id],
                   departments[cur->dept_id],
                   cur->rating,
                   cur->resolved_at[0] ? cur->resolved_at : "N/A");
            found = 1;
        }
        cur = cur->next;
    }
    if (!found) printf("  [No resolved tickets yet]\n");

    printf("\n  View Action History Log? (1=Yes/0=No): ");
    int yn;
    scanf("%d", &yn);
    clear_input_buffer();
    if (yn == 1) hist_display();
}

/* ── Official Portal main loop ── */
static void portal_official(void) {
    printf("\n");
    print_subheader("Official (Field Technician) Login");

    char uname[MAX_NAME], pass[MAX_NAME];
    Official *off = NULL;
    int attempts = 0;

    while (attempts < 3) {
        read_line("Username : ", uname, MAX_NAME);
        read_password("Password : ", pass, MAX_NAME);

        anim_spinner("Authenticating Officer", NULL);
        off = official_find_by_creds(uname, pass);
        memset(pass, 0, sizeof(pass));
        if (off) break;

        attempts++;
        if (attempts >= 3) {
            printf("  " ANSI_RED ANSI_BOLD
                   "[SECURITY ALERT] Maximum login attempts exceeded. System locked."
                   ANSI_RESET "\n");
            fflush(stdout);
            exit(1);
        }
        printf("  " ANSI_AMBER "[!] Invalid credentials. (%d/3 attempts failed)"
               ANSI_RESET "\n", attempts);
    }

    const char *dept_name = (off->dept_assigned == -1)
                            ? "All Departments"
                            : departments[off->dept_assigned];
    printf("  " ANSI_GREEN "[OK] Welcome, %s! Department: %s" ANSI_RESET "\n",
           off->name, dept_name);
    anim_typewriter(ANSI_BCYAN, "FIELD OFFICER WORKBENCH -- DISPATCH CONSOLE");

    static const SidebarItem off_items[] = {
        { '1', "Assigned Tasks",     NULL },
        { '2', "Priority Triage",    NULL },
        { '3', "Ticket Details",     NULL },
        { '4', "Update Status",      NULL },
        { '5', "Escalate Urgent",    NULL },
        { '6', "Mark Resolved",      NULL },
        { '7', "Archive / History",  NULL },
        { '0', "Logout",             NULL }
    };

    int key;
    do {
        int d_act = 0, d_esc = 0, d_res = 0, d_pri10 = 0;
        GrievanceNode *cur = ledger_head;
        while (cur) {
            if (off->dept_assigned == -1 || cur->dept_id == off->dept_assigned) {
                if (strcmp(cur->status, "Resolved") == 0) {
                    d_res++;
                } else {
                    d_act++;
                    if (strcmp(cur->status, "Escalated") == 0) d_esc++;
                    if (cur->priority >= PRIORITY_EMERGENCY)   d_pri10++;
                }
            }
            cur = cur->next;
        }

        char sub[192], ws1[80], ws2[80], ws3[80], ws4[80], ws5[80], st1[40], st2[40];
        snprintf(sub, sizeof(sub), "Officer: %s (#%d) | Scope: %s",
                 off->name, off->official_id, dept_name);
        snprintf(ws1, sizeof(ws1), "Active Queue       : %d Tickets", d_act);
        snprintf(ws2, sizeof(ws2), "Priority 10 Urgent : %d Cases", d_pri10);
        snprintf(ws3, sizeof(ws3), "Escalated Alerts   : %d Cases", d_esc);
        snprintf(ws4, sizeof(ws4), "Resolved Archive   : %d Completed", d_res);
        snprintf(ws5, sizeof(ws5), "Tip: Hover left edge or select [2] for Priority Queue.");
        snprintf(st1, sizeof(st1), " Queue: %-3d Esc: %-3d", d_act, d_esc);
        snprintf(st2, sizeof(st2), " Dept: %.15s", dept_name);

        SidebarLayoutConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.portal_title         = "OFFICIAL DASHBOARD  |  FIELD DISPATCH CONSOLE";
        cfg.subtitle             = sub;
        cfg.workspace_title      = "Departmental Dispatch Summary";
        cfg.workspace_lines[0]   = ws1;
        cfg.workspace_lines[1]   = ws2;
        cfg.workspace_lines[2]   = ws3;
        cfg.workspace_lines[3]   = ws4;
        cfg.workspace_lines[4]   = "─────────────────────────────────────────────────────────";
        cfg.workspace_lines[5]   = ws5;
        cfg.workspace_line_count = 6;
        cfg.items                = off_items;
        cfg.item_count           = (int)(sizeof(off_items) / sizeof(off_items[0]));
        cfg.stats_line1          = st1;
        cfg.stats_line2          = st2;
        cfg.allow_secret_admin   = 0;

        key = run_sidebar_menu(&cfg);
        switch (key) {
            case '1': official_view_assigned(off); break;
            case '2': official_view_priority(off); break;
            case '3': official_view_details();     break;
            case '4': official_update_status(off); save_all_with_spinner("Status & ledger saved."); break;
            case '5': official_escalate(off);      save_all_with_spinner("Escalation persisted."); break;
            case '6': official_resolve(off);       save_all_with_spinner("Resolution archived."); break;
            case '7': official_view_archive(off);  break;
            case '0': printf("  " ANSI_GREEN "Logged out safely." ANSI_RESET "\n"); break;
        }
        if (key != '0') press_enter();
    } while (key != '0');
    memset(uname, 0, sizeof(uname));
}

/* ═══════════════════════════════════════════════════════════════
 *  ADMIN PORTAL
 * ═══════════════════════════════════════════════════════════════ */

/* ── 1. Manage Official Accounts ── */
static void admin_manage_officials(void) {
    printf("\n");
    print_subheader("Manage Official Accounts");

    printf("  Current Officials:\n");
    printf("  %-5s %-20s %-15s %-20s %s\n",
           "ID","Name","Username","Department","Active");
    print_line('-', 68);
    int active_offs = 0;
    for (int i = 0; i < official_count; i++) {
        if (officials[i].active) active_offs++;
        printf("  %-5d %-20s %-15s %-20s %s\n",
               officials[i].official_id,
               officials[i].name,
               officials[i].username,
               officials[i].dept_assigned == -1
                   ? "All Departments"
                   : departments[officials[i].dept_assigned],
               officials[i].active ? "Yes" : "No");
    }
    print_line('-', 68);
    printf("  Total Officials: %d (Active: %d)\n", official_count, active_offs);

    printf("\n  Options:\n");
    printf("  1. Add New Official\n");
    printf("  2. Remove Official (Deactivate)\n");
    printf("  3. Reset Official Password\n");
    printf("  0. Back\n");
    int ch = read_int("Choice: ", 0, 3);

    if (ch == 1) {
        if (official_count >= MAX_OFFICIALS) {
            printf("  [!] Official registry full.\n"); return;
        }
        Official no;
        memset(&no, 0, sizeof(no));
        no.official_id  = next_official_id;
        no.active       = 1;
        no.dept_assigned= -1;
        read_line("Name     : ", no.name,     MAX_NAME);
        read_line("Username : ", no.username, MAX_NAME);
        read_password("Password : ", no.password, MAX_NAME);
        officials[official_count++] = no;
        next_official_id++;
        save_officials();
        {
            char logbuf[256];
            snprintf(logbuf, sizeof(logbuf),
                     "Name: %s; Username: %s; Dept: All/Unassigned",
                     no.name, no.username);
            append_master_record("OFFICIAL_ADD", "Admin", no.official_id, logbuf);
        }
        printf("  [OK] Official added. ID: %d\n", no.official_id);
    }
    else if (ch == 2) {
        printf("  Enter official ID to deactivate: ");
        int oid; scanf("%d", &oid); clear_input_buffer();
        for (int i = 0; i < official_count; i++) {
            if (officials[i].official_id == oid) {
                officials[i].active = 0;
                save_officials();
                {
                    char logbuf[256];
                    snprintf(logbuf, sizeof(logbuf),
                             "Official %s (%s) deactivated",
                             officials[i].name, officials[i].username);
                    append_master_record("OFFICIAL_DEACTIVATE", "Admin", oid, logbuf);
                }
                printf("  [OK] Official #%d deactivated.\n", oid);
                return;
            }
        }
        printf("  [!] Official not found.\n");
    }
    else if (ch == 3) {
        printf("  Enter official ID: ");
        int oid; scanf("%d", &oid); clear_input_buffer();
        for (int i = 0; i < official_count; i++) {
            if (officials[i].official_id == oid && officials[i].active) {
                read_password("New Password: ", officials[i].password, MAX_NAME);
                save_all_with_spinner("Officer credentials saved.");
                append_master_record("OFFICIAL_PASS_RESET", "Admin", oid,
                                     "Password updated by Administrator");
                printf("  [OK] Password updated.\n");
                return;
            }
        }
        printf("  [!] Official not found.\n");
    }
}

/* ── 2. Manage Citizen Accounts (Edit & Delete) ── */
static void admin_manage_citizens(void) {
    printf("\n");
    print_subheader("Manage Citizen Accounts");

    printf("  Registered Citizens:\n");
    printf("  %-5s %-20s %-16s %-14s %s\n",
           "ID","Name","Aadhaar","Phone","Status");
    print_line('-', 68);
    int active_cits = 0;
    for (int i = 0; i < citizen_count; i++) {
        if (citizens[i].active) active_cits++;
        printf("  %-5d %-20s %-16s %-14s %s\n",
               citizens[i].citizen_id,
               citizens[i].name,
               citizens[i].aadhaar,
               citizens[i].phone,
               citizens[i].active ? "Active" : "Deactivated");
    }
    print_line('-', 68);
    printf("  Total Registered Citizens: %d (Active: %d | Deactivated: %d)\n",
           citizen_count, active_cits, citizen_count - active_cits);

    printf("\n  Options:\n");
    printf("  1. Edit Citizen Details\n");
    printf("  2. Deactivate Citizen (Soft Delete)\n");
    printf("  3. Reactivate Citizen\n");
    printf("  4. Delete Citizen Permanently\n");
    printf("  0. Back\n");
    int ch = read_int("Choice: ", 0, 4);

    if (ch == 1) {
        printf("  Enter Citizen ID to edit: ");
        int cid; scanf("%d", &cid); clear_input_buffer();
        Citizen *c = NULL;
        for (int i = 0; i < citizen_count; i++) {
            if (citizens[i].citizen_id == cid) { c = &citizens[i]; break; }
        }
        if (!c) { printf("  [!] Citizen not found.\n"); return; }

        printf("  Editing Citizen #%d (%s):\n", c->citizen_id, c->name);
        char new_name[MAX_NAME];
        char new_phone[MAX_PHONE];
        char new_aadhaar[MAX_AADHAAR];

        printf("  Current Name   : %s\n", c->name);
        read_line("New Name (leave blank to keep current)   : ", new_name, MAX_NAME);
        if (new_name[0]) {
            sanitize_pipes(new_name);
            strncpy(c->name, new_name, MAX_NAME - 1);
        }

        printf("  Current Phone  : %s\n", c->phone);
        read_line("New Phone (leave blank to keep current)  : ", new_phone, MAX_PHONE);
        if (new_phone[0]) {
            sanitize_pipes(new_phone);
            strncpy(c->phone, new_phone, MAX_PHONE - 1);
        }

        printf("  Current Aadhaar: %s\n", c->aadhaar);
        read_line("New Aadhaar (leave blank to keep current): ", new_aadhaar, MAX_AADHAAR);
        if (new_aadhaar[0]) {
            sanitize_pipes(new_aadhaar);
            int dup = 0;
            for (int i = 0; i < citizen_count; i++) {
                if (citizens[i].citizen_id != cid && strcmp(citizens[i].aadhaar, new_aadhaar) == 0) {
                    dup = 1; break;
                }
            }
            if (dup) {
                printf("  [!] That Aadhaar is already registered by another citizen.\n");
            } else {
                strncpy(c->aadhaar, new_aadhaar, MAX_AADHAAR - 1);
            }
        }

        save_citizens();
        hist_push(c->citizen_id, "Citizen Profile Updated", "Admin");
        save_history();
        {
            char logbuf[320];
            snprintf(logbuf, sizeof(logbuf),
                     "Name: %s; Aadhaar: %s; Phone: %s",
                     c->name, c->aadhaar, c->phone);
            append_master_record("CITIZEN_EDIT", "Admin", c->citizen_id, logbuf);
        }
        printf("  [OK] Citizen #%d updated successfully.\n", c->citizen_id);
    }
    else if (ch == 2) {
        printf("  Enter Citizen ID to deactivate: ");
        int cid; scanf("%d", &cid); clear_input_buffer();
        for (int i = 0; i < citizen_count; i++) {
            if (citizens[i].citizen_id == cid) {
                citizens[i].active = 0;
                save_citizens();
                hist_push(cid, "Citizen Deactivated", "Admin");
                save_history();
                {
                    char logbuf[256];
                    snprintf(logbuf, sizeof(logbuf),
                             "Citizen %s deactivated", citizens[i].name);
                    append_master_record("CITIZEN_DEACTIVATE", "Admin", cid, logbuf);
                }
                printf("  [OK] Citizen #%d (%s) deactivated.\n", cid, citizens[i].name);
                return;
            }
        }
        printf("  [!] Citizen not found.\n");
    }
    else if (ch == 3) {
        printf("  Enter Citizen ID to reactivate: ");
        int cid; scanf("%d", &cid); clear_input_buffer();
        for (int i = 0; i < citizen_count; i++) {
            if (citizens[i].citizen_id == cid) {
                citizens[i].active = 1;
                save_citizens();
                hist_push(cid, "Citizen Reactivated", "Admin");
                save_history();
                {
                    char logbuf[256];
                    snprintf(logbuf, sizeof(logbuf),
                             "Citizen %s reactivated", citizens[i].name);
                    append_master_record("CITIZEN_REACTIVATE", "Admin", cid, logbuf);
                }
                printf("  [OK] Citizen #%d (%s) reactivated.\n", cid, citizens[i].name);
                return;
            }
        }
        printf("  [!] Citizen not found.\n");
    }
    else if (ch == 4) {
        printf("  Enter Citizen ID to permanently delete: ");
        int cid; scanf("%d", &cid); clear_input_buffer();
        int found = -1;
        for (int i = 0; i < citizen_count; i++) {
            if (citizens[i].citizen_id == cid) { found = i; break; }
        }
        if (found == -1) { printf("  [!] Citizen not found.\n"); return; }

        printf("  Confirm permanent deletion of #%d (%s)? (y/n): ",
               cid, citizens[found].name);
        char yn[4]; fgets(yn, 4, stdin);
        if (yn[0] == 'y' || yn[0] == 'Y') {
            char del_name[MAX_NAME];
            strncpy(del_name, citizens[found].name, MAX_NAME - 1);
            del_name[MAX_NAME - 1] = '\0';
            for (int i = found; i < citizen_count - 1; i++) {
                citizens[i] = citizens[i + 1];
            }
            citizen_count--;
            save_citizens();
            hist_push(cid, "Citizen Deleted Permanently", "Admin");
            save_history();
            {
                char logbuf[256];
                snprintf(logbuf, sizeof(logbuf),
                         "Citizen %s permanently deleted from directory", del_name);
                append_master_record("CITIZEN_DELETE", "Admin", cid, logbuf);
            }
            printf("  [OK] Citizen #%d permanently removed.\n", cid);
        }
    }
}

/* ── 3. Assign Department to Official ── */
static void admin_assign_dept(void) {
    printf("\n");
    print_subheader("Assign Department to Official");

    printf("  Enter official ID: ");
    int oid; scanf("%d", &oid); clear_input_buffer();
    Official *off = NULL;
    for (int i = 0; i < official_count; i++) {
        if (officials[i].official_id == oid && officials[i].active) {
            off = &officials[i]; break;
        }
    }
    if (!off) { printf("  [!] Official not found.\n"); return; }

    printf("  Departments:\n");
    for (int i = 0; i < NUM_DEPARTMENTS; i++)
        printf("    %d. %s\n", i, departments[i]);
    printf("    5. Unassigned (all departments)\n");
    int dept = read_int("Assign Department (0-5): ", 0, NUM_DEPARTMENTS);
    off->dept_assigned = (dept == NUM_DEPARTMENTS) ? -1 : dept;
    save_officials();
    {
        char logbuf[256];
        snprintf(logbuf, sizeof(logbuf),
                 "Official %s assigned to %s",
                 off->name,
                 off->dept_assigned == -1 ? "All Departments"
                                          : departments[off->dept_assigned]);
        append_master_record("OFFICIAL_ASSIGN", "Admin", oid, logbuf);
    }
    printf("  [OK] %s assigned to %s.\n",
           off->name,
           off->dept_assigned == -1 ? "All Departments"
                                    : departments[off->dept_assigned]);
}

/* ── 3. Service Records & SLA Operations ── */
static void admin_db_maintenance(void) {
    printf("\n");
    print_subheader("Service Records & SLA Operations");

    int total = 0, pending = 0, in_prog = 0, escalated = 0, resolved = 0;
    GrievanceNode *cur = ledger_head;
    while (cur) {
        total++;
        if      (strcmp(cur->status, "Pending")     == 0) pending++;
        else if (strcmp(cur->status, "In Progress") == 0) in_prog++;
        else if (strcmp(cur->status, "Escalated")   == 0) escalated++;
        else if (strcmp(cur->status, "Resolved")    == 0) resolved++;
        cur = cur->next;
    }

    printf("  Total Grievances : %d\n", total);
    printf("  Pending          : %d\n", pending);
    printf("  In Progress      : %d\n", in_prog);
    printf("  Escalated        : %d  <-- Need attention\n", escalated);
    printf("  Resolved         : %d\n", resolved);
    printf("  Citizens         : %d\n", citizen_count);
    printf("  Officials        : %d\n", official_count);

    printf("\n  Options:\n");
    printf("  1. View All Records\n");
    printf("  2. Reset System Records to Baseline\n");
    printf("  3. Advance Calendar (+1 Day Review Cycle)\n");
    printf("  0. Back\n");
    int ch = read_int("Choice: ", 0, 3);

    if (ch == 1) {
        printf("\n  COMPLETE RECORDS:\n");
        print_line('-', 76);
        cur = ledger_head;
        if (!cur) { printf("  [Empty]\n"); return; }
        printf("  %-6s %-18s %-12s %-16s %-4s %-4s %s\n",
               "ID","Citizen","Village","Department","Pri","Days","Status");
        print_line('-', 76);
        int shown = 0;
        while (cur) {
            const char *cname = "Unknown";
            for (int i = 0; i < citizen_count; i++) {
                if (citizens[i].citizen_id == cur->citizen_id) {
                    cname = citizens[i].name;
                    break;
                }
            }
            printf("  %-6d %-18.18s %-12s %-16s %-4d %-4d %s\n",
                   cur->ticket_id,
                   cname,
                   villages[cur->village_id],
                   departments[cur->dept_id],
                   cur->priority,
                   cur->days_pending,
                   cur->status);
            shown++;
            cur = cur->next;
        }
        print_line('-', 76);
        printf("  Total Grievance Records Displayed: %d\n", shown);
    }
    else if (ch == 2) {
        printf("  Confirm full reset? This clears all data! (y/n): ");
        char yn[4]; fgets(yn, 4, stdin);
        if (yn[0] == 'y' || yn[0] == 'Y') {
            seed_initial_data();
        }
    }
    else if (ch == 3) {
        /* ── SLA Escalation Check ── */
        printf("\n  [SLA REVIEW] Advancing pending cycle (+1 day) for unresolved tickets...\n");
        int esc_count = 0;
        char ts[MAX_TS]; get_timestamp(ts);
        cur = ledger_head;
        while (cur) {
            if (strcmp(cur->status, "Resolved") != 0) {
                cur->days_pending++;
                if (cur->days_pending >= ESCALATION_THRESHOLD &&
                    strcmp(cur->status, "Escalated") != 0) {
                    cur->priority = PRIORITY_EMERGENCY;
                    ll_set_status(cur, "Escalated");
                    pq_remove(cur->ticket_id);
                    pq_insert(cur->ticket_id,
                              PRIORITY_EMERGENCY,
                              cur->days_pending, cur);
                    hist_push(cur->ticket_id,
                              "Auto-Escalated (Day Threshold)", "System");
                    {
                        char logbuf[256];
                        snprintf(logbuf, sizeof(logbuf),
                                 "Auto-Escalated to Priority %d at Day %d",
                                 PRIORITY_EMERGENCY, cur->days_pending);
                        append_master_record("SLA_AUTO_ESCALATE", "System",
                                             cur->ticket_id, logbuf);
                    }
                    printf("  [ALERT] #%d auto-escalated (Day %d)\n",
                           cur->ticket_id, cur->days_pending);
                    esc_count++;
                }
            }
            cur = cur->next;
        }
        save_all();
        {
            char logbuf[128];
            snprintf(logbuf, sizeof(logbuf),
                     "SLA Cycle (+1 Day) completed; newly escalated: %d", esc_count);
            append_master_record("SLA_CYCLE_CHECK", "Admin", esc_count, logbuf);
        }
        printf("  SLA review complete. Newly escalated tickets: %d\n", esc_count);
    }
}

/* ── 4. Analytics & Monitoring Reports ── */
static void admin_analytics(void) {
    printf("\n");
    print_subheader("System Monitoring & Analytics Reports");

    /* Department-wise workload counters */
    int d_tot[NUM_DEPARTMENTS]={0}, d_pend[NUM_DEPARTMENTS]={0},
        d_esc[NUM_DEPARTMENTS]={0},  d_res[NUM_DEPARTMENTS]={0};
    float d_rat[NUM_DEPARTMENTS]={0}; int d_rc[NUM_DEPARTMENTS]={0};

    /* Village-wise counters */
    int v_tot[NUM_VILLAGES]={0}, v_pend[NUM_VILLAGES]={0};

    int grand=0, grand_pend=0, grand_esc=0, grand_res=0;

    GrievanceNode *cur = ledger_head;
    while (cur) {
        int d = cur->dept_id, v = cur->village_id;
        d_tot[d]++; v_tot[v]++; grand++;
        if (strcmp(cur->status,"Resolved")==0) {
            d_res[d]++; grand_res++;
            if (cur->rating>0){d_rat[d]+=(float)cur->rating;d_rc[d]++;}
        } else if (strcmp(cur->status,"Escalated")==0) {
            d_esc[d]++; grand_esc++;
            grand_pend++; v_pend[v]++;
        } else {
            d_pend[d]++; grand_pend++; v_pend[v]++;
        }
        cur = cur->next;
    }

    printf("\n  SYSTEM OVERVIEW\n");
    printf("  Total Grievances : %d\n", grand);
    printf("  Active / Pending : %d\n", grand_pend);
    printf("  Escalated        : %d\n", grand_esc);
    printf("  Resolved         : %d\n", grand_res);
    printf("  Citizens         : %d  |  Officials: %d\n",
           citizen_count, official_count);

    printf("\n  DEPARTMENT-WISE BACKLOG\n");
    printf("  %-18s %5s %7s %9s %8s %9s\n",
           "Department","Total","Pending","Escalated","Resolved","Avg ★");
    print_line('-', 68);
    for (int i = 0; i < NUM_DEPARTMENTS; i++) {
        float avg = d_rc[i]>0 ? d_rat[i]/(float)d_rc[i] : 0.0f;
        printf("  %-18s %5d %7d %9d %8d   %4.1f/5\n",
               departments[i], d_tot[i], d_pend[i],
               d_esc[i], d_res[i], avg);
    }

    printf("\n  VILLAGE-WISE PENDING LOAD\n");
    printf("  %-16s %8s %10s\n", "Village","Total","Pending");
    print_line('-', 40);
    for (int i = 0; i < NUM_VILLAGES; i++) {
        printf("  %-16s %8d %10d\n",
               villages[i], v_tot[i], v_pend[i]);
    }

    printf("\n  FIELD DISPATCH PRIORITY QUEUE (Urgent First):\n");
    pq_display();
}

/* ── Admin Portal main loop ── */
static void portal_admin(void) {
    printf("\n");
    print_subheader("Administrative Governance Portal — Login");

    char uname[MAX_NAME], pass[MAX_NAME];
    int attempts = 0;

    while (attempts < 3) {
        read_line("Username : ", uname, MAX_NAME);
        read_password("Password : ", pass, MAX_NAME);

        anim_spinner("Verifying Master Credentials", NULL);
        int ok = (strcmp(uname, ADMIN_USERNAME) == 0 &&
                  hash_password(pass) == ADMIN_PASS_HASH);
        memset(pass, 0, sizeof(pass));
        if (ok) break;

        attempts++;
        if (attempts >= 3) {
            memset(uname, 0, sizeof(uname));
            printf("  " ANSI_RED ANSI_BOLD
                   "[SECURITY ALERT] Maximum login attempts exceeded. System locked."
                   ANSI_RESET "\n");
            fflush(stdout);
            exit(1);
        }
        printf("  " ANSI_RED "[!] Invalid admin credentials. (%d/3 attempts failed)"
               ANSI_RESET "\n", attempts);
    }
    printf("  " ANSI_GREEN "[OK] Admin access granted." ANSI_RESET "\n");
    anim_typewriter(ANSI_CYAN, "ADMINISTRATIVE CONTROL CENTER -- GOVERNANCE");

    static const SidebarItem adm_items[] = {
        { '1', "Manage Officers",   NULL },
        { '2', "Manage Citizens",   NULL },
        { '3', "Assign Department", NULL },
        { '4', "Records & SLA Ops", NULL },
        { '5', "Analytics & Stats", NULL },
        { '6', "System Audit Log",  NULL },
        { '0', "Logout",            NULL }
    };

    int key;
    do {
        int tot = 0, pend = 0, inp = 0, esc = 0, res = 0;
        collect_system_kpis(&tot, &pend, &inp, &esc, &res);

        char ws1[80], ws2[80], ws3[80], ws4[80], ws5[80], st1[40], st2[40];
        snprintf(ws1, sizeof(ws1), "Total Grievances : %d   (Active: %d)", tot, tot - res);
        snprintf(ws2, sizeof(ws2), "Pending          : %d   |   In Progress: %d", pend, inp);
        snprintf(ws3, sizeof(ws3), "Escalated Urgent : %d Cases", esc);
        snprintf(ws4, sizeof(ws4), "Resolved Archive : %d Completed", res);
        snprintf(ws5, sizeof(ws5), "Citizens: %-4d  |  Field Officers: %-3d", citizen_count, official_count);
        snprintf(st1, sizeof(st1), " Total: %-3d Esc: %-3d", tot, esc);
        snprintf(st2, sizeof(st2), " Cit: %-3d  Off: %-3d", citizen_count, official_count);

        SidebarLayoutConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.portal_title         = "ADMINISTRATIVE CONTROL CENTER  |  RESTRICTED";
        cfg.subtitle             = "Hardware-Verified Session · Full Governance & SLA Oversight";
        cfg.workspace_title      = "System Governance Telemetry";
        cfg.workspace_lines[0]   = ws1;
        cfg.workspace_lines[1]   = ws2;
        cfg.workspace_lines[2]   = ws3;
        cfg.workspace_lines[3]   = ws4;
        cfg.workspace_lines[4]   = "─────────────────────────────────────────────────────────";
        cfg.workspace_lines[5]   = ws5;
        cfg.workspace_line_count = 6;
        cfg.items                = adm_items;
        cfg.item_count           = (int)(sizeof(adm_items) / sizeof(adm_items[0]));
        cfg.stats_line1          = st1;
        cfg.stats_line2          = st2;
        cfg.allow_secret_admin   = 0;

        key = run_sidebar_menu(&cfg);
        switch (key) {
            case '1': admin_manage_officials(); break;
            case '2': admin_manage_citizens();  break;
            case '3': admin_assign_dept();      break;
            case '4': admin_db_maintenance();   save_all_with_spinner("Registry & SLA saved."); break;
            case '5': admin_analytics();        break;
            case '6': hist_display();           break;
            case '0': printf("  " ANSI_GREEN "Admin logged out safely." ANSI_RESET "\n"); break;
        }
        if (key != '0') press_enter();
    } while (key != '0');

    memset(uname, 0, sizeof(uname));
    memset(pass,  0, sizeof(pass));
}

/* ═══════════════════════════════════════════════════════════════
 *  HARDWARE DEVICE BINDING — verify admin_device.key
 * ═══════════════════════════════════════════════════════════════ */

/* Returns 1 if current machine's fingerprint matches stored key */
static int verify_device_key(void) {
    FILE *fp = fopen(FILE_DEVICE_KEY, "r");
    if (!fp) return 0;

    char stored[64];
    memset(stored, 0, sizeof(stored));
    if (!fgets(stored, sizeof(stored), fp)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    /* Strip whitespace / newlines */
    size_t len = strlen(stored);
    while (len > 0 &&
           (stored[len - 1] == '\n' || stored[len - 1] == '\r' ||
            stored[len - 1] == ' '))
        stored[--len] = '\0';

    unsigned long stored_fp = strtoul(stored, NULL, 16);
    unsigned long current_fp = get_machine_fingerprint();
    return (stored_fp != 0 && current_fp != 0 && stored_fp == current_fp);
}

/* One-time device binding: ./rural_app --bind-admin */
static void bind_admin_device(void) {
    printf("\n");
    print_subheader("ADMIN DEVICE BINDING — One-Time Setup");
    printf("  This will permanently bind the Admin Portal to this machine.\n\n");

    char uname[MAX_NAME], pass[MAX_NAME];
    int attempts = 0;

    while (attempts < 3) {
        read_line("Admin Username : ", uname, MAX_NAME);
        read_password("Admin Password : ", pass, MAX_NAME);

        anim_spinner("Verifying Credentials & Hardware Fingerprint", NULL);
        int ok = (strcmp(uname, ADMIN_USERNAME) == 0 &&
                  hash_password(pass) == ADMIN_PASS_HASH);
        memset(pass, 0, sizeof(pass));

        if (ok) {
            unsigned long fp = get_machine_fingerprint();
            if (fp == 0) {
                printf("  " ANSI_RED "[ERROR] Could not determine machine fingerprint."
                       ANSI_RESET "\n");
                memset(uname, 0, sizeof(uname));
                return;
            }

            FILE *kf = fopen(FILE_DEVICE_KEY, "w");
            if (!kf) {
                printf("  " ANSI_RED "[ERROR] Could not write device key file."
                       ANSI_RESET "\n");
                memset(uname, 0, sizeof(uname));
                return;
            }
            fprintf(kf, "%lx\n", fp);
            fclose(kf);

            printf("\n  " ANSI_GREEN "[SECURITY] Admin Portal permanently bound to this"
                   " hardware device." ANSI_RESET "\n");
            printf("  Device Fingerprint: 0x%lx\n", fp);
            printf("  Key File: %s\n\n", FILE_DEVICE_KEY);
            memset(uname, 0, sizeof(uname));
            return;
        }

        attempts++;
        if (attempts >= 3) {
            memset(uname, 0, sizeof(uname));
            printf("  " ANSI_RED "[SECURITY ALERT] Maximum attempts exceeded."
                   " Binding aborted." ANSI_RESET "\n");
            return;
        }
        printf("  " ANSI_RED "[!] Invalid admin credentials. (%d/3 attempts failed)"
               ANSI_RESET "\n", attempts);
    }
    memset(uname, 0, sizeof(uname));
}

/* Switch working directory to executable folder if data files live there */
static void ensure_data_working_dir(const char *argv0) {
    FILE *cwd_chk = fopen(FILE_GRIEVANCES, "r");
    if (cwd_chk) {
        fclose(cwd_chk);
        return;
    }
    char dir[512];
    memset(dir, 0, sizeof(dir));
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, dir, (DWORD)(sizeof(dir) - 1));
    if (len == 0 || len >= sizeof(dir) - 1) {
        if (argv0) strncpy(dir, argv0, sizeof(dir) - 1);
    }
    char *sep1 = strrchr(dir, '\\');
    char *sep2 = strrchr(dir, '/');
    char *sep  = (sep1 && sep2) ? (sep1 > sep2 ? sep1 : sep2) : (sep1 ? sep1 : sep2);
    if (sep) {
        *sep = '\0';
        char probe[600];
        snprintf(probe, sizeof(probe), "%s\\%s", dir, FILE_GRIEVANCES);
        FILE *f2 = fopen(probe, "r");
        if (f2) {
            fclose(f2);
            SetCurrentDirectoryA(dir);
        }
    }
#else
    ssize_t len = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
    if (len > 0) {
        dir[len] = '\0';
    } else if (argv0) {
        strncpy(dir, argv0, sizeof(dir) - 1);
    }
    char *sep = strrchr(dir, '/');
    if (sep) {
        *sep = '\0';
        char probe[600];
        snprintf(probe, sizeof(probe), "%s/%s", dir, FILE_GRIEVANCES);
        FILE *f2 = fopen(probe, "r");
        if (f2) {
            fclose(f2);
            if (chdir(dir) != 0) { /* stay in cwd if chdir blocked */ }
        }
    }
#endif
}

/* ═══════════════════════════════════════════════════════════════
 *  MAIN ENTRY POINT
 * ═══════════════════════════════════════════════════════════════ */
int main(int argc, char *argv[]) {
    ensure_data_working_dir(argc > 0 ? argv[0] : NULL);
    console_init();

    /* ── Startup Banner & Animated Sync Bar ── */
    printf("\n");
    print_header("RURAL PUBLIC SERVICE REQUEST & GRIEVANCE MANAGEMENT SYSTEM");
    printf("  " ANSI_GRAY "Government of India  |  Gram Panchayat Service Portal"
           ANSI_RESET "\n");

    anim_boot_progress_bar("Loading Service Registries");

    /* ── Load all persistent data from text files ── */
    load_officials();
    load_citizens();
    load_grievances();
    load_history();

    /* ── Auto-seed if no grievance data found ── */
    {
        FILE *chk = fopen(FILE_GRIEVANCES, "r");
        if (!chk) {
            printf("  " ANSI_AMBER "Initializing portal service registry..."
                   ANSI_RESET "\n");
            seed_initial_data();
        } else {
            fclose(chk);
            int gc = 0;
            GrievanceNode *c = ledger_head;
            while (c) { gc++; c = c->next; }
            printf("  " ANSI_GREEN "Service registry ready:" ANSI_RESET
                   " Citizens=%d | Officials=%d | Grievances=%d\n",
                   citizen_count, official_count, gc);
        }
    }

    /* ── Handle --bind-admin: one-time device registration ── */
    if (argc > 1 && strcmp(argv[1], "--bind-admin") == 0) {
        bind_admin_device();
        save_all_with_spinner("All portal records saved safely.");
        ll_free_all();
        bst_free(bst_root);
        pq_free();
        hist_free();
        return 0;
    }

    anim_typewriter(ANSI_BCYAN, "WELCOME -- HOVER LEFT EDGE OR PRESS KEY TO SELECT PORTAL");
    sleep_ms(180);

    static const SidebarItem main_items[] = {
        { '1', "Citizen Portal",   NULL },
        { '2', "Official Portal",  NULL },
        { '0', "Exit & Save",      NULL }
    };

    /* ── Main Menu — Collapsible Left Sidebar + Mouse/Key Navigation ── */
    int running = 1;
    while (running) {
        int tot = 0, pend = 0, inp = 0, esc = 0, res = 0;
        collect_system_kpis(&tot, &pend, &inp, &esc, &res);

        char ws1[80], ws2[80], ws3[80], ws4[80], st1[40], st2[40];
        snprintf(ws1, sizeof(ws1), "Active Service Requests : %d Tickets (%d Urgent)",
                 tot - res, esc);
        snprintf(ws2, sizeof(ws2), "Pending: %-4d  In Progress: %-4d  Resolved: %-4d",
                 pend, inp, res);
        snprintf(ws3, sizeof(ws3), "Registered Citizens: %-4d  |  Field Officers: %-3d",
                 citizen_count, official_count);
        snprintf(ws4, sizeof(ws4), "Villages Covered: Rampur, Sitapur, Gopalpur, Krishnapur, Balipur");
        snprintf(st1, sizeof(st1), " Grievances: %-3d Act: %-2d", tot, tot - res);
        snprintf(st2, sizeof(st2), " Hover Left / Tab");

        SidebarLayoutConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.portal_title         = "GRAM PANCHAYAT PUBLIC SERVICE PORTAL";
        cfg.subtitle             = "Citizen -> Official Dispatch System";
        cfg.workspace_title      = "Public Service Portal Hub";
        cfg.workspace_lines[0]   = "Select an operational portal from the slide-out drawer:";
        cfg.workspace_lines[1]   = "  [1] Citizen Portal   -- Lodge, Track, Reopen & Rate";
        cfg.workspace_lines[2]   = "  [2] Official Portal  -- Priority Triage & Field Action";
        cfg.workspace_lines[3]   = "  [0] Exit & Save      -- Persist Records & Close";
        cfg.workspace_lines[4]   = "─────────────────────────────────────────────────────────";
        cfg.workspace_lines[5]   = ws1;
        cfg.workspace_lines[6]   = ws2;
        cfg.workspace_lines[7]   = ws3;
        cfg.workspace_lines[8]   = ws4;
        cfg.workspace_line_count = 9;
        cfg.items                = main_items;
        cfg.item_count           = (int)(sizeof(main_items) / sizeof(main_items[0]));
        cfg.stats_line1          = st1;
        cfg.stats_line2          = st2;
        cfg.allow_secret_admin   = 1;

        int key = run_sidebar_menu(&cfg);
        switch (key) {
            case '1':
                portal_citizen();
                break;
            case '2':
                portal_official();
                break;
            case '0':
                running = 0;
                break;
            case 0x01:
                /* Ctrl+Shift+A (SOH) — hardware-locked admin verification */
                anim_spinner("Validating Terminal Security Context", NULL);
                if (verify_device_key()) {
                    portal_admin();
                } else {
                    printf("  " ANSI_RED "[!] Invalid choice." ANSI_RESET "\n");
                    sleep_ms(450);
                }
                break;
            default:
                printf("  " ANSI_RED "[!] Invalid choice." ANSI_RESET "\n");
                sleep_ms(300);
                break;
        }
    }

    printf("\n");
    save_all_with_spinner("All portal records saved safely.");
    printf("  " ANSI_CYAN "Goodbye!" ANSI_RESET "\n\n");

    /* ── Cleanup heap ── */
    ll_free_all();
    bst_free(bst_root);
    pq_free();
    hist_free();

    return 0;
}
/* ═══════════════════════════════════════════════════════════════
 *  END OF rural_grievance.c
 * ═══════════════════════════════════════════════════════════════ */
