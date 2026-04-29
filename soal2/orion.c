#include "arena.h"
#include <errno.h>

#define SAVE_FILE "players.dat"     // file persistence

// === Globals (IPC IDs) ===
static int shm_id_players = -1;
static int shm_id_battles = -1;
static int shm_id_queue = -1;
static int msgq_id = -1;
static int sem_id_players = -1;
static int sem_id_battles = -1;
static int sem_id_queue = -1;
 
static ShmPlayers *shm_players = NULL;
static ShmBattles *shm_battles = NULL;
static ShmQueue *shm_queue = NULL;

// === Save ===
static void save_players(void) {
    sem_lock(sem_id_players);
    FILE *f = fopen(SAVE_FILE, "wb");
    if (f) {
        fwrite(shm_players, sizeof(ShmPlayers), 1, f);
        fclose(f);
    }
    sem_unlock(sem_id_players);
}

// === Load ===
static void load_players(void) {
    char cwd[256];
    getcwd(cwd, sizeof(cwd));

    FILE *f = fopen(SAVE_FILE, "rb");
    if (f) {
        size_t n = fread(shm_players, sizeof(ShmPlayers), 1, f);
        fclose(f);
        if (n != 1) {
            printf("[Orion] Save file corrupt, starting fresh.\n");
            shm_players->count = 0;
            return;
        }
        // Validate player count
        if (shm_players->count < 0 || shm_players->count > MAX_PLAYERS) {
            printf("[Orion] Save file invalid count=%d, starting fresh.\n",
                   shm_players->count);
            shm_players->count = 0;
            return;
        }
        // Reset all login state (in_use) when server starts
        for (int i = 0; i < shm_players->count; i++) {
            shm_players->entries[i].p.in_use = 0;
            shm_players->entries[i].p.pid    = 0;
        }
        printf("[Orion] Loaded %d player(s) from %s\n", shm_players->count, SAVE_FILE);
    } else {
        shm_players->count = 0;
        printf("[Orion] No save file found, starting fresh.\n");
    }
}

// === Find Player By Username ===
static int find_player(const char *username) {
    for (int i = 0; i < shm_players->count; i++) {
        if (strcmp(shm_players->entries[i].p.username, username) == 0) {
            return i;
        }
    }
    return -1;
}

// === Send Response Helper ===
static void send_resp(pid_t pid, int status, const char *msg, int idata) {
    IpcResp r;
    memset(&r, 0, sizeof(r));
    r.mtype  = (long)pid;
    r.status = status;
    r.idata  = idata;
    if (msg) strncpy(r.msg, msg, sizeof(r.msg) - 1);
    msgsnd(msgq_id, &r, sizeof(r) - sizeof(long), 0);
}

// === Find Free Battle Slot ===
static int find_free_battle(void) {
    for (int i = 0; i < MAX_BATTLES; i++) {
        if (!shm_battles->battles[i].active) {
            return i;
        }
    }
    return -1;
}

// === Bot Thread (match with bot) ===
typedef struct {
    int battle_idx;
    int bot_slot;       // 1 = bot is player1, 2 = bot is player2
} BotArg;
 
static void *bot_thread(void *arg) {
    BotArg *ba = (BotArg *)arg;
    int bidx = ba->battle_idx;
    int bot_slot = ba->bot_slot;
    free(ba);
 
    while (1) {
        sem_lock(sem_id_battles);
        Battle *b = &shm_battles->battles[bidx];
 
        if (!b->active || b->finished) {
            sem_unlock(sem_id_battles);
            break;
        }
 
        time_t now = time(NULL);
        int    *opp_hp   = (bot_slot == 1) ? &b->hp2 : &b->hp1;
        time_t *bot_last = (bot_slot == 1) ? &b->last_atk1 : &b->last_atk2;
 
        if (now - *bot_last >= ATTACK_COOLDOWN) {
            int dmg = BASE_DAMAGE + (rand() % 5);   // Bot damage: random around base
            *opp_hp -= dmg;
            *bot_last = now;
 
            if (*opp_hp <= 0) {
                b->finished = (bot_slot == 1) ? 1 : 2;
            }
        }
 
        sem_unlock(sem_id_battles);
 
        if (shm_battles->battles[bidx].finished) {
            break;
        }
        usleep(300000);     // check every 0.3 seconds
    }
    return NULL;
}

