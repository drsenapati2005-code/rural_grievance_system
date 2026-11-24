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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ═══════════════════════════════════════════════════════════════
 *  COMPILE-TIME CONSTANTS
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_OFFICIALS        20
#define MAX_CITIZENS         100
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

/* Admin master credentials */
#define ADMIN_USERNAME       "admin"
#define ADMIN_PASSWORD       "admin@2024"

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

/* ── 2. Official account (Array DS element) ── */
typedef struct {
    int  official_id;
    char name[MAX_NAME];
    char username[MAX_NAME];
    char password[MAX_NAME];
    int  dept_assigned;           /* -1 = unassigned, else dept index */
    int  active;
} Official;

/* ── 3. Grievance — Singly Linked List node ── */
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

    struct GrievanceNode *next;   /* linked list pointer */
} GrievanceNode;

/* ── 4. BST node (Binary Search Tree DS) ── */
typedef struct BSTNode {
    int            ticket_id;
    GrievanceNode *ref;           /* direct pointer into linked list */
    struct BSTNode *left, *right;
} BSTNode;

/* ── 5. Priority Queue node (sorted linked list DS) ── */
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

/* Data structure roots */
static GrievanceNode *ledger_head = NULL;   /* Singly Linked List */
static BSTNode       *bst_root    = NULL;   /* Binary Search Tree */
static PQNode        *pq_front    = NULL;   /* Priority Queue      */
static HistEntry     *hist_head   = NULL;   /* History / Audit Log */

/* ═══════════════════════════════════════════════════════════════
 *  UTILITY FUNCTIONS
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

static void print_line(char ch, int n) {
    for (int i = 0; i < n; i++) putchar(ch);
    putchar('\n');
}

static void print_header(const char *title) {
    print_line('=', 68);
    printf("   %s\n", title);
    print_line('=', 68);
}

static void print_subheader(const char *title) {
    print_line('-', 68);
    printf("  %s\n", title);
    print_line('-', 68);
}

/* Safe string input — reads a full line, strips newline */
static void read_line(const char *prompt, char *buf, int size) {
    printf("  %s", prompt);
    fflush(stdout);
    if (fgets(buf, size, stdin)) {
        buf[strcspn(buf, "\n")] = '\0';
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
        printf("  [!] Enter a number between %d and %d.\n", lo, hi);
    }
}

static void press_enter(void) {
    printf("\n  Press ENTER to continue...");
    fflush(stdout);
    clear_input_buffer();
}

