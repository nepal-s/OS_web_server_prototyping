/*
 ***************************************************************************
 * Clarkson University                                                     *
 * CS 444/544: Operating Systems, Spring 2024                              *
 * Project: Prototyping a Web Server/Browser                               *
 * Created by Daqing Hou, dhou@clarkson.edu                                *
 *            Xinchao Song, xisong@clarkson.edu                            *
 * April 10, 2022                                                          *
 * Copyright © 2022-2024 CS 444/544 Instructor Team. All rights reserved.  *
 * Unauthorized use is strictly prohibited.                                *
 ***************************************************************************
 */

#include "net_util.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <assert.h>

#define NUM_VARIABLES 26
#define NUM_SESSIONS 128
#define NUM_BROWSER 128
#define DATA_DIR "./sessions"
#define SESSION_PATH_LEN 128

typedef struct browser_struct {
    bool in_use;
    int socket_fd;
    int session_id;
} browser_t;

typedef struct session_struct {
    bool in_use;
    bool variables[NUM_VARIABLES];
    double values[NUM_VARIABLES];
} session_t;

static browser_t browser_list[NUM_BROWSER];                             // Stores the information of all browsers.
static session_t session_hash[NUM_SESSIONS];				//making session_hash. Converting session_list to hashmap/dictionary.
static session_t session_list[NUM_SESSIONS];                            // Stores the information of all sessions.
static pthread_mutex_t browser_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the browser list.
static pthread_mutex_t session_list_mutex = PTHREAD_MUTEX_INITIALIZER;  // A mutex lock for the session list.

// Returns the string format of the given session.
// There will be always 9 digits in the output string.
void session_to_str(int session_id, char result[]);

// Determines if the given string represents a number.
bool is_str_numeric(const char str[]);

// Process the given message and update the given session if it is valid.
bool process_message(int session_id, const char message[]);

// Broadcasts the given message to all browsers with the same session ID.
void broadcast(int session_id, const char message[]);

// Gets the path for the given session.
void get_session_file_path(int session_id, char path[]);

// Loads every session from the disk one by one if it exists.
void load_all_sessions();

// Saves the given sessions to the disk.
void save_session(int session_id, char respond[]);                             

// Assigns a browser ID to the new browser.
// Determines the correct session ID for the new browser
// through the interaction with it.
int register_browser(int browser_socket_fd);

// Handles the given browser by listening to it,
// processing the message received,
// broadcasting the update to all browsers with the same session ID,
// and backing up the session on the disk.
void browser_handler(int browser_socket_fd);

// Starts the server.
// Sets up the connection,
// keeps accepting new browsers,
// and creates handlers for them.

void check_data_dir();					        
void start_server(int port);
int hash(int);						            
int generate_key();					            
session_t* get_session(int key);		        
int hash(int key)					            
{
	return key % NUM_SESSIONS;
}
int generate_key()					            
{
	srand(time(NULL));
	return rand();
}
session_t* get_session(int session_id)			
{
	int hs = hash(session_id);
	assert(hs < NUM_SESSIONS);
	return &session_hash[hs];
}

/**
 * Returns the string format of the given session.
 * There will be always 9 digits in the output string.
 *
 * @param session_id the session ID
 * @param result an array to store the string format of the given session;
 *               any data already in the array will be erased
 */
void session_to_str(int session_id, char result[]) {
    memset(result, 0, BUFFER_LEN);
    session_t session = session_list[session_id];

    for (int i = 0; i < NUM_VARIABLES; ++i) {
        if (session.variables[i]) {
            char line[32];

            if (session.values[i] < 1000) {
                sprintf(line, "%c = %.6f\n", 'a' + i, session.values[i]);
            } else {
                sprintf(line, "%c = %.8e\n", 'a' + i, session.values[i]);
            }

            strcat(result, line);
        }
    }
}