// === Battle Thread (Notification to both clients) ===
typedef struct {
    int battle_idx;
    int p1_idx;
    int p2_idx;      // if -1 = bot
    pid_t pid1;
    pid_t pid2;      // if 0 = bot
} BattleArg;

static void send_battle_update(int msgq, pid_t pid, Battle *b, 
                               int self_slot,   // 1 or 2
                               const char *opp_name, 
                               int self_weapon, int opp_weapon,
                               int self_lvl, int opp_lvl,
                               int last_dmg, int is_ultimate) {
    if (pid == 0)  {
        return;   // if is a bot
    }
    IpcResp r;
    memset(&r, 0, sizeof(r));
    r.mtype  = (long)pid;
    r.status = RESP_BATTLE_UPDATE;
    r.hp_self = (self_slot == 1) ? b->hp1 : b->hp2;
    r.hp_opp  = (self_slot == 1) ? b->hp2 : b->hp1;
    strncpy(r.opp_name, opp_name, MAX_USERNAME - 1);
    r.self_weapon = self_weapon;
    r.opp_weapon  = opp_weapon;
    r.battle_over = 0;
    r.self_lvl    = self_lvl;
    r.opp_lvl     = opp_lvl;
    r.last_dmg    = last_dmg;
    r.is_ultimate = is_ultimate;
    r.max_hp_self = (self_slot == 1) ? b->max_hp1 : b->max_hp2;
    r.max_hp_opp  = (self_slot == 1) ? b->max_hp2 : b->max_hp1;
    msgsnd(msgq, &r, sizeof(r) - sizeof(long), 0);
}

static void send_battle_end(int msgq, pid_t pid, int won, const char *opp_name,
                            int self_weapon, int opp_weapon, int hp_self, int hp_opp,
                            int self_lvl, int opp_lvl, int max_hp_self, int max_hp_opp) {
    if (pid == 0) {
        return;
    }
    IpcResp r;
    memset(&r, 0, sizeof(r));
    r.mtype       = (long)pid;
    r.status      = RESP_BATTLE_END;
    r.battle_over = won ? 1 : 2;
    r.hp_self     = hp_self;
    r.hp_opp      = hp_opp;
    strncpy(r.opp_name, opp_name, MAX_USERNAME - 1);
    r.self_weapon = self_weapon;
    r.opp_weapon  = opp_weapon;
    r.self_lvl    = self_lvl;
    r.opp_lvl     = opp_lvl;
    r.max_hp_self = max_hp_self;
    r.max_hp_opp  = max_hp_opp;
    msgsnd(msgq, &r, sizeof(r) - sizeof(long), 0);
}
 
