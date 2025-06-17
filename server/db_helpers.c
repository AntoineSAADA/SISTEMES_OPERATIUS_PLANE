/* ==================================================================
 *                DATABASE  – REGISTER / LOGIN / QUERIES / MISC
 * ================================================================== */
#include <mysql/mysql.h>      /* toujours AVANT tout MYSQL*          */
#include <stdio.h>
#include <string.h>
#include <unistd.h>           /* write()                             */

#ifndef BUFFER_SIZE           /* récupéré depuis le serveur          */
#define BUFFER_SIZE 2048
#endif
#ifndef USERNAME_LEN
#define USERNAME_LEN 50
#endif

/* ─────────────────────────────────────────────────────────────── */
/*  REGISTER                                                      */
/* ─────────────────────────────────────────────────────────────── */
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
    } else
        write(client_socket, "Registration successful\n", 24);
}

/* ─────────────────────────────────────────────────────────────── */
/*  LOGIN                                                          */
/* ─────────────────────────────────────────────────────────────── */
void loginUser(MYSQL *conn,
               const char *username,
               const char *password,
               int  client_socket,
               char *loggedInUser,
               int  *isLoggedIn)
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
        fprintf(stderr, "(LOGIN-query) %s\n", mysql_error(conn));
        write(client_socket, "Login query failed\n", 19);
        return;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        fprintf(stderr, "(LOGIN-store) %s\n", mysql_error(conn));
        write(client_socket, "Login failed\n", 13);
        return;
    }

    if (mysql_num_rows(res) > 0) {
        MYSQL_ROW row = mysql_fetch_row(res);
        strncpy(loggedInUser, row[1], USERNAME_LEN - 1);
        *isLoggedIn = 1;
        write(client_socket, "Login successful\n", 17);

        /* met à jour last_login */
        snprintf(sql, sizeof(sql),
                 "UPDATE players SET last_login = NOW() "
                 "WHERE username='%s'", username);
        mysql_query(conn, sql);
    } else
        write(client_socket, "Invalid credentials\n", 20);

    mysql_free_result(res);
}

/* ─────────────────────────────────────────────────────────────── */
/*  LOGOUT  (simple accusé + mise à jour last_login)              */
/* ─────────────────────────────────────────────────────────────── */
void logoutUser(MYSQL *conn,
                const char *username,
                int client_socket)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "UPDATE players SET last_login = NOW() "
             "WHERE username='%s'", username);
    mysql_query(conn, sql);
    write(client_socket, "Logout successful\n", 18);
}

/* ─────────────────────────────────────────────────────────────── */
/*  DELETE ACCOUNT                                                */
/*      – supprime le joueur → déclenche   ON DELETE CASCADE      */
/* ─────────────────────────────────────────────────────────────── */
void deleteAccount(MYSQL *conn,
                   const char *username,
                   int client_socket)
{
    char sql[256];
    snprintf(sql, sizeof(sql),
             "DELETE FROM players WHERE username='%s'", username);

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "(DELETE) %s\n", mysql_error(conn));
        write(client_socket, "Account deletion failed\n", 24);
    } else
        write(client_socket, "Account deleted\n", 16);
}

/* ─────────────────────────────────────────────────────────────── */
/*  QUERY 1 : Top 5 joueurs par total_score                       */
/* ─────────────────────────────────────────────────────────────── */
void queryOne(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT username, total_score "
        "FROM players "
        "ORDER BY total_score DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) { write(client_socket, "Query1 failed\n", 14); return; }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res)       { write(client_socket, "Query1 failed\n", 14); return; }

    char response[BUFFER_SIZE] = "Top 5 players by score:";
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char part[128];
        snprintf(part, sizeof(part), " | User:%s Score:%s", row[0], row[1]);
        strncat(response, part, sizeof(response) - strlen(response) - 1);
    }
    mysql_free_result(res);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg), "QUERY1_RESULT:%s\n", response);
    write(client_socket, msg, strlen(msg));
}

/* ─────────────────────────────────────────────────────────────── */
/*  QUERY 2 : 5 dernières parties                                 */
/* ─────────────────────────────────────────────────────────────── */
void queryTwo(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT id_game, name, status, winner_id "
        "FROM game "
        "ORDER BY created_at DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) { write(client_socket, "Query2 failed\n", 14); return; }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res)       { write(client_socket, "Query2 failed\n", 14); return; }

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
    snprintf(msg, sizeof(msg), "QUERY2_RESULT:%s\n", response);
    write(client_socket, msg, strlen(msg));
}

/* ─────────────────────────────────────────────────────────────── */
/*  QUERY 3 : Top 5 joueurs par kills                              */
/* ─────────────────────────────────────────────────────────────── */
void queryThree(MYSQL *conn, int client_socket)
{
    const char *sql =
        "SELECT p.username, h.kills "
        "FROM history h "
        "JOIN players p ON p.id_player = h.id_player "
        "ORDER BY h.kills DESC "
        "LIMIT 5";

    if (mysql_query(conn, sql)) { write(client_socket, "Query3 failed\n", 14); return; }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res)       { write(client_socket, "Query3 failed\n", 14); return; }

    char response[BUFFER_SIZE] = "Top 5 players by kills:";
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        char part[128];
        snprintf(part, sizeof(part), " | User:%s Kills:%s", row[0], row[1]);
        strncat(response, part, sizeof(response) - strlen(response) - 1);
    }
    mysql_free_result(res);

    char msg[BUFFER_SIZE + 32];
    snprintf(msg, sizeof(msg), "QUERY3_RESULT:%s\n", response);
    write(client_socket, msg, strlen(msg));
}
