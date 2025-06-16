/* =============================================================
 *  Dogfight Server – version « duels »
 *  → gère un nombre illimité de matchs 1 v 1 en parallèle.
 *  → chaque invitation concerne **un seul** adversaire ;
 *    l’acceptation crée un slot Game indépendant.
 * ===========================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

/* ------------------ Paramètres généraux ------------------ */
#define PORT                 12345
#define BUFFER_SIZE          2048
#define MAX_PLAYERS          100
#define USERNAME_LEN         50
#define MAX_CLIENTS          100
#define MAX_INVITES          50
#define MAX_GAMES            20
#define MAX_PROJECTILES      256
#define MAX_ACTIVE_MISSILES  5
#define PLANE_SIZE           40
#define PROJECTILE_SIZE      20
#define BOARD_WIDTH          560
#define BOARD_HEIGHT         540
#define PROJECTILE_SPEED     20.0f
#define MAX_HEALTH           100
#define DMG_PER_HIT          10
#define TICK_HZ              60
#define GAME_OVER_PREFIX     "GAME_OVER:"

/* ----------------------- Structures ----------------------- */
/* invitation = un duel */
typedef struct {
    int  active;
    char inviter[USERNAME_LEN];
    char invitee[USERNAME_LEN];
    int  response;                 /* 0 = pending, 1 = yes, -1 = no */
} Invitation;

typedef struct {
    int   active, id;
    float x, y, dx, dy;
    char  owner[USERNAME_LEN];
} Projectile;

typedef struct {
    int   active;
    char  players[2][USERNAME_LEN];   /* duel → exactement 2 joueurs */
    float posX[2], posY[2];
    int   hp[2];

    Projectile proj[MAX_PROJECTILES];
    int   projCount, nextProjId;

    pthread_mutex_t lock;
} Game;

/* ----------------------- Globals -------------------------- */
static char g_connected[MAX_PLAYERS][USERNAME_LEN];
static int  g_numPlayers = 0;

static int  g_clientSockets[MAX_CLIENTS];
static char g_socketUsers [MAX_CLIENTS][USERNAME_LEN];
static int  g_clientCount = 0;

static Invitation g_invites[MAX_INVITES];
static Game       g_games  [MAX_GAMES];

/* Mutexes */
static pthread_mutex_t m_players = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_clients = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_invites = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_games   = PTHREAD_MUTEX_INITIALIZER;

/* DB credentials (adapter si besoin) */
static const char *DB_HOST = "localhost";
static const char *DB_USER = "so";
static const char *DB_PASS = "so";
static const char *DB_NAME = "SO";
static unsigned int DB_PORT = 0;

/* -------------------- Déclarations ------------------------ */
void *handleClient(void *arg);
void *physicsLoop(void *arg);
void  sendState(Game *gm);

void registerUser(MYSQL*, const char*, const char*, const char*, int);
void loginUser   (MYSQL*, const char*, const char*, int, char*, int*);
void queryOne(MYSQL*, int);
void queryTwo(MYSQL*, int);
void queryThree(MYSQL*, int);

void addConnectedPlayer(const char*);
void removeConnectedPlayer(const char*);
void broadcastPlayersList(void);

void addClientSocket(int);
void removeClientSocket(int);
void broadcastMessage(const char*);
int  socketFromUsername(const char*);
void setSocketUsername(int,const char*);

int  findInviteSlot(const char*);
void createInvitation(const char*, const char*);
void handleInviteAnswer(const char*, const char*, int);
void startGame(Invitation*);

int  findGameByPlayer(const char*);
void fireFromPlayer(int, const char*, float, float, float, float);

/* ===========================================================
 *                        MAIN
 * =========================================================*/
int main(void)
{
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORT)
    };

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    listen(server_sock, 10);
    printf("Server listening on %d\n", PORT);

    pthread_t physTid;
    pthread_create(&physTid, NULL, physicsLoop, NULL);

    while (1) {
        int client = accept(server_sock, NULL, NULL);
        addClientSocket(client);
        pthread_t tid;
        int *p = malloc(sizeof(int));
        *p = client;
        pthread_create(&tid, NULL, handleClient, p);
    }
}

/* ===========================================================
 *            Gestion des sockets clients connectés
 * =========================================================*/