static void *battle_thread(void *arg) {
    BattleArg *ba = (BattleArg *)arg;
    int bidx  = ba->battle_idx;
    int p1idx = ba->p1_idx;
    int p2idx = ba->p2_idx;
    pid_t pid1 = ba->pid1;
    pid_t pid2 = ba->pid2;
    free(ba);
 
    // Get name, weapon, and lvl of both player
    char name1[MAX_USERNAME], name2[MAX_USERNAME];
    int  wpn1, wpn2, lvl1, lvl2;
 
    sem_lock(sem_id_players);
    strncpy(name1, shm_players->entries[p1idx].p.username, MAX_USERNAME - 1);
    wpn1 = shm_players->entries[p1idx].p.weapon_idx;
    lvl1 = shm_players->entries[p1idx].p.lvl;
    if (p2idx >= 0) {
        strncpy(name2, shm_players->entries[p2idx].p.username, MAX_USERNAME - 1);
        wpn2 = shm_players->entries[p2idx].p.weapon_idx;
        lvl2 = shm_players->entries[p2idx].p.lvl;
    } else {
        strncpy(name2, "Wild Beast", MAX_USERNAME - 1);
        wpn2 = -1;
        lvl2 = 1;
    }
    sem_unlock(sem_id_players);
 
    // Send the first update
    sem_lock(sem_id_battles);
    Battle *b = &shm_battles->battles[bidx];
    send_battle_update(msgq_id, pid1, b, 1, name2, wpn1, wpn2, lvl1, lvl2, 0, 0);
    if (pid2) {
        send_battle_update(msgq_id, pid2, b, 2, name1, wpn2, wpn1, lvl2, lvl1, 0, 0);
    }
    sem_unlock(sem_id_battles);
 
    // Monitor loop
    int update_tick = 0;
    while (1) {
        usleep(100000);     // 100ms
 
        sem_lock(sem_id_battles);
        b = &shm_battles->battles[bidx];
 
        if (!b->active) {
            sem_unlock(sem_id_battles); 
            break;
        }
 
        if (b->finished) {
            int p1_won = (b->finished == 1);
            int p2_won = (b->finished == 2);
 
            send_battle_end(msgq_id, pid1, p1_won, name2, wpn1, wpn2, b->hp1, b->hp2, 
                           lvl1, lvl2, b->max_hp1, b->max_hp2);

            if (pid2) send_battle_end(msgq_id, pid2, p2_won, name1, wpn2, wpn1, b->hp2, b->hp1, 
                           lvl2, lvl1, b->max_hp2, b->max_hp1);
 
            // Update stats
            sem_unlock(sem_id_battles);
            sem_lock(sem_id_players);
 
            PlayerEntry *e1 = &shm_players->entries[p1idx];
            // XP & gold
            int xp1 = p1_won ? 50 : 15;
            int xp2 = p2_won ? 50 : 15;
            int g1  = p1_won ? 120 : 30;
            int g2  = p2_won ? 120 : 30;
 
            e1->p.xp   += xp1;
            e1->p.gold += g1;
            if (e1->p.xp >= e1->p.lvl * 100) {
                e1->p.lvl++;
            }
 
            // History for p1
            if (e1->history_count < MAX_HISTORY) {
                MatchRecord *rec = &e1->history[e1->history_count++];
                strncpy(rec->opponent, name2, MAX_USERNAME - 1);
                strncpy(rec->result, p1_won ? "WIN" : "LOSS", 7);
                rec->xp_gained  = xp1;
                rec->timestamp  = time(NULL);
            }
 
            if (p2idx >= 0) {
                PlayerEntry *e2 = &shm_players->entries[p2idx];
                e2->p.xp   += xp2;
                e2->p.gold += g2;
                if (e2->p.xp >= e2->p.lvl * 100) {
                    e2->p.lvl++;
                }
 
                if (e2->history_count < MAX_HISTORY) {
                    MatchRecord *rec = &e2->history[e2->history_count++];
                    strncpy(rec->opponent, name1, MAX_USERNAME - 1);
                    strncpy(rec->result, p2_won ? "WIN" : "LOSS", 7);
                    rec->xp_gained  = xp2;
                    rec->timestamp  = time(NULL);
                }
            }
 
            sem_unlock(sem_id_players);
            save_players();
 
            // Cleanup battle
            sem_lock(sem_id_battles);
            shm_battles->battles[bidx].active = 0;
            sem_unlock(sem_id_battles);
            break;
        }

        // Send HP update to client (only if battle isnt finished)
        update_tick++;
        if (update_tick % 3 == 0) {     // every ~300ms
            // Get and reset last_dmg so it only appears onece in combat log
            int dmg1 = b->last_dmg1; b->last_dmg1 = 0;
            int dmg2 = b->last_dmg2; b->last_dmg2 = 0;
            int ult1 = b->last_ult1; b->last_ult1 = 0;
            int ult2 = b->last_ult2; b->last_ult2 = 0;

            send_battle_update(msgq_id, pid1, b, 1, name2, wpn1, wpn2, lvl1, lvl2, dmg1, ult1);
            if (pid2) send_battle_update(msgq_id, pid2, b, 2, name1, wpn2, wpn1, lvl2, lvl1, dmg2, ult2);
        }

        sem_unlock(sem_id_battles);
    }
    return NULL;
}

