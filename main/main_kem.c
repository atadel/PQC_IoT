//Author: Aleksandra Tadel
//Based on wolfSSL example: https://github.com/wolfSSL/wolfssl-examples/blob/master/pq/ml_kem/ml_kem.c

#include <esp_log.h>
#include <wolfssl/ssl.h>
#include <wolfssl/version.h>
#include <wolfssl/wolfcrypt/types.h>
#include <wolfssl/wolfcrypt/wc_port.h>
#include <wolfssl/wolfcrypt/mem_track.h>
#include <esp_heap_caps.h>
#include "esp_timer.h"



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
#include "measure.h"

//PARAMETRY PROGRAMU 
extern uint32_t stack_size;
#define BENCH_STACK_SIZE 24*1024  // 24KB - stack size used in mqtt_task, adjust as needed
//#define TEST_LEVEL WC_ML_KEM_512
//#define TEST_LEVEL WC_ML_KEM_768
#define TEST_LEVEL WC_ML_KEM_1024

TaskHandle_t xKEMTaskHandle = NULL;


//ZMIENNE GLOBALNE 

int ret;
static MlKemKey AliceKey, BobKey;
static WC_RNG rng;
uint32_t start_cycles, end_cycles;
int64_t start_time, end_time;
uint32_t heap_before, heap_after, heap_min, heap_now, peak_used;
// Bufory statyczne (poza stosem)
//static byte alice_pub[WC_ML_KEM_512_PUBLIC_KEY_SIZE];
//static byte bob_ct[WC_ML_KEM_512_CIPHER_TEXT_SIZE];
//static byte alice_pub[WC_ML_KEM_768_PUBLIC_KEY_SIZE];
//static byte bob_ct[WC_ML_KEM_768_CIPHER_TEXT_SIZE];
static byte alice_pub[WC_ML_KEM_1024_PUBLIC_KEY_SIZE];
static byte bob_ct[WC_ML_KEM_1024_CIPHER_TEXT_SIZE];
static byte bob_ss[WC_ML_KEM_SS_SZ];
static byte alice_ss[WC_ML_KEM_SS_SZ];
WOLFSSL_CTX* ctx;
WOLFSSL* ssl;



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




// POJEDYNCZE TASKI
void task_Empty(void *pv) {
    //start_measure("Empty");
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_Init(void *pv) {
    ///start_measure("Init");
    int ret = wc_MlKemKey_Init(&AliceKey, TEST_LEVEL, NULL, INVALID_DEVID);
    ret |= wc_MlKemKey_Init(&BobKey, TEST_LEVEL, NULL, INVALID_DEVID);
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}


void task_MakeKey(void *pv) { //Generates an encapsulation key and a corresponding decapsulation key.
    //start_measure("MakeKey");
    int ret = wc_MlKemKey_MakeKey(&AliceKey, &rng); //do Alice Key zapisujemy klucz do enkapsulacji
    //end_measure();
    if (ret != 0) printf("Błąd MakeKey: %d\n", ret);
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_Encode(void *pv) { //Encode the public key.
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    word32 pubKeySz = sizeof(alice_pub);
    //start_measure("Encode");
    int ret = wc_MlKemKey_EncodePublicKey(&AliceKey, alice_pub, pubKeySz); //wyciąga klucz publiczny i długość
    //end_measure();
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_Decode(void *pv) { //Decode public key.
    //start_measure("Decode");
    int ret = wc_MlKemKey_DecodePublicKey(&BobKey, alice_pub, sizeof(alice_pub)); //zapisuje klucz alice
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_Encaps(void *pv) { //Encapsulate with random number generator and derive secret.
    //Bob will use only Alice's public key to generate a shared secret and an ecapsulated instance of the shared secret;
    //start_measure("Encapsulate");
    int ret = wc_MlKemKey_Encapsulate(&BobKey, bob_ct, bob_ss, &rng); 
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_Decaps(void *pv) { // Decapsulate the cipher text to calculate the shared secret.
    //Alice will use Bob's ciphertext and her private key to obtain the same shared secret
    //start_measure("Decapsulate");
    int ret = wc_MlKemKey_Decapsulate(&AliceKey, alice_ss, bob_ct, sizeof(bob_ct));
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}


void benchmark_stack_individual(void *pvParameters) {

    for (int i = 100; i > 0; i--) {
        TaskHandle_t xMainTask = xTaskGetCurrentTaskHandle();
   
        wc_InitRng(&rng);
        vTaskDelay(10);
        xTaskCreatePinnedToCore(task_Empty, "b_empty",   BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);

        xTaskCreatePinnedToCore(task_Init,    "b_init",   BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);

        xTaskCreatePinnedToCore(task_MakeKey, "b_make",   BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);
  
        xTaskCreatePinnedToCore(task_Encode,  "b_encode", BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);

        xTaskCreatePinnedToCore(task_Decode,  "b_decode", BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);

        xTaskCreatePinnedToCore(task_Encaps,  "b_encaps", BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);

        xTaskCreatePinnedToCore(task_Decaps,  "b_decaps", BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(1);
        vTaskDelay(15);

        char* a = to_hex_string(alice_ss, WC_ML_KEM_SS_SZ);
        char* b = to_hex_string(bob_ss, WC_ML_KEM_SS_SZ);
        /*
        if (a != NULL && b != NULL) {
            printf("Alice's Shared Secret: %s\n\n", a);
            printf("Bob's Shared Secret: %s\n\n", b);
        } else {
            printf("Błąd: nie udało się zaalokować pamięci na hex string.\n");
        }

        // Weryfikacja
        if (XMEMCMP(alice_ss, bob_ss, WC_ML_KEM_SS_SZ) == 0) {
            printf("\n Klucze są zgodne.\n");
        } else {
            printf("\n Klucze się różnią!\n");
        }
        printf("Wolny heap: %lu bajtów\n", esp_get_free_heap_size());
        */
        // Czyszczenie
        free(a);
        free(b);

        memset(alice_pub, 0, sizeof(alice_pub));
        memset(bob_ct,  0, sizeof(bob_ct));
        memset(bob_ss,  0, sizeof(bob_ss));
        memset(alice_ss, 0, sizeof(alice_ss));
        
        wc_MlKemKey_Free(&AliceKey);
        wc_MlKemKey_Free(&BobKey);
        wc_FreeRng(&rng);
 
        printf("--- KONIEC BENCHMARKU ---\n");
        //printf("Wolny heap: %lu bajtów\n", esp_get_free_heap_size());
    }
    export_csv();
    
     vTaskDelete(NULL);
     printf("Wolny heap: %lu bajtów\n", esp_get_free_heap_size());
}
void app_main(void)
{
    int ret = wolfSSL_Init();

    if (ret == WOLFSSL_SUCCESS) {
        printf("Sukces: Biblioteka wolfSSL zainicjalizowana!\n");
        xTaskCreatePinnedToCore(benchmark_stack_individual, "bench_ctrl", 8192, NULL, 5, NULL, 1);
    } else {
        printf("Blad: Nie udalo sie zainicjalizowac wolfSSL: %d\n", ret);
        return;
    }
    
#ifdef WOLFSSL_HAVE_KYBER
    //printf("Kyber KEM jest dostepny w tej kompilacji!\n");
    ssl = wolfSSL_new(ctx);
    //wolfSSL_UseKeyShare(ssl, WOLFSSL_P521_KYBER_LEVEL5);
#else
    printf("UWAGA: Kyber KEM NIE jest wlaczoany w menuconfig!\n");
#endif


    //printf("--- KONIEC TESTU ---\n\n");
    
}