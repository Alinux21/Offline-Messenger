#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <sqlite3.h>

#define PORT 2908
#define MAX_CLIENTS 100

extern int errno;

typedef struct thData
{
    int idThread;      // id-ul thread-ului tinut in evidenta de acest program
    char username[30]; // usernameul pentru fiecare user
    int sd;            // descriptorul intors de accept
    int sd_listen;
} thData;

typedef struct MSG
{
    char sender[30];
    char recipient[30];
    char message[220];
    int reply_id;
    int id;
} Message;

typedef struct LoginData
{
    char username[30];
    char password[30];
} LoginData;

thData clients[MAX_CLIENTS];

static void *treat(void *);

int is_online(char username[30])
{
    for (int i = 1; i < 100; i++)
    {
        if (strcmp(clients[i].username, username) == 0 && strcmp(username, "") != 0)
        {
            return 1;
        }
    }
    return 0;
}

int process_login(int client, char *msgrasp, LoginData login_info)
{
    int ok = 0;
    sqlite3 *db; sqlite3_stmt *res;
    int rc = sqlite3_open("MessengerDB", &db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return -1;
    }
    const char *sql = "SELECT * FROM login;";
    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));return -1;
    }
    int user_found = 0, correct_pass = 0;
    while (sqlite3_step(res) == SQLITE_ROW)
    {
        const char *username = (const char *)sqlite3_column_text(res, 0);
        if (strcmp(login_info.username, username) == 0)
        {
            user_found = 1;
            const char *password = (const char *)sqlite3_column_text(res, 1);
            if (strcmp(login_info.password, password) == 0)
            {
                correct_pass = 1;break;
            }
        }
    }
    sqlite3_finalize(res);
    sqlite3_close(db);
    if (user_found == 0)
    {
        strcat(msgrasp, "User not found!");}
    else if (correct_pass == 0)
    {
        strcat(msgrasp, "Incorrect password!");}
    else
    {
        ok = 1;
        strcat(msgrasp, "Logged in successfully!");
        for (int i = 1; i < 100; ++i)
        {
            if (clients[i].sd == client)
            {
                strcpy(clients[i].username, login_info.username); break;}}}
    return ok;
}

void disconnect_user(int client)
{
    for (int i = 1; i < 100; i++)
    {
        if (clients[i].sd == client)
        {
            strcpy(clients[i].username, "");
            clients[i].sd = 0;
            clients[i].sd_listen = 0;
            break;
        }
    }
}

int get_sd_listen_by_username(char username[30])
{
    for (int i = 1; i < 100; i++)
    {
        if (strcmp(clients[i].username, username) == 0)
        {
            return clients[i].sd_listen;
        }
    }
}

Message get_message_by_id(int id, char user1[30], char user2[30])
{

    sqlite3 *db;
    sqlite3_stmt *res;
    int rc = sqlite3_open("MessengerDB", &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    }

    char sql[200];

    if (strcmp(user1, user2) < 0)
    {
        sprintf(sql, "SELECT sender,recipient,message,id FROM History_%s_%s WHERE id=%d;", user1, user2, id);
    }
    else
    {
        sprintf(sql, "SELECT sender,recipient,message,id FROM History_%s_%s WHERE id=%d;", user2, user1, id);
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
    }
    Message prev_message;
    memset(&prev_message, 0, sizeof(Message));

    rc = sqlite3_step(res);
    if (rc == SQLITE_DONE)
    {
        fprintf(stderr, "Failed to execute SELECT query: %s\n", sqlite3_errmsg(db));
    }
    printf("\n  %s %s %s \n", (const char *)sqlite3_column_text(res, 0), (const char *)sqlite3_column_text(res, 1), (const char *)sqlite3_column_text(res, 2));
    fflush(stdout);

    strcpy(prev_message.sender, (const char *)sqlite3_column_text(res, 0));
    strcpy(prev_message.recipient, (const char *)sqlite3_column_text(res, 1));
    strcpy(prev_message.message, (const char *)sqlite3_column_text(res, 2));
    prev_message.id = sqlite3_column_int(res, 3);

    sqlite3_finalize(res);
    sqlite3_close(db);

    return prev_message;
}

