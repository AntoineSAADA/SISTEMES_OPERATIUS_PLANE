#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// Maximum players that can be tracked
#define MAX_PLAYERS 100
#define USERNAME_LEN 50

// -------------------------------
// Global list of connected players
// protected by a mutex.
// -------------------------------
pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
char g_connectedPlayers[MAX_PLAYERS][USERNAME_LEN];
int g_numPlayers = 0;

// -------------------------------
// Function Prototypes
// -------------------------------
void *handleClient(void *arg);

void registerUser(MYSQL *conn, const char *username, const char *email, const char *password, int client_socket);
void loginUser(MYSQL *conn, const char *email, const char *password, int client_socket, char *loggedInUser, int *isLoggedIn);
void queryOne(MYSQL *conn, int client_socket);
void queryTwo(MYSQL *conn, int client_socket);
void queryThree(MYSQL *conn, int client_socket);

// Our new commands to manage the global player list
void addConnectedPlayer(const char *username);
void removeConnectedPlayer(const char *username);
void getConnectedPlayersList(char *outBuffer, int outBufferSize);

int main() {
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 2. Reuse port
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 3. Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 4. Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Main accept loop
    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_socket < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        printf("New client connected.\n");

        // Create a thread to handle this client
        pthread_t tid;
        int *arg = malloc(sizeof(int));
        *arg = client_socket;
        pthread_create(&tid, NULL, handleClient, arg);
        // Detach the thread so that resources are freed automatically
        pthread_detach(tid);
    }

    // Normally never reached, but if you do close the server:
    close(server_fd);
    return 0;
}

// ---------------------------------------------------
// The thread routine that handles one client connection
// ---------------------------------------------------
void *handleClient(void *arg) {
    int client_socket = *((int *)arg);
    free(arg);

    // Each thread gets its own connection to MySQL
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "so", "so", "SO", 0, NULL, 0)) {
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
            // Client disconnected unexpectedly
            printf("Client disconnected unexpectedly.\n");
            // Ne pas appeler removeConnectedPlayer() ici
            close(client_socket);
            mysql_close(conn);
            return NULL;
        }

        buffer[bytes_read] = '\0'; // null-terminate

        // Parse command
        char *command = strtok(buffer, ":");
        if (!command) {
            write(client_socket, "Invalid command\n", 16);
            continue;
        }

        // ---------------------------------------------------
        // REGISTER:username:email:password
        // ---------------------------------------------------
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
        // ---------------------------------------------------
        // LOGIN:email:password
        // If success, store username in currentUser
        // ---------------------------------------------------
        else if (strcmp(command, "LOGIN") == 0) {
            char *email = strtok(NULL, ":");
            char *password = strtok(NULL, ":");
            if (email && password) {
                loginUser(conn, email, password, client_socket, currentUser, &loggedIn);
                if (loggedIn) {
                    printf("[DEBUG] Player logged in: %s\n", currentUser);
                    addConnectedPlayer(currentUser);
                } else {
                    printf("[DEBUG] Login failed for email: %s\n", email);
                }
            } else {
                write(client_socket, "Missing parameters for LOGIN\n", 29);
            }
        }
        // ---------------------------------------------------
        // LOGOUT: explicit command to remove the player from the list
        // ---------------------------------------------------
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
        // ---------------------------------------------------
        // GET_PLAYERS: return the list of connected players
        // ---------------------------------------------------
        else if (strcmp(command, "GET_PLAYERS") == 0) {
            char listBuf[BUFFER_SIZE];
            getConnectedPlayersList(listBuf, sizeof(listBuf));
            write(client_socket, listBuf, strlen(listBuf));
        }
        // ---------------------------------------------------
        // QUERY1, QUERY2, QUERY3 (same as before)
        // ---------------------------------------------------
        else if (strcmp(command, "QUERY1") == 0) {
            queryOne(conn, client_socket);
        }
        else if (strcmp(command, "QUERY2") == 0) {
            queryTwo(conn, client_socket);
        }
        else if (strcmp(command, "QUERY3") == 0) {
            queryThree(conn, client_socket);
        }
        // ---------------------------------------------------
        // Unknown command
        // ---------------------------------------------------
        else {
            write(client_socket, "Unknown command\n", 16);
        }
    }

    // Unreachable in this structure, but logically:
    close(client_socket);
    mysql_close(conn);
    return NULL;
}

// ---------------------------------------------------
// Add a connected player
// ---------------------------------------------------
void addConnectedPlayer(const char *username) {
    pthread_mutex_lock(&g_mutex);

    if (g_numPlayers < MAX_PLAYERS) {
        printf("[DEBUG] Adding player to list: %s\n", username);
        strncpy(g_connectedPlayers[g_numPlayers], username, USERNAME_LEN - 1);
        g_connectedPlayers[g_numPlayers][USERNAME_LEN - 1] = '\0';
        g_numPlayers++;
        printf("[DEBUG] Player added successfully: %s (Total: %d players)\n", username, g_numPlayers);
    } else {
        printf("[DEBUG] Player list full! Cannot add %s\n", username);
    }

    pthread_mutex_unlock(&g_mutex);
}

