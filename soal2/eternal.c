#include "arena.h"
#include <termios.h>
#include <errno.h>

// === Globals ===
static int   msgq_id   = -1;
static pid_t my_pid;
static char  my_username[MAX_USERNAME];
static int   my_pidx   = -1;   // player index di SHM (Shared Memory)
static int   my_gold   = 150;
static int   my_lvl    = 1;
static int   my_xp     = 0;
static int   my_weapon_idx = -1;  /* -1 = no weapon */

// === Terminal Utilities ===
static struct termios orig_termios;
 
static void term_raw(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 1;  // this means a 0.1s timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
 
static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
 
static void clear_screen(void) {
    printf("\033[2J\033[H");
}

// === Send Request Helpers ===
static void send_req(int cmd, const char *d1, const char *d2, int idata) {
    IpcMsg m;
    memset(&m, 0, sizeof(m));
    m.mtype      = 1;       // server listens on mtype=1
    m.cmd        = cmd;
    m.sender_pid = my_pid;
    if (d1) strncpy(m.data1, d1, MAX_USERNAME - 1);
    if (d2) strncpy(m.data2, d2, MAX_PASSWORD - 1);
    m.idata = idata;
    msgsnd(msgq_id, &m, sizeof(m) - sizeof(long), 0);
}
 
// Wait for a response from server (mtype = my_pid)
static int recv_resp(IpcResp *r, int timeout_sec) {
    time_t deadline = time(NULL) + timeout_sec;
    while (time(NULL) < deadline) {
        int rc = msgrcv(msgq_id, r, sizeof(*r) - sizeof(long), (long)my_pid, IPC_NOWAIT);
        if (rc >= 0) {
            return 1;
        }
        if (errno != ENOMSG) {
            return 0;
        }
        usleep(100000);
    }
    return 0;
}

// === ASCII Banner ===
static void print_banner(void) {
    printf("\033[0;33m");
    printf(" _____ _____ _____ _____ __    _____    _____ _____ \n");
    printf("| __  |  _  |_   _|_   _|  |  |   __|  |     |   __|\n");
    printf("| __ -|     | | |   | | |  |__|   __|  |  |  |   __|\n");
    printf("|_____|__|__| |_|   |_| |_____|_____|  |_____|__|   \n");
    printf("\033[0m");

    printf("\033[1;36m");
    printf(" _____ _____ _____ _____ _ _____ _____              \n");
    printf("|   __|_   _|   __| __  |_|     |   | |             \n");
    printf("|   __| | | |   __|    -| |  |  | | | |             \n");
    printf("|_____| |_| |_____|__|__|_|_____|_|___|             \n");
    printf("\033[0m\n");
}

// === Refresh Player Info from SHM (via login response) ===
static void print_profile(void) {
    printf("\n\033[1;33m=== PROFILE ===\033[0m\n");
    printf("Name : %-16s Lvl : %d\n", my_username, my_lvl);
    printf("Gold : %-16d XP  : %d\n", my_gold, my_xp);
    printf("\n");
}

// === Register ===
static void do_register(void) {
    char uname[MAX_USERNAME], pass[MAX_PASSWORD];
    printf("\033[1;32mCREATE ACCOUNT\033[0m\n");
    printf("Username: "); fflush(stdout);
    scanf("%31s", uname);
    printf("Password: "); fflush(stdout);
    scanf("%31s", pass);
 
    send_req(MSG_REGISTER, uname, pass, 0);
 
    IpcResp r;
    if (recv_resp(&r, 5)) {
        if (r.status == RESP_OK) {
            printf("\033[1;32m%s\033[0m\n", r.msg);
        } else {
            printf("\033[1;31m%s\033[0m\n", r.msg);
        }
    } else {
        printf("Server timeout.\n");
    }
    printf("Press [ENTER]...");
    getchar();
    getchar();
}

// === Login ===
static int do_login(void) {
    char uname[MAX_USERNAME], pass[MAX_PASSWORD];
    printf("\033[1;36mLOGIN\033[0m\n");
    printf("Username: "); fflush(stdout);
    scanf("%31s", uname);
    printf("Password: "); fflush(stdout);
    scanf("%31s", pass);
 
    send_req(MSG_LOGIN, uname, pass, 0);
 
    IpcResp r;
    if (recv_resp(&r, 5)) {
        if (r.status == RESP_OK) {
            printf("\033[1;32m%s\033[0m\n", r.msg);
            strncpy(my_username, uname, MAX_USERNAME - 1);
            my_pidx       = r.idata;
            my_gold       = r.p_gold;
            my_lvl        = r.p_lvl;
            my_xp         = r.p_xp;
            my_weapon_idx = r.p_weapon_idx;
            printf("Press [ENTER]..."); getchar(); getchar();
            return 1;
        } else {
            printf("\033[1;31m%s\033[0m\n", r.msg);
        }
    } else {
        printf("Server timeout.\n");
    }
    printf("Press [ENTER]...");
    getchar();
    getchar();

    return 0;
}

// === Armory ===
static void do_armory(void) {
    clear_screen();
    printf("\033[1;33m=== ARMORY ===\033[0m\n");
    printf("Gold: %d\n\n", my_gold);

    printf("+----+----------------+---------+-----------+\n");
    printf("|    | %-14s | %-7s | %-7s |\n", "Name", "Price", "Bonus DMG");
    printf("+----+----------------+---------+-----------+\n");

    for (int i = 0; i < NUM_WEAPONS; i++) {
        int owned = (my_weapon_idx == i);
        printf("| %d. | %-14s | %5d G | +%-4d Dmg | %s\n",
               i + 1,
               WEAPONS[i].name,
               WEAPONS[i].cost,
               WEAPONS[i].bonus_dmg,
               owned ? "\033[1;32m[HIGHEST OWNED]\033[0m" : "");
    }

    printf("+----+----------------+---------+-----------+\n\n");

    printf("0. Back | Choice: ");
    fflush(stdout);
 
    int choice;
    scanf("%d", &choice);
    if (choice < 1 || choice > NUM_WEAPONS) {
        return;
    }
 
    send_req(MSG_BUY_WEAPON, my_username, NULL, choice - 1);
 
    IpcResp r;
    if (recv_resp(&r, 5)) {
        if (r.status == RESP_OK) {
            printf("\033[1;32m%s\033[0m\n", r.msg);
            /* Sync gold dan weapon terbaru dari server */
            my_gold        = r.p_gold;
            my_weapon_idx  = r.p_weapon_idx;
        } else {
            printf("\033[1;31m%s\033[0m\n", r.msg);
        }
    }
    printf("Press [ENTER]...");
    getchar();
    getchar();
}

// === History ===
static void do_history(void) {
    clear_screen();
    send_req(MSG_GET_HISTORY, my_username, NULL, 0);
 
    printf("\033[1;33m=== MATCH HISTORY ===\033[0m\n");
    printf("+----------+------------------+--------+----------+\n");
    printf("| %-8s | %-16s | %-6s | %-8s |\n", "Time", "Opponent", "Res", "XP");
    printf("+----------+------------------+--------+----------+\n");
 
    IpcResp r;
    int got_any = 0;
    while (1) {
        if (!recv_resp(&r, 3)) {
            break;
        }
        if (r.status == RESP_OK) {
            break;      // sentinel
        }
 
        if (r.status == RESP_HISTORY_DATA) {
            struct tm *t = localtime(&r.rec.timestamp);
            char timebuf[16];
            strftime(timebuf, sizeof(timebuf), "%H:%M", t);
 
            int is_win = (strcmp(r.rec.result, "WIN") == 0);
            printf("| %-8s | %-16s | \033[%sm%-6s\033[0m | +%-4d XP |\n",
                   timebuf,
                   r.rec.opponent,
                   is_win ? "1;32" : "1;31",
                   r.rec.result,
                   r.rec.xp_gained);
            got_any = 1;
        }
    }

    printf("+----------+------------------+--------+----------+\n");
 
    if (!got_any) {
        printf("\nNo battles yet.\n");
    }
 
    printf("\nPress any key...");
    getchar();
    getchar();
}

// === Battle User Interface (UI) ===
#define LOG_LINES 5
 
typedef struct {
    char  lines[LOG_LINES][64];
    int   head;
} CombatLog;
 
static void log_push(CombatLog *log, const char *msg) {
    strncpy(log->lines[log->head % LOG_LINES], msg, 63);
    log->head++;
}
 
static void render_battle(const char *self_name, const char *opp_name,
                           int hp_self, int max_self,
                           int hp_opp,  int max_opp,
                           int self_lvl, int opp_lvl,
                           int self_wpn, int opp_wpn,
                           CombatLog *cl,
                           double cd_remain) {
    clear_screen();
    printf("\033[1;33m=== ARENA ===\033[0m\n\n");
 
    // Opponent bar (weapon hidden)
    printf("  %-12s  Lvl %d\n",
           opp_name, opp_lvl);
 
    int bar_opp = (max_opp > 0) ? (hp_opp * 20 / max_opp) : 0;
    if (bar_opp < 0) {
        bar_opp = 0;
    }
    printf("  \033[1;31m[ ");
    for (int i = 0; i < 20; i++) {
        printf(i < bar_opp ? "#" : " ");
    }
    printf(" ] %d/%d\033[0m\n\n", hp_opp > 0 ? hp_opp : 0, max_opp);
 
    printf("              VS\n\n");
 
    // Self bar
    printf("  %-12s  Lvl %d  | Weapon: %s\n",
           self_name, self_lvl,
           self_wpn >= 0 ? WEAPONS[self_wpn].name : "None");
 
    int bar_self = (max_self > 0) ? (hp_self * 20 / max_self) : 0;
    if (bar_self < 0) {
        bar_self = 0;
    }
    printf("  \033[1;32m[ ");
    for (int i = 0; i < 20; i++) {
        printf(i < bar_self ? "#" : " ");
    }
    printf(" ] %d/%d\033[0m\n\n", hp_self > 0 ? hp_self : 0, max_self);
 
    printf("  Combat Log:\n");
    for (int i = 0; i < LOG_LINES; i++) {
        int idx = (cl->head - LOG_LINES + i + LOG_LINES) % LOG_LINES;
        printf("  > %s\n", cl->lines[idx]);
    }
 
    if (cd_remain > 0) {
        printf("\n  CD: Atk(\033[33m%.1fs\033[0m) | Ult(\033[33m%.1fs\033[0m)\n", cd_remain, cd_remain);
    } else {
        printf("\n  CD: Atk(\033[32m0.0s\033[0m) | Ult(\033[32m0.0s\033[0m)\n");
    }
 
    printf("  [A] Attack  [U] Ultimate\n");
    fflush(stdout);
}
 
// Battle thread: reads server updates asynchronously
typedef struct {
    int  battle_idx;
    int  hp_self, hp_opp;
    int  max_self, max_opp;
    int  opp_lvl, self_lvl;
    int  self_wpn, opp_wpn;
    char opp_name[MAX_USERNAME];
    int  battle_over;   // 0 = ongoing, 1 = won, 2 = lost
    int  running;
    CombatLog log;
    pthread_mutex_t mu;
    int  msgq;
    pid_t pid;
} BattleState;
 
static void *battle_recv_thread(void *arg) {
    BattleState *bs = (BattleState *)arg;
    IpcResp r;
 
    while (bs->running) {
        int rc = msgrcv(bs->msgq, &r, sizeof(r) - sizeof(long), (long)bs->pid, IPC_NOWAIT);
        if (rc < 0) {
            usleep(50000);
            continue;
        }

        pthread_mutex_lock(&bs->mu);
        if (r.status == RESP_BATTLE_UPDATE) {
            bs->hp_self  = r.hp_self;
            bs->hp_opp   = r.hp_opp;
            bs->self_wpn = r.self_weapon;
            bs->opp_wpn  = r.opp_weapon;
            bs->self_lvl = r.self_lvl;
            bs->opp_lvl  = r.opp_lvl;

            if (r.max_hp_self > 0) {
                bs->max_self = r.max_hp_self;
            }
            if (r.max_hp_opp  > 0) {
                bs->max_opp  = r.max_hp_opp;
            }

            /* Tampilkan di combat log hanya jika ada damage baru */
            if (r.last_dmg > 0) {
                char buf[64];
                if (r.is_ultimate) {
                    snprintf(buf, sizeof(buf), "You used Ultimate for %d dmg!", r.last_dmg);
                } else {
                    snprintf(buf, sizeof(buf), "You hit for %d damage!", r.last_dmg);
                }
                log_push(&bs->log, buf);
            }
        } else if (r.status == RESP_BATTLE_END) {
            bs->hp_self     = r.hp_self;
            bs->hp_opp      = r.hp_opp;
            bs->self_lvl    = r.self_lvl;
            bs->opp_lvl     = r.opp_lvl;

            if (r.max_hp_self > 0) {
                bs->max_self = r.max_hp_self;
            }
            if (r.max_hp_opp  > 0) {
                bs->max_opp  = r.max_hp_opp;
            }

            bs->battle_over = r.battle_over;
            bs->running     = 0;
        } else if (r.status == RESP_FAIL) {
            char buf[64];
            snprintf(buf, sizeof(buf), "! %.58s", r.msg);
            log_push(&bs->log, buf);
        }
        pthread_mutex_unlock(&bs->mu);
    }
    return NULL;
}
 
static void do_battle(void) {
    // Request matchmaking
    send_req(MSG_MATCHMAKE, my_username, NULL, my_pidx);
 
    // Wait for RESP_MATCH_SEARCHING or RESP_FAIL
    IpcResp r;
    clear_screen();
    printf("Requesting matchmaking...\n");
 
    if (!recv_resp(&r, 5)) {
        printf("Server timeout.\n");
        printf("Press [ENTER]...");
        getchar();
        getchar();
        return;
    }
    if (r.status == RESP_FAIL) {
        printf("\033[1;31m%s\033[0m\n", r.msg);
        printf("Press [ENTER]...");
        getchar();
        getchar();
        return;
    }
 
    // Waiting for match
    clear_screen();
    print_banner();
    // printf("  Searching for an opponent... [%ds]\n", MATCHMAKING_TIMEOUT);
    fflush(stdout);
 
    // Wait for RESP_MATCH_FOUND or RESP_MATCH_TIMEOUT
    int battle_idx = -1;
    int found = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < MATCHMAKING_TIMEOUT + 5) {
        int rc = msgrcv(msgq_id, &r, sizeof(r) - sizeof(long), (long)my_pid, IPC_NOWAIT);
        if (rc >= 0) {
            if (r.status == RESP_MATCH_FOUND) {
                battle_idx = r.idata;
                found = 1;
                break;
            } else if (r.status == RESP_FAIL) {
                printf("\033[1;31m%s\033[0m\n", r.msg);
                printf("Press [ENTER]...");
                getchar();
                getchar();
                return;
            }
        }
        int elapsed = (int)(time(NULL) - start);
        printf("\r  Searching for an opponent... [%ds]   ", MATCHMAKING_TIMEOUT - elapsed);
        fflush(stdout);
        usleep(200000);
    }

    if (!found) {
        printf("\nTimeout.\n");
        printf("Press [ENTER]...");
        getchar();
        getchar();
        return;
    }
 
    // Setup battle state
    BattleState bs;
    memset(&bs, 0, sizeof(bs));
    bs.battle_idx = battle_idx;
    bs.running    = 1;
    bs.msgq       = msgq_id;
    bs.pid        = my_pid;
    bs.self_lvl   = my_lvl;
    bs.opp_lvl    = 1;
    bs.max_self   = calc_health(my_xp);
    bs.max_opp    = BASE_HEALTH;
    bs.hp_self    = bs.max_self;
    bs.hp_opp     = bs.max_opp;
    bs.self_wpn   = -1;
    bs.opp_wpn    = -1;
    pthread_mutex_init(&bs.mu, NULL);
 
    // Wait for first RESP_BATTLE_UPDATE to get opp name
    IpcResp first;
    int got_first = 0;
    time_t ts = time(NULL);
    while (time(NULL) - ts < 5) {
        int rc = msgrcv(msgq_id, &first, sizeof(first) - sizeof(long), (long)my_pid, IPC_NOWAIT);
        if (rc >= 0 && first.status == RESP_BATTLE_UPDATE) {
            strncpy(bs.opp_name, first.opp_name, MAX_USERNAME - 1);
            bs.hp_self  = first.hp_self;
            bs.hp_opp   = first.hp_opp;
            bs.self_wpn = first.self_weapon;
            bs.opp_wpn  = first.opp_weapon;
            got_first = 1;
            break;
        }
        usleep(100000);
    }
    if (!got_first) strncpy(bs.opp_name, "???", MAX_USERNAME - 1);
 
    // Start receive thread
    pthread_t recv_t;
    pthread_create(&recv_t, NULL, battle_recv_thread, &bs);
 
    // Enable raw terminal
    term_raw();
 
    time_t last_atk_time = 0;
    char input;
 
    while (1) {
        pthread_mutex_lock(&bs.mu);
        int over = bs.battle_over;
        double cd_remain = 0;
        if (time(NULL) - last_atk_time < ATTACK_COOLDOWN) {
            cd_remain = ATTACK_COOLDOWN - (double)(time(NULL) - last_atk_time);
        }

        render_battle(my_username, bs.opp_name,
                      bs.hp_self, bs.max_self,
                      bs.hp_opp,  bs.max_opp,
                      bs.self_lvl, bs.opp_lvl,
                      bs.self_wpn, bs.opp_wpn,
                      &bs.log, cd_remain);
        pthread_mutex_unlock(&bs.mu);
 
        if (over) {
            usleep(500000);     // tunggu 0.5s agar recv_thread selesai set state
            break;
        }

        // Non-blocking read
        if (read(STDIN_FILENO, &input, 1) == 1) {
            if (input == 'a' || input == 'A') {
                time_t now = time(NULL);
                if (now - last_atk_time >= ATTACK_COOLDOWN) {
                    send_req(MSG_ATTACK, my_username, NULL, battle_idx);
                    last_atk_time = now;
                    // note: combat log di isi oleh battle_recv_thread
                } else {
                    pthread_mutex_lock(&bs.mu);
                    log_push(&bs.log, "Cooldown active.");
                    pthread_mutex_unlock(&bs.mu);
                }
            } else if (input == 'u' || input == 'U') {
                time_t now = time(NULL);
                if (now - last_atk_time >= ATTACK_COOLDOWN) {
                    send_req(MSG_ULTIMATE, my_username, NULL, battle_idx);
                    last_atk_time = now;
                    // note: combat log di isi oleh battle_recv_thread
                } else {
                    pthread_mutex_lock(&bs.mu);
                    log_push(&bs.log, "Cooldown active.");
                    pthread_mutex_unlock(&bs.mu);
                }
            }
        }
 
        usleep(100000);     // 100ms render loop
    }
 
    bs.running = 0;
    pthread_join(recv_t, NULL);
    term_restore();
    
    // flush sisa karakter dari raw mode
    int c;
    while ((c = getchar()) != EOF && c != '\n');

    pthread_mutex_lock(&bs.mu);
    int won = (bs.battle_over == 1);
    pthread_mutex_unlock(&bs.mu);
 
    // Show result
    clear_screen();
    if (won) {
        printf("\033[1;32m=== VICTORY ===\033[0m\n");
        printf("You defeated %s!\n", bs.opp_name);
        printf("+50 XP  +120 Gold\n");
        my_xp   += 50; my_gold += 120;
    } else {
        printf("\033[1;31m=== DEFEAT ===\033[0m\n");
        printf("You were defeated by %s.\n", bs.opp_name);
        printf("+15 XP  +30 Gold\n");
        my_xp   += 15; my_gold += 30;
    }
    if (my_xp >= my_lvl * 100) {
        my_lvl++;
    }
 
    printf("\nBattle ended. Press [ENTER] to continue...");
    fflush(stdout);
    pthread_mutex_destroy(&bs.mu);
    getchar();
}