void addClientSocket(int sock)
{
    pthread_mutex_lock(&m_clients);
    g_clientSockets[g_clientCount] = sock;
    g_socketUsers [g_clientCount][0] = '\0';
    g_clientCount++;
    pthread_mutex_unlock(&m_clients);
}

void removeClientSocket(int sock)
{
    pthread_mutex_lock(&m_clients);
    for (int i = 0; i < g_clientCount; i++) {
        if (g_clientSockets[i] == sock) {
            for (int j = i; j < g_clientCount - 1; j++) {
                g_clientSockets[j] = g_clientSockets[j + 1];
                strcpy(g_socketUsers[j], g_socketUsers[j + 1]);
            }
            g_clientCount--;
            break;
        }
    }
    pthread_mutex_unlock(&m_clients);
}

int socketFromUsername(const char *u)
{
    pthread_mutex_lock(&m_clients);
    for (int i = 0; i < g_clientCount; i++) {
        if (strcmp(g_socketUsers[i], u) == 0) {
            int s = g_clientSockets[i];
            pthread_mutex_unlock(&m_clients);
            return s;
        }
    }
    pthread_mutex_unlock(&m_clients);
    return -1;
}

void setSocketUsername(int sock, const char *u)
{
    pthread_mutex_lock(&m_clients);
    for (int i = 0; i < g_clientCount; i++)
        if (g_clientSockets[i] == sock)
            strncpy(g_socketUsers[i], u, USERNAME_LEN - 1);
    pthread_mutex_unlock(&m_clients);
}

/* ===========================================================
 *             Liste publique des joueurs connectés
 * =========================================================*/
void broadcastMessage(const char *msg)
{
    pthread_mutex_lock(&m_clients);
    for (int i = 0; i < g_clientCount; i++)
        write(g_clientSockets[i], msg, strlen(msg));
    pthread_mutex_unlock(&m_clients);
}

void broadcastPlayersList(void)
{
    char buf[BUFFER_SIZE] = "";
    pthread_mutex_lock(&m_players);
    for (int i = 0; i < g_numPlayers; i++) {
        strcat(buf, g_connected[i]);
        if (i < g_numPlayers - 1) strcat(buf, ",");
    }
    pthread_mutex_unlock(&m_players);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg), "UPDATE_LIST:%s\n", buf);
    broadcastMessage(msg);
}

void addConnectedPlayer(const char *u)
{
    pthread_mutex_lock(&m_players);
    strncpy(g_connected[g_numPlayers++], u, USERNAME_LEN - 1);
    pthread_mutex_unlock(&m_players);
    broadcastPlayersList();
}

void removeConnectedPlayer(const char *u)
{
    pthread_mutex_lock(&m_players);
    for (int i = 0; i < g_numPlayers; i++) {
        if (strcmp(g_connected[i], u) == 0) {
            for (int j = i; j < g_numPlayers - 1; j++)
                strcpy(g_connected[j], g_connected[j + 1]);
            g_numPlayers--;
            break;
        }
    }
    pthread_mutex_unlock(&m_players);
    broadcastPlayersList();
}

/* ===========================================================
 *                    Invitations (DUEL)
 * =========================================================*/
int findInviteSlot(const char *inviter)
{
    for (int i = 0; i < MAX_INVITES; i++)
        if (g_invites[i].active && strcmp(g_invites[i].inviter, inviter) == 0)
            return i;
    return -1;
}

void createInvitation(const char *inviter, const char *targetCsv)
{
    pthread_mutex_lock(&m_invites);

    int slot = -1;
    for (int i = 0; i < MAX_INVITES; i++)
        if (!g_invites[i].active) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&m_invites); return; }

    Invitation *in = &g_invites[slot];
    memset(in, 0, sizeof(*in));

    in->active = 1;
    strncpy(in->inviter, inviter, USERNAME_LEN - 1);

    /* Ne garder que le premier pseudo (un seul adversaire) */
    const char *comma = strchr(targetCsv, ',');
    size_t len = comma ? (size_t)(comma - targetCsv) : strlen(targetCsv);
    if (len >= USERNAME_LEN) len = USERNAME_LEN - 1;
    strncpy(in->invitee, targetCsv, len);
    in->invitee[len] = '\0';

    pthread_mutex_unlock(&m_invites);
}

