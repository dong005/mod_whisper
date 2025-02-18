#include "mod_whisper.h"
#include "websock_glue.h"
#include <libwebsockets.h>
#include <switch_cJSON.h>
#include <switch_json.h>

// Forward declarations
int callback_ws_asr(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

// TTS callback function implementation
int callback_ws_tts(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    whisper_tts_t *context = (whisper_tts_t *)lws_wsi_user(wsi);
    cJSON *json = NULL;
    cJSON *status = NULL;
    cJSON *message = NULL;
    const char *error_msg = NULL;

    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "WebSockets TTS client established. [%p]\\n", (void *)wsi);
            context->wc_connected = TRUE;
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "WS receiving TTS data\\n");
            if (lws_frame_is_binary(context->wsi)) {
                // Handle binary audio data
                switch_buffer_write(context->audio_buffer, in, len);
                context->audio_len += len;
            } else {
                // Handle text response (status, error messages etc)
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Received text: %s\\n", (char *)in);
                
                json = cJSON_Parse((const char *)in);
                if (json) {
                    status = cJSON_GetObjectItem(json, "status");
                    if (status && status->valuestring) {
                        if (strcmp(status->valuestring, "error") == 0) {
                            message = cJSON_GetObjectItem(json, "message");
                            error_msg = message && message->valuestring ? 
                                                  message->valuestring : "Unknown error";
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                "TTS Error: %s\\n", error_msg);
                            context->wc_error = TRUE;
                        }
                    }
                    cJSON_Delete(json);
                }
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Websocket TTS connection error\\n");
            context->wc_error = TRUE;
            return -1;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Websocket TTS client connection closed\\n");
            context->started = WS_STATE_DESTROY;
            return -1;
            
        default:
            break;
    }
    return 0;
}

// libwebsocket protocols
static struct lws_protocols ws_tts_protocols[] = {
    {
        "WSBRIDGE",
        callback_ws_tts,
        0,
        RX_BUFFER_SIZE,     
    },
    { NULL, NULL, 0, 0 } /* end */
};

