# SISOP-3-2026-IT-064

**Nama:** I Made Tobby Anantha Adiwijaya  
**Prodi:** Teknologi Informasi  
**NRP:** 5027251064  

## Table of Contents
- [Struktur Repository](#struktur-repository)  
- [Soal 1 - Present Day, Present Time](#soal-1---present-day-present-time)
	- [protocol.h](#protocolh)  
	- [wired.c](#wiredc)  
- [Soal 2 - The Battle of Eterion](#soal-2---the-battle-of-eterion)
	- [Makefile](#makefile)  
	- [arena.h](#arenah)

## Struktur Repository
![image](assets/directory-tree.png)  

## Soal 1 - Present Day, Present Time
Pada soal ini diminta untuk membuat infrastruktur komunikasi digital client-server bernama ``"The Wired"`` menggunakan socket programming pada bahasa C. Tujuannya adalah untuk mendemonstrasikan komunikasi jaringan asinkron multi-klien **tanpa** menggunakan proses `fork()`.  

Beberapa program yang harus dibuat, yakni:
- ``protocol.h``  
File ini berperan sebagai header konfigurasi standar yang dijembatani antara sistem klien dan server agar selaras.  

- ``wired.c``  
Bertindak sebagai SERVER, pusat yang menerima banyak koneksi NAVI, menangani identitas unik, siaran pesan (broadcast), perintah admin jarak jauh (RPC), dan pencatatan permanen.  

- ``navi.c``
Bertindak sebagai CLIENT, aplikasi pengguna yang dapat masuk sebagai entitas biasa atau sebagai The Knights (admin), mengirim dan menerima pesan secara asinkron tanpa proses fork.  

### protocol.h  
Tidak berisi logika, hanya definisi konstanta yang digunakan bersama oleh server dan klien.
```h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PORT 8080
#define MAX_CLIENTS 100
#define BUFFER_SIZE 1024

#endif
```
``PORT 8080`` adalah nomor port TCP untuk komunikasi dan ``BUFFER_SIZE`` adalah ukuran maksimum buffer untuk pesan.  

### wired.c

#### Logging Function 
```c
void log_event(const char *role, const char *msg) {
    FILE *f = fopen("history.log", "a");

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [%s] %s\n",
	tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	tm->tm_hour, tm->tm_min, tm->tm_sec,
	role, msg);

    fclose(f);
}
```
Fungsi ini bertugas menangani pencatatan sistem operasional (system logging) ke ``history.log`` dengan menggunakan struktur waktu ``<time.h>`` untuk mengambil ``localtime()`` guna merekam jam eksekusi sistem. Sistem membuka file log menggunakan mode append ``"a"`` agar penulisan log baru ditambahkan di akhir file tanpa menimpa histori log sebelumnya.  
Format:
```
[YYYY-MM-DD HH:MM:SS] [System/Admin/User] [Status/Command/Chat]
```

#### User Chats Logging Function
```c
void log_chat(const char *user, const char *msg) {
    FILE *f = fopen("history.log", "a");

    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

	char clean_msg[BUFFER_SIZE];
    strncpy(clean_msg, msg, sizeof(clean_msg)-1);
    clean_msg[sizeof(clean_msg)-1] = '\0';
    clean_msg[strcspn(clean_msg, "\n")] = '\0';

    fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02d] [User] [[%s]: %s]\n",
	tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	tm->tm_hour, tm->tm_min, tm->tm_sec,
	user, clean_msg);

    fclose(f);
}
```
Fungsi ini mencatat pesan obrolan pengguna ke ``history.log``. Dibanding sebelumnya, fungsi ini dilengkapi dengan proteksi memori menggunakan ``strncpy`` untuk membatasi panjang string maksimal, serta pembersihan karakter newline (``\n``) melalui ``strcspn()``. Manipulasi memori ini wajib dilakukan agar output pada file log tetap sejajar secara linier dan terstruktur rapi.  
Format:
```
[YYYY-MM-DD HH:MM:SS] [User] [[nama]: pesan]
```

#### Duplicate Name Function
```c
int is_name_exist(const char *name) {
    for(int i = 0; i < client_count; i++) {
		if(strcmp(clients[i].name, name) == 0) {
	    	return 1;
		}
    }
    return 0;
}
```
Fungsi ini melakukan perulangan pada array ``clients`` aktif dan menggunakan komparasi ``strcmp()`` untuk membandingkan ``clients[i].name`` dengan parameter ``name``. Fungsi mengembalikan nilai 1 apabila ada identitas yang sama dan nilai 0 jika belum ada.

#### Remove Client Function
```c
void remove_client(int index) {
    char logbuf[128];
    sprintf(logbuf, "[User '%s' disconnected]", clients[index].name);
    log_event("System", logbuf);

    close(clients[index].sock);

    for(int i = index; i < client_count - 1; i++) {
		clients[i] = clients[i+1];
    }
    client_count--;
}
```
Fungsi untuk menghapus klien dari daftar saat putus koneksi atau perintah keluar. Sistem akan mencatat log pemutusan kemudian melakukan penutupan socket klien melalui ``close()``, setelah itu akan sistem menghapus entri dari array dengan menggeser elemen setelahnya ke kiri, lalu mengurangi ``client_count`` agar susunan memori tertata ulang.

#### Broadcast Function
```c
void broadcast(const char *msg, int sender_sock) {
    for(int i = 0; i < client_count; i++) {
		if(clients[i].sock != sender_sock && clients[i].is_admin == 0) {
	    	send(clients[i].sock, msg, strlen(msg), 0);
		}
    }
}
```
Fungsi ini berfungsi untuk mengirim pesan ke semua klien biasa (non-admin) kecuali pengirim itu sendiri. Di sini sistem akan memeriksa ``clients[i].is_admin == 0`` dan ``sock != sender_sock``. Sehingga Admin tidak menerima siaran percakapan dan perintah admin akan ditangani terpisah.  

#### Main Function
```c
int main()
```
placeholderrrrrrrrrrrrrrrrrrrrr

## Soal 2 - The Battle of Eterion
Pada soal ini diminta untuk membangun sebuah sistem permainan battle arena multiplayer berbasis terminal yang disebut Eterion. Sistem ini terdiri dari dua program terpisah yang saling berkomunikasi menggunakan mekanisme Inter-Process Communication (IPC) milik Linux, yaitu Shared Memory, Message Queue, dan Semaphore.

Beberapa program yang harus dibuat, yakni:  
- ``Makefile``  
File berguna untuk membantu saat proses debugging (opsional).  

- ``orion.c``  
Bertindak sebagai SERVER, mengelola seluruh state permainan, data pemain, antrian matchmaking, dan logika battle.  

- ``eternal.c``  
Bertindak sebagai CLIENT, menyediakan antarmuka terminal untuk pemain agar bisa register, login, masuk battle, membeli senjata, dan melihat riwayat.  

- ``arena.h``  
File header bersama yang mendefinisikan semua struct, konstanta, IPC keys, dan fungsi inline yang digunakan oleh program ``orion.c`` dan ``eternal.c``.

### Makefile
File ini dipergunakan untuk membantu dalam proses debugging. Setiap ingin run kedua program tersebut, gunakan ``make`` pada terminal untuk compile. Setiap menghentikan program, gunakan ``make clear_ipc`` untuk menghapus sisa shared memory.
```make
CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -lrt

all: server client

server: orion.c arena.h
	$(CC) $(CFLAGS) orion.c -o orion $(LDFLAGS)

client: eternal.c arena.h
	$(CC) $(CFLAGS) eternal.c -o eternal $(LDFLAGS)

clean:
	rm -f orion eternal

clear_ipc:
	ipcs -m | grep 0x00001234 | awk '{print $$2}' | xargs -r ipcrm -m
	ipcs -q | grep 0x0000ABCD | awk '{print $$2}' | xargs -r ipcrm -q
	ipcs -s | grep 0x0000EF01 | awk '{print $$2}' | xargs -r ipcrm -s
	ipcs -s | grep 0x0000EF02 | awk '{print $$2}' | xargs -r ipcrm -s
	ipcs -s | grep 0x0000EF03 | awk '{print $$2}' | xargs -r ipcrm -s
	ipcs -m | grep 0x00005678 | awk '{print $$2}' | xargs -r ipcrm -m
	ipcs -m | grep 0x00009012 | awk '{print $$2}' | xargs -r ipcrm -m
```

### arena.h
Ini merupakan file header yang di-include oleh kedua program. File ini tidak berisi logika, melainkan hanya definisi bersama agar kedua program memiliki pemahaman yang sama terhadap struktur data dan protokol komunikasi.
#### IPC Keys
```h
// === IPC Keys ===
#define SHM_KEY_PLAYERS 0x00001234    // Shared memory: player data
#define SHM_KEY_BATTLES 0x00005678    // Shared memory: active battles
#define SHM_KEY_QUEUE 0x00009012    // Shared memory: matchmaking queue

#define MSG_KEY_MAIN 0x0000ABCD    // Message queue: client <to> server
#define SEM_KEY_PLAYERS 0x0000EF01    // Semaphore: player data
#define SEM_KEY_BATTLES 0x0000EF02    // Semaphore: battle data
#define SEM_KEY_QUEUE 0x0000EF03    // Semaphore: matchmaking queue
```
IPC yang berfungsi untuk menyimpan Shared Memory untuk setiap konstanta yang dideklarasikan.

#### 