void handleInviteAnswer(const char *inviter, const char *invitee, int accept)
{
    pthread_mutex_lock(&m_invites);
    int idx = findInviteSlot(inviter);
    if (idx < 0) { pthread_mutex_unlock(&m_invites); return; }

    Invitation *in = &g_invites[idx];
    if (strcmp(in->invitee, invitee) != 0) { pthread_mutex_unlock(&m_invites); return; }

    in->response = accept ? 1 : -1;

    /* Message à envoyer uniquement aux deux intéressés */
    char resultMsg[BUFFER_SIZE];
    snprintf(resultMsg, sizeof(resultMsg), "INVITE_RESULT:%s:%s\n",
             inviter, accept ? "ACCEPTED" : "REJECTED");
    int sInv  = socketFromUsername(inviter);
    int sInvt = socketFromUsername(invitee);
    if (sInv  != -1) write(sInv,  resultMsg, strlen(resultMsg));
    if (sInvt != -1) write(sInvt, resultMsg, strlen(resultMsg));

    if (accept)
        startGame(in);            /* lancement du duel */

    in->active = 0;
    pthread_mutex_unlock(&m_invites);
}

/* ===========================================================
 *                    Création d’un match
 * =========================================================*/
void startGame(Invitation *in)
{
    pthread_mutex_lock(&m_games);
    int slot = -1;
    for (int i = 0; i < MAX_GAMES; i++)
        if (!g_games[i].active) { slot = i; break; }
    if (slot < 0) { pthread_mutex_unlock(&m_games); return; }

    Game *gm = &g_games[slot];
    memset(gm, 0, sizeof(*gm));
    gm->active     = 1;
    gm->nextProjId = 1;
    pthread_mutex_init(&gm->lock, NULL);

    strcpy(gm->players[0], in->inviter);
    strcpy(gm->players[1], in->invitee);

    gm->hp[0] = gm->hp[1] = MAX_HEALTH;
    gm->posX[0] =  PLANE_SIZE;
    gm->posY[0] =  BOARD_HEIGHT / 2.0f;
    gm->posX[1] =  BOARD_WIDTH - PLANE_SIZE;
    gm->posY[1] =  BOARD_HEIGHT / 2.0f;

    sendState(gm);               /* état initial */
    pthread_mutex_unlock(&m_games);
}

/* ===========================================================
 *                  Utilitaires « jeu »
 * =========================================================*/
int findGameByPlayer(const char *u)
{
    for (int i = 0; i < MAX_GAMES; i++) {
        if (g_games[i].active) {
            if (strcmp(g_games[i].players[0], u) == 0) return i;
            if (strcmp(g_games[i].players[1], u) == 0) return i;
        }
    }
    return -1;
}

void fireFromPlayer(int sock, const char *u, float x, float y, float dx, float dy)
{
    int gid = findGameByPlayer(u);
    if (gid < 0) return;
    Game *gm = &g_games[gid];

    pthread_mutex_lock(&gm->lock);

    /* limite de missiles actifs par joueur */
    int cnt = 0;
    for (int k = 0; k < gm->projCount; k++)
        if (gm->proj[k].active && strcmp(gm->proj[k].owner, u) == 0) cnt++;

    if (cnt >= MAX_ACTIVE_MISSILES || gm->projCount >= MAX_PROJECTILES) {
        pthread_mutex_unlock(&gm->lock);
        return;
    }

    Projectile *pr = &gm->proj[gm->projCount++];
    pr->active = 1;
    pr->id     = gm->nextProjId++;
    pr->x      = x;
    pr->y      = y;
    pr->dx     = dx;
    pr->dy     = dy;
    strncpy(pr->owner, u, USERNAME_LEN - 1);

    char ack[128];
    snprintf(ack, sizeof(ack),
             "FIRE_ACK:%d:%s:%.0f:%.0f:%.0f:%.0f\n",
             pr->id, pr->owner, pr->x, pr->y, pr->dx, pr->dy);

    for (int i = 0; i < 2; i++) {
        int dst = socketFromUsername(gm->players[i]);
        if (dst != -1) write(dst, ack, strlen(ack));
    }

    pthread_mutex_unlock(&gm->lock);
}

