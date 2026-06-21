#include <esp_log.h>
#include <wolfssl/ssl.h>
#include <wolfssl/version.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/mem_track.h>
#include <esp_heap_caps.h>
#include "esp_timer.h"
#include <wolfssl/openssl/ssl.h> // Tu siedzi Twoja funkcja
#include <wolfssl/wolfcrypt/logging.h>
#include <wolfssl/wolfcrypt/asn.h> // Wymagany nagłówek
#include "esp_heap_trace.h"
#include "measure.h"

/* wolfSSL */
/* Always include wolfcrypt/settings.h before any other wolfSSL file.    */
/* Reminder: settings.h pulls in user_settings.h; don't include it here. */
#ifdef WOLFSSL_USER_SETTINGS
    #include <wolfssl/wolfcrypt/settings.h>
    #ifndef WOLFSSL_ESPIDF
        #warning "Problem with wolfSSL user_settings."
        #warning "Check components/wolfssl/include"
#endif
    #include <wolfssl/wolfcrypt/port/Espressif/esp32-crypt.h>
#else
    /* Define WOLFSSL_USER_SETTINGS project wide for settings.h to include   */
    /* wolfSSL user settings in ./components/wolfssl/include/user_settings.h */
    #error "Missing WOLFSSL_USER_SETTINGS in CMakeLists or Makefile:\
    CFLAGS +=-DWOLFSSL_USER_SETTINGS"
#endif

//wifi
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"

#define ENABLE_MQTT_TLS

//mqtt
#include "mqtt_client.h"
#include <wolfmqtt/mqtt_client.h>
#include <wolfmqtt/mqtt_socket.h>

/* project */
#include <wolfssl/wolfcrypt/mlkem.h>
#include <wolfssl/wolfcrypt/wc_mlkem.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/mlkem.h>
#ifndef WOLFSSL_USER_SETTINGS
    #include <wolfssl/options.h>
#endif
#include <wolfssl/wolfcrypt/settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>




//wifi 
#define WIFI_SSID "WifiName"
#define WIFI_PASS "WifiPassword"

#define WOLFSSL_TLS13
#define HAVE_SUPPORTED_CURVES

static volatile bool wifi_connected = false; 

//mqtt
#define MQTT_HOST "X.X.X.X"

#define MQTT_CLIENT_ID "esp32s3-publisher"
#define MQTT_TOPIC     "test/topic"
#define MQTT_QOS             MQTT_QOS_0
#define MQTT_KEEP_ALIVE_SEC  60
#define MQTT_CMD_TIMEOUT_MS  30000
#define MQTT_CON_TIMEOUT_MS  5000
#define MQTT_PUBLISH_MSG     "Test Publish"
#define MQTT_USERNAME        NULL
#define MQTT_PASSWORD        NULL
#ifdef ENABLE_MQTT_TLS
    #define MQTT_USE_TLS     1
    #define MQTT_PORT        8883
#else
    #define MQTT_USE_TLS     0
    #define MQTT_PORT        1883
#endif
#define MQTT_MAX_PACKET_SZ   2048
#define INVALID_SOCKET_FD    -1
#define PRINT_BUFFER_SIZE    80
#define TLS_HANDSHAKE_ITERATIONS 60
#define NO_PSK


//mqtt pqc
#define MQTT_TOPIC_ALICE_PUBKEY "alice/pubkey"
#define MQTT_TOPIC_BOB_CT   "bob/ct"

/* Requires BSD Style Socket */

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"

#if defined(HAVE_SOCKET) && ! defined(HAVE_NETX)
#ifndef ENABLE_MQTT_TLS
#include <sys/types.h>
#include <sys/socket.h>
#ifndef WOLFSSL_LWIP
    #include <netinet/in.h>
#endif
#include <netdb.h>
#include <unistd.h>
#endif
#endif

// bufory MQTT
static byte tx_buf[2048];
static byte rx_buf[2048];
static int mSockFd = INVALID_SOCKET_FD;

static const char *TAG = "wifi";

TaskHandle_t xKEMTaskHandle = NULL;

static TaskHandle_t g_tls_loop_task = NULL;


