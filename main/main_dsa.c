/* gnu c and posix includes */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <wolfssl/wolfcrypt/dilithium.h>

#include <wolfssl/wolfcrypt/settings.h>
/* wolfssl includes */
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
#include <wolfssl/wolfcrypt/random.h>
#ifndef HAVE_ML_DSA
    #error "UWAGA: HAVE_ML_DSA nie jest zdefiniowane! wolfSSL nie widzi Twoich ustawień!"
#endif
#include "measure.h"

//do testów
extern uint32_t stack_size;
#define BENCH_STACK_SIZE 24*1024 


struct ml_dsa_len_t {
    uint8_t      sec_cat;
    const char * name;
    uint16_t     priv_len;
    uint16_t     pub_len;
    uint16_t     sig_len;
};




/* ML-DSA supported parameter sets, and key and signature sizes.
 * From tables 1 and 2 of:
 *   - https://nvlpubs.nist.gov/nistpubs/FIPS/NIST.FIPS.204.pdf
 * */
static struct ml_dsa_len_t ml_dsa_len[] =
    { {2, "ML-DSA-44", 2560, 1312, 2420},
      {3, "ML-DSA-65", 4032, 1952, 3309},
      {5, "ML-DSA-87", 4896, 2592, 4627} };



//funkcje pomocnicze     
/*
static int  ml_dsa_sec_valid(void);
static int  ml_dsa_valid_len(word32 priv_len, word32 pub_len, word32 sig_len);
static void ml_dsa_dump_hex(const uint8_t * data, size_t len,
                            const char * what);
static int  ml_dsa_dump_file(const uint8_t * data, size_t len,
                             const char * what);
static void ml_dsa_free(void ** p);
*/ 

//parametry
//static const char * prog_name = "ml_dsa_test";
//static const char * default_msg = "wolfssl ml-dsa test!";
//dsa87
static const char * msg =  "example_message_for_ml_dsa_test";


//globalne zmienne

static int          sec_cat = 5 /* ML-DSA security category */;
static int          verbose = 0;
static int          write_to_file = 0;

static MlDsaKey key;
static WC_RNG rng;


//87
/*
static byte priv[4896];
static byte pub[2592];
static byte sig[4627];
*/
/*
 //65
static byte priv[4032];
static byte pub[1952];
static byte sig[3309];


//44
static byte priv[2560];
static byte pub[1312];
static byte sig[2420];


*/
static byte priv[4896];
static byte pub[2592];
static byte sig[4627];


static word32 priv_len;
static word32 pub_len;
static word32 sig_len;

static int ml_dsa_priv_len = 0;
static int ml_dsa_pub_len  = 0;
static int ml_dsa_sig_len  = 0;

static int verify_res;
static int verify_rc;


