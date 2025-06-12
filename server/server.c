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

#define PORT               12345
#define BUFFER_SIZE        2048
#define MAX_PLAYERS        100
#define USERNAME_LEN       50
#define MAX_CLIENTS        100
#define MAX_INVITES        50
#define MAX_GAMES          20
#define MAX_PROJECTILES    256
#define MAX_ACTIVE_MISSILES 5
#define PLANE_SIZE         40
#define PROJECTILE_SIZE    20
#define BOARD_WIDTH        560
#define BOARD_HEIGHT       540
#define PROJECTILE_SPEED   20.0f
#define MAX_HEALTH         100
#define DMG_PER_HIT        10
#define TICK_HZ            60
#define GAME_OVER_PREFIX "GAME_OVER:"


/* Invitation structure */
typedef struct {
    int active;
    char inviter[USERNAME_LEN];
    char invitees[MAX_PLAYERS][USERNAME_LEN];
    int numInvitees;
    int responses[MAX_PLAYERS]; /* 0=pending,1=yes,-1=no */
} Invitation;

/* Projectile */
typedef struct {
    int active, id;
    float x, y, dx, dy;
    char owner[USERNAME_LEN];
} Projectile;

/* Game state */
typedef struct {
    int active;
    char players[MAX_PLAYERS][USERNAME_LEN];
    int numPlayers;
    float posX[MAX_PLAYERS], posY[MAX_PLAYERS];
    int hp[MAX_PLAYERS];
    Projectile proj[MAX_PROJECTILES];
    int projCount, nextProjId;
    pthread_mutex_t lock;
} Game;

/* Globals */
static char g_connected[MAX_PLAYERS][USERNAME_LEN];
static int  g_numPlayers = 0;
static int  g_clientSockets[MAX_CLIENTS];
static char g_socketUsers[MAX_CLIENTS][USERNAME_LEN];
static int  g_clientCount = 0;
static Invitation g_invites[MAX_INVITES];
static Game       g_games[MAX_GAMES];

/* Mutexes */
static pthread_mutex_t m_players  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_clients  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_invites  = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t m_games    = PTHREAD_MUTEX_INITIALIZER;

/* DB credentials */
const char *DB_HOST = "localhost";
const char *DB_USER = "so";
const char *DB_PASS = "so";
const char *DB_NAME = "SO";
unsigned int DB_PORT = 0;

/* Prototypes */
void *handleClient(void *arg);
void *physicsLoop(void *arg);
void sendState(Game *gm);

void registerUser(MYSQL*, const char*, const char*, const char*, int);
void loginUser(MYSQL*, const char*, const char*, int, char*, int*);
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

/* Main */
int main(void) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(PORT)
    };
    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
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

/* Client management */
void addClientSocket(int sock){
    pthread_mutex_lock(&m_clients);
    g_clientSockets[g_clientCount] = sock;
    g_socketUsers [g_clientCount][0] = '\0';
    g_clientCount++;
    pthread_mutex_unlock(&m_clients);
}

void removeClientSocket(int sock){
    pthread_mutex_lock(&m_clients);
    for(int i=0;i<g_clientCount;i++){
        if(g_clientSockets[i]==sock){
            for(int j=i;j<g_clientCount-1;j++){
                g_clientSockets[j]=g_clientSockets[j+1];
                strcpy(g_socketUsers[j],g_socketUsers[j+1]);
            }
            g_clientCount--;
            break;
        }
    }
    pthread_mutex_unlock(&m_clients);
}

int socketFromUsername(const char *u){
    pthread_mutex_lock(&m_clients);
    for(int i=0;i<g_clientCount;i++){
        if(strcmp(g_socketUsers[i],u)==0){
            int s = g_clientSockets[i];
            pthread_mutex_unlock(&m_clients);
            return s;
        }
    }
    pthread_mutex_unlock(&m_clients);
    return -1;
}

void setSocketUsername(int sock,const char *u){
    pthread_mutex_lock(&m_clients);
    for(int i=0;i<g_clientCount;i++){
        if(g_clientSockets[i]==sock)
            strncpy(g_socketUsers[i],u,USERNAME_LEN-1);
    }
    pthread_mutex_unlock(&m_clients);
}

