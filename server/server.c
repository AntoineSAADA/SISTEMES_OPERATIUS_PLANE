#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 12345
#define BUFFER_SIZE 1024

// Maximum players that can be tracked
#define MAX_PLAYERS 100
#define USERNAME_LEN 50

// Maximum concurrent clients for broadcasting
#define MAX_CLIENTS 100

// -------------------------------
// Global list of connected players (pseudos)
// Protégé par un mutex.
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
char g_connectedPlayers[MAX_PLAYERS][USERNAME_LEN];
int g_numPlayers = 0;

// Liste globale des sockets clients pour diffusion
int g_clientSockets[MAX_CLIENTS];
int g_clientCount = 0;
pthread_mutex_t g_clientsMutex = PTHREAD_MUTEX_INITIALIZER;

// -------------------------------
// Prototypes de fonctions
// -------------------------------
void *handleClient(void *arg);

void registerUser(MYSQL *conn, const char *username, const char *email, const char *password, int client_socket);
void loginUser(MYSQL *conn, const char *username, const char *password, int client_socket,
               char *loggedInUser, int *isLoggedIn);
void queryOne(MYSQL *conn, int client_socket);
void queryTwo(MYSQL *conn, int client_socket);
void queryThree(MYSQL *conn, int client_socket);

void addConnectedPlayer(const char *username);
void removeConnectedPlayer(const char *username);
void getConnectedPlayersList(char *outBuffer, int outBufferSize);

void addClientSocket(int client_socket);
void removeClientSocket(int client_socket);
void broadcastPlayersList(void);

// Paramètres de connexion à la DB
const char *DB_HOST = "localhost";
const char *DB_USER = "so";
const char *DB_PASS = "so";
const char *DB_NAME = "SO";
unsigned int DB_PORT = 0;  // 0 = port par défaut (3306)

int main() {
    int server_sock;
    struct sockaddr_in server_addr;
    int addrlen = sizeof(server_addr);

    // Création du socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Serveur en écoute sur le port %d...\n", PORT);

    while (1) {
        int client_socket = accept(server_sock, (struct sockaddr *)&server_addr, (socklen_t *)&addrlen);
        if (client_socket < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }
        printf("Nouveau client connecté.\n");
        addClientSocket(client_socket);

        pthread_t tid;
        int *arg = malloc(sizeof(int));
        *arg = client_socket;
        pthread_create(&tid, NULL, handleClient, arg);
        pthread_detach(tid);
    }

    close(server_sock);
    return 0;
}

void *handleClient(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        fprintf(stderr, "Database connection failed: %s\n", mysql_error(conn));
        close(client_socket);
        return NULL;
    }
    printf("Thread: Database connected successfully.\n");

    char buffer[BUFFER_SIZE];
    char currentUser[USERNAME_LEN] = {0};
    int loggedIn = 0;

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            printf("Client disconnected unexpectedly.\n");
            if (loggedIn) {
                removeConnectedPlayer(currentUser);
                printf("[DEBUG] Player forcibly disconnected: %s\n", currentUser);
            }
            removeClientSocket(client_socket);
            close(client_socket);
            mysql_close(conn);
            return NULL;
        }

        // Important: enlever '\r' ou '\n' à la fin
        buffer[bytes_read] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        char *command = strtok(buffer, ":");
        if (!command) {
            write(client_socket, "Invalid command\n", 16);
            continue;
        }

        if (strcmp(command, "REGISTER") == 0) {
            char *username = strtok(NULL, ":");
            char *email = strtok(NULL, ":");
            char *password = strtok(NULL, ":");
            if (username && email && password) {
                registerUser(conn, username, email, password, client_socket);
            } else {
                write(client_socket, "Missing parameters for REGISTER\n", 32);
            }
        }
        else if (strcmp(command, "LOGIN") == 0) {
            // Format attendu : "LOGIN:<username>:<password>"
            char *username_param = strtok(NULL, ":");
            char *password = strtok(NULL, ":");
            if (username_param && password) {
                loginUser(conn, username_param, password, client_socket, currentUser, &loggedIn);
            } else {
                write(client_socket, "Missing parameters for LOGIN\n", 29);
            }
        }
        else if (strcmp(command, "LOGOUT") == 0) {
            if (loggedIn) {
                removeConnectedPlayer(currentUser);
                loggedIn = 0;
                printf("[DEBUG] Player logged out: %s\n", currentUser);
                write(client_socket, "Logout successful\n", 18);
            } else {
                write(client_socket, "Not logged in\n", 14);
            }
        }
        else if (strcmp(command, "QUERY1") == 0) {
            queryOne(conn, client_socket);
        }
        else if (strcmp(command, "QUERY2") == 0) {
            queryTwo(conn, client_socket);
        }
        else if (strcmp(command, "QUERY3") == 0) {
            queryThree(conn, client_socket);
        }
        else {
            write(client_socket, "Unknown command\n", 16);
        }
    }

    // (En principe, on ne devrait jamais atteindre ici dans la boucle.)
    close(client_socket);
    mysql_close(conn);
    return NULL;
}