static int
ml_dsa_sec_valid(void)
{
    int rc = 0;

    switch (sec_cat) {
    case 2:
    case 3:
    case 5:
        rc = 0;
        break;
    default:
        printf("error: invalid security category: %d\n", sec_cat);
        rc = -1;
        break;
    }

    return rc;
}
/*
static int
ml_dsa_valid_len(word32 priv_len,
                 word32 pub_len,
                 word32 sig_len)
{
    size_t i = 0;

    if (ml_dsa_sec_valid() != 0) {
        return -1;
    }

    switch(sec_cat) {
    case 2:
        i = 0; break;
    case 3:
        i = 1; break;
    case 5:
        i = 2; break;
    default:
        return -1;
    }

    if (ml_dsa_len[i].priv_len != priv_len) {
        printf("error: priv_len: got %d, expected %d\n",
               ml_dsa_len[i].priv_len,
               priv_len);
        return -1;
    }

    if (ml_dsa_len[i].pub_len != pub_len) {
        printf("error: pub_len: got %d, expected %d\n",
               ml_dsa_len[i].pub_len,
               pub_len);
        return -1;
    }

    if (ml_dsa_len[i].sig_len != sig_len) {
        printf("error: sig_len: got %d, expected %d\n",
               ml_dsa_len[i].sig_len,
               sig_len);
        return -1;
    }

    //printf("info: ML-DSA lens good\n");
    printf("info: using %s, Security Category %d: "
           "priv_len %d, pub_len %d, sig_len %d\n",
           ml_dsa_len[i].name, ml_dsa_len[i].sec_cat,
           ml_dsa_len[i].priv_len,
           ml_dsa_len[i].pub_len,
           ml_dsa_len[i].sig_len);

    return 0;
}

static void
ml_dsa_dump_hex(const uint8_t * data,
                size_t          len,
                const char *    what)
{
    if (!verbose) { return; }
    printf("info: dumping %s:\n", what);
    printf("%s (%zu):\n", what, len);

    for (size_t i = 0; i < len; ++i) {
        printf("0x%02x ", data[i]);

        if (((i + 1) % 8 == 0)) {
            printf("\n");
        }
    }

    printf("\n");

    return;
}
static int
ml_dsa_dump_file(const uint8_t * data,
                 size_t          len,
                 const char *    what)
{
    FILE * file = NULL;
    int    n_write = 0;
    int    err = 0;

    if (!write_to_file) { return 0; }

    file = fopen(what, "w+");

    if (file == NULL) {
        printf("error: fopen(%s, \"w+\") failed\n", what);
        return -1;
    }

    n_write = fwrite(data, 1, len, file);

    if (n_write != (int) len) {
        printf("error: fwrite(%p, 1, %zu, %p) returned %d, expected %zu\n",
               data, len, file, n_write, len);
        fclose(file);
        return -1;
    }

    err = fclose(file);
    file = NULL;

    if (err) {
        printf("error: fclose returned: %d\n", err);
        return -1;
    }

    return 0;
}


static void
ml_dsa_free(void ** p)
{
  if (p && *p) {
    free(*p);
    *p = NULL;
  }
}
*/
void task_Empty(void *pv) {
    //start_measure("Empty");
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}

void task_init(void *pv)
{
    wc_MlDsaKey_Init(&key, NULL, INVALID_DEVID);

    if (wc_MlDsaKey_SetParams(&key, sec_cat) != 0) {
        printf("SetParams failed\n");
    }

    //printf("INIT DONE, stack watermark: %u bytes\n", uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);

}

void task_makekey(void *pv)
{
    //printf("=== MAKEKEY TASK ===\n");
    //start_measure("makekey");
    wc_MlDsaKey_MakeKey(&key, &rng);
    //end_measure();
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);

}

void task_help(void *pv)
{
    int tmp_pub, tmp_priv, tmp_sig;
    //start_measure("taskhelp");
    wc_MlDsaKey_GetPubLen(&key, &tmp_pub);
    wc_MlDsaKey_GetPrivLen(&key, &tmp_priv);
    wc_MlDsaKey_GetSigLen(&key, &tmp_sig);

    tmp_priv -= tmp_pub;

    pub_len = tmp_pub;
    priv_len = tmp_priv;
    sig_len = tmp_sig;

    wc_MlDsaKey_ExportPubRaw(&key, pub, &pub_len);
    wc_MlDsaKey_ExportPrivRaw(&key, priv, &priv_len);
    //end_measure();
        TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);
}


void task_sign(void *pv)
{
    //printf("=== SIGN TASK ===\n");

    //start_measure("sign");
    sig_len = sizeof(sig);
    wc_MlDsaKey_Sign(&key, sig, &sig_len,
                     (const byte*)msg, strlen(msg),
                     &rng);
    //end_measure();

    //printf("SIGN DONE, stack watermark: %u bytes\n", uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);

}

void task_verify(void *pv)
{
    //printf("=== VERIFY TASK ===\n");

    //start_measure("verify");
    verify_rc = wc_MlDsaKey_Verify(&key, sig, sig_len,
                                   (const byte*)msg, strlen(msg),
                                   &verify_res);
    //end_measure();

    //printf("VERIFY RESULT: %d (rc=%d), stack watermark: %u bytes\n", verify_res, verify_rc, uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));

    TaskHandle_t xTaskToNotify = (TaskHandle_t)pv;
    xTaskNotifyGive(xTaskToNotify);
    vTaskDelete(NULL);

}
 

