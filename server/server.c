#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// --- Prototypes de fonctions ---
void handleClient(int client_socket, MYSQL *conn);
void registerUser(MYSQL *conn, const char *username, const char *email, const char *password, int client_socket);
void loginUser(MYSQL *conn, const char *email, const char *password, int client_socket);
void queryOne(MYSQL *conn, int client_socket);
void queryTwo(MYSQL *conn, int client_socket);
void queryThree(MYSQL *conn, int client_socket);

// --- Fonction principale ---
int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // 1. Création du socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 2. Réutilisation du port si déjà occupé
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    // 3. Configuration de la structure d'adresse
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 4. Liaison (bind) du socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 5. Mise en écoute (listen)
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // 6. Connexion à la base de données
   // 6. Connexion à la base de données
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "so", "so", "SO", 0, NULL, 0)) {
        fprintf(stderr, "Database connection failed: %s\n", mysql_error(conn));
        exit(EXIT_FAILURE);
    }

    printf("Database connected successfully.\n");

    printf("Server listening on port %d...\n", PORT);

    // 7. Boucle d'acceptation des connexions clients
    while (1) {
        client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (client_socket < 0) {
            perror("accept failed");
            exit(EXIT_FAILURE);
        }
        printf("New client connected.\n");

        // Traiter la requête du client
        handleClient(client_socket, conn);

        // Fermer la connexion avec le client
        close(client_socket);
        printf("Client disconnected.\n");
    }

    // 8. Fermeture de la connexion à la BDD et du socket serveur
    mysql_close(conn);
    close(server_fd);
    return 0;
}

// --- Gère les commandes reçues d'un client ---
void handleClient(int client_socket, MYSQL *conn) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // Lire les données envoyées par le client
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        return;
    }

    // Format attendu : "COMMANDE:param1:param2:..."
    // Par exemple : "REGISTER:JohnDoe:john@example.com:secret"
    char *command = strtok(buffer, ":");
    if (command == NULL) {
        write(client_socket, "Invalid command\n", 16);
        return;
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
        char *email = strtok(NULL, ":");
        char *password = strtok(NULL, ":");
        if (email && password) {
            loginUser(conn, email, password, client_socket);
        } else {
            write(client_socket, "Missing parameters for LOGIN\n", 29);
        }
    }
    else if (strcmp(command, "QUERY1") == 0) {
        // Première requête : à titre d'exemple, top 5 joueurs par score
        queryOne(conn, client_socket);
    }
    else if (strcmp(command, "QUERY2") == 0) {
        // Deuxième requête : à titre d'exemple, les 5 dernières parties
        queryTwo(conn, client_socket);
    }
    else if (strcmp(command, "QUERY3") == 0) {
        // Troisième requête : à titre d'exemple, top 5 kills
        queryThree(conn, client_socket);
    }
    else {
        // Commande inconnue
        write(client_socket, "Unknown command\n", 16);
    }
}

// --- Inscription d'un utilisateur ---
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

// --- Connexion d'un utilisateur ---
void loginUser(MYSQL *conn, const char *email, const char *password, int client_socket) {
    char query[512];
    snprintf(query, sizeof(query),
             "SELECT id_player FROM players WHERE email='%s' AND password='%s'",
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
        // Utilisateur trouvé
        mysql_free_result(res);

        // Mettre à jour last_login
        snprintf(query, sizeof(query),
                 "UPDATE players SET last_login=NOW() WHERE email='%s' AND password='%s'",
                 email, password);
        mysql_query(conn, query);

        write(client_socket, "Login successful\n", 17);
    } else {
        // Aucun utilisateur correspondant
        mysql_free_result(res);
        write(client_socket, "Invalid credentials\n", 20);
    }
}

// --- Exemple de requête 1 : Top 5 joueurs par total_score ---
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

// --- Exemple de requête 2 : Les 5 dernières parties ---
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

// --- Exemple de requête 3 : Top 5 kills dans l'historique ---
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