void addClientSocket(int client_socket) {
    pthread_mutex_lock(&g_clientsMutex);
    if (g_clientCount < MAX_CLIENTS) {
        g_clientSockets[g_clientCount++] = client_socket;
    } else {
        printf("[WARNING] Too many clients connected.\n");
    }
    pthread_mutex_unlock(&g_clientsMutex);
}

void removeClientSocket(int client_socket) {
    pthread_mutex_lock(&g_clientsMutex);
    for (int i = 0; i < g_clientCount; i++) {
        if (g_clientSockets[i] == client_socket) {
            for (int j = i; j < g_clientCount - 1; j++) {
                g_clientSockets[j] = g_clientSockets[j+1];
            }
            g_clientCount--;
            break;
        }
    }
    pthread_mutex_unlock(&g_clientsMutex);
}

void broadcastPlayersList(void) {
    char listBuf[BUFFER_SIZE];
    // Construction de la liste des pseudos séparés par des virgules
    pthread_mutex_lock(&g_mutex);
    listBuf[0] = '\0';
    for (int i = 0; i < g_numPlayers; i++) {
        strcat(listBuf, g_connectedPlayers[i]);
        if (i < g_numPlayers - 1) {
            strcat(listBuf, ",");
        }
    }
    pthread_mutex_unlock(&g_mutex);

    char fullMsg[BUFFER_SIZE + 50];
    snprintf(fullMsg, sizeof(fullMsg), "UPDATE_LIST:%s\n", listBuf);

    pthread_mutex_lock(&g_clientsMutex);
    for (int i = 0; i < g_clientCount; i++) {
        write(g_clientSockets[i], fullMsg, strlen(fullMsg));
    }
    pthread_mutex_unlock(&g_clientsMutex);
}

void addConnectedPlayer(const char *username) {
    pthread_mutex_lock(&g_mutex);
    if (g_numPlayers < MAX_PLAYERS) {
        printf("[DEBUG] Adding player to list: %s\n", username);
        strncpy(g_connectedPlayers[g_numPlayers], username, USERNAME_LEN - 1);
        g_connectedPlayers[g_numPlayers][USERNAME_LEN - 1] = '\0';
        g_numPlayers++;
        printf("[DEBUG] Player added successfully: %s (Total: %d players)\n",
               username, g_numPlayers);
    } else {
        printf("[DEBUG] Player list full! Cannot add %s\n", username);
    }
    pthread_mutex_unlock(&g_mutex);
    broadcastPlayersList();
}

void removeConnectedPlayer(const char *username) {
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_numPlayers; i++) {
        if (strncmp(g_connectedPlayers[i], username, USERNAME_LEN) == 0) {
            for (int j = i; j < g_numPlayers - 1; j++) {
                strcpy(g_connectedPlayers[j], g_connectedPlayers[j+1]);
            }
            g_numPlayers--;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
    broadcastPlayersList();
}

void getConnectedPlayersList(char *outBuffer, int outBufferSize) {
    outBuffer[0] = '\0';
    pthread_mutex_lock(&g_mutex);
    for (int i = 0; i < g_numPlayers; i++) {
        strcat(outBuffer, g_connectedPlayers[i]);
        if (i < g_numPlayers - 1) {
            strcat(outBuffer, ",");
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

// -------------------------------
// Fonctions REGISTER / LOGIN
// -------------------------------
void registerUser(MYSQL *conn, const char *username, const char *email,
                  const char *password, int client_socket)
{
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO players (username, email, password) "
             "VALUES ('%s','%s','%s')",
             username, email, password);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error (REGISTER): %s\n", mysql_error(conn));
        write(client_socket, "Registration failed\n", 20);
    } else {
        write(client_socket, "Registration successful\n", 24);
    }
}

void loginUser(MYSQL *conn, const char *username, const char *password,
               int client_socket, char *loggedInUser, int *isLoggedIn)
{
    *isLoggedIn = 0;
    loggedInUser[0] = '\0';

    // Nettoyage local
    char userLocal[USERNAME_LEN];
    char passLocal[256];
    strncpy(userLocal, username, sizeof(userLocal)-1);
    userLocal[sizeof(userLocal)-1] = '\0';
    strncpy(passLocal, password, sizeof(passLocal)-1);
    passLocal[sizeof(passLocal)-1] = '\0';

    // Retirer d'éventuels \r ou \n
    userLocal[strcspn(userLocal, "\r\n")] = '\0';
    passLocal[strcspn(passLocal, "\r\n")] = '\0';

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id_player, username "
             "FROM players "
             "WHERE username='%s' AND password='%s'",
             userLocal, passLocal);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error (LOGIN): %s\n", mysql_error(conn));
        write(client_socket, "Login query failed\n", 19);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Error (LOGIN store_result): %s\n", mysql_error(conn));
        write(client_socket, "Login failed\n", 13);
        return;
    }

    if (mysql_num_rows(res) > 0) {
        MYSQL_ROW row = mysql_fetch_row(res);
        const char *foundUsername = row[1] ? row[1] : "";
        strncpy(loggedInUser, foundUsername, USERNAME_LEN - 1);
        loggedInUser[USERNAME_LEN - 1] = '\0';
        *isLoggedIn = 1;
        mysql_free_result(res);

        // On met à jour le last_login
        snprintf(query, sizeof(query),
                 "UPDATE players SET last_login=NOW() "
                 "WHERE username='%s' AND password='%s'",
                 userLocal, passLocal);
        mysql_query(conn, query);

        write(client_socket, "Login successful\n", 17);
        addConnectedPlayer(loggedInUser);
    } else {
        mysql_free_result(res);
        write(client_socket, "Invalid credentials\n", 20);
    }
}