void mldsa_task(void *pv) {
    TaskHandle_t h;
    TaskHandle_t xMainTask = xTaskGetCurrentTaskHandle();
    //printf("--- Start ML-DSA Task ---\n");
    int          rc = 0;
    printf("ML-DSA Security Category: %d\n", sec_cat);
    /* Inicjalizacja generatora liczb losowych */
    rc = wc_InitRng(&rng);
    if (rc != 0) {
        printf("error: wc_InitRng failed\n");
        goto ml_dsa_exit;
    }

    /* Sprawdzenie kategorii bezpieczeństwa */
    rc = ml_dsa_sec_valid();
    if (rc < 0) {
        goto ml_dsa_exit;
    }
    vTaskDelay(25);
    /* Loop over multiple iterations */
    for (int i = 100; i > 0; i--) {
        //printf("\n========== ITERATION %d ==========\n", i);
        
        verify_res = 0;
        verify_rc = -1;
        xTaskCreatePinnedToCore(task_Empty, "b_empty",   BENCH_STACK_SIZE, (void*)xMainTask, 1, NULL, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(5);

        /* Init task */
        xTaskCreatePinnedToCore(task_init, "b_init", BENCH_STACK_SIZE, (void*)xMainTask, 1, &h, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(5);


        //printf("info: Generowanie pary kluczy...\n");

        /* Makekey task */
        xTaskCreatePinnedToCore(task_makekey, "b_makekey", BENCH_STACK_SIZE, (void*)xMainTask, 1, &h, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(5);

        xTaskCreatePinnedToCore(task_help, "b_help", BENCH_STACK_SIZE, (void*)xMainTask, 1, &h, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(5);

        //printf("\ninfo: Podpisywanie wiadomosci...\n");

        /* Sign task */
        xTaskCreatePinnedToCore(task_sign, "b_sign", BENCH_STACK_SIZE, (void*)xMainTask, 1, &h, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(5);

        //ml_dsa_dump_hex((const uint8_t *) msg, strlen(msg), "WIADOMOSC");
        //ml_dsa_dump_hex(sig, sig_len, "PODPIS (SIGNATURE)");

        //printf("\ninfo: Weryfikacja...\n");

        /* Verify task */
        xTaskCreatePinnedToCore(task_verify, "b_verify", BENCH_STACK_SIZE, (void*)xMainTask, 1, &h, 1);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(25);

        if (verify_rc == 0 && verify_res == 1) {

            printf("SUKCES: Podpis jest PRAWIDLOWY!\n");
            /*
            printf("MESSAGE length = %u\n", (unsigned)strlen(msg));
            printf("MESSAGE:\n%s\n", msg);
            printf("SIGNATURE length = %u\n", sig_len);

            int nonzero = 0;
            for (word32 i = 0; i < sig_len; i++) {
                if (sig[i] != 0) {
                    nonzero = 1;
                    break;
                }
            }

            printf("SIGNATURE nonzero = %s\n", nonzero ? "YES" : "NO");
            */
        } else {
            printf("BLAD: Weryfikacja nieudana! verify_rc=%d, verify_res=%d\n", verify_rc, verify_res);
        }

        /* Cleanup between iterations */
       
            //printf("\ninfo: Czyszczenie pamieci po iteracji %d...\n", i);
            
            /* Free key objects */
            wc_MlDsaKey_Free(&key);
            memset(&key, 0, sizeof(MlDsaKey));
            
            /* Clear buffers */
            memset(priv, 0, sizeof(priv));
            memset(pub, 0, sizeof(pub));
            memset(sig, 0, sizeof(sig));
            
            /* Reset measurement results for next iteration */
            
            /* Small delay to allow other tasks to run */
            vTaskDelay(100);
        
    }

ml_dsa_exit:
    printf("\ninfo: Koniec testu, zwalnianie zasobow.\n");
    //printf("after test: %u bytes stack remaining\n", uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    wc_MlDsaKey_Free(&key);
    wc_FreeRng(&rng);

    printf("--- Koniec ML-DSA Task ---\n");
    export_csv();
    vTaskDelete(NULL);
}

void app_main(void) {
    // Tworzymy zadanie z dużym stosem (np. 64 KB, żeby mieć zapas)
    // 65536 bajtów to dużo, ale Dilithium tego potrzebuje
    xTaskCreatePinnedToCore(mldsa_task, "mldsa_task", 24*1024, NULL, 5, NULL, 0);
}

