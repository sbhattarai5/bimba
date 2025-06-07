#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

/*
    creating sharded buffer might help with contentions.
    create buffer_0, buffer_1, buffer_2 .... buffer_15.
    add each frame to the corresponding buffer according to sequence_num % 16.
    while sending, loop through each buffer and send one element.
*/

#define ROWS 25
#define COLS 25
#define CHANNELS 3 // RGB.
#define PORT1 50001
#define PORT2 50002
#define ACCEPTABLE_DELAY_IN_MS 100
#define BATCH_SIZE 1400

struct frame
{
    unsigned char raw_data[ROWS * COLS * CHANNELS];
    long long timestamp;
    long long sequence_num;

    bool operator<(const frame &other_frame) const
    {
        return sequence_num < other_frame.sequence_num;
    }

    frame() {}
};

struct channel
{
    int socket;
    const char *ip_address;
    int port_number;
};

void close_channel(channel c)
{
    close(c.socket);
}

long long timenow_ms()
{
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return duration.count();
}

int create_socket(const char *ip_address, int binding_port_number)
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    // bind the socket to the port.
    struct sockaddr_in my_address;
    memset(&my_address, 0, sizeof(my_address));
    my_address.sin_family = AF_INET;
    my_address.sin_port = htons(binding_port_number);
    my_address.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&my_address, sizeof(my_address)) == -1)
    {
        std::cerr << "error: failed to bind socket!" << std::endl;
        return -1;
    }
    return sockfd;
}

channel create_send_socket(const char *ip_address, bool use_low_port_for_sending)
{
    int sockfd = create_socket(ip_address, use_low_port_for_sending ? PORT1 : PORT2);
    return {sockfd, ip_address, PORT2};
}

channel create_receive_socket(const char *ip_address, bool use_low_port_for_sending)
{
    int sockfd = create_socket(ip_address, use_low_port_for_sending ? PORT2 : PORT1);
    return {sockfd, ip_address, /*port_number=*/0}; // port number is irrelevant for receiving socket.
}

bool should_drop_frame(const frame *f, const long long &last_processed_seq_num)
{
    return (f->sequence_num < last_processed_seq_num) || ((timenow_ms() - f->timestamp) > ACCEPTABLE_DELAY_IN_MS);
}

void receive_data(const channel &c)
{
    cv::namedWindow("bimba", cv::WINDOW_NORMAL);
    char final_buffer[ROWS * COLS * CHANNELS + sizeof(long long) * 2];
    struct sockaddr_in peer_address;
    socklen_t peer_address_len = sizeof(peer_address);
    long long last_processed_seq_num = -1;
    while (1)
    {
        ssize_t bytes_received = recvfrom(c.socket, final_buffer, sizeof(final_buffer), 0,
                                     (struct sockaddr*)&peer_address, &peer_address_len);
        frame f;
        memcpy(f.raw_data, final_buffer, ROWS * COLS * CHANNELS);
        memcpy(&f.timestamp, final_buffer + ROWS * COLS * CHANNELS, sizeof(long long));
        memcpy(&f.sequence_num, final_buffer + ROWS * COLS * CHANNELS + sizeof(long long), sizeof(long long));
                  
        if (should_drop_frame(&f, last_processed_seq_num))
        {
            std::cout << "dropped!" << std::endl;
            continue;
        }

        std::cout << "not dropped!" << std::endl;
        cv::Mat received_frame(ROWS, COLS, CV_8UC3, (void *)f.raw_data);

        // display the frame.
        cv::imshow("bimba", received_frame);

        // wait for 10 milliseconds and check if 'q' key was pressed
        if (cv::waitKey(10) == 'q')
        {
            std::cout << "exiting..." << std::endl;
            break;
        }
        last_processed_seq_num = f.sequence_num;
    }
    cv::destroyAllWindows();
}

void send_frame(const channel &c, const frame *f)
{
    struct sockaddr_in peer_address;
    memset(&peer_address, 0, sizeof(peer_address));
    peer_address.sin_family = AF_INET;
    peer_address.sin_port = htons(c.port_number);
    inet_pton(AF_INET, c.ip_address, &peer_address.sin_addr); // Remote IP address (localhost)

    char final_buffer[ROWS * COLS * CHANNELS + sizeof(long long) * 2];
    memcpy(final_buffer, f->raw_data, ROWS * COLS * CHANNELS);
    memcpy(final_buffer + ROWS * COLS * CHANNELS, &f->timestamp, sizeof(long long));
    memcpy(final_buffer + ROWS * COLS * CHANNELS + sizeof(long long), &f->sequence_num, sizeof(long long));

    ssize_t bytes_sent = sendto(c.socket, final_buffer, sizeof(final_buffer), 0,
                                (struct sockaddr *)&peer_address, sizeof(peer_address));
}

void send_data(const channel &c)
{
    // open the default camera (ID 0)
    cv::VideoCapture cap(0);
    if (!cap.isOpened())
    {
        std::cerr << "error: could not open camera!" << std::endl;
        return;
    }
    long long sequence_num = 0;
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
        cv::Mat resized_processed_frame;
        cv::resize(processed_frame, resized_processed_frame, cv::Size(COLS, ROWS), 0, 0, cv::INTER_LINEAR);

        // convert the frame to raw pixel data and send.
        unsigned char *raw_data = resized_processed_frame.data;
        frame f = {};
        memcpy(f.raw_data, raw_data, ROWS * COLS * CHANNELS);
        f.timestamp = timenow_ms();
        f.sequence_num = sequence_num;
        send_frame(c, &f);

        // wait for 10 milliseconds and check if 'q' key was pressed
        if (cv::waitKey(100) == 'q')
        {
            std::cout << "exiting..." << std::endl;
            break;
        }
        ++sequence_num;
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
    const char *peer_ip_address = argv[1];
    bool use_low_port_for_sending = (strcmp(argv[2], "Y") == 0);
    channel sending_channel = create_send_socket(peer_ip_address, use_low_port_for_sending);
    channel receiving_channel = create_receive_socket(peer_ip_address, use_low_port_for_sending);
    if (sending_channel.socket != -1 && receiving_channel.socket != -1)
    {
        std::thread send_thread(send_data, sending_channel);
        receive_data(receiving_channel);
        send_thread.join();
    }
    close_channel(sending_channel);
    close_channel(receiving_channel);
    return 0;
}