// === Matchmaking Thread ===
typedef struct {
    int    player_idx;
    pid_t  pid;
    time_t join_time;
    int    queue_slot;
} MatchArg;
 
static void start_battle(int p1idx, pid_t pid1, int p2idx, pid_t pid2) {
    sem_lock(sem_id_battles);
    int bidx = find_free_battle();
    if (bidx < 0) {
        sem_unlock(sem_id_battles);
        send_resp(pid1, RESP_FAIL, "No battle slot available.", 0);
        if (pid2) send_resp(pid2, RESP_FAIL, "No battle slot available.", 0);
        return;
    }

    Battle *b = &shm_battles->battles[bidx];
    memset(b, 0, sizeof(Battle));
    b->active     = 1;
    b->player1_idx = p1idx;
    b->player2_idx = p2idx;
 
    sem_lock(sem_id_players);
    b->hp1 = calc_health(shm_players->entries[p1idx].p.xp);
    b->hp2 = (p2idx >= 0)
             ? calc_health(shm_players->entries[p2idx].p.xp)
             : BASE_HEALTH;
    b->max_hp1 = b->hp1;
    b->max_hp2 = b->hp2;
    sem_unlock(sem_id_players);
 
    b->last_atk1 = 0;
    b->last_atk2 = 0;
    b->finished   = 0;
    sem_unlock(sem_id_battles);
 
    // Notify clients
    char buf[64];
    snprintf(buf, sizeof(buf), "BATTLE_START:%d", bidx);
    send_resp(pid1, RESP_MATCH_FOUND, buf, bidx);
    if (pid2) send_resp(pid2, RESP_MATCH_FOUND, buf, bidx);
 
    // Start bot thread if p2 is bot
    if (p2idx < 0) {
        BotArg *ba = malloc(sizeof(BotArg));
        ba->battle_idx = bidx;
        ba->bot_slot   = 2;
        pthread_t bt;
        pthread_create(&bt, NULL, bot_thread, ba);
        pthread_detach(bt);
    }
 
    // Start monitor thread
    BattleArg *barg = malloc(sizeof(BattleArg));
    barg->battle_idx = bidx;
    barg->p1_idx     = p1idx;
    barg->p2_idx     = p2idx;
    barg->pid1       = pid1;
    barg->pid2       = pid2;
    pthread_t mt;
    pthread_create(&mt, NULL, battle_thread, barg);
    pthread_detach(mt);
}
 
static void *matchmaking_thread(void *arg) {
    MatchArg *ma = (MatchArg *)arg;
    int    my_idx   = ma->player_idx;
    pid_t  my_pid   = ma->pid;
    int    my_qslot = ma->queue_slot;
    free(ma);
 
    // Send "searching" acknowledgment to client
    send_resp(my_pid, RESP_MATCH_SEARCHING, "Searching for an opponent...", 0);
 
    time_t deadline = time(NULL) + MATCHMAKING_TIMEOUT;
 
    while (time(NULL) < deadline) {
        sleep(1);
 
        sem_lock(sem_id_queue);
 
        // Check if player is still in queue
        if (!shm_queue->queue[my_qslot].active) {
            sem_unlock(sem_id_queue);
            return NULL;
        }
 
        // Look for another waiting player
        for (int i = 0; i < MAX_QUEUE; i++) {
            if (i == my_qslot) continue;
            if (!shm_queue->queue[i].active) continue;
 
            int opp_pidx = shm_queue->queue[i].player_idx;
            pid_t opp_pid = shm_queue->queue[i].pid;
 
            // Pair found, then remove both from queue
            shm_queue->queue[my_qslot].active = 0;
            shm_queue->queue[i].active        = 0;
            shm_queue->size -= 2;
            sem_unlock(sem_id_queue);
 
            start_battle(my_idx, my_pid, opp_pidx, opp_pid);
            return NULL;
        }
 
        sem_unlock(sem_id_queue);
    }
 
    // Timeout, then remove from queue and match with bot instead
    sem_lock(sem_id_queue);
    shm_queue->queue[my_qslot].active = 0;
    shm_queue->size--;
    sem_unlock(sem_id_queue);
 
    start_battle(my_idx, my_pid, -1, 0);
    return NULL;
}