/* Player list */
void addConnectedPlayer(const char *u){
    pthread_mutex_lock(&m_players);
    strncpy(g_connected[g_numPlayers++],u,USERNAME_LEN-1);
    pthread_mutex_unlock(&m_players);
    broadcastPlayersList();
}

void removeConnectedPlayer(const char *u){
    pthread_mutex_lock(&m_players);
    for(int i=0;i<g_numPlayers;i++){
        if(strcmp(g_connected[i],u)==0){
            for(int j=i;j<g_numPlayers-1;j++)
                strcpy(g_connected[j],g_connected[j+1]);
            g_numPlayers--;
            break;
        }
    }
    pthread_mutex_unlock(&m_players);
    broadcastPlayersList();
}

void broadcastPlayersList(void){
    char buf[BUFFER_SIZE] = "";
    pthread_mutex_lock(&m_players);
    for(int i=0;i<g_numPlayers;i++){
        strcat(buf, g_connected[i]);
        if(i<g_numPlayers-1) strcat(buf, ",");
    }
    pthread_mutex_unlock(&m_players);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg), "UPDATE_LIST:%s\n", buf);
    broadcastMessage(msg);
}

void broadcastMessage(const char *msg){
    pthread_mutex_lock(&m_clients);
    for(int i=0;i<g_clientCount;i++){
        write(g_clientSockets[i], msg, strlen(msg));
    }
    pthread_mutex_unlock(&m_clients);
}

/* Invitation */
int findInviteSlot(const char *inv){
    for(int i=0;i<MAX_INVITES;i++){
        if(g_invites[i].active && strcmp(g_invites[i].inviter,inv)==0)
            return i;
    }
    return -1;
}

void createInvitation(const char *inv,const char *csv){
    pthread_mutex_lock(&m_invites);
    int slot=-1;
    for(int i=0;i<MAX_INVITES;i++){
        if(!g_invites[i].active){ slot=i; break; }
    }
    if(slot<0){ pthread_mutex_unlock(&m_invites); return; }
    Invitation *in = &g_invites[slot];
    memset(in,0,sizeof(*in));
    in->active = 1;
    strncpy(in->inviter, inv, USERNAME_LEN-1);
    char copy[BUFFER_SIZE];
    strncpy(copy, csv, BUFFER_SIZE-1);
    copy[BUFFER_SIZE-1] = '\0';
    char *tok = strtok(copy, ",");
    int idx = 0;
    while(tok && idx<MAX_PLAYERS){
        strncpy(in->invitees[idx], tok, USERNAME_LEN-1);
        in->responses[idx++] = 0;
        tok = strtok(NULL, ",");
    }
    in->numInvitees = idx;
    pthread_mutex_unlock(&m_invites);
}

void handleInviteAnswer(const char *inv,const char *ie,int ok){
    pthread_mutex_lock(&m_invites);
    int s = findInviteSlot(inv);
    if(s<0){ pthread_mutex_unlock(&m_invites); return; }
    Invitation *in = &g_invites[s];
    for(int i=0;i<in->numInvitees;i++){
        if(strcmp(in->invitees[i],ie)==0)
            in->responses[i] = ok?1:-1;
    }
    int all=1, rej=0;
    for(int i=0;i<in->numInvitees;i++){
        if(in->responses[i]==0) all=0;
        if(in->responses[i]==-1) rej=1;
    }
    if(!all){ pthread_mutex_unlock(&m_invites); return; }

    if(!rej) startGame(in);
    in->active = 0;

    char msg[BUFFER_SIZE];
    snprintf(msg, sizeof(msg), "INVITE_RESULT:%s:%s\n",
             inv, rej?"REJECTED":"ACCEPTED");
    broadcastMessage(msg);
    pthread_mutex_unlock(&m_invites);
}

