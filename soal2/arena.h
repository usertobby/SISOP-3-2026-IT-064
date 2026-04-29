#ifndef ARENA_H
#define ARENA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

// === IPC Keys ===
#define SHM_KEY_PLAYERS 0x00001234    // Shared memory: player data
#define SHM_KEY_BATTLES 0x00005678    // Shared memory: active battles
#define SHM_KEY_QUEUE 0x00009012    // Shared memory: matchmaking queue

#define MSG_KEY_MAIN 0x0000ABCD    // Message queue: client <to> server
#define SEM_KEY_PLAYERS 0x0000EF01    // Semaphore: player data
#define SEM_KEY_BATTLES 0x0000EF02    // Semaphore: battle data
#define SEM_KEY_QUEUE 0x0000EF03    // Semaphore: matchmaking queue

// === Game Configurations ===
#define MAX_PLAYERS 64
#define MAX_BATTLES 32
#define MAX_QUEUE 32
#define MAX_HISTORY 50
#define MAX_USERNAME 32
#define MAX_PASSWORD 32

#define BASE_DAMAGE 10
#define BASE_HEALTH 100
#define ATTACK_COOLDOWN 1   // in seconds
#define MATCHMAKING_TIMEOUT 35  // in seconds

// === Weapon Configurations ===
#define NUM_WEAPONS 5

typedef struct {
    char name[32];
    int cost;
    int bonus_dmg;
} Weapon;

static const Weapon WEAPONS[NUM_WEAPONS] = {
    {"Wood Sword", 100, 5},
    {"Iron Sword", 300, 15},
    {"Steel Axe", 600, 30},
    {"Demon Blade", 1500, 60},
    {"God Slayer", 5000, 150},
};

// === Message Types (for msg queue) ===
#define MSG_REGISTER 1
#define MSG_LOGIN 2
#define MSG_LOGOUT 3
#define MSG_MATCHMAKE 4
#define MSG_ATTACK 5
#define MSG_ULTIMATE 6
#define MSG_BUY_WEAPON 7
#define MSG_GET_HISTORY 8
#define MSG_CANCEL_MATCH 9

// Response Types (server -> client, mtype = client pid)
#define RESP_OK 100
#define RESP_FAIL 101
#define RESP_MATCH_FOUND 102
#define RESP_MATCH_TIMEOUT 103
#define RESP_BATTLE_UPDATE 104
#define RESP_BATTLE_END 105
#define RESP_HISTORY_DATA 106
#define RESP_MATCH_SEARCHING 107

// === Structs ===
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    int gold;
    int lvl;
    int xp;
    int weapon_idx;     // if "-1" = unarmed, range between "0-4" = weapon index
    int in_use;         // if nilai 1 = active client connected
    pid_t pid;          // client Process ID when logged in
} Player;

typedef struct {
    char opponent[MAX_USERNAME];
    char result[8];     // either WIN or LOSS
    int xp_gained;
    time_t timestamp;
} MatchRecord;

// Extended player data and history stored in ShHM
typedef struct {
    Player p;
    MatchRecord history[MAX_HISTORY];
    int history_count;
} PlayerEntry;

typedef struct {
    int active;
    int player1_idx;  // index in players shared memory
    int player2_idx;  // if "-1" = bot
    int hp1;
    int hp2;
    time_t last_atk1;
    time_t last_atk2;
    int finished;       // if 0 = ongoing, if 1 = p1 wins, if 2 = p2 wins
    int max_hp1;
    int max_hp2;
    int last_dmg1;   // last damage from player1
    int last_dmg2;   // last damage from player2
    int last_ult1;   // 1 if last attack from p1 is ultimate
    int last_ult2;   // 1 if last attack from p2 is ultimate
} Battle;

typedef struct {
    pid_t pid;
    int player_idx;
    time_t join_time;
    int active;         // if 1 = waiting
} QueueEntry;

// === Shared Memory Layout ===
typedef struct {
    PlayerEntry entries[MAX_PLAYERS];
    int count;
} ShmPlayers;

typedef struct {
    Battle battles[MAX_BATTLES];
} ShmBattles;

typedef struct {
    QueueEntry queue[MAX_QUEUE];
    int size;
} ShmQueue;

// === Message Struct ===
typedef struct {
    long mtype;
    int cmd;            // MSG_* constant
    pid_t sender_pid;
    char data1[MAX_USERNAME];   // username or misc
    char data2[MAX_PASSWORD];   // password or misc
    int idata;                  // integer paylod (for weapon index, etc.)
} IpcMsg;

// Response Message (server -> client)
typedef struct {
    long mtype;         // client pid
    int status;         // RESP_* constant
    char msg[256];
    int idata;          // such as battle index, gold, etc.

    // Player Stats (for login and buy weaponry)
    int  p_gold;
    int  p_lvl;
    int  p_xp;
    int  p_weapon_idx;   // -1 = no weapon

    // Battle Snapshot
    int hp_self;
    int hp_opp;
    char opp_name[MAX_USERNAME];
    int opp_weapon;
    int self_weapon;
    int battle_over;    // if 0 = ongoing, 1 = won, 2 = lost

    int self_lvl;
    int opp_lvl;
    int last_dmg;    // last damage (0 = none)
    int is_ultimate;
    int  max_hp_self;
    int  max_hp_opp;

    // History Record (for RESP_HISTORY_DATA)
    MatchRecord rec;
    int history_total;
    int history_idx;
} IpcResp;

// === Semaphore Helpers ===
static inline void sem_lock(int semid) {
    struct sembuf sb = {0, -1, SEM_UNDO};
    semop(semid, &sb, 1);
}
static inline void sem_unlock(int semid) {
    struct sembuf sb = {0, 1, SEM_UNDO};
    semop(semid, &sb, 1);
}

// === Game Logic Helper (compute player stats from base + XP + weapon) ===
static inline int calc_damage(int xp, int weapon_idx) {
    int bonus = (weapon_idx >= 0 && weapon_idx < NUM_WEAPONS) ? WEAPONS[weapon_idx].bonus_dmg : 0;
    return BASE_DAMAGE + (xp / 50) + bonus;
}
static inline int calc_health(int xp) {
    return BASE_HEALTH + (xp / 10);
}

#endif