// === Request Handlers ===
static void handle_register(IpcMsg *req) {
    sem_lock(sem_id_players);
 
    if (shm_players->count >= MAX_PLAYERS) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Server full.", 0);
        return;
    }
 
    if (find_player(req->data1) >= 0) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Username already taken.", 0);
        return;
    }
 
    int idx = shm_players->count++;
    PlayerEntry *e = &shm_players->entries[idx];
    memset(e, 0, sizeof(PlayerEntry));
    strncpy(e->p.username, req->data1, MAX_USERNAME - 1);
    strncpy(e->p.password, req->data2, MAX_PASSWORD - 1);
    e->p.gold       = 150;
    e->p.lvl        = 1;
    e->p.xp         = 0;
    e->p.weapon_idx = -1;
    e->p.in_use     = 0;
 
    sem_unlock(sem_id_players);
    save_players();
    send_resp(req->sender_pid, RESP_OK, "Account created!", 0);
}
 
static void handle_login(IpcMsg *req) {
    sem_lock(sem_id_players);
    int idx = find_player(req->data1);
    if (idx < 0) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "User not found.", 0);
        return;
    }
 
    PlayerEntry *e = &shm_players->entries[idx];
    if (strcmp(e->p.password, req->data2) != 0) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Wrong password.", 0);
        return;
    }
 
    if (e->p.in_use) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Account already logged in.", 0);
        return;
    }
 
    e->p.in_use = 1;
    e->p.pid    = req->sender_pid;

    // Prepare response with full player data
    IpcResp r;
    memset(&r, 0, sizeof(r));
    r.mtype       = (long)req->sender_pid;
    r.status      = RESP_OK;
    r.idata       = idx;
    r.p_gold      = e->p.gold;
    r.p_lvl       = e->p.lvl;
    r.p_xp        = e->p.xp;
    r.p_weapon_idx = e->p.weapon_idx;
    strncpy(r.msg, "Welcome!", sizeof(r.msg) - 1);

    sem_unlock(sem_id_players);
    msgsnd(msgq_id, &r, sizeof(r) - sizeof(long), 0);
}
 
static void handle_logout(IpcMsg *req) {
    sem_lock(sem_id_players);
    int idx = find_player(req->data1);
    if (idx >= 0) {
        shm_players->entries[idx].p.in_use = 0;
        shm_players->entries[idx].p.pid    = 0;
    }
    sem_unlock(sem_id_players);
    save_players();
    send_resp(req->sender_pid, RESP_OK, "Logged out.", 0);
}
 