void startGame(Invitation *in){
    pthread_mutex_lock(&m_games);
    for(int i=0;i<MAX_GAMES;i++){
        if(!g_games[i].active){
            Game *gm = &g_games[i];
            memset(gm,0,sizeof(*gm));
            gm->active = 1;
            gm->numPlayers = 1;
            strcpy(gm->players[0], in->inviter);
            for(int j=0;j<in->numInvitees;j++){
                strcpy(gm->players[gm->numPlayers++], in->invitees[j]);
            }
            for(int j=0;j<gm->numPlayers;j++){
                gm->hp[j]   = MAX_HEALTH;
                gm->posX[j] = (j==0 ? PLANE_SIZE : BOARD_WIDTH - PLANE_SIZE);
                gm->posY[j] = BOARD_HEIGHT / 2.0f;
            }
            gm->nextProjId = 1;
            pthread_mutex_init(&gm->lock, NULL);

            // Envoie immédiatement un état initial à tous les joueurs
            sendState(gm);

            break;
        }
    }
    pthread_mutex_unlock(&m_games);
}

int findGameByPlayer(const char *u){
    for(int i=0;i<MAX_GAMES;i++){
        if(g_games[i].active){
            for(int j=0;j<g_games[i].numPlayers;j++){
                if(strcmp(g_games[i].players[j],u)==0)
                    return i;
            }
        }
    }
    return -1;
}