/* Helper to sanitize pipe characters from strings stored in text files */
static void sanitize_pipes(char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '|') str[i] = '-';
        if (str[i] == '\n' || str[i] == '\r') str[i] = ' ';
    }
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
 *  HISTORY / AUDIT LOG  (LIFO linked list — push-front)
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
 *  SINGLY LINKED LIST — Master Grievance Ledger
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
 *  BINARY SEARCH TREE — Fast Ticket ID Indexing
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
 *  PRIORITY QUEUE — Sorted Linked List (Priority DESC, Days DESC)
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

    /* ── 5 Citizens ── */
    Citizen c[5] = {
        {1, "Ramesh Kumar",  "1234-5678-9101", "9876543210", 1},
        {2, "Sunita Devi",   "2345-6789-1012", "9823456780", 1},
        {3, "Mohan Singh",   "3456-7891-0123", "9712345670", 1},
        {4, "Priya Sharma",  "4567-8910-1234", "9634567890", 1},
        {5, "Lakshmi Bai",   "5678-9101-2345", "9545678901", 1}
    };
    for (int i = 0; i < 5; i++) citizens[i] = c[i];
    citizen_count = 5;
    next_citizen_id = 6;

    /* ── 3 Officials ── */
    Official o[3] = {
        {101,"Suresh Verma",  "sverma",   "pass101",  1, 1},  /* Electricity */
        {102,"Anita Pandey",  "apandey",  "pass102",  0, 1},  /* Water */
        {103,"Rajan Tiwari",  "rtiwari",  "pass103",  2, 1}   /* Roads */
    };
    for (int i = 0; i < 3; i++) officials[i] = o[i];
    official_count   = 3;
    next_official_id = 104;

    /* ── 7 Grievances in various states ── */
    struct {
        int cid, vid, did, haz, pri, days;
        const char *desc, *status, *remarks;
        int rating; const char *feedback;
    } seeds[] = {
        /* Active escalated — live wire, Rampur, Electricity */
        {1,1,1,1,10,9,
         "High-tension live wire fallen across school road. Severe electrocution risk.",
         "Escalated","Sent to divisional lineman.",0,""},
        /* Active pending — broken handpump, Sitapur, Water */
        {2,2,0,0,5,8,
         "Community handpump broken. 300 families without drinking water.",
         "Pending","",0,""},
        /* In-Progress — pothole blocking ambulance, Gopalpur, Roads */
        {3,3,2,1,10,4,
         "Large pothole filled with stagnant water blocking ambulance route to PHC.",
         "In Progress","Road crew dispatched. Repair ETA 2 days.",0,""},
        /* Pending — MNREGA wages, Krishnapur, Welfare */
        {4,4,4,0,5,2,
         "MNREGA October wages not credited. 45 labourers affected.",
         "Pending","",0,""},
        /* Pending — drainage overflow, Balipur, Sanitation */
        {5,5,3,0,5,5,
         "Open drain overflowing near primary school. Health hazard.",
         "Pending","",0,""},
        /* Resolved with feedback — street light, Rampur, Electricity */
        {1,1,1,0,5,0,
         "All street lights on main bazaar road non-functional for 5 nights.",
         "Resolved","Fuse replaced and all lights restored.",
         4,"Fixed quickly, good work!"},
        /* Pending — water contamination, Sitapur, Water */
        {2,2,0,1,10,1,
         "Tap water appears brown/contaminated. Children falling sick.",
         "Pending","",0,""}
    };
    int n_seeds = (int)(sizeof(seeds) / sizeof(seeds[0]));
    for (int i = 0; i < n_seeds; i++) {
        GrievanceNode *g = ll_append(
            next_ticket_id, seeds[i].cid,
            seeds[i].vid,   seeds[i].did,
            seeds[i].haz,   seeds[i].desc,
            seeds[i].pri);

        ll_set_status(g, seeds[i].status);
        g->days_pending = seeds[i].days;
        strncpy(g->remarks, seeds[i].remarks, MAX_REMARKS - 1);
        g->rating = seeds[i].rating;
        strncpy(g->feedback, seeds[i].feedback, MAX_COMMENT - 1);
        if (strcmp(seeds[i].status, "Resolved") == 0)
            strncpy(g->resolved_at, ts, MAX_TS - 1);

        bst_root = bst_insert(bst_root, next_ticket_id, g);
        if (strcmp(seeds[i].status, "Resolved") != 0)
            pq_insert(next_ticket_id, seeds[i].pri, seeds[i].days, g);

        hist_push(next_ticket_id, seeds[i].status, "System");
        next_ticket_id++;
    }

    save_all();
    printf("\n  [DATABASE] Default dataset initialized successfully!\n");
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
}

/* ── 2. Track Grievance by ticket_id (BST search) ── */
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

    Citizen *cit = citizen_find_by_creds(aadhaar, phone);
    if (!cit) { printf("  [!] Invalid credentials.\n"); return; }
    printf("  [OK] Welcome, %s!\n", cit->name);

    int choice;
    do {
        printf("\n");
        print_header("CITIZEN DASHBOARD");
        printf("  1. Submit Grievance\n");
        printf("  2. Track Grievance\n");
        printf("  3. View My Grievances\n");
        printf("  4. Reopen a Resolved Grievance\n");
        printf("  5. Give Feedback\n");
        printf("  0. Logout\n");
        choice = read_int("Choice: ", 0, 5);
        switch (choice) {
            case 1: citizen_submit(cit);        break;
            case 2: citizen_track();            break;
            case 3: citizen_view_mine(cit);     break;
            case 4: citizen_reopen(cit);        break;
            case 5: citizen_feedback(cit);      break;
            case 0: printf("  Logged out.\n");  break;
        }
        if (choice != 0) press_enter();
    } while (choice != 0);
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
    int found = 0;
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
            found = 1;
        }
        cur = cur->next;
    }
    if (!found) printf("  [No active tickets in your department]\n");
}