static struct lws_protocols ws_asr_protocols[] = {
    {
        "WSBRIDGE",
        callback_ws_asr,
        0,
        RX_BUFFER_SIZE,     
    },
    { NULL, NULL, 0, 0 } /* end */
};
//ASR Functions
int callback_ws_asr(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    whisper_t *context = (whisper_t *)lws_wsi_user(wsi);
    cJSON *json = NULL;
    cJSON *status = NULL;
    cJSON *text = NULL;
    cJSON *confidence = NULL;
    cJSON *message = NULL;
    const char *error_msg = NULL;
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "WebSockets ASR client established. [%p]\\n", (void *)wsi);
            context->wc_connected = TRUE;
            break;
            
        case LWS_CALLBACK_CLIENT_RECEIVE:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "WS receiving ASR data\\n");
            if (!lws_frame_is_binary(context->wsi)) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Received text: %s\\n", (char *)in);
                
                json = cJSON_Parse((const char *)in);
                if (json) {
                    status = cJSON_GetObjectItem(json, "status");
                    text = cJSON_GetObjectItem(json, "text");
                    
                    if (status && text && status->valuestring) {
                        if (strcmp(status->valuestring, "success") == 0 && text->valuestring) {
                            context->result_text = switch_safe_strdup(text->valuestring);
                            
                            confidence = cJSON_GetObjectItem(json, "confidence");
                            if (confidence && confidence->valuedouble) {
                                context->result_confidence = confidence->valuedouble;
                            }
                            
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                                "ASR Result: %s (confidence: %.2f)\\n",
                                context->result_text, context->result_confidence);
                        } else {
                            message = cJSON_GetObjectItem(json, "message");
                            error_msg = message && message->valuestring ? 
                                                  message->valuestring : "Unknown error";
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                "ASR Error: %s\\n", error_msg);
                        }
                    }
                    cJSON_Delete(json);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                        "Failed to parse ASR response JSON\\n");
                }
            }

            switch_mutex_lock(context->mutex);
            switch_set_flag(context, ASRFLAG_RESULT_READY);
            switch_clear_flag(context, ASRFLAG_RESULT_PENDING);
            switch_mutex_unlock(context->mutex);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Websocket ASR connection error\\n");
            switch_mutex_lock(context->mutex);
            context->wc_error = TRUE;
            switch_mutex_unlock(context->mutex);
            return -1;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Websocket ASR client connection closed. %d\\n", context->started);
            switch_mutex_lock(context->mutex);
            context->started = WS_STATE_DESTROY;
            switch_mutex_unlock(context->mutex);
            return -1;
            
        default:
            break;
    }
    return 0;
}
// Setup functions
switch_status_t ws_tts_setup_connection(char * tts_server_uri, whisper_tts_t *tech_pvt, switch_memory_pool_t *pool)
{
    whisper_tts_t *context = (whisper_tts_t *) tech_pvt;
    int logs = LLL_USER | LLL_ERR | LLL_WARN;
    const char *prot;

    context->lws_info.port = CONTEXT_PORT_NO_LISTEN;
    context->lws_info.protocols = ws_tts_protocols;
    context->lws_info.gid = -1;
    context->lws_info.uid = -1;

    lws_set_log_level(logs, NULL);
    
    context->lws_context = lws_create_context(&context->lws_info);
    if (context->lws_context == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Creating libwebsocket context failed\\n");
        return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
    }

    if (lws_parse_uri(tts_server_uri, &prot, &context->lws_ccinfo.address, 
                      &context->lws_ccinfo.port, &context->lws_ccinfo.path)) {
        return SWITCH_CAUSE_INVALID_URL;
    }

    context->lws_ccinfo.ssl_connection = strcmp(prot, "ws") ? 2 : 0;
    context->lws_ccinfo.context = context->lws_context;
    context->lws_ccinfo.host = lws_canonical_hostname(context->lws_context);
    context->lws_ccinfo.origin = "origin";
    context->lws_ccinfo.userdata = (whisper_tts_t *) context;
    context->lws_ccinfo.protocol = ws_tts_protocols[0].name;

    context->wsi = lws_client_connect_via_info(&context->lws_ccinfo);
    if (context->wsi == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Websocket setup failed\\n");
        return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
    }

    ws_tts_thread_launch(context, pool);

    while (!(context->wc_connected || context->wc_error)) {
        usleep(30000);
    }

    if (context->wc_error == TRUE) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Websocket connect failed\\n");
        return SWITCH_STATUS_FALSE;
    }

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t ws_asr_setup_connection(char * asr_server_uri, whisper_t *tech_pvt, switch_memory_pool_t *pool)
{
    whisper_t *context = (whisper_t *) tech_pvt;
    int logs = LLL_USER | LLL_ERR | LLL_WARN;
    const char *prot;

    context->lws_info.port = CONTEXT_PORT_NO_LISTEN;
    context->lws_info.protocols = ws_asr_protocols;
    context->lws_info.gid = -1;
    context->lws_info.uid = -1;

    lws_set_log_level(logs, NULL);
    
    context->lws_context = lws_create_context(&context->lws_info);
    if (context->lws_context == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Creating libwebsocket context failed\\n");
        return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
    }

    if (lws_parse_uri(asr_server_uri, &prot, &context->lws_ccinfo.address, 
                      &context->lws_ccinfo.port, &context->lws_ccinfo.path)) {
        return SWITCH_CAUSE_INVALID_URL;
    }

    context->lws_ccinfo.ssl_connection = strcmp(prot, "ws") ? 2 : 0;
    context->lws_ccinfo.context = context->lws_context;
    context->lws_ccinfo.host = lws_canonical_hostname(context->lws_context);
    context->lws_ccinfo.origin = "origin";
    context->lws_ccinfo.userdata = (whisper_t *) context;
    context->lws_ccinfo.protocol = ws_asr_protocols[0].name;

    context->wsi = lws_client_connect_via_info(&context->lws_ccinfo);
    if (context->wsi == NULL) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Websocket setup failed\\n");
        return SWITCH_CAUSE_DESTINATION_OUT_OF_ORDER;
    }

    ws_asr_thread_launch(context, pool);

    while (!(context->wc_connected || context->wc_error)) {
        usleep(30000);
    }

    if (context->wc_error == TRUE) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Websocket connect failed\\n");
        return SWITCH_STATUS_FALSE;
    }

    return SWITCH_STATUS_SUCCESS;
}