//ZMIENNE GLOBALNE 
int ret;
WOLFSSL_CTX* ctx;
WOLFSSL* ssl;

//################KLUCZE########################
#define TLS_GROUP WOLFSSL_ML_KEM_512						
//#define TLS_GROUP WOLFSSL_ML_KEM_768
//#define TLS_GROUP WOLFSSL_ECC_X25519


//##################SERVER CA CERTS ##################################
static const char ca_cert_pem[]= "-----BEGIN CERTIFICATE-----\n"
"ca_certificate_data\n"
"-----END CERTIFICATE-----";



char* to_hex_string(const unsigned char* array, size_t length)
{
    char* outstr = malloc(2 * length + 1);
    if (!outstr) return outstr;

    char* p = outstr;
    for (size_t i = 0; i < length; ++i) {
        p += sprintf(p, "%02hhx", array[i]);
    }

    return outstr;
}

void print_current_time(void) {
    time_t now;
    struct tm timeinfo;
    char strftime_buf[64];

    time(&now); // aktualny czas systemowy
    localtime_r(&now, &timeinfo); //konwersja

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    printf("Aktualny czas w ESP32: %s\n", strftime_buf);
}

void obtain_time(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Czekaj na synchronizację czasu
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    while (timeinfo.tm_year < (2020 - 1900)) {
        ESP_LOGI("TIME", "Czekam na synchronizację czasu... (%d)", ++retry);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI("TIME", "Czas zsynchronizowany!");
}


    // MQTT FUNCTION

    static int mqtt_message_cb(MqttClient *client, MqttMessage *msg,
                            byte msg_new, byte msg_done)
    {
        if (msg_new) {
            printf("MQTT topic: %.*s\n", msg->topic_name_len, msg->topic_name);
        }

        if (msg->buffer && msg->buffer_len > 0) {
            printf("Payload: %.*s\n", msg->buffer_len, msg->buffer);
        }

        return MQTT_CODE_SUCCESS;
    }

    static void setup_timeout(struct timeval* tv, int timeout_ms)
    {
        tv->tv_sec = timeout_ms / 1000;
        tv->tv_usec = (timeout_ms % 1000) * 1000;

        /* Make sure there is a minimum value specified */
        if (tv->tv_sec < 0 || (tv->tv_sec == 0 && tv->tv_usec <= 0)) {
            tv->tv_sec = 0;
            tv->tv_usec = 100;
        }
    }

    static int socket_get_error(int sockFd)
    {
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        (void)getsockopt(sockFd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        return so_error;
    }

    static int mqtt_net_connect(void *context, const char* host, word16 port,
        int timeout_ms)
    {
        int sockFd;
        int *pSockFd = (int*)context;
        struct sockaddr_in addr;

        if (pSockFd == NULL) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(host);

        sockFd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockFd < 0) {
            return MQTT_CODE_ERROR_NETWORK;
        }

        if (connect(sockFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sockFd);
            return MQTT_CODE_ERROR_NETWORK;
        }

        *pSockFd = sockFd;

        return MQTT_CODE_SUCCESS;
    }

    static int mqtt_net_read(void *context, byte* buf, int buf_len, int timeout_ms)
    {
        int rc;
        int *pSockFd = (int*)context;
        int bytes = 0;
        struct timeval tv;

        if (pSockFd == NULL) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }

        /* Setup timeout */
        setup_timeout(&tv, timeout_ms);
        (void)setsockopt(*pSockFd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,
                sizeof(tv));

        /* Loop until buf_len has been read, error or timeout */
        while (bytes < buf_len) {
            rc = (int)recv(*pSockFd, &buf[bytes], buf_len - bytes, 0);
            if (rc <= 0) {
                rc = socket_get_error(*pSockFd);
                if (rc == 0)
                    break; /* timeout */
                PRINTF("NetRead: Error %d", rc);
                return MQTT_CODE_ERROR_NETWORK;
            }
            bytes += rc; /* Data */
        }

        if (bytes == 0) {
            return MQTT_CODE_ERROR_TIMEOUT;
        }

        return bytes;
    }

    static int mqtt_net_write(void *context, const byte* buf, int buf_len,
        int timeout_ms)
    {
        int rc;
        int *pSockFd = (int*)context;
        struct timeval tv;

        if (pSockFd == NULL) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }

        /* Setup timeout */
        setup_timeout(&tv, timeout_ms);
        (void)setsockopt(*pSockFd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv,
                sizeof(tv));

        //printf("Próba wysłania: %d bajtów\n", buf_len);
        rc = (int)send(*pSockFd, buf, buf_len, 0);
        if (rc < 0) {
            PRINTF("NetWrite: Error %d (Sock Err %d)",
                rc, socket_get_error(*pSockFd));
            return MQTT_CODE_ERROR_NETWORK;
        }

        return rc;
    }

    static int mqtt_net_disconnect(void *context)
    {
        int *pSockFd = (int*)context;

        if (pSockFd == NULL) {
            return MQTT_CODE_ERROR_BAD_ARG;
        }

        close(*pSockFd);
        *pSockFd = INVALID_SOCKET_FD;

        return MQTT_CODE_SUCCESS;
    }


    #ifdef ENABLE_MQTT_TLS
    static int mqtt_tls_verify_cb(int preverify, WOLFSSL_X509_STORE_CTX* store)
    {
    if (!preverify) {
        //printf("ERROR: Certificate verification FAILED! Rejecting connection.\n");
        return 0;  // Reject the connection 
    }
    //printf("Certificate verification PASSED\n");
    return 1;  // Accept valid certificates 
    
    }




    static int my_tls13_secret_callback(WOLFSSL* ssl, int id, const unsigned char* secret, 
                                        int secretSz, void* ctx) 
    {
        unsigned char clientRandom[32];
        int clientRandomSz = wolfSSL_get_client_random(ssl, clientRandom, sizeof(clientRandom));
        const char* label = NULL;

        // etykiety do wiresharka
        switch (id) {
            case CLIENT_HANDSHAKE_TRAFFIC_SECRET: label = "CLIENT_HANDSHAKE_TRAFFIC_SECRET"; break;
            case SERVER_HANDSHAKE_TRAFFIC_SECRET: label = "SERVER_HANDSHAKE_TRAFFIC_SECRET"; break;
            case CLIENT_TRAFFIC_SECRET:         label = "CLIENT_TRAFFIC_SECRET_0";         break;
            case SERVER_TRAFFIC_SECRET:         label = "SERVER_TRAFFIC_SECRET_0";         break;
            case EXPORTER_SECRET:                 label = "EXPORTER_SECRET";                 break;
        }

        if (label && clientRandomSz > 0) {
            //key.log format
            printf("%s ", label);
            for (int i = 0; i < clientRandomSz; i++) printf("%02x", clientRandom[i]);
            printf(" ");
            for (int i = 0; i < secretSz; i++) printf("%02x", secret[i]);
            printf("\n");
        }
        return 0;
    }

    /* Use this callback to setup TLS certificates and verify callbacks */
    static void mqtt_tls_reset(MqttClient* client)
    {
        if (client == NULL)
            return;

        if (client->tls.ssl) {
            wolfSSL_free(client->tls.ssl);
            client->tls.ssl = NULL;
        }
        if (client->tls.ctx) {
            wolfSSL_CTX_free(client->tls.ctx);
            client->tls.ctx = NULL;
        }
    }

    static int mqtt_tls_cb(MqttClient* client)
    {

        int groups[]= {TLS_GROUP}; // Lista grup do negocjacji (w tym hybryda i czysta PQC)
        //int groups[] ={WOLFSSL_ECC_X25519};
        int numGroups = 1;
        
        int rc = WOLFSSL_FAILURE;

        /* Use highest available and allow downgrade. If wolfSSL is built with
        * old TLS support, it is possible for a server to force a downgrade to
        * an insecure version. */
        //client->tls.ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
 
        //printf("[TLS] Initializing TLS context \n");
        client->tls.ctx = wolfSSL_CTX_new(wolfTLSv1_3_client_method());

        //SERVER AUTHENTICATION
        if (client->tls.ctx) {

            //printf("[TLS CERT] Loading CA certificate\n");


            if (!client->tls.ctx) {
                printf("[ERROR] ctx is NULL before load_verify_buffer\n");
            }
            if (strlen(ca_cert_pem) <= 0) {
                printf("[ERROR] CA cert buffer length is zero\n");
            }
            rc = wolfSSL_CTX_load_verify_buffer(client->tls.ctx,
                                        (const unsigned char*)ca_cert_pem,
                                        (long)(strlen(ca_cert_pem)),
                                        WOLFSSL_FILETYPE_PEM);
            //@return  1 on success.
            //@return  0 on failure.

            //printf("[TLS CERT] wolfSSL_CTX_load_verify_buffer returned: %d\n", rc);
            if (rc != WOLFSSL_SUCCESS) {
            printf("[ERROR] wolfSSL_CTX_load_verify_buffer failed: %d\n", rc);
            wolfSSL_CTX_free(client->tls.ctx);
            client->tls.ctx = NULL;
            return rc;
        }


            
            rc = wolfSSL_CTX_set_groups(client->tls.ctx, groups, numGroups);
        
        if (rc != WOLFSSL_SUCCESS) {
            printf("[ERROR] Błąd ustawiania grupy PQC: %d. Sprawdź czy grupa jest włączona w wolfSSL.\n", rc);
        } else {
            //printf("[TLS GROUP] Grupa dodana do Client Hello!\n");
        }

        //CLIENT AUTHORIZATION
        /* If using a client certificate it can be loaded using: 
        rc = wolfSSL_CTX_use_certificate_buffer(client->tls.ctx,
                            clientCertBuf, clientCertSize, WOLFSSL_FILETYPE_PEM);
                            */

        #if !defined(NO_CERT)
           
            /*
            rc = wolfSSL_CTX_check_private_key(client->tls.ctx);
            rc = wolfSSL_check_private_key(client->tls.ssl);
            if (rc != WOLFSSL_SUCCESS) {
                printf("[ERROR] Klucz prywatny NIE PASUJE do certyfikatu! Kod: %d\n", rc);
            } else {
                
                printf("[TLS] Potwierdzono: Klucz pasuje do certyfikatu.\n");
            }
            */
            client->tls.ssl = wolfSSL_new(client->tls.ctx);
            if (client->tls.ssl == NULL) {
                printf("[ERROR] wolfSSL_new failed\n");
                return WOLFSSL_FAILURE;
            }
            //start_measure("KeyShare");
             //This function creates a key share entry from the group including generating a key pair. 
            rc = wolfSSL_UseKeyShare(client->tls.ssl, TLS_GROUP); // Wymuszamy użycie Key Share
            //end_measure();
            if (rc != WOLFSSL_SUCCESS) {
                printf("[ERROR] wolfSSL_UseKeyShare failed: %d\n", rc);
                return rc;
            }

            //printf("[TLS] Secret callback  \n");
            wolfSSL_set_tls13_secret_cb(client->tls.ssl, my_tls13_secret_callback, NULL);

            wolfSSL_CTX_set_verify(client->tls.ctx,
                                WOLFSSL_VERIFY_PEER,
                                mqtt_tls_verify_cb);
            wolfSSL_set_verify(client->tls.ssl,
                            WOLFSSL_VERIFY_PEER,
                            mqtt_tls_verify_cb);
            //wolfSSL_CTX_set_verify_depth(client->tls.ctx, 4);
            //printf("[TLS CERT] Verification callback set on context and ssl\n");


        #endif /* !NO_CERT */



        }
        return rc;
    }
    #else
    static int mqtt_tls_cb(MqttClient* client)
    {
        (void)client;
        return 0;
    }
    #endif /* ENABLE_MQTT_TLS */