void sendState(Game *gm)
{
    char projBuf[BUFFER_SIZE] = "";
    char hpBuf  [BUFFER_SIZE] = "";
    char posBuf [BUFFER_SIZE] = "";

    int first = 1;
    for (int i = 0; i < gm->projCount; i++) {
        Projectile *p = &gm->proj[i];
        if (!p->active) continue;
        if (!first) strcat(projBuf, ",");
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%d:%.0f:%.0f", p->id, p->x, p->y);
        strcat(projBuf, tmp);
        first = 0;
    }

    for (int i = 0; i < 2; i++) {
        if (i) { strcat(hpBuf, ","); strcat(posBuf, ","); }
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%s:%d", gm->players[i], gm->hp[i]);
        strcat(hpBuf, tmp);
        snprintf(tmp, sizeof(tmp), "%s:%.0f:%.0f",
                 gm->players[i], gm->posX[i], gm->posY[i]);
        strcat(posBuf, tmp);
    }

    char msg[BUFFER_SIZE];
    int len = snprintf(msg, sizeof(msg), "STATE:%s|%s|%s\n",
                       projBuf, hpBuf, posBuf);

    for (int i = 0; i < 2; i++) {
        int dst = socketFromUsername(gm->players[i]);
        if (dst != -1) write(dst, msg, len);
    }
}

/* ===========================================================
 *               Boucle physique (60 Hz)
 * =========================================================*/
void *physicsLoop(void *arg)
{
    (void)arg;
    struct timespec req = { 0, (long)(1e9 / TICK_HZ) };

    while (1) {
        nanosleep(&req, NULL);

        pthread_mutex_lock(&m_games);
        for (int gi = 0; gi < MAX_GAMES; gi++) {
            Game *gm = &g_games[gi];
            if (!gm->active) continue;

            pthread_mutex_lock(&gm->lock);

            /* ---- projectiles ---- */
            for (int i = 0; i < gm->projCount; i++) {
                Projectile *pr = &gm->proj[i];
                if (!pr->active) continue;
                pr->x += pr->dx;
                pr->y += pr->dy;

                for (int p = 0; p < 2; p++) {
                    if (strcmp(pr->owner, gm->players[p]) == 0) continue;
                    float ax1 = gm->posX[p] - PLANE_SIZE / 2.0f;
                    float ay1 = gm->posY[p] - PLANE_SIZE / 2.0f;
                    float ax2 = ax1 + PLANE_SIZE;
                    float ay2 = ay1 + PLANE_SIZE;
                    float px1 = pr->x, py1 = pr->y;
                    float px2 = pr->x + PROJECTILE_SIZE, py2 = pr->y + PROJECTILE_SIZE;
                    if (px1 < ax2 && px2 > ax1 && py1 < ay2 && py2 > ay1) {
                        pr->active = 0;
                        gm->hp[p] -= DMG_PER_HIT;
                        if (gm->hp[p] < 0) gm->hp[p] = 0;
                        char hit[64];
                        snprintf(hit, sizeof(hit), "HIT:%s:%d\n",
                                 gm->players[p], gm->hp[p]);
                        for (int q = 0; q < 2; q++) {
                            int dst = socketFromUsername(gm->players[q]);
                            if (dst != -1) write(dst, hit, strlen(hit));
                        }
                        break;
                    }
                }

                if (pr->active &&
                    (pr->x < -PROJECTILE_SIZE ||
                     pr->x > BOARD_WIDTH + PROJECTILE_SIZE))
                    pr->active = 0;
            }

            /* compact */
            int w = 0;
            for (int r = 0; r < gm->projCount; r++)
                if (gm->proj[r].active) gm->proj[w++] = gm->proj[r];
            gm->projCount = w;

            /* ---- Game over ? ---- */
            int alive0 = gm->hp[0] > 0;
            int alive1 = gm->hp[1] > 0;
            if (!alive0 || !alive1) {
                char msg[128];
                snprintf(msg, sizeof(msg), GAME_OVER_PREFIX"%s\n",
                         alive0 ? gm->players[0]
                                : alive1 ? gm->players[1] : "NONE");
                for (int p = 0; p < 2; p++) {
                    int dst = socketFromUsername(gm->players[p]);
                    if (dst != -1) write(dst, msg, strlen(msg));
                }
                gm->active = 0;
                pthread_mutex_unlock(&gm->lock);
                continue;
            }

            sendState(gm);
            pthread_mutex_unlock(&gm->lock);
        }
        pthread_mutex_unlock(&m_games);
    }
    return NULL;
}

/* ===========================================================
 *             Thread client : handleClient
 * =========================================================*/