void send_new_messages(char username[30])
{
    sqlite3 *db;
    sqlite3_stmt *res;
    sqlite3_stmt *res2;
    sqlite3_stmt *res3;
    sqlite3_stmt *res4;
    Message message;
    Message prev_message;

    int rc = sqlite3_open("MessengerDB", &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_exec(db, "BEGIN TRANSACTION", 0, 0, 0);

    char sql[200];
    sprintf(sql, "SELECT name FROM sqlite_master WHERE type='table' AND (name LIKE 'History_%s_%%' OR name LIKE 'History_%%_%s');", username, username);
    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    while (sqlite3_step(res) == SQLITE_ROW)
    {
        printf("\nDATABASE:%s\n", (const char *)sqlite3_column_text(res, 0));

        char sql2[200];
        sprintf(sql2, "SELECT sender,recipient,message,id,reply_id FROM %s WHERE read_%s=0;", (const char *)sqlite3_column_text(res, 0), username);
        rc = sqlite3_prepare_v2(db, sql2, -1, &res2, 0);

        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(res);
            sqlite3_close(db);
            return;
        }

        while (sqlite3_step(res2) == SQLITE_ROW)
        {

            memset(&message, 0, sizeof(Message));
            memset(&prev_message, 0, sizeof(Message));

            strcpy(message.sender, (const char *)sqlite3_column_text(res2, 0));
            strcpy(message.recipient, (const char *)sqlite3_column_text(res2, 1));
            strcpy(message.message, (const char *)sqlite3_column_text(res2, 2));
            message.id = sqlite3_column_int(res2, 3);
            message.reply_id = sqlite3_column_int(res2, 4);

            printf("\n%s %s %s %d %d\n", message.sender, message.recipient, message.message, message.id, message.reply_id);
            if (message.reply_id != 0)
            {

                if (write(get_sd_listen_by_username(username), &message, sizeof(message)) <= 0)
                {
                    perror("[Thread]Eroare la write() către client.\n");
                }
                prev_message = get_message_by_id(message.reply_id, message.sender, message.recipient);
                if (write(get_sd_listen_by_username(username), &prev_message, sizeof(prev_message)) <= 0)
                {
                    perror("[Thread]Eroare la write() către client.\n");
                }
            }
            else
            {
                if (write(get_sd_listen_by_username(username), &message, sizeof(message)) <= 0)
                {
                    perror("[Thread]Eroare la write() către client.\n");
                }
            }
        }

        char sql3[200];
        sprintf(sql3, "UPDATE %s SET read_%s=1;", (const char *)sqlite3_column_text(res, 0), username);
        printf("\n%s\n", sql3);
        rc = sqlite3_prepare_v2(db, sql3, -1, &res3, 0);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
            sqlite3_finalize(res);
            sqlite3_close(db);
            return;
        }

        rc = sqlite3_step(res3);
        if (rc != SQLITE_DONE)
        {
            fprintf(stderr, "Failed to execute UPDATE query: %s\n", sqlite3_errmsg(db));
        }

        sqlite3_finalize(res3);

        sqlite3_finalize(res2);
    }

    sqlite3_finalize(res);

    sqlite3_exec(db, "COMMIT", 0, 0, 0);

    sqlite3_close(db);
}

void process_history(int sd, char user1[30], char user2[30])
{

    sqlite3 *db;
    sqlite3_stmt *res;
    int rc = sqlite3_open("MessengerDB", &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char sql[200];

    if (strcmp(user1, user2) < 0)
    {
        sprintf(sql, "SELECT sender,recipient,message,id FROM History_%s_%s;", user1, user2);
    }
    else
    {
        sprintf(sql, "SELECT sender,recipient,message,id FROM History_%s_%s;", user2, user1);
    }

    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }
    Message history_message;
    while (sqlite3_step(res) == SQLITE_ROW)
    {
        memset(&history_message, 0, sizeof(Message));
        strcpy(history_message.sender, (const char *)sqlite3_column_text(res, 0));
        strcpy(history_message.message, (const char *)sqlite3_column_text(res, 2));
        history_message.id = sqlite3_column_int(res, 3);
        printf("\n%s %s %s\n", (const char *)sqlite3_column_text(res, 0), (const char *)sqlite3_column_text(res, 1), (const char *)sqlite3_column_text(res, 2));

        if (write(sd, &history_message, sizeof(Message)) <= 0)
        {
            perror("Eroare la write catre  client!\n");
            break;
        }
    }
    memset(&history_message, 0, sizeof(Message));
    strcpy(history_message.sender, "");
    if (write(sd, &history_message, sizeof(Message)) <= 0)
    {
        perror("Eroare la write catre  client!\n");
        return;
    }

    sqlite3_finalize(res);
    sqlite3_close(db);
}

