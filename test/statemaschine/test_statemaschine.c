#include "mqtt.h"
#include "unity.h"

#include<stdio.h>
#include<sys/socket.h>
#include<arpa/inet.h> //inet_addr
#include<unistd.h>
#include<string.h>
#include<time.h>     //nanosleep

extern MQTTErrorCodes_t mqtt_connect_parse_ack(uint8_t * a_message_in_ptr);

static volatile int g_socket_desc = -1;
static volatile bool g_auto_state_connection_completed = false;

void sleep_ms(int milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int open_mqtt_socket()
{
    int socket_desc;
    struct sockaddr_in server;

    //Create socket
    socket_desc = socket(AF_INET , SOCK_STREAM , 0);
    TEST_ASSERT_NOT_EQUAL( -1, socket_desc);

    server.sin_addr.s_addr = inet_addr(MQTT_SERVER);
    server.sin_family = AF_INET;
    server.sin_port = htons(MQTT_PORT);

    //Connect to remote server
    TEST_ASSERT_TRUE_MESSAGE(connect(socket_desc,
                                     (struct sockaddr *)&server, 
                                     sizeof(server)
                                     ) >= 0, 
                                     "MQTT Broker not running?");

    struct timeval tv;
    tv.tv_sec  = 10;  /* 5 Secs Timeout */
    tv.tv_usec = 0;  // Not init'ing this can cause strange errors
    setsockopt(socket_desc, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    return socket_desc;
}

int data_stream_in_fptr(uint8_t * a_data_ptr, size_t a_amount)
{
    if (g_socket_desc <= 0)
        g_socket_desc = open_mqtt_socket();

    if (g_socket_desc > 0) {
        int ret = recv(g_socket_desc, a_data_ptr, a_amount, /*MSG_NOSIGNAL*/0);
        printf("Socket in  -> %iB\n", ret);
        return ret;
    } else {
        return -1;
    }
}

int data_stream_out_fptr(uint8_t * a_data_ptr, size_t a_amount)
{
    if (g_socket_desc <= 0)
        g_socket_desc = open_mqtt_socket();
    
    printf("g_socket_desc:%i\n", g_socket_desc);
    if (g_socket_desc > 0) {
        printf("Socket out -> %lu\n", a_amount);
        int ret = send(g_socket_desc, a_data_ptr, a_amount, /*MSG_NOSIGNAL */0);
        if (ret != a_amount) {
            printf("Socket did not send enought data out\n");
            return -1;
        }
        printf("Socket out -> %iB\n", ret);
        return ret;
    } else {
        printf("Socket failed %i\n", g_socket_desc);
        g_socket_desc = 0;
        return -1;
    }
}

void connected_cb(MQTTErrorCodes_t a_status)
{
    if (Successfull == a_status)
        printf("Connectd CB SUCCESSFULL\n");
    else
        printf("Connectd CB FAIL %i\n", a_status);
    g_auto_state_connection_completed = true;
}

void test_sm_connect_manual_ack()
{
    uint8_t buffer[256];
    MQTT_shared_data_t shared;
    
    shared.buffer = buffer;
    shared.buffer_size = sizeof(buffer);
    shared.out_fptr = &data_stream_out_fptr;


    MQTT_action_data_t action;
    action.action_argument.shared_ptr = &shared;
    MQTTErrorCodes_t state = mqtt(ACTION_INIT,
                                  &action);

    MQTT_connect_t connect_params;
    sprintf((char*)(connect_params.client_id), "JAMKtest");
    connect_params.last_will_topic[0] = '\0';
    connect_params.last_will_message[0] = '\0';
    connect_params.username[0] = '\0';
    connect_params.password[0] = '\0';
    connect_params.keepalive = 0;
    connect_params.connect_flags.clean_session = true;

    action.action_argument.connect_ptr = &connect_params;

    state = mqtt(ACTION_CONNECT,
                 &action);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    sleep_ms(400);
    
    // Wait response from borker
    int rcv = data_stream_in_fptr(buffer, sizeof(MQTT_fixed_header_t));
    if (0 < rcv) {
        state = mqtt_connect_parse_ack(buffer);
    } else
    {
        state = NoConnection;
    }
    TEST_ASSERT_EQUAL_INT(Successfull, state);
    shared.state = STATE_CONNECTED;

    state = mqtt(ACTION_DISCONNECT,
                 NULL);

    TEST_ASSERT_EQUAL_INT(Successfull, state);
    
    close(g_socket_desc);
    g_socket_desc = 0;
}

void test_sm_connect_auto_ack()
{
    uint8_t buffer[256];
    MQTT_shared_data_t shared;
    
    shared.buffer = buffer;
    shared.buffer_size = sizeof(buffer);
    shared.out_fptr = &data_stream_out_fptr;
    shared.connected_cb_fptr = &connected_cb;
    g_auto_state_connection_completed = false;

    MQTT_action_data_t action;
    action.action_argument.shared_ptr = &shared;
    MQTTErrorCodes_t state = mqtt(ACTION_INIT,
                                  &action);

    MQTT_connect_t connect_params;
    sprintf((char*)(connect_params.client_id), "JAMKtest2");
    connect_params.last_will_topic[0] = '\0';
    connect_params.last_will_message[0] = '\0';
    connect_params.username[0] = '\0';
    connect_params.password[0] = '\0';
    connect_params.keepalive = 0;
    connect_params.connect_flags.clean_session = true;

    action.action_argument.connect_ptr = &connect_params;

    state = mqtt(ACTION_CONNECT,
                 &action);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    // Wait response and request parse for it
    // Parse will call given callback which will set global flag to true
    int rcv = data_stream_in_fptr(buffer, sizeof(MQTT_fixed_header_t));
    if (0 < rcv) {
        MQTT_input_stream_t input;
        input.data = buffer;
        input.size_of_data = (uint32_t)rcv;
        action.action_argument.input_stream_ptr = &input;

        state = mqtt(ACTION_PARSE_INPUT_STREAM,
                     &action);
    } else {
        TEST_ASSERT(0);
    }

    do {
        /* Wait callback */
        sleep_ms(10);
    } while( false == g_auto_state_connection_completed );

    state = mqtt(ACTION_DISCONNECT,
                 NULL);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    close(g_socket_desc);
    g_socket_desc = 0;
}

void test_sm_connect_auto_ack_keepalive()
{
    uint8_t buffer[256];
    MQTT_shared_data_t shared;
    
    shared.buffer = buffer;
    shared.buffer_size = sizeof(buffer);
    shared.out_fptr = &data_stream_out_fptr;
    shared.connected_cb_fptr = &connected_cb;
    g_auto_state_connection_completed = false;

    MQTT_action_data_t action;
    action.action_argument.shared_ptr = &shared;
    MQTTErrorCodes_t state = mqtt(ACTION_INIT,
                                  &action);

    MQTT_connect_t connect_params;
    sprintf((char*)(connect_params.client_id), "JAMKtest3");
    connect_params.last_will_topic[0] = '\0';
    connect_params.last_will_message[0] = '\0';
    connect_params.username[0] = '\0';
    connect_params.password[0] = '\0';
    connect_params.keepalive = 2;
    connect_params.connect_flags.clean_session = true;

    action.action_argument.connect_ptr = &connect_params;

    state = mqtt(ACTION_CONNECT,
                 &action);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    // Wait response and request parse for it
    // Parse will call given callback which will set global flag to true
    int rcv = data_stream_in_fptr(buffer, sizeof(MQTT_fixed_header_t));
    if (0 < rcv) {
        MQTT_input_stream_t input;
        input.data = buffer;
        input.size_of_data = (uint32_t)rcv;
        action.action_argument.input_stream_ptr = &input;

        state = mqtt(ACTION_PARSE_INPUT_STREAM,
                     &action);
    } else {
        TEST_ASSERT(0);
    }

    do {
        /* Wait callback */
        sleep_ms(1);
    } while( false == g_auto_state_connection_completed );

    MQTT_action_data_t ap;
    ap.action_argument.epalsed_time_in_ms = 500;
    state = mqtt(ACTION_KEEPALIVE, &ap);
    printf("Keepalive cmd status %i\n", state);
    ap.action_argument.epalsed_time_in_ms = 500;

    for (uint8_t i = 0; i < 10; i++)
    {
        printf("g_shared_data->state %u\n", shared.state);

        if (Successfull == state) {
            int rcv = data_stream_in_fptr(buffer, sizeof(MQTT_fixed_header_t));
            if (0 < rcv) {
                MQTT_input_stream_t input;
                input.data = buffer;
                input.size_of_data = (uint32_t)rcv;
                action.action_argument.input_stream_ptr = &input;

                state = mqtt(ACTION_PARSE_INPUT_STREAM,
                            &action);
                TEST_ASSERT_EQUAL_INT(Successfull, state);
            } else {
                printf("no ping resp\n");
                state = Successfull;
                //TEST_ASSERT(0);
            }
        }
        
        printf("sleeping..\n");
        sleep_ms(500);
        printf("slept\n");
        state = mqtt(ACTION_KEEPALIVE, &ap);
        //TEST_ASSERT_EQUAL_INT(Successfull, state);
        
    }
    state = mqtt(ACTION_DISCONNECT,
                 NULL);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    close(g_socket_desc);
    g_socket_desc = 0;
}


void test_sm_publish()
{
    uint8_t buffer[256];
    MQTT_shared_data_t shared;
    
    shared.buffer = buffer;
    shared.buffer_size = sizeof(buffer);
    shared.out_fptr = &data_stream_out_fptr;
    shared.connected_cb_fptr = &connected_cb;
    g_auto_state_connection_completed = false;

    MQTT_action_data_t action;
    action.action_argument.shared_ptr = &shared;
    MQTTErrorCodes_t state = mqtt(ACTION_INIT,
                                  &action);

    MQTT_connect_t connect_params;
    sprintf((char*)(connect_params.client_id), "JAMKtest4");
    connect_params.last_will_topic[0] = '\0';
    connect_params.last_will_message[0] = '\0';
    connect_params.username[0] = '\0';
    connect_params.password[0] = '\0';
    connect_params.keepalive = 2;
    connect_params.connect_flags.clean_session = true;

    action.action_argument.connect_ptr = &connect_params;

    state = mqtt(ACTION_CONNECT,
                 &action);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    // Wait response and request parse for it
    // Parse will call given callback which will set global flag to true
    int rcv = 0;
    while(rcv == 0) {
        rcv = data_stream_in_fptr(buffer, sizeof(MQTT_fixed_header_t));
        if (rcv == 0)
            sleep_ms(10);
    }

    if (0 < rcv) {
        MQTT_input_stream_t input;
        input.data = buffer;
        input.size_of_data = (uint32_t)rcv;
        action.action_argument.input_stream_ptr = &input;

        state = mqtt(ACTION_PARSE_INPUT_STREAM,
                     &action);
    } else {
        TEST_ASSERT(0);
    }

    do {
        /* Wait callback */
        sleep_ms(1);
    } while( false == g_auto_state_connection_completed );

    MQTT_publish_t publish;
    publish.flags.dup = false;
    publish.flags.retain = false;
    publish.flags.qos = QoS0;

    const char topic[] = "test/msg";
    publish.topic_ptr = (uint8_t*) topic;
    publish.topic_length = strlen(topic);

    const   char message[] = "FooBarMessage";

    publish.message_buffer_ptr = (uint8_t*)message;
    publish.message_buffer_size = strlen(message);

    hex_print((uint8_t *) publish.message_buffer_ptr, publish.message_buffer_size);

    action.action_argument.publish_ptr = &publish;

    state = mqtt(ACTION_PUBLISH,
                 &action);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    state = mqtt(ACTION_DISCONNECT,
                 NULL);

    TEST_ASSERT_EQUAL_INT(Successfull, state);

    close(g_socket_desc);
    g_socket_desc = 0;
}

/****************************************************************************************
 * TEST main                                                                            *
 ****************************************************************************************/
int main(void)
{  
    UnityBegin("State Maschine");
    unsigned int tCntr = 1;

    RUN_TEST(test_sm_connect_manual_ack,                    tCntr++);
    RUN_TEST(test_sm_connect_auto_ack,                      tCntr++);
    RUN_TEST(test_sm_connect_auto_ack_keepalive,            tCntr++);
    RUN_TEST(test_sm_publish,                               tCntr++);
    return (UnityEnd());
}