void *handleClient(void *arg)
{
    int sock = *((int*)arg);
    free(arg);

    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS,
                            DB_NAME, DB_PORT, NULL, 0)) {
        close(sock);
        return NULL;
    }

    char buf[BUFFER_SIZE];
    char user[USERNAME_LEN] = "";
    int  logged = 0;

    while (1) {
        int r = read(sock, buf, BUFFER_SIZE - 1);
        if (r <= 0) {
            if (logged) removeConnectedPlayer(user);
            removeClientSocket(sock);
            close(sock);
            mysql_close(conn);
            return NULL;
        }
        buf[r] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';

        char *cmd = strtok(buf, ":");
        if (!cmd) continue;

        /* ---------- REGISTER / LOGIN ---------- */
        if (strcmp(cmd, "REGISTER") == 0) {
            char *u = strtok(NULL, ":");
            char *e = strtok(NULL, ":");
            char *p = strtok(NULL, ":");
            if (u && e && p) registerUser(conn, u, e, p, sock);
        }
        else if (strcmp(cmd, "LOGIN") == 0) {
            char *u = strtok(NULL, ":");
            char *p = strtok(NULL, ":");
            if (u && p) loginUser(conn, u, p, sock, user, &logged);
            if (logged) {
                setSocketUsername(sock, user);
                addConnectedPlayer(user);
            }
        }
        else if (strcmp(cmd, "LOGOUT") == 0) {
            if (logged) {
                removeConnectedPlayer(user);
                logged = 0;
                write(sock, "Logout successful\n", 18);
            }
        }
        /* ------------ QUERIES SQL -------------- */
        else if (strcmp(cmd, "QUERY1") == 0) queryOne(conn, sock);
        else if (strcmp(cmd, "QUERY2") == 0) queryTwo(conn, sock);
        else if (strcmp(cmd, "QUERY3") == 0) queryThree(conn, sock);

        /* ------------ INVITES & MATCH ---------- */
        else if (strcmp(cmd, "INVITE") == 0 && logged) {
            char *c = strtok(NULL, ":");
            if (c) {
                createInvitation(user, c);
                char m[BUFFER_SIZE];
                snprintf(m, sizeof(m), "INVITE_REQUEST:%s:%s\n", user, c);
                broadcastMessage(m);
            }
        }
        else if (strcmp(cmd, "INVITE_RESP") == 0 && logged) {
            char *inviter = strtok(NULL, ":");
            char *resp    = strtok(NULL, ":");
            if (inviter && resp)
                handleInviteAnswer(inviter, user,
                                   strcmp(resp, "ACCEPT") == 0);
        }
        else if (strcmp(cmd, "MOVE") == 0 && logged) {
            char *sx = strtok(NULL, ":");
            char *sy = strtok(NULL, ":");
            if (sx && sy) {
                int gid = findGameByPlayer(user);
                if (gid >= 0) {
                    Game *gm = &g_games[gid];
                    pthread_mutex_lock(&gm->lock);
                    int idx = strcmp(gm->players[0], user) == 0 ? 0 : 1;
                    gm->posX[idx] = atof(sx);
                    gm->posY[idx] = atof(sy);
                    pthread_mutex_unlock(&gm->lock);
                }
            }
        }
        else if (strcmp(cmd, "FIRE") == 0 && logged) {
            char *sx  = strtok(NULL, ":");
            char *sy  = strtok(NULL, ":");
            char *sdx = strtok(NULL, ":");
            char *sdy = strtok(NULL, ":");
            if (sx && sy && sdx && sdy)
                fireFromPlayer(sock, user,
                               atof(sx), atof(sy),
                               atof(sdx), atof(sdy));
        }
        else if (strcmp(cmd, "LIST") == 0 && logged) {
            char list[BUFFER_SIZE] = "";
            pthread_mutex_lock(&m_players);
            for (int i = 0; i < g_numPlayers; i++) {
                strcat(list, g_connected[i]);
                if (i < g_numPlayers - 1) strcat(list, ",");
            }
            pthread_mutex_unlock(&m_players);
            char msg[BUFFER_SIZE + 32];
            snprintf(msg, sizeof(msg), "UPDATE_LIST:%s\n", list);
            write(sock, msg, strlen(msg));
        }
    }
}

/* -----------------------------------------------------------------
 *  Fonctions SQL (register / login / query1 / query2 / query3)
 *  → placées dans un fichier à part pour alléger ce .c
 * ----------------------------------------------------------------*/
#include "db_helpers.c"