// === Main Menu (after successful login) ===
static void game_menu(void) {
    while (1) {
        clear_screen();
        print_banner();
        print_profile();
        
        printf("\033[1;33m=== OPTION ===\033[0m\n");
        printf("1. Battle\n");
        printf("2. Armory\n");
        printf("3. History\n");
        printf("4. Logout\n\n");
        printf("> Choice: ");
        fflush(stdout);
 
        int ch;
        scanf("%d", &ch);
 
        switch (ch) {
            case 1:
                do_battle();
                break;
            case 2:
                do_armory();
                break;
            case 3:
                do_history();
                break;
            case 4:
                send_req(MSG_LOGOUT, my_username, NULL, 0);
                IpcResp r;
                recv_resp(&r, 3);
                return;
            default:
                break;
        }
    }
}

// === Main Function ===
int main(void) {
    my_pid = getpid();
 
    // Connect to message queue
    msgq_id = msgget(MSG_KEY_MAIN, 0666);
    if (msgq_id < 0) {
        printf("\033[1;31mOrion are you there?\033[0m\n\n");
        return 1;
    }
 
    int ch;
 
    while (1) {
        clear_screen();
        print_banner();
        printf("1. Register\n");
        printf("2. Login\n");
        printf("3. Exit\n");
        printf("Choice: ");
        fflush(stdout);
 
        if (scanf("%d", &ch) != 1) {
            getchar();
            continue;
        }
 
        switch (ch) {
            case 1:
                clear_screen();
                print_banner();
                do_register();
                break;
            case 2:
                clear_screen();
                print_banner();
                if (do_login()) {
                    game_menu();
                }
                break;
            case 3:
                printf("Be safe, Warrior.\n");
                return 0;
            default:
                break;
        }
    }
}