static void mqtt_tls_handshake_once_task(void* pvParameters)
{
    int iter = (int)(intptr_t)pvParameters;
    int rc;
    int net_connected = 0;
    int mqtt_connected = 0;
    MqttNet net;
    MqttClient client;
    MqttConnect connect;


    memset(&net, 0, sizeof(net));
    memset(&client, 0, sizeof(client));
    memset(&connect, 0, sizeof(connect));

    net.connect = mqtt_net_connect;
    net.read = mqtt_net_read;
    net.write = mqtt_net_write;
    net.disconnect = mqtt_net_disconnect;
    net.context = &mSockFd;
    printf("\n[TLS LOOP] Iteration %d/%d\n", iter, TLS_HANDSHAKE_ITERATIONS);
    set_current_iteration(iter);

    rc = MqttClient_Init(&client, &net, mqtt_message_cb,
                         tx_buf, sizeof(tx_buf),
                         rx_buf, sizeof(rx_buf), MQTT_CMD_TIMEOUT_MS);
    if (rc != MQTT_CODE_SUCCESS) {
        printf("[TLS LOOP] MqttClient_Init failed: %d\n", rc);
        xTaskNotifyGive(g_tls_loop_task);
        vTaskDelete(NULL);
        return;
    }
    rc = MqttClient_NetConnect(&client, MQTT_HOST, MQTT_PORT,
                               MQTT_CON_TIMEOUT_MS, MQTT_USE_TLS, mqtt_tls_cb);
    if (rc == MQTT_CODE_SUCCESS) {
        net_connected = 1;
        connect.keep_alive_sec = MQTT_KEEP_ALIVE_SEC;
        connect.clean_session = 1;
        connect.client_id = MQTT_CLIENT_ID;

        rc = MqttClient_Connect(&client, &connect);
        if (rc == MQTT_CODE_SUCCESS) {
            mqtt_connected = 1;
        }
    }
    //end_measure();

    if (rc == MQTT_CODE_SUCCESS) {
        printf("[TLS LOOP] Handshake OK\n");
    }
    else {
        printf("[TLS LOOP] Handshake failed rc=%d\n", rc);
    }
    

    if (mqtt_connected) {
        MqttClient_Disconnect(&client);
    }
    if (net_connected) {
        MqttClient_NetDisconnect(&client);
    }

    mqtt_tls_reset(&client);
    MqttClient_DeInit(&client);
    memset(tx_buf, 0, sizeof(tx_buf));
    memset(rx_buf, 0, sizeof(rx_buf));
        //printf("[TLS LOOP] Heap after cleanup: free=%lu largest=%lu\n",
            //(unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
            //(unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    //reset_heap_monitor_between_iterations();

    xTaskNotifyGive(g_tls_loop_task);
    vTaskDelete(NULL);
}