// Thread functions
void ws_tts_thread_launch(whisper_tts_t *tech_pvt, switch_memory_pool_t *pool)
{
    switch_thread_t *thread;
    switch_threadattr_t *thd_attr = NULL;

    switch_threadattr_create(&thd_attr, pool);
    switch_threadattr_detach_set(thd_attr, 1);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    tech_pvt->started = WS_STATE_STARTED;
    switch_thread_create(&thread, thd_attr, ws_tts_thread_run, tech_pvt, pool);
}

void ws_asr_thread_launch(whisper_t *tech_pvt, switch_memory_pool_t *pool)
{
    switch_thread_t *thread;
    switch_threadattr_t *thd_attr = NULL;

    switch_threadattr_create(&thd_attr, pool);
    switch_threadattr_detach_set(thd_attr, 1);
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    tech_pvt->started = WS_STATE_STARTED;
    switch_thread_create(&thread, thd_attr, ws_asr_thread_run, tech_pvt, pool);
}

void *SWITCH_THREAD_FUNC ws_tts_thread_run(switch_thread_t *thread, void *obj)
{
    whisper_tts_t *context = (whisper_tts_t *) obj;
    do {
        lws_service(context->lws_context, WS_TIMEOUT_MS);
    } while (context->started == WS_STATE_STARTED);
    return NULL;
}

void *SWITCH_THREAD_FUNC ws_asr_thread_run(switch_thread_t *thread, void *obj)
{
    whisper_t *context = (whisper_t *) obj;
    do {
        lws_service(context->lws_context, WS_TIMEOUT_MS);
    } while (context->started == WS_STATE_STARTED);
    return NULL;
}

// Close functions
void ws_tts_close_connection(whisper_tts_t *tech_pvt)
{
    whisper_tts_t *context = (whisper_tts_t *) tech_pvt;
    lws_cancel_service(context->lws_context);
    context->started = WS_STATE_DESTROY;
    lws_context_destroy(context->lws_context);
}

void ws_asr_close_connection(whisper_t *tech_pvt)
{
    whisper_t *context = (whisper_t *) tech_pvt;
    lws_cancel_service(context->lws_context);
    context->started = WS_STATE_DESTROY;
    lws_context_destroy(context->lws_context);
}

// Utility functions
switch_status_t ws_send_binary(struct lws *websocket, void *data, int rlen)
{
    if (lws_write(websocket, data, rlen, LWS_WRITE_BINARY) < 0) {
        return SWITCH_STATUS_FALSE;
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t ws_send_text(struct lws *websocket, char *text)
{
    if (lws_write(websocket, (unsigned char*)text, strlen(text), LWS_WRITE_TEXT) < 0) {
        return SWITCH_STATUS_FALSE;
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t ws_send_json(struct lws *websocket, ks_json_t *json_object)
{
    char *json_str = ks_json_print_unformatted(json_object);
    switch_status_t status = ws_send_text(websocket, json_str);
    ks_pool_free(&json_str);
    return status;
}

switch_status_t whisper_get_final_transcription(whisper_t *context)
{
    switch_mutex_lock(context->mutex);
    if (switch_test_flag(context, ASRFLAG_RESULT_READY)) {
        switch_clear_flag(context, ASRFLAG_RESULT_READY);
        switch_mutex_unlock(context->mutex);
        return SWITCH_STATUS_SUCCESS;
    }
    switch_mutex_unlock(context->mutex);
    return SWITCH_STATUS_FALSE;
}

switch_status_t whisper_get_speech_synthesis(whisper_tts_t *context)
{
    if (context->audio_buffer_raw != NULL && context->audio_len > 0) {
        return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
}

void whisper_fire_event(whisper_t *context, char * event_subclass)
{
    switch_event_t *event;
    if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, event_subclass) == SWITCH_STATUS_SUCCESS) {
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "ASR-Response", context->result_text);
        switch_event_add_header(event, SWITCH_STACK_BOTTOM, "ASR-Confidence", "%f", context->result_confidence);
        switch_event_fire(&event);
    }
}