int store_message(char sender[30], char recipient[30], char message[180], int reply_id)
{
    sqlite3 *db;
    sqlite3_stmt *res;

    int rc = sqlite3_open("MessengerDB", &db);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    char user1[30], user2[30];
    memset(user1, 0, sizeof(user1));
    memset(user2, 0, sizeof(user2));
    if (strcmp(sender, recipient) < 0)
    {
        strcpy(user1, sender);
        strcpy(user2, recipient);
    }
    else
    {
        strcpy(user1, recipient);
        strcpy(user2, sender);
    }

    char sql[220];
    sprintf(sql, "INSERT INTO History_%s_%s (sender,recipient,message,read_%s,read_%s,reply_id) VALUES (?, ?, ?, ?, ?, ?);", user1, user2, user1, user2);
    rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_text(res, 1, sender, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind sender: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_text(res, 2, recipient, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind recipient: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_text(res, 3, message, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind message: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_int(res, 4, is_online(user1));
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind onlineuser1: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_int(res, 5, is_online(user2));
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind onlineuser2: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_bind_int(res, 6, reply_id);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to bind reply_id: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(res);
        sqlite3_close(db);
        return 0;
    }

    printf("\n%s\n", sql);
    fflush(stdout);

    rc = sqlite3_step(res);
    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "Execution failed: %s\n", sqlite3_errmsg(db));
    }
    else
    {
        printf(" Message was stored in the DB!\n");
        fflush(stdout);
    }

    sqlite3_stmt *res1;
    char sql1[200];
    sprintf(sql1, "SELECT id FROM History_%s_%s ORDER BY id DESC LIMIT 1;", user1, user2);

    rc = sqlite3_prepare_v2(db, sql1, -1, &res1, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 0;
    }

    rc = sqlite3_step(res1);
    if (rc == SQLITE_DONE)
    {
        fprintf(stderr, "Failed to execute SELECT query: %s\n", sqlite3_errmsg(db));
    }

    int id = sqlite3_column_int(res1, 0);

    printf("\n%s\n", sql1);
    fflush(stdout);

    sqlite3_finalize(res1);

    sqlite3_finalize(res);

    sqlite3_close(db);

    return id;
}

void online_users(char *list)
{
    memset(list, 0, sizeof(list));
    strcat(list, "Online users:\n");
    for (int i = 1; i < 100; i++)
    {
        if (clients[i].sd != 0 && strcmp(clients[i].username, "") != 0)
        {
            strcat(list, clients[i].username);
            strcat(list, "\n");
        }
    }
}

int main()
{
    struct sockaddr_in server;
    struct sockaddr_in from;
    int sd;
    int pid;
    pthread_t th[MAX_CLIENTS];
    int i = 1;

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server]Eroare la socket().\n");
        return errno;
    }

    int on = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    bzero(&server, sizeof(server));
    bzero(&from, sizeof(from));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(PORT);

    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server]Eroare la bind().\n");
        return errno;
    }

    if (listen(sd, MAX_CLIENTS) == -1)
    {
        perror("[server]Eroare la listen().\n");
        return errno;
    }

    while (1)
    {
        int client, client_listen;
        thData *td;
        int length = sizeof(from);

        printf("\n[server]Asteptam la portul %d...\n", PORT);
        fflush(stdout);

        if ((client = accept(sd, (struct sockaddr *)&from, &length)) < 0)
        {
            perror("[server]Eroare la accept().\n");
            continue;
        }

        if ((client_listen = accept(sd, (struct sockaddr *)&from, &length)) < 0)
        {
            perror("[server]Eroare la accept().\n");
            continue;
        }

        td = (struct thData *)malloc(sizeof(struct thData));
        td->idThread = i++;
        td->sd = client;
        td->sd_listen = client_listen;

        clients[td->idThread] = *td;

        pthread_create(&th[i], NULL, &treat, td);

        for (int i = 1; i < 10; i++)
        {
            printf("\n clients[%d]: user:%s idt%d sd-%d sd_listen-%d", i, clients[i].username, clients[i].idThread, clients[i].sd, clients[i].sd_listen);
            fflush(stdout);
        }
    }

    return 0;
}