// ---------------------------------------------------
// Remove a connected player
// ---------------------------------------------------
void removeConnectedPlayer(const char *username) {
    pthread_mutex_lock(&g_mutex);

    for (int i = 0; i < g_numPlayers; i++) {
        if (strncmp(g_connectedPlayers[i], username, USERNAME_LEN) == 0) {
            // shift everyone down
            for (int j = i; j < g_numPlayers - 1; j++) {
                strcpy(g_connectedPlayers[j], g_connectedPlayers[j + 1]);
            }
            g_numPlayers--;
            break;
        }
    }

    pthread_mutex_unlock(&g_mutex);
}

// ---------------------------------------------------
// Return a string that lists connected players
// ---------------------------------------------------
void getConnectedPlayersList(char *outBuffer, int outBufferSize) {
    pthread_mutex_lock(&g_mutex);

    snprintf(outBuffer, outBufferSize, "Currently connected players:\n");
    printf("[DEBUG] Request for player list. Currently %d players connected.\n", g_numPlayers);

    for (int i = 0; i < g_numPlayers; i++) {
        char line[128];
        snprintf(line, sizeof(line), " - %s\n", g_connectedPlayers[i]);
        if (strlen(outBuffer) + strlen(line) < (size_t)outBufferSize) {
            strcat(outBuffer, line);
        }
    }

    pthread_mutex_unlock(&g_mutex);
}

// ---------------------------------------------------
// REGISTER: Insert new user in DB
// ---------------------------------------------------
void registerUser(MYSQL *conn, const char *username, const char *email, const char *password, int client_socket) {
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO players (username, email, password) VALUES ('%s','%s','%s')",
             username, email, password);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "Error (REGISTER): %s\n", mysql_error(conn));
        write(client_socket, "Registration failed\n", 20);
    } else {
        write(client_socket, "Registration successful\n", 24);
    }
}

// ---------------------------------------------------
// LOGIN: Check credentials in DB
// On success: store username in currentUser, set isLoggedIn=1
// ---------------------------------------------------
void loginUser(MYSQL *conn, const char *email, const char *password, int client_socket,
               char *loggedInUser, int *isLoggedIn)
{
    // Clear
    *isLoggedIn = 0;
    loggedInUser[0] = '\0';

    char query[512];
    // We want both the id_player and username
    snprintf(query, sizeof(query),
             "SELECT id_player, username FROM players WHERE email='%s' AND password='%s'",
             email, password);

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
        // We have a match, get the username
        MYSQL_ROW row = mysql_fetch_row(res);
        // row[0] = id_player, row[1] = username
        const char *foundUsername = row[1] ? row[1] : "";
        strncpy(loggedInUser, foundUsername, USERNAME_LEN - 1);
        loggedInUser[USERNAME_LEN - 1] = '\0';
        *isLoggedIn = 1;

        // Update last_login
        mysql_free_result(res);
        snprintf(query, sizeof(query),
                 "UPDATE players SET last_login=NOW() WHERE email='%s' AND password='%s'",
                 email, password);
        mysql_query(conn, query);

        write(client_socket, "Login successful\n", 17);
    } else {
        mysql_free_result(res);
        write(client_socket, "Invalid credentials\n", 20);
    }
}

// ---------------------------------------------------
// QUERY1: Example: top 5 players by total_score
// ---------------------------------------------------
void queryOne(MYSQL *conn, int client_socket) {
    const char *query = "SELECT username, total_score FROM players ORDER BY total_score DESC LIMIT 5";
    if (mysql_query(conn, query)) {
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
    strcat(response, "Top 5 players by score:\n");

    while ((row = mysql_fetch_row(res))) {
        // row[0] = username, row[1] = total_score
        char line[128];
        snprintf(line, sizeof(line), "User: %s, Score: %s\n", row[0], row[1]);
        strcat(response, line);
    }
    mysql_free_result(res);

    write(client_socket, response, strlen(response));
}

// ---------------------------------------------------
// QUERY2: Example: last 5 games
// ---------------------------------------------------
void queryTwo(MYSQL *conn, int client_socket) {
    const char *query = "SELECT id_game, name, status, winner_id FROM game ORDER BY created_at DESC LIMIT 5";
    if (mysql_query(conn, query)) {
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
    strcat(response, "Last 5 games:\n");

    while ((row = mysql_fetch_row(res))) {
        // row[0] = id_game, row[1] = name, row[2] = status, row[3] = winner_id
        char line[128];
        snprintf(line, sizeof(line),
                 "GameID: %s, Name: %s, Status: %s, WinnerID: %s\n",
                 row[0], row[1], row[2], (row[3] ? row[3] : "NULL"));
        strcat(response, line);
    }
    mysql_free_result(res);

    write(client_socket, response, strlen(response));
}

// ---------------------------------------------------
// QUERY3: Example: top 5 kills from 'history'
// ---------------------------------------------------
void queryThree(MYSQL *conn, int client_socket) {
    const char *query =
        "SELECT p.username, h.kills "
        "FROM history h "
        "JOIN players p ON p.id_player=h.id_player "
        "ORDER BY h.kills DESC "
        "LIMIT 5";

    if (mysql_query(conn, query)) {
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
    strcat(response, "Top 5 players by kills:\n");

    while ((row = mysql_fetch_row(res))) {
        // row[0] = username, row[1] = kills
        char line[128];
        snprintf(line, sizeof(line), "User: %s, Kills: %s\n", row[0], row[1]);
        strcat(response, line);
    }
    mysql_free_result(res);

    write(client_socket, response, strlen(response));
}