//CONNECTION
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Rozłączono, ponawiam połączenie...");
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        ESP_LOGI(TAG, "Połączono z WiFi!");
        esp_netif_ip_info_t ip_info;
        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_get_ip_info(netif, &ip_info);
        ESP_LOGI("WIFI_STATUS", "Mój adres IP: " IPSTR, IP2STR(&ip_info.ip));
    }
}

void wifi_init(void)
{
    //NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    //TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    //Event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //interfejs WiFi
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //Rejestracja eventów
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    //Konfiguracja WiFi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    //Start
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void wait_for_wifi(void)
{
    while (!wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}



void app_main(void) {
    int ret = wolfSSL_Init();
    wifi_init();
    wait_for_wifi();
    obtain_time();
    //print_current_time();


    if (ret == WOLFSSL_SUCCESS) {
        printf("Wolny heap brysia: %lu bajtów\n", esp_get_free_heap_size());
        
        printf("Sukces: Biblioteka wolfSSL zainicjalizowana!\n");
        TaskHandle_t my_task_handle;

    g_tls_loop_task = xTaskGetCurrentTaskHandle();
    int iterations;
    iterations = TLS_HANDSHAKE_ITERATIONS;
    for (int iter = 1; iter <= iterations; ++iter) {
        TaskHandle_t one_shot = NULL;
    
        BaseType_t ok = xTaskCreatePinnedToCore(
            mqtt_tls_handshake_once_task,
            "tls_hs_once",
            24 * 1024,
            (void*)(intptr_t)iter,
            1,
            &one_shot,
            1
        );

        if (ok != pdPASS) {
            printf("[TLS LOOP] task create failed for iter %d\n", iter);
            continue;
        }

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        //vTaskDelay(pdMS_TO_TICKS(300));
    }

    export_csv();
    reset_measurement_results();
    vTaskDelete(NULL);

    } else {
        printf("Blad: Nie udalo sie zainicjalizowac wolfSSL: %d\n", ret);
        return;
    }}
    
