#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define ROWS 280
#define COLS 360
#define CHANNELS 3 // RGB.
#define PORT1 50001
#define PORT2 50002
#define IP "127.0.0.1"
#define ACCEPTABLE_DELAY_IN_MS 100

struct frame {
    const unsigned char raw_data[ROWS * COLS * CHANNELS];
    long long timestamp;
    long long sequence_num;
};

long long timenow_ms()
{
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return duration.count();
}

int create_socket(const char *ip_address, int port_number, bool is_sending_connection_request)
{
    if (is_sending_connection_request)
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1)
        {
            std::cerr << "error: socket creation failed!" << std::endl;
            return 1;
        }

        // specify the peer address.
        struct sockaddr_in peer_address;
        memset(&peer_address, 0, sizeof(ip_address));
        peer_address.sin_family = AF_INET; // ipv4.
        peer_address.sin_port = htons(port_number);

        // convert IP address from text to binary.
        if (inet_pton(AF_INET, ip_address, &peer_address.sin_addr) <= 0)
        {
            std::cerr << "invalid IP address!" << std::endl;
            close(sockfd);
            return 1;
        }

        // connect to the server.
        if (connect(sockfd, (struct sockaddr *)&peer_address, sizeof(peer_address)) == -1)
        {
            std::cerr << "connection failed!" << std::endl;
            close(sockfd);
            return 1;
        }
        std::cout << "connected!" << std::endl;
        return sockfd;
    }
    else
    {
        int listener_sock, connection_sock;
        struct sockaddr_in my_address, peer_address;
        socklen_t peer_address_len = sizeof(peer_address);

        listener_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_sock < 0)
        {
            std::cerr << "failed creating socket to listen!" << std::endl;
            return 1;
        }

        memset(&my_address, 0, sizeof(my_address));
        my_address.sin_family = AF_INET;
        my_address.sin_addr.s_addr = INADDR_ANY;
        my_address.sin_port = htons(port_number);

        if (bind(listener_sock, (struct sockaddr *)&my_address, sizeof(my_address)) < 0)
        {
            std::cerr << "failed binding socket!" << std::endl;
            close(listener_sock);
            return 1;
        }

        if (listen(listener_sock, 1) < 0)
        {
            std::cerr << "failed listening for connections!" << std::endl;
            close(listener_sock);
            return 1;
        }

        connection_sock = accept(listener_sock, (struct sockaddr *)&peer_address, &peer_address_len);
        if (connection_sock < 0)
        {
            std::cerr << "failed accepting connection!\n";
            close(listener_sock);
        }

        close(listener_sock);

        return connection_sock;
    }
}

int create_send_socket(const char *ip_address, bool is_sending_connection_request)
{
    return create_socket(ip_address, PORT1, is_sending_connection_request);
}

int create_receive_socket(const char *ip_address, bool is_sending_connection_request)
{
    return create_socket(ip_address, PORT2, is_sending_connection_request);
}

bool should_drop_frame(long long &frame_timestamp)
{
    return (timenow_ms() - frame_timestamp) < ACCEPTABLE_DELAY_IN_MS;
}

void receive_data(int socket)
{
    std::cout << "started receiveing: " << std::endl;
    // create a window
    cv::namedWindow("bimba", cv::WINDOW_NORMAL);
    size_t raw_data_size = 13;
    size_t raw_data_received = 0;
    size_t receive_chunk = 5000;
    unsigned char raw_data[raw_data_size];
    long long timestamp;
    std::cout << "initialized!..." << std::endl;

    while (1)
    {
        // receive raw data.
        while (raw_data_received < raw_data_size)
        {
            size_t chunk_size = (raw_data_received + receive_chunk) < raw_data_size ? receive_chunk : (raw_data_size - raw_data_received);
            ssize_t bytes_received_1 = recv(socket, raw_data + raw_data_received, chunk_size, 0);
            raw_data_received += bytes_received_1;
        }
        // receive timestamp.
        ssize_t bytes_received_2 = recv(socket, &timestamp, sizeof(timestamp), 0);

        // check if received frame is delayed more than the threshold.
        if (should_drop_frame(timestamp))
        {
            std::cout << "dropped!" << std::endl;
            continue;
        }

        std::cout << "not dropped!" << std::endl;
        cv::Mat received_frame(ROWS, COLS, CV_8UC3, (void *)raw_data);

        // display the frame.
        cv::imshow("bimba", received_frame);

        // wait for 10 milliseconds and check if 'q' key was pressed
        if (cv::waitKey(10) == 'q')
        {
            std::cout << "exiting..." << std::endl;
            break;
        }
    }
    cv::destroyAllWindows();
}

void send_frame(int socket, const unsigned char *raw_data, const long long &timestamp)
{
    // send raw data,
    if (send(socket, raw_data, ROWS * COLS * CHANNELS, 0) == -1)
    {
        std::cerr << "error: failed to send frame!" << std::endl;
        return;
    }
    // send timestamp.
    if (send(socket, &timestamp, sizeof(timestamp), 0) == -1)
    {
        std::cerr << "error: failed to send timestamp!" << std::endl;
        return;
    }
}

void send_data(int socket)
{
    // pen the default camera (ID 0)
    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        std::cerr << "error: could not open camera!" << std::endl;
        return;
    }
    while (true)
    {
        cv::Mat raw_frame;
        cap >> raw_frame; // capture a frame.

        if (raw_frame.empty())
        {
            std::cerr << "error: empty frame captured!" << std::endl;
            break;
        }

        // flip the frame horizontally (mirror effect).
        cv::Mat processed_frame;
        cv::flip(raw_frame, processed_frame, 1); // 1 means horizontal flip.

        // convert the frame to raw pixel data.
        unsigned char *raw_data = processed_frame.data;

        send_frame(socket, raw_data, timenow_ms());

        // wait for 10 milliseconds and check if 'q' key was pressed
        if (cv::waitKey(100) == 'q')
        {
            std::cout << "exiting..." << std::endl;
            break;
        }
    }
    // release the camera and close the window
    cap.release();
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "IP address and connection mode(S or R) not provided!" << std::endl;
        return 1;
    }
    // Retrieve the IP address and connection mode.
    const char *ip_address = argv[1];
    const char *connection_mode = argv[2];
    bool is_sending_connection_request = (strcmp(connection_mode, "S") == 0);
    std::cout << "connection_mode: " << is_sending_connection_request << std::endl;
    int send_socket = create_send_socket(ip_address, is_sending_connection_request);
    int receive_socket = create_receive_socket(ip_address, is_sending_connection_request);
    if (send_socket != 1 && receive_socket != 1)
    {
        // std::thread send_thread(send_data, send_socket);
        receive_data(receive_socket);
        // send_thread.join();
        // receive_thread.join();
    }
    close(send_socket);
    close(receive_socket);
    return 0;
}