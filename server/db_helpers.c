/* ==================================================================
 *                DATABASE  – REGISTER  /  LOGIN  /  QUERIES
 * ================================================================== */

/* Register a new user */
#include <mysql/mysql.h>       // <-- ajoute ceci AVANT toute utilisation de MYSQL*

void registerUser(MYSQL *conn,
                  const char *username,
                  const char *email,
                  const char *password,
                  int client_socket)
{
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO players (username, email, password) "
             "VALUES ('%s', '%s', '%s')",
             username, email, password);

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "(REGISTER) %s\n", mysql_error(conn));
        write(client_socket, "Registration failed\n", 20);
    } else {
        write(client_socket, "Registration successful\n", 24);
    }
}

/* Login existing user */
void loginUser(MYSQL *conn,
               const char *username,
               const char *password,
               int client_socket,
               char *loggedInUser,
               int *isLoggedIn)
{
    *isLoggedIn = 0;
    loggedInUser[0] = '\0';

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id_player, username "
             "FROM players "
             "WHERE username='%s' AND password='%s'",
             username, password);

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "(LOGIN) %s\n", mysql_error(conn));
        write(client_socket, "Login query failed\n", 19);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "(LOGIN store) %s\n", mysql_error(conn));
        write(client_socket, "Login failed\n", 13);
        return;
    }

    if (mysql_num_rows(res) > 0) {
        MYSQL_ROW row = mysql_fetch_row(res);
        strncpy(loggedInUser, row[1], USERNAME_LEN - 1);
        *isLoggedIn = 1;
        write(client_socket, "Login successful\n", 17);

        /* Update last_login timestamp */
        snprintf(sql, sizeof(sql),
                 "UPDATE players SET last_login = NOW() "
                 "WHERE username='%s'",
                 username);
        mysql_query(conn, sql);
    } else {
        write(client_socket, "Invalid credentials\n", 20);
    }

    mysql_free_result(res);
}

/* Query 1: Top 5 players by total_score */
void queryOne(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT username, total_score "
        "FROM players "
        "ORDER BY total_score DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) {
        write(client_socket, "Query1 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        write(client_socket, "Query1 failed\n", 14);
        return;
    }

    char response[BUFFER_SIZE] = "Top 5 players by score:";
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char part[128];
        snprintf(part, sizeof(part),
                 " | User:%s Score:%s",
                 row[0], row[1]);
        strncat(response, part, sizeof(response) - strlen(response) - 1);
    }
    mysql_free_result(res);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg),
             "QUERY1_RESULT:%s\n",
             response);
    write(client_socket, msg, strlen(msg));
}

/* Query 2: Last 5 games */
void queryTwo(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT id_game, name, status, winner_id "
        "FROM game "
        "ORDER BY created_at DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) {
        write(client_socket, "Query2 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        write(client_socket, "Query2 failed\n", 14);
        return;
    }

    char response[BUFFER_SIZE] = "Last 5 games:";
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char part[160];
        snprintf(part, sizeof(part),
                 " | GameID:%s Name:%s Status:%s Winner:%s",
                 row[0], row[1], row[2], row[3] ? row[3] : "NULL");
        strncat(response, part, sizeof(response) - strlen(response) - 1);
    }
    mysql_free_result(res);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg),
             "QUERY2_RESULT:%s\n",
             response);
    write(client_socket, msg, strlen(msg));
}

/* Query 3: Top 5 players by kills */
void queryThree(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT p.username, h.kills "
        "FROM history h "
        "JOIN players p ON p.id_player = h.id_player "
        "ORDER BY h.kills DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) {
        write(client_socket, "Query3 failed\n", 14);
        return;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        write(client_socket, "Query3 failed\n", 14);
        return;
    }

    char response[BUFFER_SIZE] = "Top 5 players by kills:";
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char part[128];
        snprintf(part, sizeof(part),
                 " | User:%s Kills:%s",
                 row[0], row[1]);
        strncat(response, part, sizeof(response) - strlen(response) - 1);
    }
    mysql_free_result(res);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg),
             "QUERY3_RESULT:%s\n",
             response);
    write(client_socket, msg, strlen(msg));
}
