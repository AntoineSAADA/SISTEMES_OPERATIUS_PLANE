/* ------------------------------------------------------------------
 *  server.c  – Multiplayer game server (v5 : chat + move + parties)
 * ------------------------------------------------------------------
 *  ↳  Base : votre v4 (invitation protocol) conservée intégralement
 *  ↳  Ajouts v5 :
 *        • mappage socket⇆username pour adresser un joueur
 *        • structure Game + lancement auto quand tout le monde accepte
 *        • commandes CHAT et MOVE diffusées aux joueurs de la partie
 * ------------------------------------------------------------------ */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <mysql/mysql.h>
 #include <sys/socket.h>
 #include <netinet/in.h>
 #include <unistd.h>
 #include <arpa/inet.h>
 #include <pthread.h>
 
 /* -------------------------  constants  ------------------------- */
 #define PORT            12345
 #define BUFFER_SIZE     1024
 
 #define MAX_PLAYERS     100
 #define USERNAME_LEN    50
 
 #define MAX_CLIENTS     100
 #define MAX_INVITES     50        /* simultaneous invitations    */
 
 /* === v5 ADD >>> */
 #define MAX_GAMES       20
 /* <<< v5 ADD === */
 
 /* -------------------------  invitation structure -------------- */
 typedef struct
 {
     int  active;                                   /* 1 = pending   */
     char inviter[USERNAME_LEN];                    /* who invited   */
     char invitees[MAX_PLAYERS][USERNAME_LEN];      /* invited users */
     int  numInvitees;
     int  responses[MAX_PLAYERS];                   /* 0 pend /1 ok/-1 no */
 } Invitation;
 
 /* === v5 ADD >>> */
 typedef struct
 {
     int  active;
     char players[MAX_PLAYERS][USERNAME_LEN];
     int  numPlayers;
 } Game;
 /* <<< v5 ADD === */
 
 /* -------------------------  globals --------------------------- */
 pthread_mutex_t g_mutex        = PTHREAD_MUTEX_INITIALIZER; /* player list  */
 pthread_mutex_t g_clientsMutex = PTHREAD_MUTEX_INITIALIZER; /* socket list  */
 pthread_mutex_t g_inviteMutex  = PTHREAD_MUTEX_INITIALIZER; /* invitations  */
 /* === v5 ADD >>> */ pthread_mutex_t g_gameMutex = PTHREAD_MUTEX_INITIALIZER; /* games */
 /* <<< v5 ADD === */
 
 char g_connectedPlayers[MAX_PLAYERS][USERNAME_LEN];
 int  g_numPlayers = 0;
 
 /* sockets */
 int  g_clientSockets[MAX_CLIENTS];
 /* === v5 ADD >>> */
 char g_socketUsers[MAX_CLIENTS][USERNAME_LEN];  /* "" tant que pas loggé */
 /* <<< v5 ADD === */
 int  g_clientCount = 0;
 
 Invitation g_invites[MAX_INVITES] = {0};
 /* === v5 ADD >>> */ Game g_games[MAX_GAMES] = {0}; /* <<< v5 ADD === */
 
 /* -------------------------  DB connection data ---------------- */
 const char *DB_HOST = "localhost";
 const char *DB_USER = "so";
 const char *DB_PASS = "so";
 const char *DB_NAME = "SO";
 unsigned int DB_PORT = 0;   /* 0 = default 3306 */
 
 /* -------------------------  prototypes ------------------------ */
 void *handleClient(void *arg);
 
 /* DB helpers */
 void registerUser(MYSQL *, const char *, const char *, const char *, int);
 void loginUser   (MYSQL *, const char *, const char *, int, char *, int *);
 void queryOne(MYSQL *, int);
 void queryTwo(MYSQL *, int);
 void queryThree(MYSQL *, int);
 
 /* player/sockets */
 void addConnectedPlayer   (const char *);
 void removeConnectedPlayer(const char *);
 void broadcastPlayersList (void);
 
 void addClientSocket(int);
 void removeClientSocket(int);
 void broadcastMessage(const char *msg);
 
 /* === v5 ADD >>> */
 void setSocketUsername(int,const char*);
 int  socketFromUsername(const char*);
 int  findGameByPlayer(const char*);
 void startGame(Invitation*);
 /* <<< v5 ADD === */
 
 /* invitations */
 static int findInviteSlotByInviter(const char *);
 void createInvitation(const char *inviter, const char *csvList);
 void handleInviteAnswer(const char *inviter, const char *invitee,
                         int accepted);
 
 /* ------------------------------------------------------------------
  *                              main
  * ------------------------------------------------------------------ */
 int main(void)
 {
     int server_sock;
     struct sockaddr_in server_addr;
     int addrlen = sizeof(server_addr);
 
     server_sock = socket(AF_INET, SOCK_STREAM, 0);
     if (server_sock == 0) { perror("socket"); exit(EXIT_FAILURE); }
 
     int opt = 1;
     setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                &opt, sizeof(opt));
 
     server_addr.sin_family      = AF_INET;
     server_addr.sin_addr.s_addr = INADDR_ANY;
     server_addr.sin_port        = htons(PORT);
 
     if (bind(server_sock, (struct sockaddr *) &server_addr,
              sizeof(server_addr)) < 0)
     { perror("bind"); exit(EXIT_FAILURE); }
 
     if (listen(server_sock, 10) < 0) { perror("listen"); exit(EXIT_FAILURE); }
 
     printf("Server listening on port %d …\n", PORT);
 
     while (1)
     {
         int client_socket = accept(server_sock, (struct sockaddr *) &server_addr,
                                    (socklen_t *) &addrlen);
         if (client_socket < 0) { perror("accept"); continue; }
 
         addClientSocket(client_socket);
 
         pthread_t tid;
         int *arg = malloc(sizeof(int));
         *arg = client_socket;
         pthread_create(&tid, NULL, handleClient, arg);
         pthread_detach(tid);
     }
     return 0;  /* never reached */
 }
 
 /* ==================================================================
  *                     invitation helpers  (NEW)
  * ================================================================== */
 static int findInviteSlotByInviter(const char *inviter)
 {
     for (int i = 0; i < MAX_INVITES; i++)
         if (g_invites[i].active &&
             strcmp(g_invites[i].inviter, inviter) == 0)
             return i;
     return -1;
 }
 
 void createInvitation(const char *inviter, const char *csvList)
 {
     pthread_mutex_lock(&g_inviteMutex);
 
     int slot = -1;
     for (int i = 0; i < MAX_INVITES; i++)
         if (!g_invites[i].active) { slot = i; break; }
     if (slot == -1) { pthread_mutex_unlock(&g_inviteMutex); return; }
 
     Invitation *inv = &g_invites[slot];
     memset(inv, 0, sizeof(*inv));
     inv->active = 1;
     strncpy(inv->inviter, inviter, USERNAME_LEN - 1);
 
     /* split invitee CSV */
     char copy[BUFFER_SIZE];
     strncpy(copy, csvList, sizeof(copy) - 1);
     copy[sizeof(copy) - 1] = '\0';
 
     char *tok = strtok(copy, ",");
     int  idx  = 0;
     while (tok && idx < MAX_PLAYERS)
     {
         strncpy(inv->invitees[idx], tok, USERNAME_LEN - 1);
         inv->responses[idx] = 0;
         idx++;
         tok = strtok(NULL, ",");
     }
     inv->numInvitees = idx;
 
     pthread_mutex_unlock(&g_inviteMutex);
 }
 
 /* === v5 ADD >>> */
 void startGame(Invitation *inv)
 {
     pthread_mutex_lock(&g_gameMutex);
     for (int g=0; g<MAX_GAMES; g++)
         if (!g_games[g].active)
         {
             Game *gm = &g_games[g];
             gm->active = 1;
             strcpy(gm->players[0], inv->inviter);
             gm->numPlayers = 1;
             for (int i=0;i<inv->numInvitees;i++)
                 strcpy(gm->players[gm->numPlayers++], inv->invitees[i]);
             break;
         }
     pthread_mutex_unlock(&g_gameMutex);
 }
 int findGameByPlayer(const char *u)
 {
     for (int g=0; g<MAX_GAMES; g++)
         if (g_games[g].active)
             for (int p=0; p<g_games[g].numPlayers; p++)
                 if (strcmp(g_games[g].players[p],u)==0) return g;
     return -1;
 }
 /* <<< v5 ADD === */
 
 void handleInviteAnswer(const char *inviter, const char *invitee,
                         int accepted)
 {
     pthread_mutex_lock(&g_inviteMutex);
 
     int slot = findInviteSlotByInviter(inviter);
     if (slot == -1) { pthread_mutex_unlock(&g_inviteMutex); return; }
 
     Invitation *inv = &g_invites[slot];
 
     for (int i = 0; i < inv->numInvitees; i++)
         if (strcmp(inv->invitees[i], invitee) == 0)
             inv->responses[i] = accepted ? 1 : -1;
 
     /* check whether all responded */
     int allAnswered = 1, anyRejected = 0;
     for (int i = 0; i < inv->numInvitees; i++)
     {
         if (inv->responses[i] == 0)  allAnswered = 0;
         if (inv->responses[i] == -1) anyRejected = 1;
     }
 
     if (!allAnswered)
     { pthread_mutex_unlock(&g_inviteMutex); return; }
 
     /* everyone answered -> broadcast result and free slot */
     char msg[BUFFER_SIZE];
     snprintf(msg, sizeof(msg), "INVITE_RESULT:%s:%s\n",
              inv->inviter, anyRejected ? "REJECTED" : "ACCEPTED");
 
     if (!anyRejected) startGame(inv);           /* === v5 ADD  */
 
     inv->active = 0;                            /* free slot    */
     pthread_mutex_unlock(&g_inviteMutex);
 
     broadcastMessage(msg);
 }
 
 /* ==================================================================
  *                   player / socket helper functions
  * ================================================================== */
 void broadcastMessage(const char *msg)
 {
     pthread_mutex_lock(&g_clientsMutex);
     for (int i = 0; i < g_clientCount; i++)
         write(g_clientSockets[i], msg, strlen(msg));
     pthread_mutex_unlock(&g_clientsMutex);
 }
 
 void addClientSocket(int client_socket)
 {
     pthread_mutex_lock(&g_clientsMutex);
     if (g_clientCount < MAX_CLIENTS)
     {
         g_clientSockets[g_clientCount] = client_socket;
         /* === v5 ADD >>> */ g_socketUsers[g_clientCount][0]='\0'; /* <<< v5 ADD === */
         g_clientCount++;
     }
     pthread_mutex_unlock(&g_clientsMutex);
 }
 
 void removeClientSocket(int client_socket)
 {
     pthread_mutex_lock(&g_clientsMutex);
     for (int i = 0; i < g_clientCount; i++)
         if (g_clientSockets[i] == client_socket)
         {
             for (int j = i; j < g_clientCount - 1; j++)
             {
                 g_clientSockets[j] = g_clientSockets[j + 1];
                 /* === v5 ADD >>> */ strcpy(g_socketUsers[j], g_socketUsers[j+1]);
                 /* <<< v5 ADD === */
             }
             g_clientCount--;
             break;
         }
     pthread_mutex_unlock(&g_clientsMutex);
 }
 
 /* === v5 ADD >>> */
 void setSocketUsername(int sock,const char *u)
 {
     pthread_mutex_lock(&g_clientsMutex);
     for (int i=0;i<g_clientCount;i++)
         if (g_clientSockets[i]==sock)
         { strncpy(g_socketUsers[i],u,USERNAME_LEN-1); break; }
     pthread_mutex_unlock(&g_clientsMutex);
 }
 int socketFromUsername(const char *u)
 {
     pthread_mutex_lock(&g_clientsMutex);
     for (int i=0;i<g_clientCount;i++)
         if (strcmp(g_socketUsers[i],u)==0)
         { int s=g_clientSockets[i]; pthread_mutex_unlock(&g_clientsMutex); return s; }
     pthread_mutex_unlock(&g_clientsMutex);
     return -1;
 }
 /* <<< v5 ADD === */
 
 void broadcastPlayersList(void)
 {
     char listBuf[BUFFER_SIZE] = "";
 
     pthread_mutex_lock(&g_mutex);
     for (int i = 0; i < g_numPlayers; i++)
     {
         strcat(listBuf, g_connectedPlayers[i]);
         if (i < g_numPlayers - 1) strcat(listBuf, ",");
     }
     pthread_mutex_unlock(&g_mutex);
 
     char msg[BUFFER_SIZE + 32];
     snprintf(msg, sizeof(msg), "UPDATE_LIST:%s\n", listBuf);
     broadcastMessage(msg);
 }
 
 void addConnectedPlayer(const char *username)
 {
     pthread_mutex_lock(&g_mutex);
     if (g_numPlayers < MAX_PLAYERS)
     {
         strncpy(g_connectedPlayers[g_numPlayers], username, USERNAME_LEN - 1);
         g_connectedPlayers[g_numPlayers][USERNAME_LEN - 1] = '\0';
         g_numPlayers++;
     }
     pthread_mutex_unlock(&g_mutex);
 
     broadcastPlayersList();
 }
 
 void removeConnectedPlayer(const char *username)
 {
     pthread_mutex_lock(&g_mutex);
     for (int i = 0; i < g_numPlayers; i++)
         if (strcmp(g_connectedPlayers[i], username) == 0)
         {
             for (int j = i; j < g_numPlayers - 1; j++)
                 strcpy(g_connectedPlayers[j], g_connectedPlayers[j + 1]);
             g_numPlayers--;
             break;
         }
     pthread_mutex_unlock(&g_mutex);
 
     broadcastPlayersList();
 }
 
 /* ==================================================================
  *                     per‑client thread
  * ================================================================== */
 void *handleClient(void *arg)
 {
     int client_socket = *((int *) arg);
     free(arg);
 
     MYSQL *conn = mysql_init(NULL);
     if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS,
                             DB_NAME, DB_PORT, NULL, 0))
     {
         fprintf(stderr, "DB connection failed: %s\n", mysql_error(conn));
         close(client_socket);
         return NULL;
     }
 
     char buffer[BUFFER_SIZE];
     char currentUser[USERNAME_LEN] = "";
     int  loggedIn = 0;
 
     while (1)
     {
         int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
         if (bytes_read <= 0)
         {
             if (loggedIn) removeConnectedPlayer(currentUser);
             removeClientSocket(client_socket);
             close(client_socket);
             mysql_close(conn);
             return NULL;
         }
 
         buffer[bytes_read] = '\0';
         buffer[strcspn(buffer, "\r\n")] = '\0';
 
         char *command = strtok(buffer, ":");
         if (!command) continue;
 
         /* ----------------  BASIC PROTOCOL  ---------------- */
         if (strcmp(command, "REGISTER") == 0)
         {
             char *u = strtok(NULL, ":");
             char *e = strtok(NULL, ":");
             char *p = strtok(NULL, ":");
             if (u && e && p) registerUser(conn, u, e, p, client_socket);
             else write(client_socket, "Missing parameters for REGISTER\n", 32);
         }
         else if (strcmp(command, "LOGIN") == 0)
         {
             char *u = strtok(NULL, ":");
             char *p = strtok(NULL, ":");
             if (u && p)
                 loginUser(conn, u, p, client_socket, currentUser, &loggedIn);
             else
                 write(client_socket, "Missing parameters for LOGIN\n", 29);
 
             if (loggedIn) setSocketUsername(client_socket, currentUser);  /* v5 */
         }
         else if (strcmp(command, "LOGOUT") == 0)
         {
             if (loggedIn)
             {
                 removeConnectedPlayer(currentUser);
                 loggedIn = 0;
                 write(client_socket, "Logout successful\n", 18);
             }
             else write(client_socket, "Not logged in\n", 14);
         }
         else if (strcmp(command, "QUERY1") == 0)        queryOne  (conn, client_socket);
         else if (strcmp(command, "QUERY2") == 0)        queryTwo  (conn, client_socket);
         else if (strcmp(command, "QUERY3") == 0)        queryThree(conn, client_socket);
 
         /* ----------------  INVITATION PROTOCOL  ------------ */
         else if (strcmp(command, "INVITE") == 0 && loggedIn)
         {
             char *csv = strtok(NULL, ":");
             if (!csv) continue;
 
             createInvitation(currentUser, csv);
 
             char msg[BUFFER_SIZE];
             snprintf(msg, sizeof(msg), "INVITE_REQUEST:%s:%s\n",
                      currentUser, csv);
             broadcastMessage(msg);
         }
         else if (strcmp(command, "INVITE_RESP") == 0 && loggedIn)
         {
             char *inviter = strtok(NULL, ":");
             char *resp    = strtok(NULL, ":");
             if (!inviter || !resp) continue;
             int accepted = (strcmp(resp, "ACCEPT") == 0);
             handleInviteAnswer(inviter, currentUser, accepted);
         }
 
         /* === v5 ADD >>>  CHAT & MOVE  ---------------------- */
         else if (strcmp(command,"CHAT")==0 && loggedIn)
         {
             char *body = strtok(NULL,"");
             if (!body) continue;
             int gid = findGameByPlayer(currentUser);
             if (gid==-1) continue;
 
             char pkt[BUFFER_SIZE];
             snprintf(pkt,sizeof(pkt),"CHAT_MSG:%s:%s\n",currentUser,body);
 
             pthread_mutex_lock(&g_gameMutex);
             for (int p=0;p<g_games[gid].numPlayers;p++)
             {
                 int dst=socketFromUsername(g_games[gid].players[p]);
                 if (dst!=-1) write(dst,pkt,strlen(pkt));
             }
             pthread_mutex_unlock(&g_gameMutex);
         }
         else if (strcmp(command,"MOVE")==0 && loggedIn)
         {
             char *x=strtok(NULL,":");
             char *y=strtok(NULL,":");
             if(!x||!y)continue;
             int gid=findGameByPlayer(currentUser);
             if(gid==-1)continue;
 
             char pkt[BUFFER_SIZE];
             snprintf(pkt,sizeof(pkt),"MOVE:%s:%s:%s\n",currentUser,x,y);
 
             pthread_mutex_lock(&g_gameMutex);
             for(int p=0;p<g_games[gid].numPlayers;p++)
             {
                 int dst=socketFromUsername(g_games[gid].players[p]);
                 if(dst!=-1) write(dst,pkt,strlen(pkt));
             }
             pthread_mutex_unlock(&g_gameMutex);
         }

         /* ----------  FIRE  ------------ */
else if (strcmp(command,"FIRE")==0 && loggedIn)
{
    char *x  = strtok(NULL,":");
    char *y  = strtok(NULL,":");
    char *dx = strtok(NULL,":");
    char *dy = strtok(NULL,":");
    if (!x||!y||!dx||!dy) continue;

    int gid = findGameByPlayer(currentUser);
    if (gid==-1) continue;

    char pkt[BUFFER_SIZE];
    snprintf(pkt,sizeof(pkt),"FIRE:%s:%s:%s:%s:%s\n",
             currentUser,x,y,dx,dy);

    pthread_mutex_lock(&g_gameMutex);
    for(int p=0;p<g_games[gid].numPlayers;p++)
    {
        int dst = socketFromUsername(g_games[gid].players[p]);
        if(dst!=-1) write(dst,pkt,strlen(pkt));
    }
    pthread_mutex_unlock(&g_gameMutex);
}

         else
             write(client_socket, "Unknown command\n", 16);
     }
 }
 
 /* ==================================================================
  *                DATABASE  – REGISTER / LOGIN  (inchangé)
  * ================================================================== */
 /* — vos fonctions registerUser, loginUser, queryOne, queryTwo, queryThree
    sont strictement les mêmes qu’en v4 : recopiez‑les ou laissez‑les comme
    elles sont dans votre projet. — */
 
 
 /* ==================================================================
  *                DATABASE  – REGISTER  /  LOGIN
  * ================================================================== */
 void registerUser(MYSQL *conn, const char *username, const char *email,
                   const char *password, int client_socket)
 {
     char sql[512];
     snprintf(sql, sizeof(sql),
              "INSERT INTO players (username, email, password) "
              "VALUES ('%s', '%s', '%s')",
              username, email, password);
 
     if (mysql_query(conn, sql))
     {
         fprintf(stderr, "(REGISTER) %s\n", mysql_error(conn));
         write(client_socket, "Registration failed\n", 20);
     }
     else
         write(client_socket, "Registration successful\n", 24);
 }
 
 void loginUser(MYSQL *conn, const char *username, const char *password,
                int client_socket, char *loggedInUser, int *isLoggedIn)
 {
     *isLoggedIn = 0;
     loggedInUser[0] = '\0';
 
     char sql[512];
     snprintf(sql, sizeof(sql),
              "SELECT id_player, username "
              "FROM players "
              "WHERE username='%s' AND password='%s'",
              username, password);
 
     if (mysql_query(conn, sql))
     {
         fprintf(stderr, "(LOGIN) %s\n", mysql_error(conn));
         write(client_socket, "Login query failed\n", 19);
         return;
     }
 
     MYSQL_RES *res = mysql_store_result(conn);
     if (!res)
     {
         fprintf(stderr, "(LOGIN store) %s\n", mysql_error(conn));
         write(client_socket, "Login failed\n", 13);
         return;
     }
 
     if (mysql_num_rows(res) > 0)
     {
         MYSQL_ROW row = mysql_fetch_row(res);
         strncpy(loggedInUser, row[1], USERNAME_LEN - 1);
         *isLoggedIn = 1;
         write(client_socket, "Login successful\n", 17);
 
         /* update last_login */
         snprintf(sql, sizeof(sql),
                  "UPDATE players SET last_login = NOW() "
                  "WHERE username='%s'", username);
         mysql_query(conn, sql);
 
         addConnectedPlayer(loggedInUser);
     }
     else
         write(client_socket, "Invalid credentials\n", 20);
 
     mysql_free_result(res);
 }
 
 /* ==================================================================
  *                       SAMPLE QUERY FUNCTIONS
  * ================================================================== */
 void queryOne(MYSQL *conn, int client_socket)
 {
     const char *sql =
         "SELECT username, total_score "
         "FROM players "
         "ORDER BY total_score DESC "
         "LIMIT 5";
 
     if (mysql_query(conn, sql))
     { write(client_socket, "Query1 failed\n", 14); return; }
 
     MYSQL_RES *res = mysql_store_result(conn);
     if (!res) { write(client_socket, "Query1 failed\n", 14); return; }
 
     char response[BUFFER_SIZE] = "Top 5 players by score:";
     MYSQL_ROW row;
     while ((row = mysql_fetch_row(res)))
     {
         char part[128];
         snprintf(part, sizeof(part), " | User:%s Score:%s", row[0], row[1]);
         strncat(response, part, sizeof(response) - strlen(response) - 1);
     }
     mysql_free_result(res);
 
     char msg[BUFFER_SIZE + 32];
     snprintf(msg, sizeof(msg), "QUERY1_RESULT:%s\n", response);
     write(client_socket, msg, strlen(msg));
 }
 
 void queryTwo(MYSQL *conn, int client_socket)
 {
     const char *sql =
         "SELECT id_game, name, status, winner_id "
         "FROM game "
         "ORDER BY created_at DESC "
         "LIMIT 5";
 
     if (mysql_query(conn, sql))
     { write(client_socket, "Query2 failed\n", 14); return; }
 
     MYSQL_RES *res = mysql_store_result(conn);
     if (!res) { write(client_socket, "Query2 failed\n", 14); return; }
 
     char response[BUFFER_SIZE] = "Last 5 games:";
     MYSQL_ROW row;
     while ((row = mysql_fetch_row(res)))
     {
         char part[160];
         snprintf(part, sizeof(part),
                  " | GameID:%s Name:%s Status:%s Winner:%s",
                  row[0], row[1], row[2], row[3] ? row[3] : "NULL");
         strncat(response, part, sizeof(response) - strlen(response) - 1);
     }
     mysql_free_result(res);
 
     char msg[BUFFER_SIZE + 32];
     snprintf(msg, sizeof(msg), "QUERY2_RESULT:%s\n", response);
     write(client_socket, msg, strlen(msg));
 }
 
 void queryThree(MYSQL *conn, int client_socket)
 {
     const char *sql =
         "SELECT p.username, h.kills "
         "FROM history h "
         "JOIN players p ON p.id_player = h.id_player "
         "ORDER BY h.kills DESC "
         "LIMIT 5";
 
     if (mysql_query(conn, sql))
     { write(client_socket, "Query3 failed\n", 14); return; }
 
     MYSQL_RES *res = mysql_store_result(conn);
     if (!res) { write(client_socket, "Query3 failed\n", 14); return; }
 
     char response[BUFFER_SIZE] = "Top 5 players by kills:";
     MYSQL_ROW row;
     while ((row = mysql_fetch_row(res)))
     {
         char part[128];
         snprintf(part, sizeof(part), " | User:%s Kills:%s", row[0], row[1]);
         strncat(response, part, sizeof(response) - strlen(response) - 1);
     }
     mysql_free_result(res);
 
     char msg[BUFFER_SIZE + 32];
     snprintf(msg, sizeof(msg), "QUERY3_RESULT:%s\n", response);
     write(client_socket, msg, strlen(msg));
 }