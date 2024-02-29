#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <pthread.h>
extern int errno;

int port;

typedef struct thData
{
    int idThread;      // id-ul thread-ului tinut in evidenta de acest program
    char username[30]; // usernameul
    int sd_listen;     // sd pe care thread-ul asculta

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

void clearLine()
{
    printf("\033[K"); // Clear the current line
}

void printServerMessage(Message message)
{
    clearLine();
    if (message.reply_id != 0)
    {
        printf("\r    |_->[#%d]%s:%s\n", message.id, message.sender, message.message); // Move to the beginning of the line and print the message
    }
    else
    {
        printf("\r[#%d]%s:%s\n", message.id, message.sender, message.message); // Move to the beginning of the line and print the message
    }
    printf("[client]: "); // Restore the client prompt
    fflush(stdout);       // Flush the output to ensure it's immediately visible
}

void *listen_for_messages(void *arg)
{
    struct thData tdL;
    tdL = *((struct thData *)arg);

    Message msg;
    Message prev_msg;
    while (1)
    {
        memset(&msg, 0, sizeof(msg));
        memset(&prev_msg, 0, sizeof(prev_msg));

        if (read(tdL.sd_listen, &msg, sizeof(Message)) <= 0)
        {
            perror("[Thread]Eroare la read() de la server.\n");
            break;
        }
        if (msg.reply_id != 0)
        {
            if (read(tdL.sd_listen, &prev_msg, sizeof(Message)) <= 0)
            {
                perror("[Thread]Eroare la read() de la server.\n");
                break;
            }
            printServerMessage(prev_msg);
            fflush(stdout);
            // printf("\n %s %s %s \n",prev_msg.sender,prev_msg.recipient,prev_msg.message);fflush(stdout);
        }
        printServerMessage(msg);
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int sd, sd_listen;
    struct sockaddr_in server;

    if (argc != 3)
    {
        printf("Sintaxa: %s <adresa_server> <port>\n", argv[0]);
        return -1;
    }

    port = atoi(argv[2]);

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }

    if ((sd_listen = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Eroare la socket().\n");
        return errno;
    }
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(argv[1]);
    server.sin_port = htons(port);

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }
    if (connect(sd_listen, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client]Eroare la connect().\n");
        return errno;
    }

    char msg[200];
    char user[30];
    char msgrasp[200];
    LoginData login_info;
    Message message;
    int logged_in = 0;

    while (1) // login
    {
        printf("[client]:");
        fflush(stdout);

        memset(msg, 0, sizeof(msg));
        memset(msgrasp, 0, sizeof(msgrasp));
        memset(user, 0, sizeof(user));
        memset(&login_info, 0, sizeof(LoginData));

        read(0, msg, sizeof(msg));
        msg[strcspn(msg, "\n")] = 0;

        if (strcmp(msg, "login") == 0)
        {
            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write() catre server");
                break;
            }

            printf("\nUser:");
            fflush(stdout);

            read(0, user, sizeof(user));
            user[strcspn(user, "\n")] = 0;

            char *passwd;
            passwd = getpass("\nPassword:");
            fflush(stdout);

            strcpy(login_info.username, user);
            strcpy(login_info.password, passwd);


            if (write(sd, &login_info, sizeof(login_info)) <= 0)
            {
                perror("Eroare la write login catre server\n");
                break;
            }
        }
        else if (strcmp(msg, "quit") == 0)
        {
            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write() catre server");
                break;
            }
            close(sd);
            close(sd_listen);
            return 0;
        }
        else
        {
            printf("[server]You must login first!\n");
            continue;
        }

        if (read(sd, msgrasp, sizeof(msgrasp)) <= 0)
        {
            perror("Eroare la read() de la server");
            break;
        }

        printf("[server]:%s\n", msgrasp);
        fflush(stdout);

        if (strcmp(msgrasp, "Logged in successfully!") == 0)
        {
            logged_in = 1;
            break;
        }
    }

    pthread_t listen_thread;
    thData *td;
    td = (struct thData *)malloc(sizeof(struct thData));
    td->idThread = 1;
    td->sd_listen = sd_listen;

    pthread_create(&listen_thread, NULL, listen_for_messages, td); // thread to listen from incoming messages

    char dest_user[30], reply_user[30], dest_msg[200];

    while (1)
    {
        printf("[client]:");
        fflush(stdout);

        memset(msg, 0, sizeof(msg));
        memset(msgrasp, 0, sizeof(msgrasp));

        read(0, msg, sizeof(msg));
        msg[strcspn(msg, "\n")] = 0;

        if (strcmp(msg, "login") == 0)
        {
            printf("[server]You are already logged in!\n");
            fflush(stdout);
            continue;
        }
        else if (strncmp(msg, "msg", 3) == 0)
        {
            printf("\r");
            fflush(stdout);
            memset(&message, 0, sizeof(Message));
            memset(dest_user, 0, sizeof(dest_user));
            memset(dest_msg, 0, sizeof(dest_msg));

            sscanf(msg, "msg %s %[^\n]", dest_user, dest_msg);
            // printf("%s\n", msg);
            // printf("%s %s\n", dest_user, dest_msg);
            fflush(stdout);
            if (strcmp(dest_user, "") == 0)
            {
                printf("[server]You need to specify a user!\n");
                fflush(stdout);
                continue;
            }
            else if (strcmp(dest_msg, "") == 0)
            {
                printf("[server]You need to specify a message!\n");
                fflush(stdout);
                continue;
            }
            else if (strcmp(dest_user, login_info.username) == 0)
            {
                printf("[server]You can't message yourself!\n");
                fflush(stdout);
                continue;
            }

            if (write(sd, "msg", 4) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }

            strcpy(message.sender, user);
            strcpy(message.recipient, dest_user);
            strcpy(message.message, dest_msg);

            if (write(sd, &message, sizeof(Message)) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }

            if (read(sd, &message.id, sizeof(message.id)) <= 0)
            {
                perror("Eroare la read_id()\n");
                return errno;
            }

            printf("\r[#%d]%s->%s:%s\n", message.id, message.sender, message.recipient, message.message);
            fflush(stdout);
        }
        else if (strncmp(msg, "reply", 5) == 0)
        {

            memset(&message, 0, sizeof(Message));
            memset(dest_user, 0, sizeof(dest_user));
            memset(reply_user, 0, sizeof(reply_user));
            memset(dest_msg, 0, sizeof(dest_msg));
            int reply_id = 0;

            sscanf(msg, "reply %s %s %d %[^\n]", reply_user, dest_user, &reply_id, dest_msg);
            // printf("%s\n", msg);
            // printf("%s %d %s\n", dest_user, reply_id, dest_msg);
            fflush(stdout);

            if ((strcmp(reply_user, "") == 0 || strcmp(dest_user, "") == 0 || strcmp(dest_msg, "") == 0))
            {
                printf("[server]Invalid syntax! reply <reply_user> <conv_user> <message_id> <message>.\n");
            }

            if (reply_id <= 0)
            {
                printf("[server]You need to specify a valid message id!\n");
                fflush(stdout);
                continue;
            }

            if (write(sd, "reply", 6) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }

            strcpy(message.sender, user);
            strcpy(message.recipient, dest_user);
            strcpy(message.message, dest_msg);
            message.reply_id = reply_id;

            if (write(sd, &message, sizeof(Message)) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }

            if (read(sd, &message.id, sizeof(message.id)) <= 0)
            {
                perror("Eroare la read_id()\n");
                return errno;
            }

            printf("\r[#%d]%s->%s:%s\n", message.id, message.sender, message.recipient, message.message);
        }
        else if (strcmp(msg, "quit") == 0)
        {
            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }
            close(sd);
            close(sd_listen);
            printf("[server]Server closed connection.\n");
            fflush(stdout);
            return 0;
        }
        else if (strncmp(msg, "history", 7) == 0)
        {
            char history_user[30];

            Message history_message;
            memset(&history_message, 0, sizeof(history_message));

            sscanf(msg, "history %s", history_user);
            if (strcmp(history_user, user) == 0)
            {
                printf("[server]You can't have a conversation history with yourself.\n");
                continue;
            }

            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write() catre server");
                return errno;
            }

            memset(msg, 0, sizeof(msg));

            while (1)
            {

                if (read(sd, &history_message, sizeof(Message)) <= 0)
                {
                    perror("Eroare la write() catre server\n");
                    break;
                }

                if (strcmp(history_message.sender, "") == 0)
                {
                    break;
                }

                printf("[%d]%s:%s\n", history_message.id, history_message.sender, history_message.message);
            }
        }
        else if (strcmp(msg, "online") == 0)
        {
            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }
        }
        else if (strncmp(msg, "all ", 4) == 0)
        {

            if (write(sd, msg, sizeof(msg)) <= 0)
            {
                perror("Eroare la write()\n");
                return errno;
            }
            int cnt;

            if (read(sd, &cnt, sizeof(cnt)) <= 0)
            {
                perror("Eroare la read cnt()");
                return errno;
            }

            for (int i = 0; i < cnt; i++)
            {

                if (read(sd, &message, sizeof(message)) <= 0)
                {
                    perror("Eroare la read_id()\n");
                    return errno;
                }

                printf("\r[#%d]%s->%s:%s\n", message.id, message.sender, message.recipient, message.message);
                fflush(stdout);
            }
        }
        else
        {
            printf("[server]Unknown command!Commands are: msg <user> <message>, reply <user> <msg_id> <message>, history <user>, online, all <message>.\n");
            fflush(stdout);
            continue;
        }

        if (read(sd, msgrasp, sizeof(msgrasp)) <= 0)
        {
            perror("Eroare la read() de la server");
            break;
        }

        if (strcmp(msgrasp, "Sent!") == 0 || strcmp(msgrasp, "Replied!") == 0 || strcmp(msgrasp, "All") == 0)
        {
        }
        else
        {
            printf("[server]:%s\n", msgrasp);
            fflush(stdout);
        }
    }

    close(sd);
    close(sd_listen);
    pthread_join(listen_thread, NULL);
    return 0;
}