static void handle_matchmake(IpcMsg *req) {
    int pidx = req->idata;          // player index sent by client
 
    // Check player already in battle
    sem_lock(sem_id_battles);
    for (int i = 0; i < MAX_BATTLES; i++) {
        Battle *b = &shm_battles->battles[i];
        if (b->active && !b->finished && (b->player1_idx == pidx || b->player2_idx == pidx)) {
            sem_unlock(sem_id_battles);
            send_resp(req->sender_pid, RESP_FAIL, "Already in a battle.", 0);
            return;
        }
    }
    sem_unlock(sem_id_battles);
 
    // Add to queue
    sem_lock(sem_id_queue);
    int slot = -1;
    for (int i = 0; i < MAX_QUEUE; i++) {
        if (!shm_queue->queue[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        sem_unlock(sem_id_queue);
        send_resp(req->sender_pid, RESP_FAIL, "Queue full.", 0);
        return;
    }
    shm_queue->queue[slot].pid        = req->sender_pid;
    shm_queue->queue[slot].player_idx = pidx;
    shm_queue->queue[slot].join_time  = time(NULL);
    shm_queue->queue[slot].active     = 1;
    shm_queue->size++;
    sem_unlock(sem_id_queue);
 
    // Spawn matchmaking thread
    MatchArg *ma = malloc(sizeof(MatchArg));
    ma->player_idx = pidx;
    ma->pid        = req->sender_pid;
    ma->join_time  = time(NULL);
    ma->queue_slot = slot;
    pthread_t t;
    pthread_create(&t, NULL, matchmaking_thread, ma);
    pthread_detach(t);
}
 
static void handle_attack(IpcMsg *req) {
    int bidx = req->idata;
    int pidx;
 
    sem_lock(sem_id_players);
    pidx = find_player(req->data1);
    int xp   = (pidx >= 0) ? shm_players->entries[pidx].p.xp   : 0;
    int wpn  = (pidx >= 0) ? shm_players->entries[pidx].p.weapon_idx : -1;
    sem_unlock(sem_id_players);
 
    if (pidx < 0 || bidx < 0 || bidx >= MAX_BATTLES) {
        send_resp(req->sender_pid, RESP_FAIL, "Invalid attack.", 0);
        return;
    }
 
    sem_lock(sem_id_battles);
    Battle *b = &shm_battles->battles[bidx];
 
    if (!b->active || b->finished) {
        sem_unlock(sem_id_battles);
        send_resp(req->sender_pid, RESP_FAIL, "Battle not active.", 0);
        return;
    }
 
    int self_slot = (b->player1_idx == pidx) ? 1 : 2;
    time_t *last_atk = (self_slot == 1) ? &b->last_atk1 : &b->last_atk2;
    int    *opp_hp   = (self_slot == 1) ? &b->hp2 : &b->hp1;
 
    time_t now = time(NULL);
    if (now - *last_atk < ATTACK_COOLDOWN) {
        sem_unlock(sem_id_battles);
        send_resp(req->sender_pid, RESP_FAIL, "Cooldown active.", 0);
        return;
    }
 
    int dmg    = calc_damage(xp, wpn);
    *opp_hp   -= dmg;
    *last_atk  = now;
    
    // Keep damage info for combat log client
    if (self_slot == 1) {
        b->last_dmg1 = dmg; b->last_ult1 = 0;
    } else {
        b->last_dmg2 = dmg; b->last_ult2 = 0;
    }

    if (*opp_hp <= 0) {
        b->finished = self_slot;    // if self_slot wins
    }
 
    sem_unlock(sem_id_battles);
    // Battle monitor thread will then send an update
}
 
static void handle_ultimate(IpcMsg *req) {
    int bidx = req->idata;
    int pidx;
 
    sem_lock(sem_id_players);
    pidx = find_player(req->data1);
    int xp  = (pidx >= 0) ? shm_players->entries[pidx].p.xp  : 0;
    int wpn = (pidx >= 0) ? shm_players->entries[pidx].p.weapon_idx : -1;
    sem_unlock(sem_id_players);
 
    if (pidx < 0 || wpn < 0) {
        send_resp(req->sender_pid, RESP_FAIL, "No weapon equipped for Ultimate!", 0);
        return;
    }
 
    sem_lock(sem_id_battles);
    Battle *b = &shm_battles->battles[bidx];
 
    if (!b->active || b->finished) {
        sem_unlock(sem_id_battles);
        send_resp(req->sender_pid, RESP_FAIL, "Battle not active.", 0);
        return;
    }
 
    int self_slot = (b->player1_idx == pidx) ? 1 : 2;
    time_t *last_atk = (self_slot == 1) ? &b->last_atk1 : &b->last_atk2;
    int    *opp_hp   = (self_slot == 1) ? &b->hp2 : &b->hp1;
 
    time_t now = time(NULL);
    if (now - *last_atk < ATTACK_COOLDOWN) {
        sem_unlock(sem_id_battles);
        send_resp(req->sender_pid, RESP_FAIL, "Cooldown active.", 0);
        return;
    }
 
    int base_dmg = calc_damage(xp, wpn);
    int ult_dmg  = base_dmg * 3;
    *opp_hp    -= ult_dmg;
    *last_atk   = now;
    
    // Keep damage info for combat log client
    if (self_slot == 1) {
        b->last_dmg1 = ult_dmg; b->last_ult1 = 1;
    } else {
        b->last_dmg2 = ult_dmg; b->last_ult2 = 1;
    }

    if (*opp_hp <= 0) {
        b->finished = self_slot;
    }
 
    sem_unlock(sem_id_battles);
}
 
static void handle_buy_weapon(IpcMsg *req) {
    int wpn_idx = req->idata;
    if (wpn_idx < 0 || wpn_idx >= NUM_WEAPONS) {
        send_resp(req->sender_pid, RESP_FAIL, "Invalid weapon.", 0);
        return;
    }
 
    sem_lock(sem_id_players);
    int pidx = find_player(req->data1);
    if (pidx < 0) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Player not found.", 0);
        return;
    }
 
    PlayerEntry *e = &shm_players->entries[pidx];
    int cost = WEAPONS[wpn_idx].cost;
    
    // Check if weapon is already owned
    if (e->p.weapon_idx == wpn_idx) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "You already own this weapon.", 0);
        return;
    }

    if (e->p.gold < cost) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Not enough gold.", 0);
        return;
    }
 
    e->p.gold -= cost;

    // Always equip the weapon with the highest damage
    if (e->p.weapon_idx < 0 || WEAPONS[wpn_idx].bonus_dmg > WEAPONS[e->p.weapon_idx].bonus_dmg) {
        e->p.weapon_idx = wpn_idx;
    }
 
    int new_gold = e->p.gold;
    int new_weapon = e->p.weapon_idx;
    sem_unlock(sem_id_players);
    save_players();

    // Send response with the latest gold and weapon
    IpcResp r;
    memset(&r, 0, sizeof(r));
    r.mtype        = (long)req->sender_pid;
    r.status       = RESP_OK;
    r.idata        = new_gold;
    r.p_gold       = new_gold;
    r.p_weapon_idx = new_weapon;
    snprintf(r.msg, sizeof(r.msg), "Bought %s! Gold: %d", WEAPONS[wpn_idx].name, new_gold);
    msgsnd(msgq_id, &r, sizeof(r) - sizeof(long), 0);
}
 