void fireFromPlayer(int sock,const char *u,float x,float y,float dx,float dy){
    int gid = findGameByPlayer(u);
    if(gid<0) return;
    Game *gm = &g_games[gid];
    pthread_mutex_lock(&gm->lock);
    int cnt = 0;
    for(int k=0;k<gm->projCount;k++){
        if(gm->proj[k].active && strcmp(gm->proj[k].owner,u)==0)
            cnt++;
    }
    if(cnt>=MAX_ACTIVE_MISSILES || gm->projCount>=MAX_PROJECTILES){
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
    strncpy(pr->owner, u, USERNAME_LEN-1);

    char ack[128];
    snprintf(ack, sizeof(ack),
             "FIRE_ACK:%d:%s:%.0f:%.0f:%.0f:%.0f\n",
             pr->id, pr->owner, pr->x, pr->y, pr->dx, pr->dy);

    for(int p=0;p<gm->numPlayers;p++){
        int dst = socketFromUsername(gm->players[p]);
        if(dst!=-1) write(dst, ack, strlen(ack));
    }
    pthread_mutex_unlock(&gm->lock);
}

/* Updated sendState: includes projectiles, health, and all player positions */
void sendState(Game *gm){
    char buf[BUFFER_SIZE];
    char projBuf[BUFFER_SIZE] = "";
    char hpBuf[BUFFER_SIZE]   = "";
    char posBuf[BUFFER_SIZE]  = "";

    /* Build projectiles list */
    int first = 1;
    for(int i = 0; i < gm->projCount; i++){
        if(gm->proj[i].active){
            if(!first) strcat(projBuf, ",");
            char tmp[64];
            snprintf(tmp, sizeof(tmp),
                     "%d:%.0f:%.0f",
                     gm->proj[i].id,
                     gm->proj[i].x,
                     gm->proj[i].y);
            strcat(projBuf, tmp);
            first = 0;
        }
    }

    /* Build health list */
    for(int i = 0; i < gm->numPlayers; i++){
        if(i > 0) strcat(hpBuf, ",");
        char tmp[64];
        snprintf(tmp, sizeof(tmp),
                 "%s:%d",
                 gm->players[i],
                 gm->hp[i]);
        strcat(hpBuf, tmp);
    }

    /* Build positions list */
    for(int i = 0; i < gm->numPlayers; i++){
        if(i > 0) strcat(posBuf, ",");
        char tmp[64];
        snprintf(tmp, sizeof(tmp),
                 "%s:%.0f:%.0f",
                 gm->players[i],
                 gm->posX[i],
                 gm->posY[i]);
        strcat(posBuf, tmp);
    }

    /* Compose and send – note the '|' separator between projBuf, hpBuf and posBuf */
    int len = snprintf(buf, sizeof(buf),
                       "STATE:%s|%s|%s\n",
                       projBuf, hpBuf, posBuf);

    for(int i=0;i<gm->numPlayers;i++){
        int dst = socketFromUsername(gm->players[i]);
        if(dst!=-1) write(dst, buf, len);
    }
}

/* Physics loop */
void *physicsLoop(void *arg) {
    (void)arg;
    struct timespec req = { 0, (long)(1e9 / TICK_HZ) };

    while (1) {
        nanosleep(&req, NULL);

        pthread_mutex_lock(&m_games);
        for (int gi = 0; gi < MAX_GAMES; gi++) {
            if (!g_games[gi].active) continue;
            Game *gm = &g_games[gi];

            pthread_mutex_lock(&gm->lock);

            /* 1) Update projectile positions & collisions */
            for (int i = 0; i < gm->projCount; i++) {
                Projectile *pr = &gm->proj[i];
                if (!pr->active) continue;
                pr->x += pr->dx;
                pr->y += pr->dy;

                /* Collision AABB */
                for (int p = 0; p < gm->numPlayers; p++) {
                    if (strcmp(pr->owner, gm->players[p]) == 0) continue;
                    float px1 = pr->x, py1 = pr->y;
                    float px2 = pr->x + PROJECTILE_SIZE, py2 = pr->y + PROJECTILE_SIZE;
                    float ax1 = gm->posX[p] - PLANE_SIZE/2.0f,
                          ay1 = gm->posY[p] - PLANE_SIZE/2.0f;
                    float ax2 = ax1 + PLANE_SIZE, ay2 = ay1 + PLANE_SIZE;
                    if (px1 < ax2 && px2 > ax1 && py1 < ay2 && py2 > ay1) {
                        pr->active = 0;
                        gm->hp[p] -= DMG_PER_HIT;
                        if (gm->hp[p] < 0) gm->hp[p] = 0;
                        /* Send HIT immediately */
                        char hitMsg[64];
                        snprintf(hitMsg, sizeof(hitMsg),
                                 "HIT:%s:%d\n",
                                 gm->players[p], gm->hp[p]);
                        for (int q = 0; q < gm->numPlayers; q++) {
                            int dst = socketFromUsername(gm->players[q]);
                            if (dst != -1) write(dst, hitMsg, strlen(hitMsg));
                        }
                        break;
                    }
                }

                /* Deactivate off-screen */
                if (pr->active &&
                    (pr->x < -PROJECTILE_SIZE || pr->x > BOARD_WIDTH + PROJECTILE_SIZE)) {
                    pr->active = 0;
                }
            }

            /* 2) Compact active projectiles */
            int w = 0;
            for (int r = 0; r < gm->projCount; r++) {
                if (gm->proj[r].active) {
                    gm->proj[w++] = gm->proj[r];
                }
            }
            gm->projCount = w;

            /* --- NOUVEAU : détection Game Over et nettoyage --- */
          {
    int deadCount = 0, winnerIdx = -1;
    for (int i = 0; i < gm->numPlayers; i++) {
        if (gm->hp[i] <= 0) deadCount++;
        else                 winnerIdx = i;
    }

    if (deadCount > 0) {
        /* 1. Diffuser GAME_OVER (inclut le vainqueur ou NONE) */
        char msg[128];
        snprintf(msg, sizeof(msg),
                 GAME_OVER_PREFIX"%s\n",
                 winnerIdx >= 0 ? gm->players[winnerIdx] : "NONE");

        for (int i = 0; i < gm->numPlayers; i++) {
            int dst = socketFromUsername(gm->players[i]);
            if (dst != -1) write(dst, msg, strlen(msg));
        }

        /* 2. Libérer la partie */
        gm->active = 0;
        pthread_mutex_unlock(&gm->lock);
        continue;              /* ← on passe tout de suite au game suivant */
    }
}
            /* --------------------------------------------------- */

            /* 3) Send full state (projectiles, hp, positions) */
            sendState(gm);

            pthread_mutex_unlock(&gm->lock);
        }
        pthread_mutex_unlock(&m_games);
    }
    return NULL;
}

/* Client thread */
void *handleClient(void *arg){
    int sock = *((int*)arg);
    free(arg);
    MYSQL *conn = mysql_init(NULL);
    if(!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)){
        close(sock);
        return NULL;
    }
    char buf[BUFFER_SIZE], user[USERNAME_LEN] = "";
    int logged = 0;
    while(1){
        int r = read(sock, buf, BUFFER_SIZE-1);
        if(r <= 0){
            if(logged) removeConnectedPlayer(user);
            removeClientSocket(sock);
            close(sock);
            mysql_close(conn);
            return NULL;
        }
        buf[r] = '\0';
        buf[strcspn(buf, "\r\n")] = '\0';
        char *cmd = strtok(buf, ":");
        if(!cmd) continue;
        if(strcmp(cmd,"REGISTER")==0){
            char *u=strtok(NULL,":"),*e=strtok(NULL,":"),*p=strtok(NULL,":");
            if(u&&e&&p) registerUser(conn,u,e,p,sock);
        }
        else if(strcmp(cmd,"LOGIN")==0){
            char *u=strtok(NULL,":"),*p=strtok(NULL,":");
            if(u&&p) loginUser(conn,u,p,sock,user,&logged);
            if(logged){
                setSocketUsername(sock,user);
                addConnectedPlayer(user);
            }
        }
        else if(strcmp(cmd,"LOGOUT")==0){
            if(logged){
                removeConnectedPlayer(user);
                logged = 0;
                write(sock,"Logout successful\n",17);
            }
        }
        else if(strcmp(cmd,"QUERY1")==0) queryOne(conn,sock);
        else if(strcmp(cmd,"QUERY2")==0) queryTwo(conn,sock);
        else if(strcmp(cmd,"QUERY3")==0) queryThree(conn,sock);
        else if(strcmp(cmd,"INVITE")==0 && logged){
            char *c=strtok(NULL,":");
            if(c){
                createInvitation(user,c);
                char m[BUFFER_SIZE];
                snprintf(m,sizeof(m),"INVITE_REQUEST:%s:%s\n",user,c);
                broadcastMessage(m);
            }
        }
        else if(strcmp(cmd,"INVITE_RESP")==0 && logged){
            char *i=strtok(NULL,":"),*r2=strtok(NULL,":");
            if(i&&r2) handleInviteAnswer(i,user,strcmp(r2,"ACCEPT")==0);
        }
        else if(strcmp(cmd,"MOVE")==0 && logged){
            char *x=strtok(NULL,":"),*y=strtok(NULL,":");
            if(x&&y){
                int gx = findGameByPlayer(user);
                if(gx>=0){
                    Game *gm = &g_games[gx];
                    pthread_mutex_lock(&gm->lock);
                    for(int j=0;j<gm->numPlayers;j++){
                        if(strcmp(gm->players[j],user)==0){
                            gm->posX[j] = atof(x);
                            gm->posY[j] = atof(y);
                        }
                    }
                    pthread_mutex_unlock(&gm->lock);
                    char m[BUFFER_SIZE];
                    snprintf(m,sizeof(m),"MOVE:%s:%s:%s\n",user,x,y);
                    for(int j=0;j<gm->numPlayers;j++){
                        int d = socketFromUsername(gm->players[j]);
                        if(d!=-1) write(d,m,strlen(m));
                    }
                }
            }
        }
        else if(strcmp(cmd,"FIRE")==0 && logged){
            char *sx=strtok(NULL,":"),*sy=strtok(NULL,":"),*sdx=strtok(NULL,":"),*sdy=strtok(NULL,":");
            if(sx&&sy&&sdx&&sdy)
                fireFromPlayer(sock,user,atof(sx),atof(sy),atof(sdx),atof(sdy));
        }
        else if (strcmp(cmd,"LIST")==0 && logged)
{
    // Renvoie la liste actuelle uniquement à ce client
    char buf[BUFFER_SIZE] = "";
    pthread_mutex_lock(&m_players);
    for(int i=0;i<g_numPlayers;i++){
        strcat(buf, g_connected[i]);
        if(i<g_numPlayers-1) strcat(buf, ",");
    }
    pthread_mutex_unlock(&m_players);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg), "UPDATE_LIST:%s\n", buf);
    write(sock, msg, strlen(msg));
}

    }
}

/* DB helper implementations */
#include "db_helpers.c"