/**
 * Determines if the given string represents a number.
 *
 * @param str the string to determine if it represents a number
 * @return a boolean that determines if the given string represents a number
 */
bool is_str_numeric(const char str[]) {
    if (str == NULL) {
        return false;
    }

    if (!(isdigit(str[0]) || (str[0] == '-') || (str[0] == '.'))) {
        return false;
    }

    int i = 1;
    while (str[i] != '\0') {
        if (!(isdigit(str[i]) || str[i] == '.')) {
            return false;
        }
        i++;
    }

    return true;
}

/**
 * Process the given message and update the given session if it is valid.
 * If the message is valid, the function will return true; otherwise, it will return false.
 *
 * @param session_id the session ID
 * @param message the message to be processed
 * @return a boolean that determines if the given message is valid
 */
bool process_message(int session_id, const char message[]) {
    char *token;
    int result_idx;
    double first_value;
    char symbol;
    double second_value;

    // Makes a copy of the string since strtok() will modify the string that it is processing.
    char data[BUFFER_LEN];
    strcpy(data, message);

    // Processes the result variable.
    token = strtok(data, " ");
    result_idx = token[0] - 'a';
    char res = result_idx +97;                                       

    if(!(islower(res) && isalpha(res))){                                
        return false;
    }
    else if(token[1] != '\0'){                                     
        return false;
    }


    // Processes "=".
    token = strtok(NULL, " ");

    if(token == NULL){                                                 
        return false;
    }

    int equal = token[0] - '=';
    if (token[0] != 0 && token[1] != '\0'){
        return false;

    }



    // Processes the first variable/value.
    token = strtok(NULL, " ");

    if(token == NULL){
        return false;
    }
    if (is_str_numeric(token)) {
        first_value = strtod(token, NULL);
    } else {
        int first_idx = token[0] - 'a';
        char res = first_idx +97;                                         
        if(!(islower(res) && isalpha(res) || token[1] != '\0')){            
            return false;
        }
        first_value = session_list[session_id].values[first_idx];
    }

    // Processes the operation symbol.
    token = strtok(NULL, " ");

    if (token == NULL) {
        session_list[session_id].variables[result_idx] = true;
        session_list[session_id].values[result_idx] = first_value;
        return true;
    }
    symbol = token[0];
    if (symbol == '+'){                                                 
        if(token[1] != '\0'){
            return false;
        }
    }
    else if (symbol == '-'){                                           
        if(token[1] != '\0'){
            return false;
        }
    }

    else if (symbol == '*'){                                            
        if(token[1] != '\0'){
            return false;
        }
    }
    else if (symbol == '/'){                                            
        if(token[1] != '\0'){
            return false;
        }
    }
    else{                                                               
        return false;
    }

    // Processes the second variable/value.
    token = strtok(NULL, " ");

    if (token  == NULL){
        return false;
    }

    if (is_str_numeric(token)) {
        second_value = strtod(token, NULL);
    } else {
        int second_idx = token[0] - 'a';
        char res = second_idx +97;                                        
        if(!(islower(res) && isalpha(res)) || token[1] != '\0'){            
            return false;
        }
        second_value = session_list[session_id].values[second_idx];
    }

    // No data should be left over thereafter.
    token = strtok(NULL, " ");

    if (token == NULL){                                                 

    }
    else if(token[0] != '\0'){                                          
        return false;
    }

    session_list[session_id].variables[result_idx] = true;

    if (symbol == '+') {
        session_list[session_id].values[result_idx] = first_value + second_value;
    } else if (symbol == '-') {
        session_list[session_id].values[result_idx] = first_value - second_value;
    } else if (symbol == '*') {
        session_list[session_id].values[result_idx] = first_value * second_value;
    } else if (symbol == '/') {
        session_list[session_id].values[result_idx] = first_value / second_value;
    }

    return true;
}

/**
 * Broadcasts the given message to all browsers with the same session ID.
 *
 * @param session_id the session ID
 * @param message the message to be broadcasted
 */