static void handle_get_history(IpcMsg *req) {
    sem_lock(sem_id_players);
    int pidx = find_player(req->data1);
    if (pidx < 0) {
        sem_unlock(sem_id_players);
        send_resp(req->sender_pid, RESP_FAIL, "Player not found.", 0);
        return;
    }
 
    PlayerEntry *e = &shm_players->entries[pidx];
    int total = e->history_count;
 
    // Send each record as a separate message
    for (int i = total - 1; i >= 0 && i >= total - MAX_HISTORY; i--) {
        IpcResp r;
        memset(&r, 0, sizeof(r));
        r.mtype        = (long)req->sender_pid;
        r.status       = RESP_HISTORY_DATA;
        r.rec          = e->history[i];
        r.history_total = total;
        r.history_idx   = total - 1 - i;
        msgsnd(msgq_id, &r, sizeof(r) - sizeof(long), 0);
    }
 
    sem_unlock(sem_id_players);
 
    // Sentinel, end of history
    IpcResp end;
    memset(&end, 0, sizeof(end));
    end.mtype        = (long)req->sender_pid;
    end.status       = RESP_OK;
    end.history_total = total;
    end.history_idx   = -1;     // this one is sentinel
    msgsnd(msgq_id, &end, sizeof(end) - sizeof(long), 0);
}