/* ── 2. View by Priority (Priority Queue traversal) ── */
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
    ll_set_status(g, new_status[sc - 1]);
    pq_update_priority(tid, g->priority, g->days_pending);

    char remarks[MAX_REMARKS];
    read_line("Add/Update Remarks: ", remarks, MAX_REMARKS);
    if (remarks[0])
        strncpy(g->remarks, remarks, MAX_REMARKS - 1);

    hist_push(tid, new_status[sc - 1], off->name);
    save_all();
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
    read_line("Username : ", uname, MAX_NAME);
    read_line("Password : ", pass,  MAX_NAME);

    Official *off = official_find_by_creds(uname, pass);
    if (!off) { printf("  [!] Invalid credentials.\n"); return; }

    const char *dept_name = (off->dept_assigned == -1)
                            ? "All Departments"
                            : departments[off->dept_assigned];
    printf("  [OK] Welcome, %s! Department: %s\n", off->name, dept_name);

    int choice;
    do {
        printf("\n");
        print_header("OFFICIAL DASHBOARD");
        printf("  1. View Assigned Tasks\n");
        printf("  2. View by Priority\n");
        printf("  3. View Ticket Details\n");
        printf("  4. Update Status & Remarks\n");
        printf("  5. Escalate Critical / Overdue\n");
        printf("  6. Mark as Resolved\n");
        printf("  7. View Archive / History\n");
        printf("  0. Logout\n");
        choice = read_int("Choice: ", 0, 7);
        switch (choice) {
            case 1: official_view_assigned(off);  break;
            case 2: official_view_priority(off);  break;
            case 3: official_view_details();      break;
            case 4: official_update_status(off);  break;
            case 5: official_escalate(off);       break;
            case 6: official_resolve(off);        break;
            case 7: official_view_archive(off);   break;
            case 0: printf("  Logged out.\n");    break;
        }
        if (choice != 0) press_enter();
    } while (choice != 0);
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
    for (int i = 0; i < official_count; i++) {
        printf("  %-5d %-20s %-15s %-20s %s\n",
               officials[i].official_id,
               officials[i].name,
               officials[i].username,
               officials[i].dept_assigned == -1
                   ? "Unassigned"
                   : departments[officials[i].dept_assigned],
               officials[i].active ? "Yes" : "No");
    }

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
        read_line("Password : ", no.password, MAX_NAME);
        officials[official_count++] = no;
        next_official_id++;
        save_officials();
        printf("  [OK] Official added. ID: %d\n", no.official_id);
    }
    else if (ch == 2) {
        printf("  Enter official ID to deactivate: ");
        int oid; scanf("%d", &oid); clear_input_buffer();
        for (int i = 0; i < official_count; i++) {
            if (officials[i].official_id == oid) {
                officials[i].active = 0;
                save_officials();
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
                read_line("New Password: ", officials[i].password, MAX_NAME);
                save_officials();
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
    for (int i = 0; i < citizen_count; i++) {
        printf("  %-5d %-20s %-16s %-14s %s\n",
               citizens[i].citizen_id,
               citizens[i].name,
               citizens[i].aadhaar,
               citizens[i].phone,
               citizens[i].active ? "Active" : "Deactivated");
    }

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
            for (int i = found; i < citizen_count - 1; i++) {
                citizens[i] = citizens[i + 1];
            }
            citizen_count--;
            save_citizens();
            hist_push(cid, "Citizen Deleted Permanently", "Admin");
            save_history();
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
    printf("  [OK] %s assigned to %s.\n",
           off->name,
           off->dept_assigned == -1 ? "All Departments"
                                    : departments[off->dept_assigned]);
}

/* ── 3. Database Maintenance ── */
static void admin_db_maintenance(void) {
    printf("\n");
    print_subheader("Database Maintenance");

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
        print_line('-', 68);
        cur = ledger_head;
        if (!cur) { printf("  [Empty]\n"); return; }
        printf("  %-6s %-16s %-16s %-5s %-8s %s\n",
               "ID","Village","Department","Pri","Days","Status");
        print_line('-', 68);
        while (cur) {
            printf("  %-6d %-16s %-16s %-5d %-8d %s\n",
                   cur->ticket_id,
                   villages[cur->village_id],
                   departments[cur->dept_id],
                   cur->priority,
                   cur->days_pending,
                   cur->status);
            cur = cur->next;
        }
    }
    else if (ch == 2) {
        printf("  Confirm full reset? This clears all data! (y/n): ");
        char yn[4]; fgets(yn, 4, stdin);
        if (yn[0] == 'y' || yn[0] == 'Y') {
            seed_initial_data();
        }
    }
    else if (ch == 3) {
        /* ── Auto-escalation time simulator ── */
        printf("\n  [SIMULATOR] Advancing all unresolved tickets by +1 day...\n");
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
                    printf("  [ALERT] #%d auto-escalated (Day %d)\n",
                           cur->ticket_id, cur->days_pending);
                    esc_count++;
                }
            }
            cur = cur->next;
        }
        save_all();
        printf("  Simulation done. New auto-escalations: %d\n", esc_count);
    }
}