void broadcast(int session_id, const char message[]) {
    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (browser_list[i].in_use && browser_list[i].session_id == session_id) {
            send_message(browser_list[i].socket_fd, message);
        }
    }
}

/**
 * Gets the path for the given session.
 *
 * @param session_id the session ID
 * @param path the path to the session file associated with the given session ID
 */
void get_session_file_path(int session_id, char path[]) {
    sprintf(path, "%s/session%d.dat", DATA_DIR, session_id);
}

/**
 * Loads every session from the disk one by one if it exists.
 * Use get_session_file_path() to get the file path for each session.
 */
void load_all_sessions() {                                                  
    // TODO

    //For looping the sessions
    for (int i = 0; i < NUM_SESSIONS; i++){
        char path [BUFFER_LEN];
        get_session_file_path(i, path);

        //Declaring file pointer
        FILE *fi;

        //Opening file for session, if it exists
        if (fi = fopen(path, "r")){
            //reading data from file
            char data[BUFFER_LEN];
            char a[1] = {'0'};
            while(a[0] != EOF){
                a[0] = fgetc(fi);
                if(a[0] != EOF){
                    strncat(data,a,1);
                }
            }

            //Parsing data from file
            char *par;
            par = strtok(data, "\n=");
            while(par != NULL){
                char *var = par;
                par = strtok(NULL, "\n=");
                double val = atof(par);
                session_list[i].variables[*var-'a'] = true;
                session_list[i].values[*var-'a'] =val;
                if(par != NULL){
                    par = strtok(NULL, "\n=");
                }
            }
            
            //closing the file
            fclose(fi);

        }
    }
}

/**
 * Saves the given sessions to the disk.
 * Use get_session_file_path() to get the file path for each session.
 *
 * @param session_id the session ID
 */
void save_session(int session_id, char respond[]) {                                         
    // TODO
    char path[BUFFER_LEN];                                                  

    //setting the session file path
    get_session_file_path(session_id, path);
    //declaring accessed file pointer
    FILE *fi;

    //opening file in read/write mode
    fi = fopen(path, "w+");
    fputs (respond, fi);

    //closing file
    fclose(fi);
}



/**
 * Assigns a browser ID to the new browser.
 * Determines the correct session ID for the new browser through the interaction with it.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 * @return the ID for the browser
 */
int register_browser(int browser_socket_fd) {
    int browser_id;

    pthread_mutex_lock(&browser_list_mutex);                                      

    for (int i = 0; i < NUM_BROWSER; ++i) {
        if (!browser_list[i].in_use) {
            browser_id = i;
            browser_list[browser_id].in_use = true;
            browser_list[browser_id].socket_fd = browser_socket_fd;
            break;
        }
    }
    pthread_mutex_unlock(&browser_list_mutex);                                      

    char message[BUFFER_LEN];
    receive_message(browser_socket_fd, message);

    int session_id = strtol(message, NULL, 10);
    if (session_id == -1) {
        pthread_mutex_lock(&session_list_mutex);                                    
        for (int i = 0; i < NUM_SESSIONS; ++i) {
            if (!session_list[i].in_use) {
                session_id = i;
                session_list[session_id].in_use = true;
                break;
            }
        }
        pthread_mutex_unlock(&session_list_mutex);                                  
    }
    browser_list[browser_id].session_id = session_id;

    sprintf(message, "%d", session_id);
    send_message(browser_socket_fd, message);

    return browser_id;
}

/**
 * Handles the given browser by listening to it, processing the message received,
 * broadcasting the update to all browsers with the same session ID, and backing up
 * the session on the disk.
 *
 * @param browser_socket_fd the socket file descriptor of the browser connected
 */