// -------------------------------
// Fonctions QUERY
// -------------------------------

// On va construire TOUTES les infos sur une SEULE ligne
// (puis un seul \n final), pour que le client la lise en un coup.
void queryOne(MYSQL *conn, int client_socket) {
    const char *sql =
        "SELECT username, total_score "
        "FROM players "
        "ORDER BY total_score DESC "
        "LIMIT 5";
    if (mysql_query(conn, sql)) {
        fprintf(stderr, "Error (QUERY1): %s\n", mysql_error(conn));
        write(client_socket, "Query1 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Error (QUERY1 store_result): %s\n", mysql_error(conn));
        write(client_socket, "Query1 store result failed\n", 27);
        return;
    }

    MYSQL_ROW row;
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));
    // On met tout sur une seule ligne. On peut mettre un format plus concis.
    strcat(response, "Top 5 players by score:");

    while ((row = mysql_fetch_row(res))) {
        char line[128];
        // Au lieu de "\n", on utilise un séparateur comme " | "
        snprintf(line, sizeof(line), " | User: %s, Score: %s", row[0], row[1]);
        strncat(response, line, sizeof(response) - strlen(response) - 1);
    }

    mysql_free_result(res);

    // On envoie dans UNE SEULE ligne
    char fullMsg[BUFFER_SIZE + 50];
    snprintf(fullMsg, sizeof(fullMsg), "QUERY1_RESULT:%s\n", response);
    write(client_socket, fullMsg, strlen(fullMsg));
}

void queryTwo(MYSQL *conn, int client_socket) {
    const char *sql =
        "SELECT id_game, name, status, winner_id "
        "FROM game "
        "ORDER BY created_at DESC "
        "LIMIT 5";
    if (mysql_query(conn, sql)) {
        fprintf(stderr, "Error (QUERY2): %s\n", mysql_error(conn));
        write(client_socket, "Query2 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Error (QUERY2 store_result): %s\n", mysql_error(conn));
        write(client_socket, "Query2 store result failed\n", 27);
        return;
    }

    MYSQL_ROW row;
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));
    strcat(response, "Last 5 games:");

    while ((row = mysql_fetch_row(res))) {
        char line[128];
        // On évite le "\n" -> on concatène tout sur une seule ligne
        snprintf(line, sizeof(line),
                 " | GameID: %s, Name: %s, Status: %s, WinnerID: %s",
                 row[0], row[1], row[2], (row[3] ? row[3] : "NULL"));
        strncat(response, line, sizeof(response) - strlen(response) - 1);
    }

    mysql_free_result(res);

    char fullMsg[BUFFER_SIZE + 50];
    snprintf(fullMsg, sizeof(fullMsg), "QUERY2_RESULT:%s\n", response);
    write(client_socket, fullMsg, strlen(fullMsg));
}

void queryThree(MYSQL *conn, int client_socket) {
    const char *sql =
        "SELECT p.username, h.kills "
        "FROM history h "
        "JOIN players p ON p.id_player = h.id_player "
        "ORDER BY h.kills DESC "
        "LIMIT 5";
    if (mysql_query(conn, sql)) {
        fprintf(stderr, "Error (QUERY3): %s\n", mysql_error(conn));
        write(client_socket, "Query3 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "Error (QUERY3 store_result): %s\n", mysql_error(conn));
        write(client_socket, "Query3 store result failed\n", 27);
        return;
    }

    MYSQL_ROW row;
    char response[BUFFER_SIZE];
    memset(response, 0, sizeof(response));
    strcat(response, "Top 5 players by kills:");

    while ((row = mysql_fetch_row(res))) {
        char line[128];
        snprintf(line, sizeof(line), " | User: %s, Kills: %s", row[0], row[1]);
        strncat(response, line, sizeof(response) - strlen(response) - 1);
    }

    mysql_free_result(res);

    char fullMsg[BUFFER_SIZE + 50];
    snprintf(fullMsg, sizeof(fullMsg), "QUERY3_RESULT:%s\n", response);
    write(client_socket, fullMsg, strlen(fullMsg));
}