/* ── 4. Analytics & Monitoring Reports ── */
static void admin_analytics(void) {
    printf("\n");
    print_subheader("System Monitoring & Analytics Reports");

    /* Department-wise counters (Array DS analytics) */
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

    printf("\n  OFFICER TRIAGE BOARD (Urgent First):\n");
    pq_display();
}

/* ── Admin Portal main loop ── */
static void portal_admin(void) {
    printf("\n");
    print_subheader("Admin Portal Login");

    char uname[MAX_NAME], pass[MAX_NAME];
    read_line("Username : ", uname, MAX_NAME);
    read_line("Password : ", pass,  MAX_NAME);

    if (strcmp(uname, ADMIN_USERNAME) != 0 ||
        strcmp(pass,  ADMIN_PASSWORD) != 0) {
        printf("  [!] Invalid admin credentials.\n");
        return;
    }
    printf("  [OK] Admin access granted.\n");

    int choice;
    do {
        printf("\n");
        print_header("ADMIN DASHBOARD");
        printf("  1. Manage Official Accounts\n");
        printf("  2. Manage Citizen Accounts  (Edit / Deactivate / Delete)\n");
        printf("  3. Assign Department to Official\n");
        printf("  4. Database Maintenance\n");
        printf("  5. System Monitoring & Analytics\n");
        printf("  6. View Action History\n");
        printf("  0. Logout\n");
        choice = read_int("Choice: ", 0, 6);
        switch (choice) {
            case 1: admin_manage_officials(); break;
            case 2: admin_manage_citizens();  break;
            case 3: admin_assign_dept();      break;
            case 4: admin_db_maintenance();   break;
            case 5: admin_analytics();        break;
            case 6: hist_display();           break;
            case 0: printf("  Admin logged out.\n"); break;
        }
        if (choice != 0) press_enter();
    } while (choice != 0);
}

/* ═══════════════════════════════════════════════════════════════
 *  MAIN ENTRY POINT
 * ═══════════════════════════════════════════════════════════════ */
int main(void) {
    /* ── Startup Banner ── */
    printf("\n");
    print_line('=', 68);
    printf("   RURAL PUBLIC SERVICE REQUEST & GRIEVANCE MANAGEMENT SYSTEM\n");
    printf("   Government of India  |  Panchayat Service Portal\n");
    printf("   Citizen  ->  Official (Field Technician)  ->  Admin\n");
    print_line('=', 68);
    printf("\n  Loading local data files...\n");

    /* ── Load all persistent data from text files ── */
    load_officials();
    load_citizens();
    load_grievances();
    load_history();

    /* ── Auto-seed if no grievance data found ── */
    {
        FILE *chk = fopen(FILE_GRIEVANCES, "r");
        if (!chk) {
            printf("  First run detected — initializing default database...\n");
            seed_initial_data();
        } else {
            fclose(chk);
            int gc = 0;
            GrievanceNode *c = ledger_head;
            while (c) { gc++; c = c->next; }
            printf("  Data loaded:  Citizens=%d | Officials=%d | "
                   "Grievances=%d\n",
                   citizen_count, official_count, gc);
        }
    }

    /* ── Main Menu ── */
    int choice;
    do {
        printf("\n");
        print_header("MAIN MENU — Select User Type");
        printf("  1. Citizen Portal\n");
        printf("  2. Official Portal  (Field Technician)\n");
        printf("  3. Admin Portal\n");
        printf("  0. Exit & Save\n");
        choice = read_int("Choice: ", 0, 3);

        switch (choice) {
            case 1: portal_citizen();  break;
            case 2: portal_official(); break;
            case 3: portal_admin();    break;
            case 0:
                save_all();
                printf("\n  All data saved to local text files (*.txt).\n");
                printf("  Goodbye!\n\n");
                break;
        }
    } while (choice != 0);

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