void browser_handler(int browser_socket_fd) {
    int browser_id;

    browser_id = register_browser(browser_socket_fd);

    int socket_fd = browser_list[browser_id].socket_fd;
    int session_id = browser_list[browser_id].session_id;

    printf("Successfully accepted Browser #%d for Session #%d.\n", browser_id, session_id);

    while (true) {
        char message[BUFFER_LEN];
        char respond[BUFFER_LEN];

        receive_message(socket_fd, message);
        printf("Received message from Browser #%d for Session #%d: %s\n", browser_id, session_id, message);

        if ((strcmp(message, "EXIT") == 0) || (strcmp(message, "exit") == 0)) {
            close(socket_fd);
            pthread_mutex_lock(&browser_list_mutex);
            browser_list[browser_id].in_use = false;
            pthread_mutex_unlock(&browser_list_mutex);
            printf("Browser #%d exited.\n", browser_id);
            return;
        }

        if (message[0] == '\0') {
            continue;
        }

        bool data_valid = process_message(session_id, message);
        if (!data_valid) {
            // Send the error message to the browser.
            broadcast(session_id, "ERROR");                                             
            continue;
        }

        session_to_str(session_id, respond);
        broadcast(session_id, respond);

        save_session(session_id, respond);                                             
    }
}

/**
 * Starts the server. Sets up the connection, keeps accepting new browsers,
 * and creates handlers for them.
 *
 * @param port the port that the server is running on
 */
void start_server(int port) {
    // Loads every session if there exists one on the disk.
    load_all_sessions();

    // Creates the socket.
    int server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_fd == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Binds the socket.
    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port);
    if (bind(server_socket_fd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        perror("Socket bind failed");
        exit(EXIT_FAILURE);
    }

    // Listens to the socket.
    if (listen(server_socket_fd, SOMAXCONN) < 0) {
        perror("Socket listen failed");
        exit(EXIT_FAILURE);
    }
    printf("The server is now listening on port %d.\n", port);
    pthread_t browser_threads[NUM_BROWSER];                                                     
    int i = 0;                                                                                  

    // Main loop to accept new browsers and creates handlers for them.
    while (true) {
        struct sockaddr_in browser_address;
        socklen_t browser_address_len = sizeof(browser_address);
        int browser_socket_fd = accept(server_socket_fd, (struct sockaddr *) &browser_address, &browser_address_len);
        if ((browser_socket_fd) < 0) {
            perror("Socket accept failed");
            continue;
        }

        // Starts the handler for the new browser.
        //pthread_create(&browser_threads[i++], NULL, (void*)browser_handler, browser_socket_fd);                     
        //pthread_create(&browser_threads[i++], NULL, (void *(*)(void *))browser_handler, (void *)&browser_socket_fd);
        //pthread_create(&browser_threads[i++], NULL, reinterpret_cast<void* (*)(void*)>(browser_handler), browser_socket_fd);
        pthread_create(&browser_threads[i++], NULL, reinterpret_cast<void* (*)(void*)>(browser_handler), reinterpret_cast<void*>(browser_socket_fd));


        if (i>= NUM_BROWSER){                                                                                       
            i=0;

            while (i < NUM_BROWSER){
                pthread_join(browser_threads[i++], NULL);
            }
            i=0;
        }
        //browser_handler(browser_socket_fd);
    }

    // Closes the socket.
    close(server_socket_fd);
}

/**
 * The main function for the server.
 *
 * @param argc the number of command-line arguments passed by the user
 * @param argv the array that contains all the arguments
 * @return exit code
 */
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 1) {
    } else if ((argc == 3)
               && ((strcmp(argv[1], "--port") == 0) || (strcmp(argv[1], "-p") == 0))) {
        port = strtol(argv[2], NULL, 10);

    } else {
        puts("Invalid arguments.");
        exit(EXIT_FAILURE);
    }

    if (port < 1024) {
        puts("Invalid port.");
        exit(EXIT_FAILURE);
    }

    start_server(port);

    exit(EXIT_SUCCESS);
}
