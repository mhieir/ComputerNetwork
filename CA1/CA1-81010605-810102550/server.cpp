#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <filesystem>

using namespace std;

const char SPACE = ' ';
const string NAME_COMMAND = "NAME";
const string MSG_COMMAND = "MSG";
const string PM_COMMAND = "PM";
const string USERS_COMMAND = "USERS";
const string LIST_COMMAND = "LIST";
const string GET_COMMAND = "GET";
const string PUT_COMMAND = "PUT";
const string QUIT_COMMAND = "QUIT";
const string SERVER_FILES_DIR_NAME = "server_files";
const string SERVER_FILES_DIR = SERVER_FILES_DIR_NAME + "/";

map<int, string> clients;

void broadcast(string msg, int sender)
{
    for (auto &c : clients)
    {
        if (c.first != sender)
        {
            send(c.first, msg.c_str(), msg.size(), 0);
        }
    }
}

void message_to_client(int client_socket, string message)
{
    send(client_socket, message.c_str(), message.size(), 0);
}

void handle_name(int client_socket, string username)
{
    clients[client_socket] = username;
    message_to_client(client_socket, "Hello " + username);
}

void handle_msg(int client_socket, string message)
{
    string pub_message = "PUBLIC-MESSAGE from " + clients[client_socket] + " : " + message;
    broadcast(pub_message, client_socket);
}

void handle_pm(int client_socket, string username, string message)
{
    string pv_message = "PRIVATE-MESSAGE from " + clients[client_socket] + " : " + message;
    for (auto &c : clients)
    {
        if (c.second == username)
        {
            message_to_client(c.first, pv_message);
            return;
        }
    }
    message_to_client(client_socket, "User not found");
}

void handle_users(int client_socket)
{
    string online_users = "";
    for (auto &c : clients)
    {
        if (c.first != client_socket && !c.second.empty())
        {
            online_users += c.second + " ";
        }
    }
    message_to_client(client_socket, online_users);
}

void handle_list(int client_socket)
{
    string all_files = "";
    for (const auto &entry : filesystem::directory_iterator(SERVER_FILES_DIR))
    {
        string file_name = entry.path().filename().string();
        all_files += file_name + "\n";
    }
    all_files = all_files.substr(0, all_files.size() - 1);
    message_to_client(client_socket, all_files);
}

void handle_get(int client_socket, string file_name, string destination_path)
{
    filesystem::path source = SERVER_FILES_DIR + file_name;
    filesystem::path destination = destination_path + file_name;
    try
    {
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing);
        message_to_client(client_socket, "File downloaded successfully");
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        message_to_client(client_socket, "Failed to download the file");
    }
}

void handle_put(int client_socket, string file_path)
{
    filesystem::path source = file_path;
    string file_name = source.filename();
    filesystem::path destination = SERVER_FILES_DIR + file_name;
    try
    {
        std::filesystem::copy_file(
            source,
            destination,
            std::filesystem::copy_options::overwrite_existing);
        message_to_client(client_socket, "File uploaded successfully");
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        message_to_client(client_socket, "Failed to upload the file");
    }
}

void handle_quit(int client_socket)
{
    message_to_client(client_socket, "Goodbye " + clients[client_socket]);
    broadcast(clients[client_socket] + " left the group", client_socket);
    clients.erase(client_socket);
}

void handle_command(int client_socket, string command)
{
    int first_space = command.find(' ');
    string command_name = command.substr(0, first_space);
    string rest = (first_space != command.length()) ? command.substr(first_space + 1) : "";
    if (command_name == NAME_COMMAND)
    {
        handle_name(client_socket, rest);
    }
    else if (command_name == MSG_COMMAND)
    {
        handle_msg(client_socket, rest);
    }
    else if (command_name == USERS_COMMAND)
    {
        handle_users(client_socket);
    }
    else if (command_name == LIST_COMMAND)
    {
        handle_list(client_socket);
    }
    else if (command_name == GET_COMMAND)
    {
        int second_space = rest.find(' ');
        int third_space = rest.find(' ', second_space + 1);
        string file_name = rest.substr(0, second_space);
        string destination_path = rest.substr(second_space + 1, third_space - second_space - 1);
        string working_dir = rest.substr(third_space + 1);
        if (destination_path[0] == '.')
            destination_path = working_dir + destination_path.substr(1);
        handle_get(client_socket, file_name, destination_path);
    }
    else if (command_name == QUIT_COMMAND)
    {
        handle_quit(client_socket);
    }
    else if (command_name == PM_COMMAND)
    {
        int second_space = rest.find(' ');
        string username = rest.substr(0, second_space);
        string message = rest.substr(second_space + 1);
        handle_pm(client_socket, username, message);
    }
    else if (command_name == PUT_COMMAND)
    {
        int second_space = rest.find(' ');
        string file_path = rest.substr(0, second_space);
        string working_dir = rest.substr(second_space + 1);
        if (file_path[0] == '.')
            file_path = working_dir + file_path.substr(1);
        handle_put(client_socket, file_path);
    }
    else
    {
        message_to_client(client_socket, "Wrong command");
    }
}

void handle_client(int client_socket)
{

    char buffer[1024];

    while (true)
    {

        memset(buffer, 0, sizeof(buffer));

        int bytes = recv(client_socket, buffer, sizeof(buffer), 0);

        if (bytes <= 0)
        {
            close(client_socket);
            clients.erase(client_socket);
            break;
        }

        string command(buffer);
        handle_command(client_socket, command);
    }
}

int main()
{

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return 1;
    }
    if (listen(server_fd, 10) < 0)
    {
        perror("Listen failed");
        return 1;
    }
    cout << "Server started...\n";

    filesystem::create_directory(SERVER_FILES_DIR);

    while (true)
    {

        sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);

        int client_socket = accept(server_fd,
                                   (sockaddr *)&client_addr,
                                   &client_size);

        cout << "New client connected\n";

        thread t(handle_client, client_socket);
        t.detach();
    }
}