static void *treat(void *arg)
{
    struct thData tdL;
    tdL = *((struct thData *)arg);
    printf("\n[thread]- %d -sd %d - Asteptam mesajul...\n", tdL.idThread, tdL.sd);
    fflush(stdout);
    pthread_detach(pthread_self());
    char msg[200];
    char msgrasp[200];
    LoginData login_info;
    int logged_in;

    while (1)
    {
        memset(msg, 0, sizeof(msg));
        memset(msgrasp, 0, sizeof(msgrasp));
        memset(&login_info, 0, sizeof(LoginData));

        if (read(tdL.sd, msg, sizeof(msg)) <= 0)
        {
            perror("[thread] Eroare la read() de la client");
            break;
        }

        if (strcmp(msg, "login") == 0)
        {
            if (read(tdL.sd, &login_info, sizeof(LoginData)) <= 0)
            {
                perror("[thread] Eroare la read() de la client");
                break;
            }
            if (is_online(login_info.username) == 1)
            {
                strcat(msgrasp, "User is already online!");
            }
            else
            {
                logged_in = process_login(tdL.sd, msgrasp, login_info);
            }
            printf("logged_in:%d\n", logged_in);
            fflush(stdout);
        }
        else if (strcmp(msg, "quit") == 0)
        {
            close(tdL.sd);
            disconnect_user(tdL.sd);

            printf("[thread] - %d Userul de la fd-%d s-a deconectat\n", tdL.idThread, tdL.sd);
            fflush(stdout);
            return NULL;
        }

        if (write(tdL.sd, msgrasp, sizeof(msgrasp)) <= 0)
        {
            perror("[thread] Eroare la write() catre client");
            break;
        }
        if (logged_in == 1)
        {
            strcpy(tdL.username, login_info.username);
            break;
        }
    }

    send_new_messages(tdL.username);

    Message message;

    while (1)
    {
        memset(msg, 0, sizeof(msg));
        memset(msgrasp, 0, sizeof(msgrasp));

        if (read(tdL.sd, msg, sizeof(msg)) <= 0)
        {
            perror("[thread] Eroare la read() de la client");
            break;
        }

        if (strcmp(msg, "msg") == 0)
        {
            printf("msg!\n");
            fflush(stdout);
            memset(&message, 0, sizeof(Message));
            if (read(tdL.sd, &message, sizeof(Message)) <= 0)
            {
                perror("[thread] Eroare la read() de la client");
                break;
            }
            printf("to:%s from:%s text:%s\n", message.recipient, message.sender, message.message);

            if (is_online(message.recipient) == 1)
            { // if the recipient is connected we send the message
                int recipient_sd_listen = get_sd_listen_by_username(message.recipient);
                message.id = store_message(message.sender, message.recipient, message.message, message.reply_id);
                if (write(recipient_sd_listen, &message, sizeof(Message)) <= 0)
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }

                printf("%d\n", message.id);
                fflush(stdout);

                if (write(tdL.sd, &message.id, sizeof(message.id)) <= 0)
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }
                strcat(msgrasp, "Sent!");
            }
            else
            {
                strcat(msgrasp, "User is not connected.The message will be send when they log in.");
                message.id = store_message(message.sender, message.recipient, message.message, message.reply_id);

                if (write(tdL.sd, &message.id, sizeof(message.id)) <= 0)
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }
            }
        }
        else if (strcmp(msg, "reply") == 0)
        {
            printf("reply!\n");
            fflush(stdout);
            memset(&message, 0, sizeof(Message));
            if (read(tdL.sd, &message, sizeof(Message)) <= 0)
            {
                perror("[thread] Eroare la read() de la client");
                break;
            }
            printf("to:%s from:%s text:%s replying_to:%d\n", message.recipient, message.sender, message.message, message.reply_id);

            if (is_online(message.recipient))
            {
                int recipient_sd_listen = get_sd_listen_by_username(message.recipient);
                message.id = store_message(message.sender, message.recipient, message.message, message.reply_id);

                if (write(recipient_sd_listen, &message, sizeof(Message)) <= 0) // sending reply first
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }

                Message prev_message;
                memset(&prev_message, 0, sizeof(prev_message));

                prev_message = get_message_by_id(message.reply_id, message.sender, message.recipient);

                if (write(recipient_sd_listen, &prev_message, sizeof(Message)) <= 0) // sending original message second
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }

                strcat(msgrasp, "Replied!");
            }
            else
            {
                strcat(msgrasp, "User is not connected.The reply will be send when they log in.");

                message.id = store_message(message.sender, message.recipient, message.message, message.reply_id);

                if (write(tdL.sd, &message.id, sizeof(message.id)) <= 0)
                {
                    perror("[thread] Eroare la write msg() catre client");
                    break;
                }
            }
        }
        else if (strcmp(msg, "quit") == 0)
        {
            close(tdL.sd);
            disconnect_user(tdL.sd);

            printf("[thread] - %d Userul de la fd-%d s-a deconectat\n", tdL.idThread, tdL.sd);
            fflush(stdout);
            return NULL;
        }
        else if (strncmp(msg, "history", 7) == 0)
        {
            strcpy(msg, msg + 8);
            process_history(tdL.sd, tdL.username, msg);
            strcat(msgrasp, "End of history.");
        }
        else if (strcmp(msg, "online") == 0)
        {
            printf("INTRA AICI!\n");
            fflush(stdout);
            char list[200];
            online_users(list);
            printf("%s", list);
            fflush(stdout);
            strcat(msgrasp, list);
            printf("ONLINE:%s\n", msgrasp);
            fflush(stdout);
        }
        else if (strncmp(msg, "all ",4) == 0)
        {
            printf("all!\n");fflush(stdout);
            strcpy(msg, msg + 4);
            strcat(msgrasp,"All");

            memset(&message, 0, sizeof(message));
            strcpy(message.sender, tdL.username);
            strcpy(message.message, msg);
            int cnt=0;
            for (int i = 1; i < 100; i++)
            {
                if (clients[i].sd != 0 && strcmp(clients[i].username, "") != 0 && strcmp(clients[i].username, tdL.username) != 0)
                {
                    cnt++;
                }
            }
            printf("cnt:%d",cnt);fflush(stdout);

            if(write(tdL.sd,&cnt,sizeof(cnt))<=0){
                perror("Eroare la write cnt()");
                break;
            }

            for (int i = 1; i < 100; i++)
            {
                if (clients[i].sd != 0 && strcmp(clients[i].username, "") != 0 && strcmp(clients[i].username, tdL.username) != 0)
                {
                    strcpy(message.recipient, "");
                    strcpy(message.recipient, clients[i].username);
                    message.id = store_message(message.sender, message.recipient, message.message, message.reply_id);
                    printf("%d %s %s %s ",message.id,message.sender,message.recipient,message.message);fflush(stdout);

                    if (write(clients[i].sd_listen, &message, sizeof(Message)) <= 0)
                    {
                        perror("Eroare la write broadcast()");
                        break;
                    }
                    
                    if (write(tdL.sd, &message, sizeof(Message)) <= 0)
                    {
                        perror("Eroare la write broadcast()");
                        break;
                    }

                }
            }
        }

        if (write(tdL.sd, msgrasp, sizeof(msgrasp)) <= 0)
        {
            perror("[thread] Eroare la write() catre client");
            break;
        }
    }

    close(tdL.sd); // Închide socket-ul clientului
    disconnect_user(tdL.sd);
    return NULL;
}