// === Clean Up ===
static void cleanup(void) {
    save_players();
    printf("\n[Orion] Cleaning up IPC...\n");
 
    if (shm_players) shmdt(shm_players);
    if (shm_battles) shmdt(shm_battles);
    if (shm_queue)   shmdt(shm_queue);
 
    if (shm_id_players >= 0) shmctl(shm_id_players, IPC_RMID, NULL);
    if (shm_id_battles >= 0) shmctl(shm_id_battles, IPC_RMID, NULL);
    if (shm_id_queue   >= 0) shmctl(shm_id_queue,   IPC_RMID, NULL);
    if (msgq_id        >= 0) msgctl(msgq_id, IPC_RMID, NULL);
    if (sem_id_players >= 0) semctl(sem_id_players, 0, IPC_RMID);
    if (sem_id_battles >= 0) semctl(sem_id_battles, 0, IPC_RMID);
    if (sem_id_queue   >= 0) semctl(sem_id_queue,   0, IPC_RMID);
}

static void sighandler(int sig) {
    (void)sig;
    cleanup();
    exit(0);
}

// === Main Function ===
int main(void) {
    srand(time(NULL));
    signal(SIGINT,  sighandler);
    signal(SIGTERM, sighandler);
 
    // Initialize Shared Memory
    shm_id_players = shmget(SHM_KEY_PLAYERS, sizeof(ShmPlayers), IPC_CREAT | 0666);
    shm_id_battles = shmget(SHM_KEY_BATTLES, sizeof(ShmBattles), IPC_CREAT | 0666);
    shm_id_queue   = shmget(SHM_KEY_QUEUE,   sizeof(ShmQueue), IPC_CREAT | 0666);
 
    if (shm_id_players < 0 || shm_id_battles < 0 || shm_id_queue < 0) {
        perror("shmget"); exit(1);
    }
 
    shm_players = shmat(shm_id_players, NULL, 0);
    shm_battles = shmat(shm_id_battles, NULL, 0);
    shm_queue   = shmat(shm_id_queue,   NULL, 0);
 
    memset(shm_battles, 0, sizeof(ShmBattles));
    memset(shm_queue,   0, sizeof(ShmQueue));
 
    // Initialize Message Queue
    msgq_id = msgget(MSG_KEY_MAIN, IPC_CREAT | 0666);
    if (msgq_id < 0) {
        perror("msgget");
        exit(1);
    }
 
    // Initialize Semaphores
    sem_id_players = semget(SEM_KEY_PLAYERS, 1, IPC_CREAT | 0666);
    sem_id_battles = semget(SEM_KEY_BATTLES, 1, IPC_CREAT | 0666);
    sem_id_queue   = semget(SEM_KEY_QUEUE,   1, IPC_CREAT | 0666);
 
    if (sem_id_players < 0 || sem_id_battles < 0 || sem_id_queue < 0) {
        perror("semget");
        exit(1);
    }
 
    semctl(sem_id_players, 0, SETVAL, 1);
    semctl(sem_id_battles, 0, SETVAL, 1);
    semctl(sem_id_queue,   0, SETVAL, 1);
 
    // Load persistent data (from previously)
    load_players();
 
    printf("[Orion] Orion is ready (PID: %d)\n", getpid());
    fflush(stdout);
 
    // Looping to receive messages
    IpcMsg req;
    while (1) {
        if (msgrcv(msgq_id, &req, sizeof(req) - sizeof(long), 1, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("msgrcv");
            break;
        }
 
        switch (req.cmd) {
            case MSG_REGISTER:
                handle_register(&req);
                break;
            case MSG_LOGIN:
                handle_login(&req);
                break;
            case MSG_LOGOUT:
                handle_logout(&req);
                break;
            case MSG_MATCHMAKE:
                handle_matchmake(&req);
                break;
            case MSG_ATTACK:
                handle_attack(&req);
                break;
            case MSG_ULTIMATE:
                handle_ultimate(&req);
                break;
            case MSG_BUY_WEAPON:
                handle_buy_weapon(&req);
                break;
            case MSG_GET_HISTORY:
                handle_get_history(&req);
                break;
            default:
                send_resp(req.sender_pid, RESP_FAIL, "Unknown command.", 0);
        }
    }
 
    cleanup();
    return 0;
}