#include <iostream>
#include <thread>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <filesystem>

using namespace std;

void receive_messages(int sock)
{
    char buffer[1024];
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes <= 0)
        {
            cout << "Disconnected from server\n";
            break;
        }
        cout << buffer << endl;
    }
}

int main()
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        return 1;
    }
    cout << "Connected to server\n";

    thread t(receive_messages, sock);
    t.detach();

    cout << "Please enter your username\n";
    string input;
    bool first_message = true;

    while (true)
    {
        getline(cin, input);
        if (first_message)
        {
            input = "NAME " + input;
            first_message = false;
        }
        else
        {
            string working_dir = filesystem::current_path().string();
            int first_space = input.find(' ');
            string command_name = input.substr(0, first_space);
            if (command_name == "GET" || command_name == "PUT")
                input += " " + working_dir;
        }
        send(sock, input.c_str(), input.size(), 0);
    }
}